# P18-VIZ-001 — Determinism and cross-preset equivalence

Every output of the layer is a function of its inputs: no clock, no random number, no pointer identity, no unordered iteration (gate rule 4; the only `std::set` is over `(facet id, temperature)` pairs and is
iterated for membership only). Identifiers are processed in the ascending order the core documents; ties resolve to the first entity.

## Repeatability

* `FieldExtractionIsDeterministic` — the same field extracted repeatedly and in reverse construction order is equal.
* `ColourBuffersAreDeterministicAcrossRebuilds` — every channel's colour buffer is identical across rebuilds, with and without the cache.
* `DisplayStateAndInspectionAreDeterministic` — the display state and both inspections of the same inputs are equal on every call.
* `ExtremaTieBreakDeterministic` — 25 repeated extremum queries return the identical extreme.
* `VisualizationDataIsDeterministicAndFingerprinted` — builds the whole visualization state of a steady block, a transient block (five frames) and a curved cylinder from source, digests it with FNV-1a over the
  exact bit patterns — display state, every channel's field, legend, corner colours and extremes, the vector glyphs and arrows, the boundary glyphs with their cardinalities and symbols, the playback times —
  requires a second build to give the same digest, and prints it:

```text
VIZFP steady-block 7cd19fb9128ca590
VIZFP transient-block frame 0 6e2b1d3bb510ea00
VIZFP transient-block frame 1 f852c55e57e66ad5
VIZFP transient-block frame 2 0108fcb4a733f303
VIZFP transient-block frame 3 dc58cd0f6678d744
VIZFP transient-block frame 4 fcc4d4e5a09b3bd8
VIZFP steady-cylinder bd76108f12f5fbcf
```

(The values above are from the debug-ext development run; `qualification/postrun-viz.ps1` extracts the `VIZFP` lines from each preset's own binary and compares them.)

## Cross-preset equivalence

The three presets (debug-ext, release-ext, debug-shared-ext) differ in optimisation and linkage, not in arithmetic: the fingerprints must be **identical** across them. The qualification run compares them
(`postrun-viz.ps1`, section "cross-preset classification"); the result is recorded in the README.

## Large-mesh smoke

`LargeMeshVisualizationDataSmoke` — a block with a 2 mm local size (161 nodes, 810 tetrahedra, 12 boundary facets on the loaded face): all three channels, their legends, colour buffers, extrema, the vector
overlay, and every boundary glyph with complete cardinality, in one run. `LargeCurvedMeshVisualizationDataSmoke` — a cylinder at 3 mm (900 nodes, 4118 tetrahedra, 572 boundary facets): the lateral wall
and both caps (288 + 142 + 142 glyphs) are all `Complete`. `LargeMeshVisualizationReachesTheScreen` draws the block through the production viewer. These are smoke tests of the data path at a size where an O(n²)
mistake or a sparse-identifier mistake would show; they are not benchmarks and assert no timing.
