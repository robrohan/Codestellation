import { cx } from "@lib/cx";            // paths alias (inherited via extends) -> lib/cx.ts
import type { Props } from "types";      // baseUrl -> types.d.ts
import React from "react";               // still a package: unresolved

export const cart = (p: Props) => cx("cart", p.title, String(React));
