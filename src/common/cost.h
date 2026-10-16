#ifndef CODEMAP_COST_H
#define CODEMAP_COST_H

#include <stdbool.h>
#include <stddef.h>
#include "../graph/graph.h"

/* What a body of code is worth, priced two ways.
 *
 * By hand -- Basic COCOMO (Boehm, 1981) from source lines of code, the
 * same model and defaults scc and sloccount use:
 *
 *   effort   = a * KSLOC^b          person-months
 *   schedule = c * effort^d         months
 *   people   = effort / schedule
 *   cost     = effort * (salary / 12) * overhead
 *
 * By AI -- tokens, independent of COCOMO:
 *
 *   tokens kept  = characters / chars_per_token
 *   multiplier   = reasoning_multiplier * complexity factor * size factor
 *   tokens spent = tokens kept * multiplier
 *   cost         = tokens spent / 1M * token_price
 *
 * The multiplier stands for everything an agent reads, reasons and throws
 * away per token that survives. Its factors are best guesses: complexity
 * scales with decisions per code line against a typical 0.2 (clamped to
 * 0.5x..2x), size grows with the log of the code's size (1x at 1k lines,
 * 1.5x at 10k, 2x at 100k) since bigger code means more context per change.
 *
 * Value -- the code split by ai_share: the hand-written part priced with
 * COCOMO on its own lines, plus ai_share of the AI cost.
 *
 * Estimates don't add up (both models grow faster than size), so price a
 * group of files by summing their CodeTotals and estimating once, never
 * by adding per-file estimates. */

typedef enum { COCOMO_ORGANIC, COCOMO_SEMI_DETACHED, COCOMO_EMBEDDED, COCOMO_MODE_COUNT } CocomoMode;

typedef struct {
    /* By hand. */
    double salary;     /* per developer per year, in `currency` */
    double overhead;   /* multiplier on salary: office, equipment, management... */
    CocomoMode mode;
    /* By AI. */
    double ai_share;             /* 0..1, how much of the code was AI-written */
    double token_price;          /* per million tokens, blended input/output */
    double reasoning_multiplier; /* tokens spent per token kept, before scaling */
    double chars_per_token;
    char currency[16]; /* prefix for amounts, e.g. "$", "EUR " */
} CostParams;

/* The inputs both models need, summed over any set of files. */
typedef struct {
    long long sloc;          /* source lines: not blank, not comment */
    long long chars;         /* characters in those files, comments and all */
    long long decisions;     /* cyclomatic complexity, summed over files that have it */
    long long decision_sloc; /* source lines of the files counted in `decisions` */
    int files;               /* code files counted */
} CodeTotals;

typedef struct {
    double effort_months;   /* person-months */
    double schedule_months;
    double people;
    double cost;
} CostEstimate;

typedef struct {
    double tokens_kept;
    double multiplier; /* reasoning_multiplier with both factors applied */
    double tokens_spent;
    double cost;
} AiEstimate;

/* Defaults: scc's $56,286/year, 2.4 overhead, organic; no AI share,
 * $10 per million tokens (Claude Sonnet 5.5's output rate, October 2026),
 * a reasoning multiplier of 300 and 4 characters per token. */
void cost_params_default(CostParams *p);

/* Source lines of a file: lines that are neither blank nor comment. 0 for
 * files that aren't code (markdown, JSON) or have no stats. */
int cost_sloc(const NodeStats *st, const char *language);

/* Adds one file. Files with no source lines are skipped. A graph.json
 * built before character counts existed estimates them from lines. */
void code_totals_add(CodeTotals *t, const NodeStats *st, const char *language);
void code_totals_merge(CodeTotals *into, const CodeTotals *from);

CostEstimate cost_estimate(long long sloc, const CostParams *p);
AiEstimate cost_estimate_ai(const CodeTotals *t, const CostParams *p);

/* (1 - ai_share) of the lines by hand, plus ai_share of the AI cost. */
double cost_value(const CodeTotals *t, const CostParams *p);

/* "organic", "semi-detached", "embedded". */
const char *cocomo_mode_name(CocomoMode m);
bool cocomo_mode_parse(const char *s, CocomoMode *out);

/* Amounts with a currency prefix, shortened: "$870", "$45.6k", "$1.23M". */
void cost_format_money(double amount, const char *currency, char *buf, size_t cap);

/* Counts shortened the same way: "870", "45.6k", "1.23M". */
void cost_format_count(double n, char *buf, size_t cap);

#endif
