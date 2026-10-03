# INFRA-VIEWER-001 — how the viewer is rendered and tested

```text
SUBJECT:  what "offscreen" means on this toolchain, and how a viewer is
          qualified without screenshots
DATE:     2026-10-03
```

## Offscreen on Windows means hidden, not window-less

The single most useful finding of this milestone, and it was found by trying
the obvious thing first and watching it fail.

**The obvious thing.** OCCT has `Aspect_NeutralWindow`, which looks exactly
like the type for a view with no desktop window, and `SetVirtual(true)` to say
so. The gating probe used it:

```text
OCCT 8.0.1
driver   created
view     created
FAIL at an OCCT call: OpenGl_Window::CreateWindow: SetPixelFormat failed.
                      Error code: -1073283066
```

**Why.** `OpenGl_Window` asks its `Aspect_Window` for a native handle and calls
`SetPixelFormat` on that handle's device context. An `Aspect_NeutralWindow` has
no handle, so there is no device context to format. The neutral window is for
a context created *elsewhere* and handed to OCCT, not for making one.

**What works.** A real `WNT_Window` that is **never mapped**:

```cpp
Handle(WNT_Window) window = new WNT_Window(
    "BetterCADOffscreenView", sharedWindowClass(), WS_OVERLAPPEDWINDOW, 0, 0, width, height);
window->SetVirtual(true);   // never mapped, so nothing appears on screen
view->SetWindow(window);
```

```text
OCCT 8.0.1
driver   created
view     created
window   attached 256x256 (native, never mapped)
empty    256x256, 0 non-background pixels
box      32674 non-background pixels

QUALIFIABLE: offscreen rendering works on this toolchain.
```

So the project's offscreen tests need a window manager and a GL
implementation, but no visible window and no user. That is recorded as a
limitation rather than hidden: a machine with no usable OpenGL cannot run them,
and will say which call failed.

### The consequence for the Qt widget

Qt's `offscreen` platform plugin provides no native window either, so a
`ViewportWidget` under it cannot make a view. OCCT reports
`ChoosePixelFormat failed`, and the widget treats that as a **supported,
reported state**:

```text
$ bettercad --smoke-test -platform offscreen
viewport: no 3D view (No 3D view: viewer: geometry kernel raised
          Aspect_GraphicDeviceDefinitionError: OpenGl_Window::CreateWindow:
          ChoosePixelFormat failed. Error code: 6)
exit 0
```

On a desktop the same binary prints `viewport: 3D view created`. The smoke test
asserts it printed **one or the other**, because a viewport that said nothing
would be indistinguishable from a broken one. The widget paints the reason in
place of the scene rather than leaving an empty hole.

## How a viewer is tested without screenshots

```text
NOT ASSERTED   any pixel's exact value, or equality between machines.
               Drivers differ; a test demanding byte-identical framebuffers
               would be testing the GPU.
ASSERTED       state, and COVERAGE -- how many pixels differ from the view's
               background by more than a tolerance.
```

The pattern every rendering assertion uses:

```text
empty scene      coverage == 0
display a body   coverage > 0        and < the whole view
hide it          coverage == 0
show it again    coverage == the original
```

**Why both halves matter.** `ToPixMap` returning `true` proves nothing: a view
that rendered nothing returns success and gives back a background-coloured
image. Only the comparison between an empty scene and a populated one
establishes that something was drawn. The gating probe was built this way
before any production code existed, and the discipline carried into the suite.

It earned its keep immediately. Hiding and re-showing a shaded box gave

```text
32674 covered pixels  ->  hide  ->  0  ->  show  ->  1240
```

1240 is the box's **edges**: `context->Display(object, update)` re-displays an
erased object in the object's *own* display mode, and an `AIS_Shape` defaults
to wireframe. Passing `AIS_Shaded` to the first `Display` call does not set it
on the presentation. Nothing in the viewer's state was wrong -- `visible`
flipped correctly both ways -- so no state assertion could have found it. The
fix is `presentation->SetDisplayMode(AIS_Shaded)`.

### Coverage is tested on its own

```text
RenderedImage_CoverageAndSamplingAreWellBehavedAtTheEdges
```

