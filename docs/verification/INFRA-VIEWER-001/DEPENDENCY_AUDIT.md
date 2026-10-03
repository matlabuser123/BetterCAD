# INFRA-VIEWER-001 — the dependency audit

```text
SUBJECT:  what excludes OCCT's visualization layer today, what turning it on
          costs, and what was decided
DATE:     2026-10-03
```

## What was excluding it

`deps/CMakeLists.txt` builds OCCT 8.0.1 from a pinned, hash-verified source
archive (`V8_0_1`, SHA256 `0d6913ea…`) with the same toolchain as the project,
and switched the module off explicitly:

```cmake
# Geometry kernel + STEP/STL exchange only.
-DBUILD_MODULE_Visualization:BOOL=OFF
-DUSE_OPENGL:BOOL=OFF
-DUSE_GLES2:BOOL=OFF
-DUSE_D3D:BOOL=OFF
-DUSE_FREETYPE:BOOL=OFF
```

That was the right call when it was made — nothing displayed anything — and it
is what `P16-VIZ-001` ran into.

## The state it left behind, measured

```text
libTKV3d.dll.a         PRESENT in the prefix, not linked by BetterCAD
libTKService.dll.a     PRESENT in the prefix, not linked
AIS_InteractiveContext.hxx   present
V3d_View.hxx                 present
Aspect_DisplayConnection.hxx present

TKOpenGl               ABSENT -- no library, no DLL, no headers
OpenGl_GraphicDriver.hxx     ABSENT
```

`TKV3d` and `TKService` reach the prefix as **transitive requirements of
DataExchange**, which is why the headers for `AIS` and `V3d` are there and
look usable. They are not: `V3d_Viewer` needs a `Graphic3d_GraphicDriver`, and
the only usable implementation is `OpenGl_GraphicDriver`, which lives in the
toolkit that was never built.

So the gap was never "link two more libraries". It was a dependency rebuild.

## FreeType — audited first, and NOT admitted

This was the question most likely to turn a module switch into a new
third-party dependency, so it was settled before anything was changed. OCCT
8.0.1 guards it:

```cmake
if (CAN_USE_FREETYPE AND USE_FREETYPE)
  message (STATUS "Info: FreeType is used by OCCT")
  add_definitions (-DHAVE_FREETYPE)
  ...
else()
  OCCT_CHECK_AND_UNSET ("USE_FREETYPE")
  ...
endif()
```

It only adds a define. **The Visualization module builds without FreeType**;
what is lost is text rendering inside the 3D view.

```text
DECISION: FreeType stays OFF.
WHY:      admitting it means a fourth from-source dependency -- pinned,
          hash-verified, built with this toolchain, and qualified -- for
          labels nothing has asked for yet. P16-VIZ-001's checklist wants
          element and node IDs "during inspection", which a Qt overlay or a
          property panel can show without OCCT drawing glyphs into the scene.
WHEN TO   when something needs text drawn IN the 3D scene. That is the
REVISIT:  milestone to add it in, with its own gate.
```

This matters beyond convenience: `INFRA-NETGEN-001` established that admitting
a dependency is its own gated decision, including its licence. Not admitting
one is the cheaper answer whenever it is honest.

## What changed

Two lines of intent in `deps/CMakeLists.txt`:

```text
-DBUILD_MODULE_Visualization:BOOL=OFF   ->  ON
-DUSE_OPENGL:BOOL=OFF                   ->  ON
```

Everything else is untouched: the same pinned tag, the same SHA256, the same
toolchain, the same prefix, the same `BUILD_LIBRARY_TYPE=Shared`, the same
`BUILD_RELEASE_DISABLE_EXCEPTIONS=OFF`. GLES2, D3D, FreeType, Draw, VTK, TBB
and the ApplicationFramework module all stay off.

`deps/` is outside the source fingerprint
(`apps include src tests examples cmake CMakeLists.txt CMakePresets.json`), as
`INFRA-NETGEN-001` established, so this is a toolchain change rather than a
change to the qualified tree.

## Cost

Changing the CMake cache invalidates the whole external build, so this is a
full rebuild of OCCT rather than an incremental one:

```text
5606 objects, from TKernel upward
```

Recorded because it is the honest cost of the switch, and because a future
reader deciding whether to flip another OCCT option should know that any
option change costs the same.

## Where the viewer code may live

Set by `ARCHITECTURE.md` and enforced by `tests/architecture/CheckLayering.cmake`,
not by preference:

```text
rule 2   Qt headers only from apps/bettercad/ and src/renderer/
         qt_allowed_regex   ^(apps/bettercad|src/renderer)/
rule 1   OCCT headers (.hxx) only from an occt/ adapter directory
         occt_allowed_regex ^src/(.+/)?occt/
layer    renderer = 6, so it may use every module below it
```

So:

```text
include/bettercad/renderer/   public headers -- no Qt, no OCCT
src/renderer/                 the module (layer 6)
src/renderer/occt/            the ONLY place OCCT visualization headers appear
apps/bettercad/               the Qt widget that hosts the view
```

**At the time of this audit** `src/renderer/` was an empty directory holding
one `.gitkeep`, and `src/CMakeLists.txt` did not add it. The module was
nevertheless already registered in the layering table, so no renumbering was
needed — the same property that let `meshing` share layer 4 with `drawing`.
This paragraph describes the tree the audit examined, not the tree this
milestone committed.

## The gating question

Everything above is mechanical. The one thing that cannot be assumed is
whether a view can actually be **created and rendered without a visible
window** on this toolchain, from a test, under CTest.

```text
needed   OpenGl_GraphicDriver + V3d_Viewer + V3d_View
         a window object the driver accepts with no desktop window
         V3d_View::ToPixMap, which renders through an FBO
risk     a machine or session with no usable OpenGL implementation has no
         driver to create, and the answer would be NOT QUALIFIABLE here
         rather than a defect in the code
```

That experiment is the next step, and it is deliberately the smallest possible
one: create a driver, create a view, render an empty scene offscreen, and read
the pixels back. Nothing about BetterCAD, nothing about meshes, nothing about
Qt. If it fails, the milestone reports **NOT QUALIFIABLE** with the exact
failing call — which is what `INFRA-NETGEN-001`'s "no ambiguous middle state"
rule requires.

## Revision

First issue, 2026-10-03.
