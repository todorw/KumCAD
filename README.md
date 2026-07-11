# KumCAD

A free, open-source 2D CAD application in the spirit of AutoCAD, built with C++20 and Qt 6.

KumCAD follows AutoCAD's conventions wherever possible — the command line drives everything, command names and aliases match (`L`, `C`, `TR`, `MI`, ...), and drawings are exchanged as DXF.

## Features

**Drawing** — LINE, CIRCLE, ARC (3-point), PLINE (line and tangent-arc segments, Close), SPLINE (fit-point B-spline), RECTANG, ELLIPSE, POINT (PDMODE/PDSIZE marker styles) with DIVIDE/MEASURE, XLINE/RAY construction lines, TEXT and MTEXT using named text styles (STYLE: font, fixed height, width factor, oblique), HATCH (solid fill or ANSI31/32/33/37 patterns with scale and angle), LEADER (arrow + annotation), and DIMENSIONS: linear, aligned, radius, diameter, and angular, with named dimension styles (DIMSTYLE: create/restore, text height, arrow size, decimals).

**Editing** — MOVE, COPY, ROTATE, SCALE, MIRROR, OFFSET (including polylines with arcs and ellipses), TRIM, EXTEND, FILLET (tangent arc or sharp corner), STRETCH (crossing window), LENGTHEN (DElta/Percent/Total), BREAK (two-point or at-point), ALIGN (move+rotate+optional scale), ARRAY (rectangular and polar), PEDIT (Close/Open/Join/Decurve), MATCHPROP, ERASE, EXPLODE, and grip editing (drag endpoints, midpoints, centers, quadrants). Full undo/redo; multi-entity operations are single undo steps.

**Layouts** — Model and multiple paper-space tabs (LAYOUT New/Copy/Rename/Delete); draw title blocks and notes directly on the sheet, PAGESETUP sets paper size and orientation, MVIEW places viewports (drag to move, VPSCALE to set the scale, Del to remove), and printing a layout plots the sheet with its viewports and paper entities.

**Blocks** — BLOCK turns a selection into a reusable definition; INSERT places it anywhere and prompts for attribute values (ATTDEF defines tags inside blocks); EXPLODE breaks it back apart. XREF attaches an external DXF/DWG as a live reference (reloaded from disk on open, dimmed on screen, cached in the file so drawings open even when the reference is missing). GROUP names a selection so one click selects it all; PURGE drops unused blocks and layers.

**Drafting aids** — object snap (endpoint, midpoint, center, quadrant, node, intersection, perpendicular, tangent, nearest — toggled per-kind via OSNAP, F3), ortho mode (F8), grid snap (F9), polar tracking (F10, POLARANG sets the increment), object snap tracking with alignment guides (F11), crossing/window selection, measurement commands (DIST, AREA, ID). Dimensions drawn with object snap are associative: they follow the geometry they measure.

**Organization** — layers with visibility, locking, colors, linetypes, and lineweights; per-entity color/linetype/lineweight overrides (ByLayer default, LWEIGHT + LWDISPLAY); linetypes (CONTINUOUS, DASHED, DOT, DASHDOT, CENTER, HIDDEN, PHANTOM) with LTSCALE; a dockable Properties panel and Layers panel.

**Input/output** — DXF read/write (ACI and true colors, old-style POLYLINE, polyline bulges, splines, MTEXT, hatch patterns, leaders, all dimension kinds, linetypes, lineweights, STYLE/DIMSTYLE tables, blocks with attributes, xrefs, and multiple layouts with viewports and paper entities), DWG import **and export** via LibreDWG (optional, see below), plus fit-to-page printing and PDF export of model or layout.

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

### DWG import (optional)

DWG reading uses [LibreDWG](https://www.gnu.org/software/libredwg/). If it isn't packaged for your distro, build it from source into your user prefix:

```sh
curl -LO https://github.com/LibreDWG/libredwg/releases/download/0.13.3/libredwg-0.13.3.tar.gz
tar xzf libredwg-0.13.3.tar.gz && cd libredwg-0.13.3
./configure --prefix=$HOME/.local --disable-shared --enable-static --disable-bindings --with-pic CFLAGS="-O2 -fPIC -Wno-error"
make -j && make install
```

Then reconfigure KumCAD (`cmake -B build`) — it picks LibreDWG up automatically, enabling *.dwg in the Open dialog and DWG (R2000) in Save As. DWG export covers the core entity set and reports anything it had to skip; DXF remains the lossless format.

## Usage

Type a command in the command line at the bottom (or use the toolbar) and follow the prompts — points can be clicked in the canvas or typed as `x,y`. Enter/right-click finishes a command, Escape cancels. Select entities first for modify commands (MOVE, TRIM edges, etc.), exactly like AutoCAD's noun-verb workflow.

## Status

KumCAD is young and aims at AutoCAD LT-style 2D drafting. Not yet implemented: tables, dynamic blocks, multileader styles, annotative scaling, sheet-set tooling, and DWG export of hatches/leaders/paper space. Contributions welcome.
