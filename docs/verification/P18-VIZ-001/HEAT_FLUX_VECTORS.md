# P18-VIZ-001 — Heat-flux vectors

The recovered conductive heat flux **q = −k ∇T** (W/m²), one vector per tetrahedron, as POST stored it.

## Stored as POST made it

`heatFluxVectorGlyphs(mesh, post, state, elements)` reads `post.heatFluxOf(state, element)` and `post.heatFluxMagnitudeOf(state, element)` **by element identity** (`ordinalOf`, never the raw id) and places
the glyph at the tetrahedron's centroid (the mean of its four node positions, metres). The glyph keeps the stored `HeatFlux3` **unchanged** — not sign-flipped, not normalised, not rounded. The display
direction q/|q| is formed locally and lives only in the glyph (`HeatFluxVectorDirection`; mutations `VIZ57`, `VIZ58` flip them and are killed).

`RecoveredHeatFluxFlowsFromHotToColdOnARealSlab` runs the real chain (document → mesh → solve → recovery) on a slab held at 400 K and 300 K and checks that every recovered q points from the hot end to the cold
one; `HeatFluxVectorDirection` checks the exact components on a hand post.

## No direction is invented

A zero vector (|q| = 0) has **no direction**: `hasDirection = false`, it produces no arrow, and it is counted in `skipped` — it is not given an arbitrary axis and not divided by zero
(`ZeroHeatFluxVector`; mutation `VIZ59`).

## Display scaling is a view choice

`heatFluxVectorOverlay(glyphs, scale, reference, minimumFraction)` draws an arrow of length `scale · |q| / reference` metres, tail at the centroid and head further along q/|q| (mutation `VIZ68`). `reference`
is the caller's (typically the largest |q| of the state), so two states can share a scale. The scale, the reference and the threshold are rejected unless finite and positive (threshold in [0, 1)); the length
itself must come out finite — a reference of 1e-300 against a flux of 1e10 overflows to infinity and is refused rather than drawn (`AnOverflowingArrowLengthIsRefusedAndNeverReachesTheGeometry`; mutation `VIZ70`).

* **The threshold is readability, not physics.** An arrow shorter than `minimumFraction · reference` is suppressed and counted in `belowThreshold`; the inspector still returns the actual value and nothing
  in the result is zeroed (`TheDisplayThresholdIsReadabilityNotPhysics`; mutation `VIZ67`).
* **Accounting:** `glyphs + skipped + tooSmall + belowThreshold` equals the number of glyphs given, always.
* **Scaling is proportional and does not mutate the result** (`VectorScalingDoesNotMutateResult`: the display length is proportional to the scale and to |q| — the longest arrow, |q| = 12, is exactly `scale` long — and the post compares equal to a copy taken before).

## q is not q″

The recovered flux q (inside the body, where heat *flows*) and the applied flux q″ (a boundary input, **positive into the body**) are different quantities and are drawn as different overlays in different colours
(magenta vs red, [BOUNDARY_SYMBOLS.md](BOUNDARY_SYMBOLS.md)). On a face where heat leaves, the two arrows point in *opposite* senses of the same normal
(`RecoveredQAndAppliedQDoublePrimeAreDistinct`).
