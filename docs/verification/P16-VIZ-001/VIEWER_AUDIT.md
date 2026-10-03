# P16-VIZ-001 — viewer architecture audit

```text
SUBJECT:  what BetterCAD's viewer is today, and what P16-VIZ-001 would need
DATE:     2026-10-03
RESULT:   the milestone is BLOCKED. There is no viewer.
```

The milestone brief's step 2 asks for an audit of the existing viewer before
any production code, and warns: *"Do not create a parallel viewer framework if
the existing one is suitable."* This is that audit. **There is no existing
one.**

## The required table

```text
Existing viewer object | Purpose | Canonical owner | Selection path | Reusable? | Risk
---------------------- + ------- + --------------- + -------------- + --------- + ----
                            (no rows: none of these exists)
```

## What the desktop application actually is

`apps/bettercad/` is **118 lines in three files**, and its own header comment
says what it is:

```cpp
/// Top-level window of the desktop application. P0 placeholder: menus, status
/// bar and an empty viewport area; modelling UI arrives with later milestones.
class MainWindow final : public QMainWindow {
```

The central widget is a label:

```cpp
auto* viewport = new QLabel(tr("Viewport — not implemented yet"), this);
viewport->setObjectName(QStringLiteral("ViewportPlaceholder"));
```

Everything else is a File menu with Quit, a Help menu with About, and a status
bar reading "Ready".

```text
apps/bettercad/MainWindow.cpp    55 lines
apps/bettercad/main.cpp          43 lines
apps/bettercad/MainWindow.hpp    20 lines
src/renderer/                    EMPTY -- one .gitkeep
```

## Measured absences

Each of these is a `grep` over `src/ apps/ include/ tests/ cmake/ deps/`:

```text
AIS_*            0 occurrences      no interactive objects, no AIS_Shape
V3d_*            0 occurrences      no view, no viewer
Graphic3d_*      0 occurrences      no graphic driver, no structures
OpenGl_*         0 occurrences      no rendering
Aspect_*         0 occurrences      no display connection, no window
QWidget outside apps/bettercad     0 occurrences
QOpenGL / QPainter / QGraphics      0 occurrences in src/ or include/
```

So there is no scene graph, no presentation or view model, no selection owner,
no document/view synchronisation, no visibility control, no section view, no
measurement overlay and no annotation display. There is also **no CAD display
at all** — nothing draws a solid, so there is nothing for a mesh to be
inspected against or selected alongside.

The only GUI test in the repository is

```text
tests/CMakeLists.txt:1411   "$<TARGET_FILE:bettercad>" --smoke-test -platform offscreen
```

which starts the application, shows the window and exits.

## The dependency is not built for a viewer

This is the part that makes the gap bigger than a few files of Qt.

```text
OCCT toolkits LINKED by BetterCAD today
  TKernel TKMath TKG TKG2d TKG3d TKBRep TKBO TKBool TKPrim TKTopAlgo
  TKGeomAlgo TKGeomBase TKFillet TKOffset TKHLR TKMesh TKDESTEP TKLCAF
  TKXCAF TKXSBase
  -- modelling and exchange only. No visualization toolkit is linked.

OCCT visualization in the dependency PREFIX
  libTKV3d.dll.a         present     AIS_*, V3d_* live here
  libTKService.dll.a     present
  AIS_InteractiveContext.hxx   present
  V3d_View.hxx                 present
  Aspect_DisplayConnection.hxx present

  TKOpenGl               ABSENT -- no library, no DLL
  OpenGl_GraphicDriver.hxx     ABSENT
```

`V3d_Viewer` needs a `Graphic3d_GraphicDriver`, and the only usable
implementation is `OpenGl_GraphicDriver` from `TKOpenGl`. **It is not in this
build of the dependency.** So a view cannot be instantiated at all until OCCT
is rebuilt with its visualization driver enabled — a `deps/` change and a
toolchain qualification, which is the shape `INFRA-NETGEN-001` already
established for admitting a dependency.

Qt's own `Qt6OpenGL` and `Qt6OpenGLWidgets` **are** present, so the Qt side of
an embedded view is available.

## What the roadmap says

`ROADMAP.md` is explicit, and it agrees:

> **Desktop Application.** The desktop executable is a placeholder shell. The
> goal is the first practical interactive environment on Qt 6: command system,
> model tree, property editor, **3D viewport**, sketch environment, diagnostics
> panel; orbit/pan/zoom/standard views; **selection of bodies, faces, edges,
> vertices**, sketches and features.

A 3D viewport and CAD selection are **direction, not qualified capability**.

## The checklist against the facts

