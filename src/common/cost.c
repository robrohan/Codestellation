#include "cost.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* a, b, c, d per mode, from Boehm's Basic COCOMO table. */
static const double COEF[COCOMO_MODE_COUNT][4] = {
    { 2.4, 1.05, 2.5, 0.38 }, /* organic: small team, familiar problem */
    { 3.0, 1.12, 2.5, 0.35 }, /* semi-detached */
    { 3.6, 1.20, 2.5, 0.32 }, /* embedded: tight hardware/process constraints */
};

static const char *MODE_NAMES[COCOMO_MODE_COUNT] = { "organic", "semi-detached", "embedded" };

/* Decisions per code line in typical code -- roughly the median across a
 * spread of real projects -- where the complexity factor is 1x. */
#define TYPICAL_DENSITY 0.2
/* Characters per line (blank and comment lines included) when a graph.json
 * has no character counts. */
#define CHARS_PER_LINE_GUESS 32

void cost_params_default(CostParams *p) {
    p->salary = 56286.0;
    p->overhead = 2.4;
    p->mode = COCOMO_ORGANIC;
    p->ai_share = 0.0;
    p->token_price = 10.0;
    p->reasoning_multiplier = 300.0;
    p->chars_per_token = 4.0;
    snprintf(p->currency, sizeof(p->currency), "$");
}

int cost_sloc(const NodeStats *st, const char *language) {
    if (!st->has_stats) return 0;
    if (language && (strcmp(language, "markdown") == 0 || strcmp(language, "json") == 0)) return 0;
    int n = st->lines - st->blank_lines - st->comment_lines;
    return n > 0 ? n : 0;
}

void code_totals_add(CodeTotals *t, const NodeStats *st, const char *language) {
    int sloc = cost_sloc(st, language);
    if (sloc <= 0) return;
    t->sloc += sloc;
    t->chars += st->chars >= 0 ? st->chars : (long long)st->lines * CHARS_PER_LINE_GUESS;
    if (st->complexity >= 0) {
        t->decisions += st->complexity;
        t->decision_sloc += sloc;
    }
    t->files++;
}

void code_totals_merge(CodeTotals *into, const CodeTotals *from) {
    into->sloc += from->sloc;
    into->chars += from->chars;
    into->decisions += from->decisions;
    into->decision_sloc += from->decision_sloc;
    into->files += from->files;
}

CostEstimate cost_estimate(long long sloc, const CostParams *p) {
    CostEstimate e = { 0, 0, 0, 0 };
    if (sloc <= 0) return e;
    int m = p->mode >= 0 && p->mode < COCOMO_MODE_COUNT ? (int)p->mode : COCOMO_ORGANIC;
    e.effort_months = COEF[m][0] * pow((double)sloc / 1000.0, COEF[m][1]);
    e.schedule_months = COEF[m][2] * pow(e.effort_months, COEF[m][3]);
    e.people = e.schedule_months > 0 ? e.effort_months / e.schedule_months : 0;
    e.cost = e.effort_months * (p->salary / 12.0) * p->overhead;
    return e;
}

AiEstimate cost_estimate_ai(const CodeTotals *t, const CostParams *p) {
    AiEstimate e = { 0, 0, 0, 0 };
    if (t->sloc <= 0) return e;
    double cpt = p->chars_per_token > 0 ? p->chars_per_token : 4.0;
    e.tokens_kept = (double)t->chars / cpt;

    double complexity = 1.0;
    if (t->decision_sloc > 0) {
        complexity = ((double)t->decisions / (double)t->decision_sloc) / TYPICAL_DENSITY;
        complexity = fmin(fmax(complexity, 0.5), 2.0);
    }
    double size = 1.0 + 0.5 * log10(fmax((double)t->sloc / 1000.0, 1.0));

    e.multiplier = p->reasoning_multiplier * complexity * size;
    e.tokens_spent = e.tokens_kept * e.multiplier;
    e.cost = e.tokens_spent / 1e6 * p->token_price;
    return e;
}

double cost_value(const CodeTotals *t, const CostParams *p) {
    double share = fmin(fmax(p->ai_share, 0.0), 1.0);
    double by_hand = cost_estimate((long long)llround((double)t->sloc * (1.0 - share)), p).cost;
    return by_hand + share * cost_estimate_ai(t, p).cost;
}

const char *cocomo_mode_name(CocomoMode m) {
    return m >= 0 && m < COCOMO_MODE_COUNT ? MODE_NAMES[m] : MODE_NAMES[0];
}

bool cocomo_mode_parse(const char *s, CocomoMode *out) {
    for (int i = 0; i < COCOMO_MODE_COUNT; i++) {
        if (strcmp(s, MODE_NAMES[i]) == 0) {
            *out = (CocomoMode)i;
            return true;
        }
    }
    return false;
}

static void format_short(double v, const char *prefix, char *buf, size_t cap) {
    double a = fabs(v);
    if (a >= 1e9) snprintf(buf, cap, "%s%.2fB", prefix, v / 1e9);
    else if (a >= 1e6) snprintf(buf, cap, "%s%.2fM", prefix, v / 1e6);
    else if (a >= 1e3) snprintf(buf, cap, "%s%.1fk", prefix, v / 1e3);
    else if (a >= 10.0 || a == 0.0) snprintf(buf, cap, "%s%.0f", prefix, v);
    else snprintf(buf, cap, "%s%.2f", prefix, v);
}

void cost_format_money(double amount, const char *currency, char *buf, size_t cap) {
    format_short(amount, currency ? currency : "", buf, cap);
}

void cost_format_count(double n, char *buf, size_t cap) {
    format_short(n, "", buf, cap);
}
