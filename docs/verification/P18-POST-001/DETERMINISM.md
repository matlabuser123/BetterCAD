# P18-POST-001 — determinism

```text
SUBJECT:  the same inputs give the same bits, in the same order, in every run and every preset
```

## What fixes the order

| Source of order | Fixed by |
| --- | --- |
| elements | `mesh.tetrahedra()` (P16's deterministic order); every element array is parallel to it |
| element identities | `elementIds()` ascending, `ordinalOf` a binary search |
| nodes | `ThermalDofIndex` order (ascending `NodeId`), through the numbering |
| local corners | the tetrahedron's own node order, never sorted |
| transient states | the solution's own order and times; nothing re-sorted, merged or skipped |
| arithmetic | one scalar loop per element, the same expression order; no reduction across elements, no parallel algorithm |

The source audit (`qualification/source-audit.txt`) finds in `ThermalPost.hpp` and `ThermalPost.cpp`, in code: **0** `unordered_*`, **0** `std::hash`,
**0** `std::thread / async / jthread / mutex / atomic`, **0** OpenMP or `std::execution`, **0** `rand`, `time(` or `chrono`, **0** Eigen or dense-matrix
use. (One hit of the `convection|generation` pattern in code is the word "generation" inside a string literal about a *mesh generation*; it names no term of
the flux.)

## Repeat tests

| Test | Repeats | Differences | Fingerprint |
| --- | --- | --- | --- |
| `RepeatedRecoveryIsBitwiseDeterministic` | 25 recoveries of 384 tetrahedra on a jittered 5^3-node grid with a scattered field | **0** (the defaulted `==` over all three arrays), and the input vector unchanged | `6ba1d58f018354ca` |
| `RepeatedBlockRecoveryIsDeterministic` | 20 recoveries of the same block solution | **0** (full-result `==`) | — |
| `FineCylinderRecoverySmoke` | the real fine cylinder (Ndof 900, Ntet 4118) with a 350 K cap and convection | counts 900 / 4118 / 4118 / 4118, all finite | `262ce7759cb1d043` |
| `RecoveryDoesNotMutateItsInputs` | the solution, the system and the mesh stamp before and after | equal | — |

The fingerprint is FNV-1a (64 bit) over the bytes of the bit pattern of every gradient, flux and magnitude component, in element order (the cylinder case also
folds every nodal temperature first), so a change of one bit anywhere, or of the order of two elements, changes it. `==` on doubles says two values are equal as
numbers; the fingerprint is what says they have the same bits (it distinguishes `+0` from `-0`). The same two values are compared across presets by the post-run script
(`qualification/postrun-summary.txt`): debug-ext, release-ext and debug-shared-ext must print **identical** fingerprints, which is the cross-preset
equivalence the milestone requires (static and shared linking, optimised and unoptimised code, the same bits).

## Inputs are not mutated

A recovery takes `const` references and builds its output in temporaries. The test records the solution, the assembled system and the mesh stamp before,
runs a recovery, and compares after (`RecoveryDoesNotMutateItsInputs`); the compile-fail cases `THE_TEMPERATURES_ARE_READ_ONLY`, `…GRADIENTS…`, `…HEAT_FLUXES…`,
`…MAGNITUDES…`, `…ELEMENT_IDS…` and `THE_SOURCE_IS_READ_ONLY` show a held result cannot be changed through its accessors either.

## What is not claimed

The bits are identical for the same compiler flags of one preset on one machine across runs, and (measured, not assumed) identical across the three presets of this
qualification. Another compiler, another architecture or `-ffast-math` could change the last bit of a double; that is a build-system fact, not a property
of this code, and nothing here depends on a tolerance tighter than `1e-12` relative.
