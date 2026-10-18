# P18-LOAD-001 — determinism

```text
TESTS:  ThermalLoad_RepeatedPreparationIsBitIdentical, _DiagnosticsAreDeterministic,
        _BitFingerprintForCrossPresetComparison, _LoadOrderOnlyAffectsTheSummationOrder
CLAIM:  the same loads on the same model give the same resolved target order, the
        same heat-input vector, the same per-load totals, the same combined
        total and the same diagnostic code and message, BIT FOR BIT on the same
        build; across presets the claim is MEASURED, not assumed
```

## What makes it structural

`ThermalLoadVector.cpp` and `ThermalTarget.cpp` are serial. There is no
unordered container, no `static` or `mutable` or `thread_local` storage, no
parallelism, no clock and no random source (the audit is in
[GEOMETRY_REFERENCE.md](GEOMETRY_REFERENCE.md)). The order of every sum is fixed
by two orderings that are themselves deterministic: the loads in the order given,
and the entities within a load in the strictly ascending order P16's mapping
returns them (a repeated or out-of-order list is refused, so the order cannot be
chosen by the caller). The vector is value-initialised to zero and sized by the
number of unknowns. The prepared loads' defaulted `operator==` compares the
vector, the contributions and the provenance bit for bit, and that is what the
repeat tests use.

## Same-preset repeatability

```text
50 preparations of a four-load mixed set (two fluxes on the end cap, one on the
start cap, one generation) compared with the first by the class's own equality:
                                                          0 bitwise differences
a failing set (a resolvable load, then two unresolvable ones), 20 repeats:
  same error code, same message, same classification, every time
  the FIRST bad load in canonical order is the one named
```

Load order moves only the order of the additions. The suite asserts the combined
vector agrees to the tolerance of the sum, and that the diagnostic names the first
failing load in the order given, not the first in some internal order.

## Cross-preset

Measured on the qualifying run, on each preset's OWN binary, after the run:

```text
preset             ThermalLoad_*                          bit fingerprint
                                                          hand cases           real block (9 nodes)
debug-ext          66 / 66, 21 588 assertions             3063bb8f8518f832     9a0ad0769a0a552f
release-ext        66 / 66, 21 588 assertions             3063bb8f8518f832     9a0ad0769a0a552f
debug-shared-ext   66 / 66, 21 588 assertions             3063bb8f8518f832     9a0ad0769a0a552f

distinct fingerprints: 1 and 1 -- BITWISE IDENTICAL across the three presets
the suite's 42 measurement lines (sorted): identical in all three, 0 differing
discrete classifications (problem values, stale reasons, entity counts): identical
```

Each fingerprint is a 64-bit FNV-1a hash over the bit patterns of prepared vectors
and totals. The hand-case one covers a general triangle with a negative flux and a
general tetrahedron, with no mesher involved. The block one covers the real
40 × 30 × 20 mm block with a flux on its end cap and a generation in its volume.
They are recorded, not promised: the test itself asserts only that each is
reproducible within one process. That all three presets agree bit for bit is a
measured property of this toolchain, which compiles in ISO mode without
fused-multiply-add contraction and evaluates the same double-precision operations
in the same order at every optimisation level and in both linkages. A different
compiler or a `-ffast-math` build would be expected to move the last bits, and
the tolerances elsewhere in the suite (1e-13 to 1e-15, or in rounding units) are
what would hold in that case.

The brief's six cross-preset fixtures, and the lines that carry them:

| Fixture | Printed measurement |
| --- | --- |
| surface analytical | single triangle: A = 3 m², total 36 W, nodes [12 12 12] W; general triangle A = 1.38025359988663 m², total −345.753526771601 W |
| volume analytical | unit tetrahedron: total 4 W, nodes [1 1 1 1] W; general tetrahedron V = 0.7825 m³, total 14.085 W |
| P16 mapped face | end cap: 2 facets, 4 loaded nodes of 9, 0.6 W, sum error 0 |
| remesh case | stamp changed, P1 stale for reason mesh, P2 current; coarse 156 nodes against fine 900 |
| unresolved-reference failure | a Side face with no entity is refused as `target_invalid`; diagnostics repeated 20 times |
| multi-load superposition | L1 0.36 W + L2 0.15 W = 0.51 W, residual 0; mixed 0.9 W + 9.6 W = 10.5 W |