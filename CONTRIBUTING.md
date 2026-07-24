# Contributing to KumCAD

Thanks for considering it. KumCAD is a young project and genuinely needs outside eyes — bug reports and real-world usage are as valuable as code.

## Getting set up

See the [Building](README.md#building) section of the README. The short version:

```sh
cmake -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The 3D modeling window needs OpenCASCADE; DWG import/export needs LibreDWG. Both are optional — KumCAD builds and runs fully without either, just with that one area disabled.

## Project layout

- `src/core/` — the actual engine: geometry, document model, file I/O, PCB/schematic logic, the 3D modeling core (`core3d/`, only built when OpenCASCADE is found). No Qt dependency. This is where correctness lives, and where tests live.
- `src/app/` — the Qt 6 UI: `DrawingView` (the 2D canvas), `CommandDispatcher` (parses typed commands, dispatches to `src/app/commands/`), `Window3D`/`AssemblyWindow`/`BlockEditorWindow` (the 3D and block-editing windows), dock panels.
- `tests/core/` — Catch2 tests, one file per core area, mirroring `src/core/`'s own layout.

## Code conventions

These aren't arbitrary — they're what the existing codebase actually does, consistently, and a PR that doesn't match will stand out immediately in review:

- **Match the real tool's own conventions.** Command names/aliases follow AutoCAD's own (`L` for LINE, `TR` for TRIM, ...); file formats match DXF/DWG/.kicad_sch/.kicad_pcb/.kicad_mod/STEP exactly, not an approximation; 3D concepts match FreeCAD's own vocabulary (Pad/Pocket, sketch, feature tree).
- **No comments explaining *what* the code does.** Names should already make that obvious. A comment earns its place only by explaining a non-obvious *why* — a hidden constraint, a workaround, a disclosed simplification versus the real tool's own more complex behavior. Look at almost any header in `src/core/` for the expected tone.
- **Disclose simplifications, don't hide them.** If your implementation is a deliberately simpler version of what the real tool does (e.g. a fixed style instead of a full named-style table), say so directly in a comment where it's defined, and in your PR description. Silent simplifications that look like full implementations are the one thing actively unwelcome here.
- **Don't add speculative abstraction, config flags, or error handling for cases that can't happen.** A new command that does one thing doesn't need a plugin system.
- **Core logic gets tested; interactive UI glue doesn't.** Anything in `src/core/` needs Catch2 tests. `DrawCommand`-derived classes in `src/app/commands/` (the interactive point-and-type state machines) are established as *not* individually unit tested in this codebase — their logic is thin enough that the value is in the underlying core function's own tests instead. If you're adding a new core function, add tests; if you're adding a new typed command that's mostly plumbing around an already-tested core function, matching existing commands' lack of a dedicated test file is fine, not a shortcut.
- **Zero warnings.** Build with `-Wall -Wextra -Wswitch` clean. `EntityType`/`FeatureType`-style enums are matched with exhaustive `switch` statements on purpose (no `default:` catch-all) specifically so the compiler forces every call site when a new enum value is added — don't add a `default:` to make a warning go away; add the missing case.
- **Keep persisted enums append-only.** `EntityType`, `FeatureType`, and similar enums that get written to saved files must only ever have values *appended*, never reordered or removed, or old files silently corrupt on load.

## Making changes

- One logical feature or fix per commit/PR — easier to review, easier to bisect.
- Run the full test suite before opening a PR. If you touched `src/core/`, also check for new warnings on a full rebuild.
- If your change affects the DXF/DWG/.kicad_*/STEP file format, add a round-trip test (write, read back, assert the fields survived) — that's the existing pattern throughout `tests/core/`.
- Describe *why* in your PR, not just what — especially for any disclosed simplification.

## Reporting bugs / requesting features

Open a GitHub issue. For a bug, the most useful things to include are: what you did, what you expected, what happened instead, and — if it's a file-format issue — the file itself (or a minimal one that reproduces it). For anything that touched a real AutoCAD/KiCad/FreeCAD file from outside KumCAD, please say so explicitly; that's exactly the kind of real-world testing this project needs most right now.

## Conduct

Be respectful and assume good faith. Disagreements about design are fine and expected; personal attacks aren't.
