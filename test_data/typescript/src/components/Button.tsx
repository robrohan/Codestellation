// TypeScript ESM style: imports name the compiled .js, the file is .ts
import { cx } from "../lib/cx.js";              // -> lib/cx.ts

export function Button({ label }: { label: string }) {
  return <button className={cx("btn")}>{label}</button>;
}
