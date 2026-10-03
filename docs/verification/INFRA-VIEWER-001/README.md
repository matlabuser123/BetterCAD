# INFRA-VIEWER-001 — OCCT Visualization Toolchain Qualification

```text
STATUS:   QUALIFIED
TASK:     INFRA-VIEWER-001 -- make OCCT's visualization layer buildable,
          runnable, testable and consumable, and give BetterCAD a viewport
PHASE:    infrastructure, out of band
DATE:     2026-10-03
```

**AUTHORIZED BY THE OWNER** on 2026-10-03, after `P16-VIZ-001` was recorded
BLOCKED at e7d90e9. Infrastructure rather than a P16 capability: it exists so
that `P16-VIZ-001` has somewhere to display a mesh.

Same shape as `INFRA-NETGEN-001`, and the same discipline: it ends either
**QUALIFIED** or **NOT QUALIFIABLE**, with no ambiguous middle state.

## Documents

```text
DEPENDENCY_AUDIT.md      what excluded OCCT's visualization layer, what
                         turning it on cost, and the FreeType decision
OFFSCREEN_RENDERING.md   what "offscreen" means on Windows, and how a viewer
                         is qualified without screenshots
ADVERSARIAL_REVIEW.md    5 production defects, 2 test gaps, 4 integration
                         traps, the viewer-specific attacks, the limitations
                         carried, the mutation table, and what went wrong in
                         the review itself
FREEZE.md                the identity of the tree that was qualified, and
                         the gates cleared before the freeze
qualification/           the logs, the mutation harness, and two discarded
                         attempts with the reasons
```

## Baseline

```text
branch        main
HEAD at start e7d90e9  BetterCAD: record P16-VIZ-001 blocked on the absent viewport
tree          cb167c16ef4f54e14d333d0df3b02486352316fa
origin/main   e7d90e9  (HEAD == origin/main)
working tree  clean
compiler      GNU 16.1.0 (MinGW, WinLibs POSIX UCRT), C++23
Qt            6.11.2
OCCT          8.0.1, pinned at V8_0_1, SHA256 0d6913ea...
build root    C:/Users/uqhas/AppData/Local/bc-build  (outside OneDrive)
```

## What forced it

`P16-VIZ-001`'s audit found that BetterCAD had no viewport at all:
`apps/bettercad/` was 118 lines ending in a `QLabel` reading *"Viewport — not
implemented yet"*, `src/renderer/` held one `.gitkeep`, and there were zero
occurrences of `AIS_*`, `V3d_*`, `Graphic3d_*` or `OpenGl_*` in the repository.

And the dependency could not have supplied one: `deps/CMakeLists.txt` built
OCCT with `BUILD_MODULE_Visualization=OFF` and `USE_OPENGL=OFF`. `TKV3d` and
`TKService` reached the prefix as transitive requirements of DataExchange --
which is why the `AIS` and `V3d` headers were there and looked usable -- but
**`TKOpenGl` was absent entirely**, and `V3d_Viewer` needs a
`Graphic3d_GraphicDriver` whose only usable implementation lives there.

## The gate, answered first

The one thing that could not be assumed was whether a view can be created and
rendered **without a visible window** on this toolchain. So that was settled
before any production code, by a program built **outside the repository**
against the installed dependency -- the same approach `INFRA-NETGEN-001` took
with Netgen:

```text
OCCT 8.0.1
driver   created
view     created
window   attached 256x256 (native, never mapped)
empty    256x256, 0 non-background pixels
box      32674 non-background pixels

QUALIFIABLE: offscreen rendering works on this toolchain.
```

Had it failed, the milestone would have reported **NOT QUALIFIABLE** with the
failing call and stopped there.

The probe also produced this milestone's most reusable finding: **an
`Aspect_NeutralWindow` is not enough on Windows.** OCCT asks the window for a
native handle and calls `SetPixelFormat` on its device context, so a
window-less view fails at creation. Offscreen here means a real window that is
never mapped. Full account in
[OFFSCREEN_RENDERING.md](OFFSCREEN_RENDERING.md).

## The dependency change

