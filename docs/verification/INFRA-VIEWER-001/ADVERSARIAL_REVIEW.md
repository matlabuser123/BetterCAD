# INFRA-VIEWER-001 — adversarial review

```text
SUBJECT:  an attempt to disprove the milestone before calling it complete
DATE:     2026-10-03
METHOD:   read the diff against the lifecycle's questions and against the
          invariant this milestone exists to protect; then mutation-test the
          adapter.
```

**Five production defects and two test gaps found and fixed, four
integration traps recorded, six limitations carried.** Every defect was found
by running something, not by reading it: three by the suite, one by a test
written to answer a question raised while reading, one by a measurement taken
because a comment would otherwise have asserted something unverified. Both test
gaps were found by mutation.

---

## Finding 1 — an erased body came back as wireframe

**Severity: real, and invisible to state assertions. Fixed.**

Hiding a shaded box and showing it again produced

```text
32674 covered pixels  ->  hide  ->  0  ->  show  ->  1240
```

1240 is the box's **edges**. `AIS_InteractiveContext::Display(object, update)`
re-displays an erased object in the object's *own* display mode, and an
`AIS_Shape`'s default is wireframe; passing `AIS_Shaded` to the first `Display`
call sets the mode for that call only, not on the presentation.

What makes this worth recording is that **no state assertion could have caught
it**. `DisplayedObject::visible` flipped to false and back to true correctly,
`displayed()` had the right contents, and every handle was valid. Only the
coverage comparison -- is the image after showing the same as the image before
hiding -- could see it.

**Fix.** `presentation->SetDisplayMode(AIS_Shaded)` at display time, so the
mode belongs to the presentation and survives an erase. The mutation that
removes it is killed.

---

## Finding 2 — arguments validated after being used

**Severity: real but minor. Fixed.**

`createOffscreen(0, 256)` returned an `Internal` error rather than
`InvalidArgument`. The size check lived in `Impl::make`, which runs *after*
`createOffscreen` has already built a `WNT_Window` out of the width and height
-- and a zero-width window is created quite happily, so the failure surfaced
later and as the wrong kind.

**Fix.** A shared `checkSize` at the top of both factories, before a window is
made out of anything. The mutation that moves it back is killed by the
validation test.

The general shape is worth naming: **a validation that runs after the value has
been used is not a validation, it is a second opinion about a decision already
taken.**

---

## Finding 3 — a pick test that could not fail

**Severity: a test gap, found while designing the mutations. Closed.**

The original pick test displayed one body and required the pick to resolve to
its object. That assertion is satisfied by an implementation that ignores what
it detected entirely and returns the first displayed object -- which is exactly
the mutation in the list, and it would have survived.

**Fix.** The pick test now displays **two solids 60 mm apart**, scans a
horizontal line across a top view, and requires both to be hit, nothing else to
be hit, and the left solid to be hit to the left of the right one. It scans
rather than naming pixel coordinates because `FitAll` decides the layout, so
fixed coordinates would assert about the camera rather than the pick.

With that test, the mutation is killed.

---

## Finding 4 — a bounds check that could not be caught

**Severity: a test gap, found by a surviving mutation. Closed.**

`RenderedImage::at` returns a default-constructed `Rgb` -- black -- for a
coordinate outside the image, and the test asserted exactly that:

```cpp
image.rgb = {0, 0, 0, 255, 255, 255, 0, 0, 0, 0, 0, 0};   // one white pixel
CHECK(image.at(-1, 0) == Rgb{});
CHECK(image.at(0, 5) == Rgb{});
CHECK(image.at(2, 2) == Rgb{});
```

The mutation that replaces the bounds check with a `std::clamp` onto the
nearest valid pixel **survived**. Three out-of-bounds coordinates, and each one
clamps onto a pixel that happens to be black: `(-1,0)` to `(0,0)`, `(0,5)` to
`(0,1)`, `(2,2)` to `(1,1)`. The single white pixel sits at `(1,0)` and nothing
the test asked for clamps onto it. Every assertion still read `Rgb{}`, so the
test could not tell a refusal from a clamp.

**The general shape, and it is worth naming: when the sentinel for "no value"
is a legal value, a fixture that contains it cannot test the sentinel.** Black
is both "out of bounds" and a perfectly ordinary colour.

**Fix.** The fixture now has four distinct colours and **not one of them is
black**, each pixel is read individually, a fourth out-of-bounds coordinate is
added, and coverage is checked at two tolerances:

