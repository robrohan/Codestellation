import { Button } from "./components/Button";   // -> components/Button.tsx
import type { Props } from "./types";           // -> types.d.ts

export function App(props: Props) {
  return <Button label={props.title} />;
}