```text
checkbox                                  needs                      available
Display surface mesh                      a viewport                 NO
Display volume-mesh boundary              a viewport                 NO
Optional interior element inspection      a viewport + clipping      NO
Wireframe / edge display                  a viewport                 NO
Node inspection                           picking + selection        NO
Element inspection                        picking + selection        NO
Element ID display                        on-screen labels           NO
Quality inspection                        a viewport                 NO
Worst-element navigation                  camera framing             NO
Boundary-region highlighting              highlight state in a scene NO
CAD <-> mesh selection linkage            CAD DISPLAY + selection    NO
Mesh visibility toggle                    something visible          NO
Clear stale-mesh visual state             visual state               NO
Distinguish current vs invalidated mesh   visual state               NO
Do not duplicate canonical mesh in GUI    --                         vacuous
Rendering does not mutate mesh            --                         vacuous
```

**Sixteen of eighteen require a viewport that does not exist.** The remaining
two are satisfied today only because there is no GUI model and no rendering,
which is not a result anyone should record as a pass.

## Why not build the viewer here

Because it is not this milestone, and it is far larger than it.

```text
P16-VIZ-001 is          Mesh Visualisation / Inspection
building a viewer is    rebuilding the OCCT dependency with its visualization
                        driver; embedding an OCCT view in a Qt widget; a
                        graphic driver, display connection and window; a
                        camera with orbit/pan/zoom and standard views; CAD
                        display of bodies; a selection system with owners for
                        faces, edges and vertices; highlight and visibility
                        state; and document/view synchronisation
```

`TODO.md` authorizes the first. The second is the ROADMAP's "Desktop
Application" goal, and `CLAUDE.md` is unambiguous: *"completing the task needs
work outside the authorized milestone -> finish what is authorized, and say
what was left out and why"*, and *"ROADMAP does not authorize implementation.
TODO.md authorizes implementation."*

Building it quietly inside this milestone would also mean a milestone whose
production code is ninety percent prerequisite and ten percent subject, with
the prerequisite ungated.

## What could be built without a viewport, and why it was not

The brief's own architecture separates an adapter from the display:

```text
canonical P16 mesh state
    -> read-only visualisation adapter      <- Qt-free, headless-testable
    -> viewer/render representation         <- needs a viewport
    -> selection / inspection UI            <- needs a viewport
```

The first arrow is implementable today: flattened vertex, triangle and edge
buffers derived from `VolumeMesh` and `EngineeringSurfaceMesh`; a
revision-keyed map from render primitive index to `NodeId` / `ElementId` /
boundary facet; a visual state enum over the existing currentness contract;
quality classification read from `MeshQualityReport`; worst-element lookup per
metric; and CAD <-> mesh selection resolution through `GeometryMeshMap`.

**It was not built, for one reason: its consumer does not exist.** Buffer
layout, batching strategy and the picking contract are decisions that can only
be made correctly against a real renderer — the brief itself asks for the
batching decision to be *documented* (step 78), which presumes something to
measure. Writing them now would be the speculative abstraction `CLAUDE.md`
forbids, and the milestone's gate would still fail:

```text
mesh inspectable            an array of floats is not inspectable
stale mesh obvious          obvious to whom? nothing displays it
quality defects inspectable the report already exists; seeing them is the point
CAD/mesh mapping visible    nothing is visible
```

Four of five gate clauses are about what a person can see.

## Recommendation

The precedent is in this phase already. `P16-VOL-001` was blocked on a missing
meshing backend, and the answer was `INFRA-NETGEN-001`: a separate, gated
infrastructure milestone that qualified the dependency and the toolchain, after
which the blocked milestone went through cleanly.

The same shape applies:

```text
INFRA-VIEWER-001 (or P16-VIZ-000) -- a scope decision, not Claude's
    rebuild the OCCT dependency with its visualization driver (TKOpenGl)
    embed an OCCT V3d view in a Qt widget, with a graphic driver and
      display connection
    camera: orbit, pan, zoom, standard views
    CAD body display
    a selection system with owners, and highlight and visibility state
    an offscreen-capable smoke test, as the existing one is

then P16-VIZ-001 on top of it, as specified
```

An alternative, if a viewport is not wanted yet: authorize the **adapter layer
alone** as its own milestone with a gate that does not mention visibility —
counts, identity mapping, revision invalidation, state transitions and
non-mutation, all headless. That is real, qualifiable work and it would make
`P16-VIZ-001` small when the viewport arrives. It is also not what
`P16-VIZ-001`'s gate asks for, so it would need its own entry in `TODO.md`.

Either way the choice is the owner's.

## Result

```text
RESULT:   BLOCKED
REASON:   BetterCAD has no 3D viewport, no CAD display and no selection
          system, and the OCCT dependency is built without its visualization
          driver. 16 of 18 checkboxes and 4 of 5 gate clauses require them.
NEXT:     a scope decision: authorize the viewer infrastructure, or authorize
          the headless adapter layer alone, or leave P16-VIZ-001 blocked.
CODE:     none written. No checkbox ticked.
```

## Revision

First issue, 2026-10-03.