```cpp
image.rgb = {10, 20, 30, 255, 255, 255, 40, 50, 60, 70, 80, 90};
CHECK(image.coverage(Rgb{10, 20, 30}) == 3);
CHECK(image.coverage(Rgb{10, 20, 30}, 255) == 0);   // a tolerance that swallows everything
```

Any clamping implementation now answers a real colour where black was
required. The mutation is killed, with 4 failed assertions.

---

## Finding 5 — a mutation the compiler killed, which proves less

**Severity: weak evidence, strengthened.**

The mutation "coverage ignores its tolerance" was first written as
`if (dr >= 0 || dg >= 0 || db >= 0)`, which leaves the `tolerance` parameter
unused and so fails under `-Wunused-parameter -Werror`. It was reported
**killed by the COMPILER**.

That is a weaker result than it looks, and it is recorded rather than banked:
a compiler kill shows the mutant would not build, **not that any test would
have noticed it**. The mutation was rewritten as
`if (dr > -tolerance || ...)` -- always true for a non-negative difference, so
every pixel counts as covered, and `tolerance` stays used. It now compiles, and
is killed by 8 failed assertions, including `coverage == 0` on an empty scene
and the new wide-tolerance check.

---

## Finding 6 — a resize that moved the picture but not the window

**Severity: real, and invisible to every assertion that compared sizes.
Fixed.**

`resize()` recorded the new width and height and called
`V3d_View::MustBeResized()`. The rendered image duly came out at the new size,
because `ToPixMap` takes explicit dimensions and adjusts the camera aspect for
the dump -- so `Viewer_ResizeRefusesAnEmptyArea`, which checks
`viewer.width()`, `viewer.height()`, `image.width` and `image.height`, passed
throughout and would have gone on passing.

**The window was never resized.** A pick is normalised by OCCT against the
view's *window*, so after a resize the caller was looking at one space and
picking in another. Measured, by scanning a 400x200 image of a view whose
window was still 256x256, marking every tenth row that has drawn content and
every tenth row where a pick lands:

```text
                  rows 0..200, step 10
image content:    ........#####.......
picks land   :    ............#######.
```

They overlap in a single row. A caller scanning the middle row of the image it
had just been given -- the obvious thing to do -- hit nothing at all.

**Fix.** `SetPos` then `DoResize` on the window, and only on the window the
viewer created itself: a window-backed view's HWND belongs to whoever owns it
and has already been resized by the time `resize()` is called, so calling
`SetPos` there would be fighting Qt. After the fix the two bands are identical,
and the body is framed to the new aspect as well -- coverage of the same scene
went from 4900 to 19800 pixels, because `fitAll` had been fitting to the old
square window.

```text
image content:    .....##########.....
picks land   :    .....##########.....
```

**What makes this the most instructive defect of the milestone** is that it was
not found by suspicion of `resize`. It was found by asking a question while
reading the pick path -- *whose pixels are a pick's pixels?* -- and then writing
`Viewer_PickCoordinatesStayInTheRenderedImagesSpaceAfterAResize` to make the
answer observable. The test deliberately resizes to a NON-SQUARE size that is
not the one the view was created at: either shortcut hides the question.

---

## Finding 7 — Qt's logical pixels are not the view's device pixels

**Severity: latent here, real elsewhere. Fixed, and the claim kept to what was
measured.**

Qt 6 reports a widget's size and a mouse event's position in LOGICAL pixels;
`WNT_Window` takes its extent from `GetClientRect`, in DEVICE pixels, and the
pick is normalised against that. Wherever the ratio is not 1, a coordinate
passed straight through is read in the wrong space and a pick is displaced by
more the further it is from the origin.

The widget now converts at that boundary, and `--smoke-test` prints both
extents so the conversion is observable rather than asserted:

```text
ratio 1.000   logical 1280x746   device 1280x746
ratio 1.500   logical 1280x746   device 1920x1119
ratio 3.000   logical  853x469   device 2559x1407
```

**What is NOT claimed.** This was not observed going wrong. The machine's
`AppliedDPI` is 144, which is 150% scaling, and the first version of this
finding said so and concluded the defect was live here -- but Qt reports
`devicePixelRatio() == 1.000` in the default configuration, so the conversion
is a no-op on this machine and the inference from the registry value was
wrong. The two non-unit rows above came from `QT_SCALE_FACTOR`, which is what
made the divergence measurable at all.

