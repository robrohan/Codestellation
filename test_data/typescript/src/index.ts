import React from "react";                 // package: unresolved
import { App } from "./App";               // -> App.tsx
import { cx } from "./lib";                // -> lib/index.ts
import config from "./config.json";        // -> config.json (JSON adapter)

console.log(App, cx, config, React);
