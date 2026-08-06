# KumCAD

[![CI](https://github.com/TodorW/LinuxCAD/actions/workflows/ci.yml/badge.svg)](https://github.com/TodorW/LinuxCAD/actions/workflows/ci.yml)

A free, open-source CAD/EDA suite in the spirit of AutoCAD, KiCad, and FreeCAD, built with C++20 and Qt 6.

KumCAD has three parts sharing one core: **2D drafting** (AutoCAD-style, command-line driven, DXF/DWG native), **electronic design** (KiCad-style schematic capture and PCB layout, sharing the same 2D command line and document), and **3D modeling** (FreeCAD-style parametric solid modeling, in its own window, built on OpenCASCADE). KumCAD follows each real tool's own conventions wherever possible — command names and aliases match AutoCAD's (`L`, `C`, `TR`, `MI`, ...), file formats match DXF/DWG/.kicad_sch/.kicad_pcb/.kicad_mod, and 3D concepts (sketches, pads/pockets, feature trees, assemblies) match FreeCAD's own.

## Features

### 2D Drafting (AutoCAD-style)

**Drawing** — LINE, CIRCLE, ARC (3-point), PLINE (line and tangent-arc segments, Close), SPLINE (fit-point B-spline), RECTANG, ELLIPSE, POINT (PDMODE/PDSIZE marker styles) with DIVIDE/MEASURE, XLINE/RAY construction lines, TEXT and MTEXT using named text styles (STYLE: font, fixed height, width factor, oblique), HATCH (solid fill, gradient, or ANSI31/32/33/37 patterns with scale and angle), LEADER and MLEADER (with named MLEADERSTYLEs), REVCLOUD, DONUT, POLYGON, WIPEOUT, MLINE, TOLERANCE (GD&T feature-control frames), and DIMENSIONS: linear, aligned, radius, diameter, and angular, with named dimension styles (DIMSTYLE) and DIMEDIT/DIMTEDIT.

**Editing** — MOVE, COPY, ROTATE, SCALE, MIRROR, OFFSET, TRIM, EXTEND, FILLET, CHAMFER, STRETCH, LENGTHEN, BREAK, BREAKLINE, ALIGN, ARRAY (rectangular/polar/path), PEDIT, MATCHPROP, JOIN, XCLIP, PRESSPULL-style solid editing on regions, NCOPY, OVERKILL (duplicate cleanup), ERASE, EXPLODE, and grip editing. REGION with boolean UNION/SUBTRACT/INTERSECT, BOUNDARY/BPOLY tracing. Full undo/redo; multi-entity operations are single undo steps.

**Layouts & blocks** — Model and multiple paper-space tabs, title blocks and viewports (MVIEW/VPSCALE), sheet sets. BLOCK/INSERT/ATTDEF/ATTEDIT, dynamic blocks (BPARAMETER: linear, flip, rotation, visibility, array, lookup), BEDIT opens a real in-place block editor (every 2D command works inside it), PADADD/PINADD turn a block into a PCB footprint or schematic symbol, WBLOCK, XREF, GROUP, PURGE.

**Drafting aids & organization** — object snap, ortho, grid snap, polar tracking, object snap tracking, layers (LAYISO/LAYOFF/LAYFRZ/LAYMRG/LAYERSTATE/LAYTRANS), linetypes and lineweights, fields, data extraction, tables, AutoLISP interpreter, ACTRECORD/ACTSTOP macro recording.

**Input/output** — DXF read/write, DWG import and export via LibreDWG (optional, see below), fit-to-page printing and PDF export.

### Electronic Design (KiCad-style)

**Schematic capture** — WIRE, BUS/BUSENTRY (bundled multi-signal buses), JUNCTION, NOCONNECT, NETLABEL, PINADD (defines a symbol's pins on a block), hierarchical sheets (SHEETNEW/SHEETGOTO), ERC (electrical rule checking: driver conflicts, unconnected pins, missing footprints), NETLIST export, ANNOTATE (auto reference-designator numbering), BOM generation, wire numbering, wire lists.

**PCB layout** — TRACK, VIA, copper pours, diff pairs, length tuning, teardrops, via stitching, panelization, multi-layer stackups, net classes, keepout zones, ratsnest, DRC, autorouting (including rip-up-and-reroute and multi-layer via insertion), FOOTPRINTGEN (parametric QFP/SOIC/HEADER/CHIP/SOT23/SOT223/BGA/mounting-hole/fiducial footprints) and PADADD (hand-placed pads on any block), pick-and-place export, Gerber X2 and Excellon drill export, Specctra DSN export.

**File interop** — real .kicad_sch, .kicad_pcb, and .kicad_mod read/write.

### 3D Modeling (FreeCAD-style)

Its own window (opened via the 2D app), built on a real B-rep kernel (OpenCASCADE), with a feature tree and undo/redo.

**Sketch-based parametric features** — New Sketch (with 2D geometric/dimensional constraints and a real constraint solver) or Sketch on Face; Pad/Pocket, Revolve/Groove, Loft, Sweep, Fillet, Chamfer, Draft, Hole, Shell, Linear/Polar/Scaled Pattern, Mirror, Slice, PressPull, Delete Face (feature suppression), Offset Solid (whole-solid grow/shrink), Helix.

**Assembly** — components from STEP files, mates (coincident, concentric, tangent, parallel, perpendicular, fixed, slider), DOF checking, interference detection, linear/polar component patterning, parts-list/BOM export, assembly STEP export.

**Specialized workbenches** — Sheet Metal (flanges, flat-pattern export), BIM/Architecture (walls, slabs, columns, beams, roofs, stairs, doors/windows, rooms, IFC-lite import/export, room/opening schedules), FEM (static, modal, and thermal analysis), TechDraw (drawing/section/detail/auxiliary views from the 3D model), Piping, CAM (3D toolpath generation), a Variables/Spreadsheet system, and a 3D AutoLISP console.

**File interop** — STEP and IGES import/export, plus KumCAD's own native .kcad3d format.

## Building

Requires CMake ≥ 3.21, a C++20 compiler, and Qt 6 (Widgets, OpenGLWidgets, PrintSupport).

```sh
cmake -B build
cmake --build build -j
./build/src/app/kumcad
```

Run the test suite (Catch2, fetched automatically):

```sh
ctest --test-dir build --output-on-failure
```

### 3D modeling (optional)

The 3D modeling window requires [OpenCASCADE (OCCT)](https://dev.opencascade.org/). If it's found by CMake's `find_package(OpenCASCADE)`, the 3D core builds automatically; if not, KumCAD still builds and runs fully as a 2D/EDA application, just without the 3D window.

### DWG import/export (optional)

DWG reading and writing uses [LibreDWG](https://www.gnu.org/software/libredwg/) (GPLv3), which is why KumCAD itself is licensed GPLv3 — see [License](#license) below. If LibreDWG isn't packaged for your distro, build it from source into your user prefix:

```sh
curl -LO https://github.com/LibreDWG/libredwg/releases/download/0.13.3/libredwg-0.13.3.tar.gz
tar xzf libredwg-0.13.3.tar.gz && cd libredwg-0.13.3
./configure --prefix=$HOME/.local --disable-shared --enable-static --disable-bindings --with-pic CFLAGS="-O2 -fPIC -Wno-error"
make -j && make install
```

Then reconfigure KumCAD (`cmake -B build`) — it picks LibreDWG up automatically, enabling *.dwg in the Open dialog and DWG (R2000) in Save As. DXF remains the lossless, dependency-free format; you can build KumCAD entirely without LibreDWG (`-DLCAD_ENABLE_DWG=OFF`) and lose nothing but DWG itself.

### Platform support

The source itself has no platform-specific code (no `#ifdef _WIN32`/`__APPLE__` branches, no POSIX-only calls) — portability comes for free from Qt6 and OpenCASCADE. [`.github/workflows/ci.yml`](.github/workflows/ci.yml) builds and runs the full test suite on Linux, Windows, and macOS on every push, so all three are continuously verified, not just assumed to work.

| Platform | Build | Packaged distributable |
|---|---|---|
| Linux (x86_64) | Native, `cmake -B build && cmake --build build` | `packaging/linux/build-appimage.sh` → portable AppImage |
| Windows (x86_64) | Cross-compiled from Linux via mingw-w64 | `packaging/windows/build-windows.sh` → portable .zip (kumcad.exe + DLLs) |
| macOS (Homebrew Qt6/OCCT) | Native, same three commands above | `packaging/macos/build-macos.sh` → kumcad.app + .dmg (script requires running on an actual Mac — macdeployqt/iconutil aren't cross-buildable from Linux; unlike the other two, this one hasn't been run on physical Apple hardware yet, so treat the packaged output as unverified until someone does) |

Each packaging script documents the platform-specific quirks it works around in its header comment.

## Usage

See [docs/QUICKSTART.md](docs/QUICKSTART.md) for a walkthrough (first launch, drawing/editing, PCB, and 3D) and a command cheat sheet.

The short version: type a command in the command line at the bottom (or use the toolbar) and follow the prompts — points can be clicked in the canvas or typed as `x,y`. Enter/right-click finishes a command, Escape cancels. Select entities first for modify commands (MOVE, TRIM edges, etc.), exactly like AutoCAD's noun-verb workflow. Schematic and PCB commands (WIRE, TRACK, FOOTPRINTGEN, DRC, ...) run in this same command line, since schematic/PCB entities live in the same document as ordinary drawing entities. 3D modeling is a separate, OCCT-backed top-level window with its own toolbar and feature tree — pick "3D Modeling" on the startup welcome screen to open one (it isn't reachable from an already-open 2D window yet).

## Status

KumCAD is a young, single-maintainer-plus-AI-assistance project, not a production-tested replacement for AutoCAD, KiCad, or FreeCAD. The command/feature coverage across all three areas is real (backed by an extensive automated test suite covering the core logic), but it hasn't had sustained real-world usage outside its own development — treat it as good for learning, personal projects, and experimentation, and expect rough edges under heavier or more unusual real-world use. Contributions, bug reports, and real-world testing are very welcome — see [CONTRIBUTING.md](CONTRIBUTING.md).

## License

KumCAD is licensed under the [GNU General Public License v3.0](LICENSE) (or, at your option, any later version). This choice follows from the optional DWG import/export feature, which links LibreDWG (GPLv3) — see [DWG import/export](#dwg-importexport-optional) above; you can build without it (`-DLCAD_ENABLE_DWG=OFF`), but the project as a whole is licensed GPLv3 either way for consistency.