It is fixed rather than carried because the conversion costs one
multiplication, and the failure it prevents -- picks that land near the cursor
but not on it -- is among the hardest things to attribute from a bug report.

The orbit tuning is deliberately left in logical pixels: a logical pixel is the
same physical size at every scale factor, so the drag that gives a quarter turn
is the same movement of the hand on any display. Converting it would make the
viewport spin half again as fast at 150%.

---

## What went wrong in the review itself

Recorded because the alternative is a reader trusting a result that was not
trustworthy, and because the failure is one any agent or developer can repeat.

**A mutation run was interrupted, kept running, and produced a complete,
plausible, entirely void `results.txt`.** Stopping the task that launched
`mutate.sh` did not stop `mutate.sh`. It ran on through all fourteen mutations
while the tree was being edited for Findings 6 and 7, so:

```text
*  mutations were applied to, and judged against, a tree that was changing
*  one was reported "killed by the COMPILER" because an unrelated edit of
   mine did not compile -- a false kill, and the weakest kind of verdict
   wearing the appearance of a result
*  its EXIT trap restored the snapshot and SILENTLY REVERTED a production fix
*  its rebuild and an interactive build wrote the test executable at the same
   time, leaving it 0 BYTES -- which the shell reported as exit 0 with no
   output, so three readings in a row were of a binary that could not run
```

The void results were deleted rather than filed, the fix was re-applied, and
the set was re-run once, uninterrupted, against the final tree.

Two durable lessons went back into the harness, where the next person will
read them: `mutate.sh` now takes a lock and refuses a concurrent run, and both
it and its README say in full why nothing may touch the tree while it runs and
why stopping it means killing the process tree.

A third is about reading evidence rather than output: **a test result from a
binary whose size was never checked is not a result.** The wandering failures
-- different assertions failing on consecutive runs of the "same" build -- were
the signal, and they were briefly mistaken for a resource leak in the viewer.
That hypothesis was tested, by creating and destroying thirty viewers in one
process and requiring every render to be correct, and it was WRONG; the test
earned its place in the suite anyway, as
`Viewer_ManyViewersInOneProcessEachRenderCorrectly`.

---

## Integration traps, recorded in the source

Not defects in BetterCAD, but each cost real time and each would cost it again.

```text
WIN32_LEAN_AND_MEAN IS LOAD-BEARING
    Without it windows.h pulls in rpc.h and rpcndr.h, which declare a global
    `byte`. That shadows the `byte` parameter in core/Uuid.hpp and the build
    fails under -Wshadow -Werror. NOMINMAX is the companion: the min/max
    macros break std::clamp, which the image conversion uses.

windows.h MUST COME FIRST
    After OCCT's headers, CS_OWNDC and WS_OVERLAPPEDWINDOW are not declared --
    OCCT includes a restricted windows.h of its own.

Aspect_NeutralWindow IS NOT A WINDOW-LESS VIEW
    OCCT calls SetPixelFormat on the window's device context, and a neutral
    window has no handle. Offscreen on Windows means a real window that is
    never mapped. See OFFSCREEN_RENDERING.md.

OCCT 8.0.1 DEPRECATES ITS OWN SPELLINGS
    Standard_True, Standard_False, Standard_Integer, Standard_Size and
    Standard_Failure::GetMessageString() all warn. With -Werror that is an
    error, so the adapter uses true, false, int, size_t and what().
```

---

## Mutation testing

Fifteen plausible mistakes in the adapter, each followed by a rebuild and the
`[renderer]` suite. Harness, list and log in `qualification/mutation/`.

Five of the fifteen reinstate a defect or a test gap this review found, so the
harness -- and not only the suite -- would catch a regression that brought one
back.

```text
 1  display mode not set on the presentation, so an erased body returns as wireframe  killed, 1 failed assertion
 2  the view size checked only after a window is built from it                        killed, 2 failed assertions
 3  a pick returns the first presentation instead of the one detected                 killed, 2 failed assertions
 4  selection reports every displayed object rather than the selected ones            killed, 2 failed assertions
 5  hiding does not record that the presentation is hidden                            killed, 2 failed assertions
 6  hiding erases nothing, so a hidden body stays drawn                               killed, 2 failed assertions
 7  a nonsensical zoom is passed to the kernel instead of ignored                     killed, 1 failed assertion
 8  resize does not record the new size, so renders keep the old one                  killed, 5 failed assertions
 9  an empty body is displayed as an empty presentation instead of refused            killed, 1 failed assertion
10  removing a presentation leaves it in the viewer's own list                        killed, 1 failed assertion
11  the background colour is recorded but never given to the view                     killed, 9 failed assertions
12  coverage ignores its tolerance and counts every pixel as different                killed, 9 failed assertions
13  sampling out of bounds returns a neighbouring pixel instead of refusing           killed, 4 failed assertions
14  a standard view is set but the camera never refits                                killed, 2 failed assertions
15  resize records the new size but never resizes the window it owns                  killed, 3 failed assertions

15 killed by a test   0 killed by the compiler   0 SURVIVED   0 not applied
```