Two lines of intent, in the same pinned superbuild:

```text
-DBUILD_MODULE_Visualization:BOOL=OFF   ->  ON
-DUSE_OPENGL:BOOL=OFF                   ->  ON
```

Everything else untouched: the same tag, the same SHA256, the same toolchain,
the same prefix, `BUILD_LIBRARY_TYPE=Shared`,
`BUILD_RELEASE_DISABLE_EXCEPTIONS=OFF`. GLES2, D3D, Draw, VTK, TBB and the
ApplicationFramework module all stay off.

**FreeType stays OFF, and that is a decision.** OCCT guards it with
`if (CAN_USE_FREETYPE AND USE_FREETYPE)` and merely drops a define, so the
Visualization module builds without it; what is lost is text drawn inside the
3D scene. Admitting it would mean a fourth pinned, hash-verified, qualified
dependency for labels nothing has asked for -- and `P16-VIZ-001` wants element
and node IDs "during inspection", which a Qt overlay or a property panel
provides. When something needs glyphs in the scene, that is its own decision.
Reasoning in [DEPENDENCY_AUDIT.md](DEPENDENCY_AUDIT.md).

**Cost: a full rebuild.** Changing the CMake cache invalidates the external
build, so this was 5606 objects from TKernel upward rather than an incremental
addition. 0 errors. Worth knowing for next time: *any* OCCT option change
costs the same.

`deps/` is outside the source fingerprint
(`apps include src tests examples cmake CMakeLists.txt CMakePresets.json`), as
`INFRA-NETGEN-001` established, so this is a toolchain change rather than a
change to the qualified tree.

## What was built

```text
include/bettercad/renderer/Viewer.hpp   the public API: no Qt, no OCCT
src/renderer/occt/OcctViewer.cpp        the ONLY file that sees OCCT
apps/bettercad/ViewportWidget.{hpp,cpp} the Qt widget that hosts a view
apps/bettercad/MainWindow.*             the placeholder QLabel replaced
tests/renderer/ViewerTests.cpp          24 tests
tests/compile_fail/ViewerMisuse.cpp     6 compile-failure cases
```

Placement is set by the layering check, not by preference:

```text
rule 1   OCCT headers only from an occt/ adapter directory under src/
rule 2   Qt headers only from apps/bettercad/ and src/renderer/
layer    renderer = 6, already registered -- no renumbering needed
```

## The invariant: the viewer owns no engineering state

```text
canonical        the Document, its features, their Bodies
viewer-derived   presentations, visibility, selection, the camera
```

A `Body` handed to `display` is copied by value, and a `Body` copy shares the
underlying kernel shape -- so the viewer holds a handle to geometry it does not
own and cannot change. There is no `Document` behind it.

Proved two ways. **At runtime**,
`Viewer_NothingItDoesChangesTheModel` fingerprints the kernel's own volume and
the document's revision of the feature, runs every operation the viewer offers,
and requires both unchanged. **At compile time**, six cases:

```text
compile_fail.viewer.display-through-a-mutable-body
compile_fail.viewer.presentation-id-as-object-id
compile_fail.viewer.object-id-as-presentation-id
compile_fail.viewer.presentation-id-from-integer
compile_fail.viewer.copy-a-viewer
compile_fail.viewer.reach-a-document-through-the-viewer
```

`PresentationId` is viewer-local for the same reasons `NodeId` is not a CAD
identity (ADR-031): handed out by one viewer, invalidated when the presentation
goes, never persisted. A pick resolves a `SelectMgr_EntityOwner` to the
`ObjectId` that was displayed, and the graphics concept never leaves the
adapter file.

## How it is tested

No screenshots, and no pixel equality between machines -- drivers differ, and a
test demanding byte-identical framebuffers would be testing the GPU. What is
asserted is **state** and **coverage**: how many pixels differ from the view's
background.

```text
empty scene      coverage == 0
display a body   coverage > 0   and < the whole view
hide it          coverage == 0
show it again    coverage == the original
```

