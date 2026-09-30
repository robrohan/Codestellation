# Test data

Small sample projects covering every file type Codestellation understands,
plus a canvas project that ties them together. Two ways to use it:

- **Everything at once:** Properties > Open Folder... on `test_data/`. You should
  get **88 files in 16 language groups with 79 dependencies** (the 15 folders
  below plus `project.json`, which counts as a JSON file).
- **As a system map:** Properties > Open Project... and pick
  `test_data/project.json`. Each language is a box; shift+click one to open
  that folder in 3D and Esc to come back. "Canvas features" goes into a nested
  canvas with a weak link, and Cmd+F searches both canvases.

## What each folder should show

Opened on its own with Open Folder.... "Unresolved" are references to things
outside the sample (packages, standard library), which are expected.

| Folder | Files | Edges | Unresolved | What it exercises |
|---|---|---|---|---|
| `c` | 5 | 4 | 0 | `#include` between `.c` and `.h` |
| `csharp` | 5 | 3 | 2 | namespaces, `using`, type references |
| `python` | 6 | 8 | 4 | `import`, `from … import`, relative imports, a `.pyi` stub |
| `go` | 4 | 4 | 2 | `import` paths resolved via `go.mod` |
| `lisp` | 5 | 4 | 0 | `in-package` / `require` / `defpackage :use`; `.lisp`, `.lsp`, `.cl` |
| `php` | 6 | 7 | 0 | namespaces, `use`, `require` |
| `vbnet` | 5 | **0** | 1 | `Imports`, `Inherits`, `Implements` -- **see below** (grammar limitation) |
| `javascript` | 6 | 5 | 1 | `require`, `import()`, `.js`/`.mjs`/`.cjs`/`.jsx` (`pg` is a package) |
| `typescript` | 12 | 12 | 2 | see below |
| `json` | 2 | 1 | 0 | `$ref` to another schema; `#/…` and URL refs are ignored |
| `sql` | 6 | 6 | 1 | see below |
| `proto` | 3 | 2 | 1 | `import`, `import public` (Google's `timestamp.proto` is external) |
| `bash` | 6 | 6 | 0 | `source`, `.`, `./x.sh`, `bash x.sh`, `$(dirname "$0")/…`, `.bash` |
| `powershell` | 10 | 11 | 1 | see below (`Az.Accounts` is an installed module) |
| `batch` | 6 | 6 | 0 | see below |

### TypeScript / TSX (`typescript/`)

- relative imports, including a directory import (`./lib` -> `lib/index.ts`)
- TypeScript ESM style: `"../lib/cx.js"` -> `cx.ts`; `"./format.mjs"` -> `format.mts`
- `import type`, `.tsx` components, `.mts` and `.cts` modules
- `tsconfig.json` (with a comment and trailing commas) `extends`
  `tsconfig.base.json`, which sets `baseUrl` and a `paths` alias:
  `src/features/cart.ts` imports `"@lib/cx"` (alias) and `"types"` (baseUrl)
- a `.ts` file importing `config.json` (a TypeScript -> JSON edge)
- `react` is a package, so it stays unresolved (twice)

### SQL (`sql/`)

- `002_invoices.sql` -> `001_customers.sql` (foreign key)
- `003_views.sql` -> 001 and 002 (a view joining `billing.invoices`; the aliases
  `i` and `c` must **not** show up as references)
- `004_alter.sql` -> 001 (`ALTER TABLE`); its `audit_log` is created nowhere,
  so that's the one unresolved reference
- `report.sql` -> `functions.sql` (function call) and `003_views.sql` (the view)

### PowerShell (`powershell/`)

- dot-sourcing: `. .\lib\common.ps1`, `. "$PSScriptRoot\lib\helpers.ps1"`, and
  unquoted `. $PSScriptRoot/common.ps1`
- `Import-Module` by path to a module folder (`modules\Deploy` ->
  `Deploy\Deploy.psd1`) and by module name (`Tools` -> `Tools.psm1`)
- `using module ..\modules\Types.psm1`; `& .\scripts\build.ps1`; running
  `.\scripts\test.ps1` directly
- the `.psd1` manifest's `RootModule` and `NestedModules`
- PowerShell -> batch: `cmd /c scripts\legacy.bat`

### Batch (`batch/`)

- `call lib\env.bat`, `call "%~dp0scripts\compile.cmd"`, `call "%~dp0..\lib\env.bat"`
- running `scripts\test.bat` directly; `start "" "%~dp0scripts\notify.bat"`
- batch -> PowerShell: `powershell -File "%~dp0tools\package.ps1"`
- `call :cleanup` is a label in the same file, so it is **not** an edge

### VB.NET: known limitation

The VB.NET sample has three base-type links (`Program` inherits
`ShapeRunnerBase`; `Square` and `Circle` implement `IShape`) but none appear:
the pinned VB.NET grammar misparses `Inherits` / `Implements` lines as a
field plus a parse error, so there's nothing to extract. This is documented in
`src/lang/vbnet/vbnet_adapter.c` and would clear up with a better grammar.

## Canvas project

- `project.json` -- title, description, root canvas
- `root.canvas` -- the language boxes, in a group, with a few labelled edges
- `canvas-features.canvas` -- markdown formatting, a group, labelled edges,
  and a weak link (dashed box) to the TypeScript box on the root canvas