**Every mutation was killed by a TEST.** None was left to the compiler, which
would have been the weaker verdict, and none survived. The run was made once,
uninterrupted, against the tree that was then frozen -- an earlier run, taken
while the tree was being edited, was void and was discarded rather than filed.

The assertion counts differ between rows because Catch2 stops a test case at
its first `REQUIRE`, so a mutation that breaks an early precondition is counted
against fewer assertions than one that breaks a late `CHECK`. The number to
read is the right-hand column.

---

## The lifecycle's questions

**What did we assume?** That a view could be created at all on this toolchain.
It was not assumed: the gating probe was built outside the repository, against
the installed dependency, before a line of production code existed, and it is
what found the neutral-window trap. The milestone would have reported NOT
QUALIFIABLE at that point rather than later.

**What case is missing?** Everything about meshes, deliberately --
`P16-VIZ-001` owns it. And within the viewer: no clipping or section plane, no
view cube, no text in the scene (FreeType is off), no multi-viewport. Each is
named in the limitations rather than left to be discovered.

**Could this pass its tests and still be wrong?** The question a viewer invites
is "does it only *look* like it works". The answer here is that no assertion
looks at the screen: they compare state, and they compare coverage between an
empty scene and a populated one. A viewer that drew nothing would fail the
coverage assertions; one that drew the wrong thing in the wrong frame would
fail the standard-view and pick assertions.

**Are the expected values independent?** Coverage figures are measured, not
predicted -- the assertions are about *relations* between renders (zero versus
non-zero, equal versus different), which is the only kind of claim that
survives a driver change. The one absolute figure asserted is that an empty
scene covers exactly 0 pixels, which is a property of the background colour
rather than of the renderer.

**Did we weaken a test or move a tolerance?** The coverage tolerance is 6 of
255 per channel, and it exists because sRGB conversion and the framebuffer's
precision make an exactly-equal background unreasonable to demand. It was not
tuned to make anything pass: the empty-scene assertion is `coverage == 0`,
which is the *strictest* possible use of it.

**Is there hidden global state?** One: the registered Win32 window class, which
is a process-wide resource by Win32's design and is created once rather than
per view. Registering one class per window would exhaust the class table in a
long session. It holds no viewer state.

**Can save/load or undo/redo change the result?** Nothing here is persisted. A
viewer is constructed, used and destroyed; presentations, visibility, selection
and the camera die with it. That is the correct lifetime for presentation
state and is why `P16-PERSIST-001` is unaffected.

**Can a parameter change leave stale geometry?** The viewer displays the `Body`
it was handed and does not watch the document. A caller that regenerates and
wants the new body displays it again. That is a deliberate division -- the
viewer has no opinion about currency -- and `P16-VIZ-001`'s stale-state
requirements are built on the core's own currency contract, not on anything
here.

**Could Debug and Release differ?** The three-preset qualification is the
answer. Nothing here depends on an unordered container, timing, a random seed
or locale.

**Can a failure leave partial state committed?** `display` appends to
`displayed` and `presentations` only after the OCCT call succeeds; every
factory returns a `Result` and a failed one yields no viewer at all. There is
no half-constructed view to hand on.

**Did we cross an architectural boundary or widen the scope?** The layering
check is the referee and it passes. `include/bettercad/renderer/Viewer.hpp`
contains no Qt and no OCCT -- the compile-failure group includes it and needs
neither. All OCCT is in `src/renderer/occt/`, which rule 1 allows. All Qt is in
`apps/bettercad/`, which rule 2 allows.

Scope beyond the viewer: two lines of intent in `deps/CMakeLists.txt`, the
`renderer` module registered in `src/CMakeLists.txt`, and the main window's
placeholder `QLabel` replaced by the viewport it was standing in for. No
meshes, no model tree, no command system.