`ToPixMap` returning `true` proves nothing: a view that rendered nothing returns
success and gives back a background-coloured image. Only the comparison
between an empty scene and a populated one establishes that something was
drawn — and that comparison is what found Finding 1.

The coverage measure is itself tested, on a hand-built 2x2 image, rather than
only through the viewer.

## The tests

Twenty-four cases, 1166 assertions, and every name says what it asserts.

```text
CREATION AND VALIDATION
  Viewer_CreatesAnOffscreenViewOnThisToolchain
  Viewer_RefusesAViewWithNoArea
  Viewer_ResizeRefusesAnEmptyArea
  Viewer_RefusesToDisplayAnEmptyBodyOrAnInvalidObject
  Viewer_SelectionRefusesAPresentationItDoesNotHave

DRAWING, MEASURED BY COVERAGE
  Viewer_AnEmptySceneCoversNothing
  Viewer_DisplaysACadBodyAndDrawsIt
  Viewer_BackgroundIsWhatWasAskedFor
  RenderedImage_CoverageAndSamplingAreWellBehavedAtTheEdges

VISIBILITY, WHICH IS NOT DELETION
  Viewer_HidingIsNotDeleting
  Viewer_HidingOneBodyLeavesTheOtherVisible
  Viewer_RemovingAPresentationLeavesTheModelAlone

IDENTITY: A PICK RESOLVES TO CAD, NOT TO GRAPHICS
  Viewer_PickingAnEmptyViewFindsNothing
  Viewer_PickingADisplayedBodyResolvesToItsObject
  Viewer_PickingDistinguishesTwoDisplayedBodies
  Viewer_PickCoordinatesStayInTheRenderedImagesSpaceAfterAResize
  Viewer_SelectionIsReportedAsCadIdentity

CAMERA
  Viewer_StandardViewsChangeWhatIsDrawn
  Viewer_ZoomAndOrbitChangeTheImageWithoutTouchingTheModel
  Viewer_IgnoresANonsensicalZoom
  StandardView_EveryViewHasADistinctName

THE INVARIANT THIS MILESTONE EXISTS TO PROTECT
  Viewer_NothingItDoesChangesTheModel
  Viewer_DisplayingDoesNotRegenerateOrHealTheBody

RESOURCE LIFETIME
  Viewer_ManyViewersInOneProcessEachRenderCorrectly
```

The last one is there because of a wrong hypothesis, and is kept because the
question was worth settling: when failures started wandering between runs, the
first explanation that fitted was a leaked GL context or window per viewer. It
was tested -- thirty viewers created and destroyed in one process, every render
required to be correct -- and it was **wrong**; the wandering had a duller cause
(see the adversarial review). The test stayed, because "a destroyed viewer gives
its native resources back" is a claim worth holding onto.

## The Qt widget, and the honest "no view" state

Qt's `offscreen` platform plugin provides no native window, so a widget under
it cannot make a view -- OCCT reports `ChoosePixelFormat failed`. That is a
**supported, reported state**:

```text
$ bettercad --smoke-test -platform offscreen
viewport: no 3D view (... ChoosePixelFormat failed. Error code: 6)
exit 0
```

On a desktop the same binary prints `viewport: 3D view created`. The smoke test
asserts it printed one or the other, because **a viewport that said nothing
would be indistinguishable from a broken one**. The widget paints the reason in
place of the scene rather than leaving an empty hole, and `display` into a
viewless widget is refused rather than silently ignored.

## Adversarial review

