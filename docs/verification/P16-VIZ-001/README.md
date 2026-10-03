# P16-VIZ-001 — Mesh Visualisation / Inspection

```text
STATUS:   BLOCKED
TASK:     P16-VIZ-001 -- mesh visualisation and inspection
PHASE:    P16 -- Meshing
DATE:     2026-10-03
```

**BetterCAD has no 3D viewport.** The desktop application is 118 lines: a
window with File and Help menus, a status bar, and a `QLabel` reading
*"Viewport — not implemented yet"*. `src/renderer/` holds one file, `.gitkeep`.
There is no CAD display, no selection system, and the OCCT dependency is built
without its visualization driver.

Sixteen of the milestone's eighteen checkboxes, and four of its five gate
clauses, require those. The audit is in
[VIEWER_AUDIT.md](VIEWER_AUDIT.md).

**No production code was written and no checkbox was ticked.**

## Baseline

```text
branch        main
HEAD          e4c2bfb  BetterCAD: add geometry-to-mesh correspondence
tree          9d0f78f9cf9a014395ca49981bf7bcd7466c5ad6
origin/main   e4c2bfb  (HEAD == origin/main)
working tree  clean
compiler      GNU 16.1.0 (MinGW, WinLibs POSIX UCRT), C++23
Qt            6.11.2
OCCT          8.0.1, modelling toolkits only -- see below
```

## Prerequisites — all met

Verified from the committed evidence, which is why the block is not a
prerequisite failure:

```text
P16-ARCH-001      25/25, PASS    9964f88
P16-DATA-001      26/26, PASS    20b04b9
P16-GEOM-001      20/20, PASS    9c755e8
P16-SURF-001      21/21, PASS    4005815
INFRA-NETGEN-001        PASS     eaf7da4
P16-VOL-001       19/19, PASS    e9fd82c
P16-SIZE-001      21/21, PASS    1ec54fc
P16-QUALITY-001   19/19, PASS    b5f9137
P16-MAP-001       20/20, PASS    e4c2bfb
```

The engineering state this milestone would display is complete and qualified.
What is missing is the means to display it.

## Why BLOCKED and not PARTIAL

`CLAUDE.md` names four stop conditions, and three apply:

```text
completing the task needs work outside the authorized milestone
    a 3D viewport, CAD display and a selection system are the ROADMAP's
    "Desktop Application" goal. TODO.md authorizes "Mesh Visualisation /
    Inspection", not the environment it would live in.

a claim cannot be verified in this environment
    "stale mesh obvious", "mesh inspectable", "CAD/mesh mapping visible" are
    claims about what a person can see. With nothing that displays, they
    cannot be verified -- and asserting them from an array of floats would be
    the false completion the rules forbid.

the task needs evidence that does not exist
    the brief requires GUI smoke tests of show/hide, wireframe toggle, picking
    and camera navigation (step 93), and a viewer audit table (step 2) whose
    rows do not exist.
```

The brief's own step 2 says *"Do not create a parallel viewer framework if the
existing one is suitable."* There is none to judge.

## The measured gap

```text
apps/bettercad/                 118 lines, "P0 placeholder" by its own comment
src/renderer/                   empty, one .gitkeep
AIS_* V3d_* Graphic3d_*         0 occurrences in the repository
OpenGl_* Aspect_*               0 occurrences
Qt outside apps/bettercad/      none
GUI tests                       one: --smoke-test -platform offscreen

OCCT toolkits linked            modelling and exchange only; no visualization
OCCT TKV3d, TKService           present in the dependency prefix, unlinked
OCCT TKOpenGl                   ABSENT from the dependency build entirely
OpenGl_GraphicDriver.hxx        ABSENT
Qt6OpenGL, Qt6OpenGLWidgets     present
```

`V3d_Viewer` needs a `Graphic3d_GraphicDriver`, and the only usable one is
`OpenGl_GraphicDriver` from `TKOpenGl`. **A view cannot be instantiated until
the OCCT dependency is rebuilt with its visualization driver** — a `deps/`
change and a toolchain qualification, which is exactly the shape
`INFRA-NETGEN-001` already established.

## What would unblock it

A scope decision, and it is the owner's. Two candidates, with the precedent
this phase already set:

```text
A  INFRA-VIEWER-001  (the INFRA-NETGEN-001 shape)
   rebuild OCCT with its visualization driver; embed a V3d view in a Qt
   widget; camera with orbit/pan/zoom and standard views; CAD body display; a
   selection system with owners, highlight and visibility state; an
   offscreen-capable smoke test.
   Then P16-VIZ-001 on top, as specified.

B  the headless adapter layer alone, as its own TODO entry
   flattened render buffers derived from VolumeMesh and
   EngineeringSurfaceMesh; a revision-keyed map from render primitive to
   NodeId / ElementId / boundary facet; a visual state over the existing
   currentness contract; quality classification read from MeshQualityReport;
   worst-element lookup per metric; CAD <-> mesh resolution through
   GeometryMeshMap.
   All Qt-free and headless-testable. It is NOT what P16-VIZ-001's gate asks
   for -- four of five clauses are about what a person can see -- so it would
   need its own entry and its own gate.
```

Option B was deliberately not started: its consumer does not exist, so buffer
layout, batching and the picking contract would be guesses, and the brief
itself asks for the batching decision to be documented against something
measurable (step 78).

## Result

```text
RESULT:   BLOCKED
TODO:     P16-VIZ-001 unchanged, 0/18, recorded as blocked with the reason
NEXT:     a scope decision -- authorize the viewer infrastructure, authorize
          the headless adapter alone, or leave this blocked and pick a
          different milestone
```

## Revision

First issue, 2026-10-03.