A hand-built 2x2 image, read pixel by pixel, counted at two tolerances, and
sampled outside its own bounds. The measure every other assertion rests on is
not itself left unmeasured.

**Its fixture has no black pixel, and that is the point.** Out-of-bounds
sampling returns a default-constructed `Rgb`, which is black -- so an image with
a black corner cannot distinguish an `at()` that *refuses* from one that
*clamps* to the nearest pixel. The first version of this test used a fixture
whose corners were black, and the mutation that clamps survived it. Four
distinct non-black colours later, the same mutation fails four assertions.
Finding 4 in [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

## Picking resolves to CAD identity

A selection owner is a graphics concept and never leaves the adapter file.
What a caller gets is the `ObjectId` that was displayed.

**The test that can actually fail.** With one presentation displayed, a
`pickAt` that ignored what it detected and returned the first displayed object
would satisfy every obvious assertion. So the pick test displays **two solids
60 mm apart**, scans a horizontal line across a top view, and requires:

```text
both objects are hit
nothing else is hit
the left solid is hit to the LEFT of the right one
```

Scanning rather than naming two pixel coordinates is deliberate: `FitAll`
decides the layout, so hard-coded coordinates would be asserting about the
camera instead of about the pick. This test kills the mutation that returns
the first presentation; without it, that mutation survived.

## Whose pixels are a pick's pixels?

The question a renderer invites and almost never gets asked. A pick is
normalised by OCCT against the **view's window**. An image comes out of
`ToPixMap` at whatever size was asked for, and `ToPixMap` adjusts the camera
aspect for the dump. Those are two different sizes and two different framings,
and nothing in the API says they are the same one.

They were not. `resize()` recorded the new size and told the view it must be
resized, but never resized the window -- so the image came out at the new size
while picks went on being taken in the old one. Scanning a 400x200 image of a
view whose window was still 256x256, marking every tenth row with drawn
content and every tenth row where a pick lands:

```text
                  rows 0..200, step 10
image content:    ........#####.......
picks land   :    ............#######.
```

One row of overlap. A caller scanning the middle row of the image it had just
been handed hit nothing.

After resizing the window too -- `SetPos` then `DoResize`, and only on the
window the viewer created itself, because a Qt-owned HWND has already been
resized by Qt -- the bands coincide, and `fitAll` frames to the new aspect
instead of the old square one:

```text
image content:    .....##########.....
picks land   :    .....##########.....
```

```text
RenderedImage coverage of the same scene:  4900  ->  19800 pixels
```

The test is
`Viewer_PickCoordinatesStayInTheRenderedImagesSpaceAfterAResize`, and it
resizes to a size that is neither square nor the one the view was created at.
Either shortcut would hide the question: a square resize hides an aspect
mistake, and the original size hides the whole thing.

**The same question at the Qt boundary** has a different answer and is dealt
with there: Qt reports logical pixels, the window is measured in device
pixels, and `ViewportWidget` converts. `--smoke-test` prints both extents and
the ratio, so the conversion is observable:

```text
ratio 1.000   logical 1280x746   device 1280x746
ratio 1.500   logical 1280x746   device 1920x1119
```

## Determinism, and what is deliberately not claimed

```text
CLAIMED      presentation state, identity resolution, coverage and state
             transitions are deterministic for identical input, and are
             asserted in all three presets.
NOT CLAIMED  framebuffer bytes are identical across machines, drivers or
             presets. They are not, and nothing here depends on it.
```

Two renders of the same scene in the same process are compared for equality --
`zoom` then `setStandardView` back must restore the earlier image -- which is a
statement about the viewer, not about the driver.

## What is not programmatically assertable

Recorded because the alternative is implying it was tested.

```text
"nothing appears on screen"
    The offscreen window is created unmapped and SetVirtual(true), which is
    what makes it invisible. Whether a window actually appeared is a property
    of the desktop, and no test here observes the desktop. The guarantee rests
    on never calling Map() -- visible in six lines of the adapter -- rather
    than on an assertion.

"the viewport looks right"
    Deliberately out of scope. The milestone proves a body is DRAWN, in the
    right frame, resolving to the right identity. Whether the result is
    legible to a person is a design question for the milestone that builds the
    modelling UI.
```