**Five production defects and two test gaps found and fixed, every one by
running or measuring something rather than by reading it.** Full account in
[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

```text
1  AN ERASED BODY CAME BACK AS WIREFRAME. Hide then show gave 1240 covered
   pixels instead of 32674 -- the box's edges. Display(object, update)
   re-displays in the object's OWN display mode, and an AIS_Shape defaults to
   wireframe. No state assertion could have caught it: `visible` flipped
   correctly both ways. Fixed with SetDisplayMode on the presentation.

2  ARGUMENTS VALIDATED AFTER BEING USED. createOffscreen(0, 256) built a
   zero-width window happily and returned Internal instead of InvalidArgument,
   because the size check lived downstream of the window it was meant to
   guard.

3  A PICK TEST THAT COULD NOT FAIL. With one body displayed, a pickAt that
   ignored what it detected would have passed. The test now displays two
   solids 60 mm apart and scans a line across them; without that, the
   corresponding mutation survived.

4  A BOUNDS CHECK THAT COULD NOT BE CAUGHT. Out of bounds returns black, and
   the fixture's corners WERE black, so a clamping at() was indistinguishable
   from a refusing one and that mutation survived. When the sentinel for "no
   value" is a legal value, a fixture containing it cannot test the sentinel.
   Four non-black colours, and the mutation dies.

5  A RESIZE THAT MOVED THE PICTURE BUT NOT THE WINDOW. The image came out at
   the new size -- ToPixMap takes explicit dimensions -- while picks went on
   being normalised against the old window. On a 400x200 image of a 256x256
   window, the drawn content was in rows 80-130 and the picks in rows 120-180:
   a caller scanning the middle row of the image it had just been handed hit
   nothing. Every assertion that compared SIZES passed throughout.

6  QT'S LOGICAL PIXELS ARE NOT THE VIEW'S DEVICE PIXELS. Fixed at the widget
   boundary. LATENT here, and said so: the ratio on this machine is 1.000, the
   non-unit measurements came from QT_SCALE_FACTOR, and the first draft of
   this finding wrongly inferred from a registry value that picks were already
   broken here.
```

**And the review's own process failed once, which is recorded rather than
tidied away.** An interrupted mutation run kept going, judged fourteen
mutations against a tree that was being edited, silently reverted a production
fix from its restore trap, and left the test executable 0 bytes -- which the
shell reported as exit 0 with no output. Those results were deleted, not
filed; the set was re-run once, uninterrupted, against the final tree; and
`mutate.sh` now takes a lock and explains the hazard.

Four integration traps are recorded in the source where the next reader will
hit them: `WIN32_LEAN_AND_MEAN` is load-bearing (`windows.h` otherwise declares
a global `byte` that shadows a parameter in `core/Uuid.hpp` and fails
`-Wshadow -Werror`); `windows.h` must precede OCCT's headers or `CS_OWNDC`
disappears; `Aspect_NeutralWindow` is not a window-less view; and OCCT 8.0.1
deprecates `Standard_True/False/Integer/Size` and `GetMessageString()`, which
`-Werror` turns into errors.

## Regression

Three presets, each cleaned, configured, built from scratch, rebuilt to prove
nothing was left to do, and run in full. Harness `qualification/qualify.cmd`,
carried byte for byte from `P15-QUAL-001`
(`git hash-object d313a64070718c44fae290ac042fe259d1a03c8b`); invocation
`qualification/run-qualification.cmd`.

```text
                   configure  clean  build        rebuild  full suite
debug-ext              0        0      0 592/592     0     0  3151/3151
release-ext            0        0      0 592/592     0     0  3151/3151
debug-shared-ext       0        0      0 592/592     0     0  3151/3151

compiler diagnostics   0 errors, 0 warnings, 0 FAILED, under -Werror,
                       in all three builds

determinism, 191 tests x5
  release-ext          100% of 191     3199 s
  debug-ext            100% of 191     3465 s

qualify.cmd exit 0 -- "every stage exited 0", 0 stage(s) failed
ran  2026-10-03 20:46:58  ->  2026-10-04 00:11:46
```

9453 test executions across the three presets, plus 1910 in the determinism
stages, with no failure anywhere. The no-op rebuild log is
`[1/8] Checking git revision` in each preset — byte-identical in shape to
`P16-MAP-001` and `P16-QUALITY-001`, which is this project's signature for
"nothing left to build": the git-revision command always reruns, leaves its
output unchanged, and Ninja prunes everything downstream.

The 191 selected by the determinism filter include `unit.RenderedImage_*`,
which does **not** begin with `Viewer_` and which the obvious filter therefore
missed — a gap found and closed before the freeze, and the reason the count is
191 rather than 168.

## The qualified tree is the committed tree

Recorded independently before the first build, and again by the harness after
the last test run. All three agree.

```text
apps               40cedc927b030d484dd68b7d1bce7860be75e811
include            cc90240ab3e9cdfa084196ca9d35686d102d8efc
src                015afd935975b906d3253ff312cbf5476181921f
tests              7d40189fcea6073233c97d2c80ae420e3e06b5f2
examples           9187931fa6824e98d3da6296cdc40af6ca4b8607
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e

whole fingerprint  335f7fc176224ffd74db687b6097a664ee4d4fde
```

Details, and the pre-freeze gates, in [FREEZE.md](FREEZE.md).

## Two attempts were discarded, and why

This milestone took three qualification runs. Both failures are kept, because
a milestone that hides its failed attempts is not evidence of anything.

```text
attempt-1-out-of-memory/
    cc1plus.exe: out of memory allocating 4198399 bytes, at object 286 of 592.
    16 cores means `cmake --build` runs 18 compilers at once, and a CLEAN build
    is 592 template-heavy translation units. ONE resource failure became FIVE
    failed stages: release-ext's build died the same way, debug-shared-ext's
    CONFIGURE failed because its try_compile checks had no memory either, and
    both repeat stages then failed for want of binaries. Fixed by capping
    CMAKE_BUILD_PARALLEL_LEVEL at 6 in this milestone's wrapper, which weakens
    no gate: same sources, same -Werror, same rebuild check, same suite, same
    repeats. Only the number of concurrent compilers changes.

attempt-2-clobbered-by-an-edit/
    PASSED -- "every stage exited 0" -- and was discarded anyway, because its
    stage record was destroyed before it could be read. cmd.exe reads a batch
    file incrementally BY BYTE OFFSET while executing it; two minutes into the
    run I reworded a comment in run-qualification.cmd, which moved everything
    after the cursor, so when qualify.cmd returned cmd resumed at a stale
    offset and called it a SECOND time. That pass truncated
    qualification-times.txt and overwrote three of debug-ext's logs.
    The verdict survived and the inference was sound -- but "the debug build
    passed" would have rested on a transcript rather than on this directory,
    and a qualification that has to be argued for is not one.
```

Both lessons are now recorded where they will be read: the parallelism cap and
its reasoning in `run-qualification.cmd`, and the never-edit-a-running-batch-file
hazard at the top of the same file.

## Result

```text
TASK:            INFRA-VIEWER-001 -- OCCT visualization toolchain qualification
IMPLEMENTATION:  OCCT rebuilt with Visualization + OpenGL from the same pinned,
                 hash-verified source; a renderer module (layer 6) whose public
                 API has no Qt and no OCCT; one OCCT adapter; a Qt viewport
                 replacing the placeholder label; camera, display, visibility
                 and picking that resolves to CAD identity
TESTS:           24 renderer cases / 1166 assertions, 6 compile-failure cases,
                 the GUI smoke test, 3151 tests in each of three presets
VALIDATION:      offscreen rendering proved by a probe built OUTSIDE the
                 repository before any production code; drawing measured by
                 coverage rather than asserted; picking proved against TWO
                 bodies and across a resize; the no-engineering-state invariant
                 proved at runtime by a kernel-volume fingerprint and at
                 compile time by six deleted-path cases
ADVERSARIAL:     5 production defects and 2 test gaps found and fixed;
                 15/15 mutations killed BY A TEST, 0 survived, 0 compiler-only
RESULT:          PASS -- QUALIFIED
EVIDENCE:        this directory; qualification/qualification-times.txt
TODO:            17 checkboxes ticked; P16-VIZ-001 unblocked
```

**QUALIFIED.** Not "NOT QUALIFIABLE": a V3d view is created and rendered
offscreen from a test in all three presets, a CAD body displays in a
Qt-embedded view, picking resolves to engineering identity, visibility works,
the viewer holds no engineering state, and 0 warnings. There is no ambiguous
middle state, which is what `INFRA-NETGEN-001`'s discipline requires.

What this unblocks is `P16-VIZ-001`, and nothing else is authorized by it.

## Revision

First issue, 2026-10-04.
