# P18-BC-001 — determinism

```text
TESTS:  ThermalBC_RepeatedPreparationIsBitIdentical, _DiagnosticsAreDeterministic,
        _BitFingerprintForCrossPresetComparison, _PrescribedTemperatureDeterministicOrder,
        _ConflictReportIsSummarisedAndOrderIndependent
CLAIM:  the same conditions on the same model give the same facet order, the same
        constraint map and attribution, the same Robin terms, the same diagnostics and
        the same conflict report, BIT FOR BIT on the same build; across presets the
        claim is MEASURED, not assumed
```

## What makes it structural

`ThermalBoundary.cpp` and `ThermalConvection.cpp` are serial. There is no unordered
container, no `static`, `mutable` or `thread_local` storage, no parallelism, no clock
and no random source (the audit is in [GEOMETRY_REFERENCE.md](GEOMETRY_REFERENCE.md)).
Every order is fixed by something deterministic:

- conditions are processed in the order given (the definition's canonical order);
- facets come from P16 strictly ascending, and a repeated or out-of-order list is
  refused, so the caller cannot choose the order;
- the unknowns of a face are sorted by `ThermalDofIndex`, and the constraint map is
  sorted by unknown by P18-DOF-001;
- the only ordered associative container sorts conflict records by condition id, so
  a diagnostic does not depend on the order the conditions were listed in;
- each Robin entry is one product and one quotient with no summation order to vary.

The prepared boundary's defaulted `operator==` compares the constraint map, the
attribution, every convection facet's terms and the provenance bit for bit, and that
is what the repeat tests use.

## Same-preset repeatability

```text
50 preparations of a five-condition mixed set (two prescribed temperatures, three
convections on three different faces) compared with the first by the class's own
equality:                                                      0 bitwise differences
a failing set (a resolvable condition, then two unresolvable ones), 20 repeats:
  same error code, same message, same classification, every time
  the FIRST bad condition in canonical order is the one named
a conflicting pair, 20 repeats, and the same pair listed in the other order:
  the same TemperatureConflict records and the same message every time
the constraint seam over all 24 permutations of four requests:  equal maps
the conflict seam over all 120 permutations of five requests:   identical reports
```

## Cross-preset

Measured on the qualifying run, on each preset's OWN binary, after the run:

```text
preset             ThermalBC_*                            bit fingerprint
                                                          hand Robin triangle   real block (9 nodes)
debug-ext          92 / 92, 4785 assertions               06de4e52c3ef700f      42982f4671b0e15b
release-ext        92 / 92, 4785 assertions               06de4e52c3ef700f      42982f4671b0e15b
debug-shared-ext   92 / 92, 4785 assertions               06de4e52c3ef700f      42982f4671b0e15b

distinct fingerprints: 1 and 1 -- BITWISE IDENTICAL across the three presets
the suite's 44 measurement lines (sorted): identical in all three, 0 differing
discrete results (problem values, stale reasons, conflict records, counts): identical
```

Each fingerprint is a 64-bit FNV-1a hash over bit patterns. The hand one covers the
nine `Kconv` entries, three `Qconv` entries and the area of a general triangle; the
block one covers every constraint (unknown and temperature) and the dense `K` and `Q`
accumulated from a prepared set of one prescribed temperature and two convections on
the real 40 × 30 × 20 mm block. They are recorded, not promised: the test itself
asserts only that each is reproducible within one process. That all three presets
agree bit for bit is a measured property of this toolchain, which compiles in ISO
mode without fused-multiply-add contraction and evaluates the same double-precision
operations in the same order at every optimisation level and in both linkages. A
different compiler or a `-ffast-math` build would be expected to move the last bits,
and the tolerances elsewhere in the suite (1e-13 to 1e-15) are what would hold in
that case.

The brief's cross-preset fixtures, and the lines that carry them:

| Fixture | Printed measurement |
| --- | --- |
| resolved node and facet counts | end cap: 2 facets, 6 corners, 4 unique nodes of 9; cylinder coarse 156 nodes against fine 900 |
| constraint unknowns and values | Dirichlet fixture: `{42,900,7}` to unknowns `{2,3,0}`, ordered `{0,2,3}`, 350 K on each |
| Robin analytical entries | `Kconv = [2 1 1; 1 2 1; 1 1 2]` W/K, `Qconv = [1200 1200 1200]` W, error 0 |
| ambient equilibrium and sign | 0 W at 300 K; +600 W at 350 K; -600 W at 250 K |
| conflict diagnostics | BC 1 at 350 K against BC 2 at 300 K refused; end cap against side wall share 2 unknowns of 9 |
| stale-source behaviour | same counts, same ids and a material edit each print their result |

One note on the post-run script: the block fingerprint is printed on a wrapped line,
and the first version of the script could not read it across the wrap, so its
cross-preset comparison reported the field as missing. The script was corrected and
rerun (`postrun-bc.ps1`, `postrun-summary.txt`); the three values above are read
from the saved suite output of each preset.