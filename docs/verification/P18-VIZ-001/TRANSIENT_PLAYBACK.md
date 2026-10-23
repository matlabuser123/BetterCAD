# P18-VIZ-001 — Transient playback

## A frame IS a stored state

`ThermalPlayback::of(post)` binds a cursor to a result. A frame is an index of the result's stored states; the time of a frame **is the stored time of that state** (`ThermalPostState::time`) — never
`frame · dt`. The last step of a run is a shortened remainder, and an output event can fall between nominal steps, so a reconstructed time is wrong exactly where it matters.

`TransientPlaybackUsesStoredTimes`: a transient slab with Δt = 0.3 s to t = 1 s stores five states. For every frame the playback's time equals the stored time **bit for bit**, and the five times are
0, 0.3, 0.6, 0.9 and 1.0 (the stored third step prints as 0.8999999999999999, the binary value of 3 × 0.3) — the last is **1.0, not 4 × 0.3 = 1.2**. They are strictly increasing. Mutation `VIZ51` (frame index
× a nominal step) is killed.

* The stored times must be **finite and strictly increasing**; a result that violates it is refused rather than re-sorted or re-gridded — a GUI that did would be inventing a history. (The core cannot store such a result, so the refusal is unreachable and `VIZ52` is reported as such.)
* A **steady** result has exactly one frame and **no time**; a loop of one frame goes nowhere (`ASteadyResultIsOneFrameWithoutATime`; `VIZ53`).
* `select(frame)` refuses a frame the result does not hold, and `timeOf` of such a frame is empty (`VIZ54`). `next` / `previous` stop at the ends without changing the frame unless `loop` is set, which wraps;
  stepping from `first()` visits exactly 0, 1, 2, 3, 4.
* The playback **rate** is a wall-clock matter for the GUI scheduler. It does not appear here, so nothing in this layer reads a clock.

## No temporal interpolation

There is no function that blends two states: no `interpolate`, no `setTime(double)`, no fractional frame (compile-fail `THE_PLAYBACK_HAS_NO_INTERPOLATION`, `THE_PLAYBACK_HAS_NO_SET_TIME`,
`THE_PLAYBACK_HAS_NO_TIME_STEP`, `THERE_IS_NO_FRAME_INTERPOLATION`). `TransientPlaybackDoesNotInterpolatePhysics`: a frame is an accepted state or it does not exist — there is no frame past the last, stepping
visits exactly the stored states in order, and playing the whole history leaves the post result **equal to a copy taken before**.

## Every field of a frame is the same state

`TransientFrameBindsAllFieldsToTheSameState`: for each of the five frames, the temperature field and the heat-flux-magnitude field are taken from state *f* (their `state` index, their `time`, and every value
bit for bit against the stored arrays), the extremum is that frame's own (not the history's), the vector glyphs carry frame *f*'s stored flux, and a node inspection at frame *f* returns frame *f*'s temperature
and time. The frames differ from one another (so a field read from the wrong frame is detectable).

`TransientFramesDrawFromTheirOwnStates` renders the five frames offscreen through the production viewer with one shared manual range: frames 0 and 2, 1 and 3, 2 and 4 draw different pictures (the front advances
from the hot cap), and replaying frame 2 reproduces its picture exactly — a frame is a state, not an animation.

## Cache invalidation

`TransientCacheInvalidatesOnSourceChange`: the same source, frame, channel and mesh is a hit (1 build, 1 hit); a canonical edit and a new solve is a new `ThermalPostSource`, so the same frame number does not hit
(2 builds). The old playback no longer `describes` the new result, the new one does, and the display state of either result against a further-edited analysis is `Stale`. Mutations `VIZ28` (frame ignored) and `VIZ29`
(source ignored) are the cache's.

## Determinism across the whole run

`VisualizationDataIsDeterministicAndFingerprinted` fingerprints every frame's colour buffer and overlay (`VIZFP transient-block frame 0 … 4`); the lines are compared across debug-ext, release-ext and
debug-shared-ext in `qualification/postrun-viz.ps1` ([DETERMINISM.md](DETERMINISM.md)).
