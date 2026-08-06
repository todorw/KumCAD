# KumCAD Quickstart

Five minutes to a working drawing, board, or 3D part. See the [README](../README.md) for building from source and full feature lists.

## First launch

KumCAD opens a **welcome screen** first, not a blank document. Pick what you're starting:

- **2D Drafting** — the default: an AutoCAD-style blank sheet.
- **Electrical Panel / P&ID / Civil / CAM** — the same 2D engine, pre-loaded with the right symbol library and mode.
- **3D Modeling** — opens a *separate* top-level window, the OCCT-backed parametric 3D core. It has its own toolbar and feature tree; there's currently no way to open a 3D window from inside an already-running 2D session — you pick it here, at startup, or via a new `.kcad3d`/STEP import from inside an existing 3D window.
- **Open Existing** / a recent-file list, if you've saved something before.

Schematic capture and PCB layout don't get their own welcome-screen card — they live in the *same* 2D document as ordinary drawing entities, so you reach them by typing their commands (`WIRE`, `TRACK`, ...) once a 2D document is open.

## 2D drafting

Everything happens through the **command line** at the bottom of the window (type and press Enter) or the **toolbar** (which just fills in the command line for you). Either way, follow the prompts.

A minimal session:

```
LINE                  (or just L)
  Specify first point: click in the canvas, or type 0,0
  Specify next point:  click again, or type 10,5
  Specify next point:  Enter to finish, or keep clicking to chain segments

CIRCLE                (or C)
  Specify center point: click
  Specify radius:       type a number, e.g. 5

MOVE                  (or M)
  Select objects first (click, or drag a selection box), THEN run MOVE
  Specify base point:   click
  Specify displacement: click the destination
```

Key conventions, all straight from AutoCAD:
- **Escape** cancels the current command; **Enter** or right-click finishes it.
- Modify commands (MOVE, COPY, ROTATE, SCALE, MIRROR, TRIM, ERASE, ...) use **noun-verb**: select the entities *first*, then run the command — same as clicking objects then pressing Delete.
- Coordinates typed as `x,y` are absolute; most commands also accept relative/polar input the way AutoCAD does.
- `Ctrl+N` new, `Ctrl+O` open, `Ctrl+S` save, from the File menu or the usual shortcuts.

### Common commands

| Command | Alias | Does |
|---|---|---|
| `LINE` | `L` | Draw line segments |
| `CIRCLE` | `C` | Draw a circle |
| `PLINE` | | Polyline (line + tangent-arc segments) |
| `ARC` | | 3-point arc |
| `RECTANG` | | Rectangle |
| `MOVE` | `M` | Move selected entities |
| `COPY` | `CO` / `CP` | Copy selected entities |
| `ROTATE` | `RO` | Rotate selected entities |
| `SCALE` | `SC` | Scale selected entities |
| `MIRROR` | `MI` | Mirror across a line |
| `TRIM` | `TR` | Trim to a cutting edge |
| `EXTEND` | | Extend to a boundary |
| `FILLET` | | Round a corner |
| `CHAMFER` | | Bevel a corner |
| `OFFSET` | | Parallel copy at a distance |
| `ARRAY` | | Rectangular / polar / path array |
| `ERASE` | `E` | Delete selected entities |
| `DIMLINEAR` / `DIMALIGNED` | `DLI` / `DAL` | Linear / aligned dimension |

Full command list is in the [README](../README.md#features) — there's no in-app command browser yet, so that list (or reading `src/app/CommandDispatcher.cpp`) is the closest thing to a reference right now.

## Electronic design (schematic + PCB)

Same document, same command line. A rough schematic-to-board flow:

```
WIRE          draw schematic connections
NETLABEL      name a net
PINADD        define a symbol's pins on a block
ERC           electrical rule check
ANNOTATE      auto-number reference designators

FOOTPRINTGEN  place a parametric footprint (QFP/SOIC/HEADER/CHIP/...)
TRACK         route copper
VIA           drop a via
DRC           design rule check
RATSNEST      show unrouted connections
```

Import/export real `.kicad_sch`/`.kicad_pcb`/`.kicad_mod` files, or Gerber X2 + Excellon for fabrication, from the File menu.

## 3D modeling

The 3D window is **toolbar-driven**, not command-line-driven — click buttons, don't type commands. Mouse: **left-drag to orbit, wheel to zoom, middle-drag to pan**.

A minimal parametric part:

1. Click **Box** (or Cylinder/Sphere/Cone/Torus/Wedge/Helix) on the Features toolbar to drop a primitive — it lands in the feature tree on the left.
2. Double-click it in the feature tree to edit its dimensions.
3. Select two features and click **Union** / **Cut** / **Intersect** to combine them.
4. For a sketch-based feature: **New Sketch...** (or **Sketch on Face...** to attach to an existing face) opens the 2D sketch editor with constraints; **Add Sketch Feature...** turns the finished sketch into a Pad/Pocket/Revolve/etc.
5. **Variables...** / **Spreadsheet...** for named parameters you can reference from feature dimensions.

The File menu has the specialized workbenches (Sheet Metal, BIM, FEM, Piping, TechDraw view generation) and STEP/IGES/STL import-export.

## Where to go from here

- [README.md](../README.md) — full feature list, build instructions, platform support.
- [CONTRIBUTING.md](../CONTRIBUTING.md) — if you want to work on KumCAD itself.
- The **Status** section of the README is worth reading honestly: this is a young, largely solo-developed project with real test coverage but limited real-world usage outside its own development. Expect rough edges; bug reports are genuinely useful.
