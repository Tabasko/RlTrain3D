# RlTrain3D — Development Guidelines

## Language & Build
- C/C++ mixed codebase; use C-style (`typedef struct`, `typedef enum`) for game data, C++ only where needed (e.g. templates, constructors).
- All source lives under `src/`. Add new files to the Makefile manually.

## Architecture
- **Systems** (`src/systems/`) own a single concern (environment, track, trains, signals). Each exposes a minimal `Create / Update / Draw / Destroy` interface in its header.
- **State** (`src/state/game_state.h`) is the single source of truth. All runtime flags go into `AppState` or `UiState` inside `GameState gs`. Do not introduce new file-level globals.
- **UI** (`src/ui/`) only reads from `gs` and writes flags back — it never mutates simulation state directly.
- **Constants and shared types** live in `src/types.h`. Colors use the `COL_` prefix. UI dimensions use `UI_SCALE` so everything scales together.

## Comments
- Comment every function in the header with a one-line description of what it does and any non-obvious side effects.
- Comment structs and enums when field names alone are not self-explanatory (see `SegHistEntry`, `ArcDirection` in `types.h` as reference).
- Comment non-obvious logic inline — algorithm choices, coordinate conventions, ring-buffer arithmetic, shader bindings.
- Skip comments that just restate the code.

## Rendering
- Use **raylib** primitives and **raygui** widgets. Do not hand-roll UI drawing when a raygui control exists.
- Gate every draw call behind the relevant `gs.app.render_*` flag so features can be toggled at runtime.
- Shaders go in `resources/shaders/glsl330/`. Load them with `GLSL_VERSION` so the path stays version-agnostic.

## Naming
- Functions: `SystemVerbNoun` — e.g. `EnvironmentGroundDraw3D`, `UiDrawLeftPanel`.
- Structs/enums/Fields: `PascalCase`.
- Boolean flags: `is_`, `has_`, `render_`, `show_` prefixes.

## Encapsulation rules
- A system must not `#include` another system's header unless there is an explicit data dependency.
- Shared data flows through `gs`, not through direct calls between systems.
- Keep `static` any symbol that does not need to be visible outside its translation unit.