---

## Attacks specific to a viewer

**Can the viewer hold a second authoritative copy of the model?** It holds
`ObjectId`s and `Body` handles -- and a `Body` copy shares the underlying
kernel shape, so there is no second geometry. There is no `Document` behind it:
the compile-failure case proves there is nothing to reach through.

**Can rendering mutate the body?** `Viewer_NothingItDoesChangesTheModel`
fingerprints the kernel's own volume and the document's revision of the
feature, runs every operation the viewer offers -- display, orbit, pan, zoom,
redraw, render, select, pick, hide, show, background, resize, remove -- and
requires both to be unchanged. The volume is the sharp one: a viewer that moved
a vertex or healed a shape would change it, and no amount of correct
bookkeeping would hide that.

**Can a presentation handle become engineering identity?** Four
compile-failure cases: `PresentationId` to `ObjectId`, `ObjectId` to
`PresentationId`, an integer to `PresentationId`, and a `Document` through the
viewer. The handle is documented as viewer-local for the same reasons `NodeId`
is not a CAD identity (ADR-031).

**Can a graphics index escape?** A `SelectMgr_EntityOwner` exists only inside
`sourceFaceOf`'s equivalent in the adapter; `pickAt` returns an `ObjectId` or
nothing.

**Can hiding delete something?** `Viewer_HidingIsNotDeleting` requires the
presentation to stay in `displayed`, the body to stay non-empty, and showing it
again to restore the *same* coverage. That last clause is what caught
Finding 1.

**Can a viewer be copied, so two objects own one native window?** The copy
constructor and assignment are deleted, and a compile-failure case proves it.

**Can OCCT or Qt leak out of their directories?** `architecture.layering`
fails the build if they do, and it runs in every preset.

**Can the dependency change be irreproducible?** The tag and the SHA256 are
untouched: `V8_0_1`, `0d6913ea…`. What changed is two module switches, in the
same pinned superbuild, built with the same toolchain into the same prefix.

**Can a machine without OpenGL pass this by accident?** No, and that is the
point of the two-render check: without a driver the factory fails and names the
failing call. The tests do not skip -- they fail -- which is the honest
behaviour, and the limitation is recorded below.

---

## Carried limitations

```text
THE TESTS NEED A GL IMPLEMENTATION AND A WINDOW MANAGER
    Not a display and not a user -- the window is never mapped -- but a
    machine with no usable OpenGL cannot run the renderer suite, and will fail
    rather than skip. On this machine 24 renderer tests pass; on a headless CI
    runner without a GL driver they would not. Recorded rather than worked
    around, because a skipped test that looks like a pass is worse.

NO TEXT IN THE 3D SCENE
    FreeType is off, so OCCT draws no glyphs in the view. P16-VIZ-001 wants
    element and node IDs "during inspection", which a Qt overlay or a property
    panel can provide. When something needs text drawn IN the scene, FreeType
    becomes a fourth pinned dependency and its own decision.

NO CLIPPING, SECTION PLANE OR VIEW CUBE
    P16-VIZ-001's optional interior inspection may want a clipping plane, and
    OCCT provides one. Not built here: this milestone's job was to make a view
    exist.

"NOTHING APPEARS ON SCREEN" IS NOT ASSERTED
    The offscreen window is created unmapped and virtual, which is what makes
    it invisible, but whether a window appeared is a property of the desktop
    and no test observes the desktop. The guarantee rests on never calling
    Map(), which is visible in the adapter.

FACE, EDGE AND VERTEX SELECTION MODES ARE NOT ACTIVATED
    display() activates selection mode 0, the whole shape. AIS_Shape provides
    the finer owners and P16-VIZ-001's CAD-to-mesh linkage is what will need
    them; activating modes nothing consumes would be speculative.

A PICK AT A NON-UNIT DEVICE PIXEL RATIO IS NOT ASSERTED
    The logical-to-device conversion's ARITHMETIC is measured -- the smoke
    test prints both extents at ratios 1.0, 1.5 and 3.0 -- but that a pick
    then lands on the thing under the cursor at 150% is not, because it needs
    a cursor. The renderer's own pick tests run in device pixels throughout,
    where the question does not arise. What is carried is the last step of the
    chain in the GUI, and the honest statement is that it is reasoned from
    two documented coordinate contracts and a measured ratio, not clicked.

NO SANITIZER COVERAGE
    This MinGW ships no libasan or libubsan. Carried.
```
