# P18-LOAD-001 — mutation testing

```text
SUBJECT:  does the suite fail when the load kernel, the target resolution or the
          provenance is changed in a way the brief names?
METHOD:   one textual mutation of production code, rebuild the test targets in
          debug-ext, run every ThermalLoad case in its OWN process through
          ctest, record, restore the file. mutate.ps1 does exactly that, so a
          mutant that aborts or crashes is counted case by case.
RUNS:     63 runs in six batches
DISTINCT: 46 mutations
RESULT:   45 killed, 1 retired as dead code (its target was deleted), 0 survived
          at the end, 0 void at the end
```

Batch sizes: batch 1 ran 40, batch 2 ran 5, batch 3 ran 12, batch 4 ran 1,
batch 5 ran 4 and batch 6 ran 1, which is 63 runs of 46 distinct mutations.
Every run's script and summary is under `qualification/`.

## How a run ends

| Verdict | Meaning |
| --- | --- |
| KILLED | at least one ThermalLoad case failed, aborted or crashed |
| SURVIVED | every case passed: a gap in the tests, an equivalent mutant, or dead code |
| VOID | the mutant did not compile (`-Werror` refuses an unused name); it proves nothing and is rewritten |

Batch 1 returned 35 KILLED, 3 VOID and 2 SURVIVED. Each of the five was
resolved rather than counted: see "What the probes found" below.

## The brief's mutation list, and which probe answers each

| Brief | Probe | Final verdict | Cases failing (of 66) |
| --- | --- | --- | --- |
| surface node contribution `q''A` instead of `q''A/3` | L01b | KILLED | 15 |
| surface divide by 4 | L02b | KILLED | 16 |
| volume divide by 3 | L03b | KILLED | 13 |
| (volume with no `/4`) | L04b | KILLED | 13 |
| overwrite shared-node contribution instead of `+=` | L11 | KILLED | 12 |
| `Q` indexed by `NodeId.value()` | L12b | KILLED | 38 |
| `Q` allocated `maxNodeId + 1` | L13 | KILLED | 30 |
| omit mesh-source provenance | L29 | KILLED | 5 |
| accept same counts as same source | L30 | KILLED | 4 |
| persist facet list into canonical load | L46 | KILLED at compile time (see below) | n/a |
| nearest-face fallback | L37 | KILLED | 6 |
| empty target to successful zero vector | L20, L21 | KILLED | 1 each |
| `abs(q'')` | L07b | KILLED | 6 |
| `abs(q''')` | L08b | KILLED | 3 |
| multiply `q''` by mm² geometry | L09b | KILLED | 15 |
| multiply `q'''` by mm³ geometry | L10b | KILLED | 13 |
| use `abs(Tet volume)` | L40 | KILLED | 1 |
| drop one boundary facet | L15 | KILLED | 10 |
| double-count one boundary facet | L43 | KILLED | 16 |
| drop one Tet | L16 | KILLED | 7 |
| double-count one Tet | L44 | KILLED | 12 |
| include material revision unnecessarily in the prepared-load identity | L45 | KILLED | 1 |
| add a convection source in LOAD | L28b (any extra source term in the vector) | KILLED | 8 |

The convection probe is representative, not literal: LOAD has no temperature, no
film coefficient and no ambient, so a convection source cannot be written in it.
What can be written is a heat input that this milestone's loads do not account
for, and `L28b` adds exactly one watt to the first entry. Eight cases notice.

L46 adds a mesh-local field (`std::uint32_t cachedFacetId`) to the canonical
surface load. It does not run to a verdict because it does not compile: the
`static_assert` in `ThermalLoadTests.cpp` (the surface load holds a face and a
flux and nothing else) and the one in P18-DATA's `ThermalDataTests.cpp` both
fire. That is a kill at compile time, recorded as such and not as a VOID.

## Every probe

| Probe | Mutation | Verdict | Failing cases |
| --- | --- | --- | --- |
| L01 / L01b | surface share without the 1/3 | KILLED | 15 / 15 |
| L02 / L02b | surface share over 4 | KILLED | 15 / 16 |
| L03 / L03b | volume share over 3 | KILLED | 12 / 13 |
| L04 / L04b | volume share without the 1/4 | KILLED | 12 / 13 |
| L05 | facet area doubled | KILLED | 15 |
| L06 / L06b | heat-flux sign flipped | KILLED | 17 / 17 |
| L07 / L07b | `abs` of the heat flux | KILLED | 6 / 6 |
| L08 / L08b | `abs` of the generation | KILLED | 3 / 3 |
| L09 / L09b | area in mm² | KILLED | 15 / 15 |
| L10 / L10b | volume in mm³ | KILLED | 12 / 13 |
| L11 | shared node overwritten, not summed | KILLED | 12 |
| L12 | vector indexed by node-id value | VOID (unused `map`) |  |
| L12b | same, with `map` still used | KILLED | 38 |
| L13 | vector sized by the largest node id | KILLED | 30 |
| L14 | numbering check removed | VOID (unused function) |  |
| L14b | same, reformulated | KILLED | 3 |
| L15 | one boundary facet dropped | KILLED | 10 |
| L16 | one tetrahedron dropped | KILLED | 7 |
| L17 | facet strictly-ascending check removed | KILLED | 1 |
| L18 | element strictly-ascending check removed | SURVIVED |  |
| L18 rerun | same, after the test written for it | KILLED | 1 |
| L19 | equal ids accepted as ascending | KILLED | 1 |
| L20 | empty facet list is a zero load | KILLED | 1 |
| L21 | empty element list is a zero load | KILLED | 1 |
| L22 | zero-area facet accepted | KILLED | 1 |
| L23 | element validity gate removed | KILLED (abort, 0xc0000409) | 1 |
| L24 | missing triangle not detected | KILLED (segmentation fault) | 1 |
| L25 | duplicate load id accepted | KILLED | 1 |
| L26 | non-finite value accepted | VOID (unused function) |  |
| L26b | same, reformulated | SURVIVED |  |
| L26c | same, after the test written for it | KILLED | 1 |
| L27 | final finiteness scan removed | SURVIVED: dead code, scan deleted | retired |
| L28 / L28b | extra source term in the load vector | KILLED | 8 / 8 |
| L29 | mesh stamp omitted from the source | KILLED | 5 |
| L30 | same counts taken as the same mesh | KILLED | 4 |
| L31 | loads omitted from the currentness comparison | KILLED | 1 |
| L32 | body omitted from the comparison | KILLED | 1 |
| L33 | geometry revision omitted from the comparison | KILLED | 1 |
| L34 | `describes(ThermalDofMap)` ignores the stamp | KILLED | 1 |
| L35 | `describes(Mesh)` ignores the stamp | KILLED | 3 |
| L36 | surface total includes the volume | KILLED | 4 |
| L37 | nearest-face fallback | KILLED | 6 |
| L38 | unresolved reported as "without facets" | KILLED | 5 |
| L39 | unresolved treated as resolved | KILLED | 5 |
| L40 | `abs` of a tetrahedron volume without the gate | KILLED | 1 |
| L41 | running-sum overflow gate removed | SURVIVED |  |
| L41b | same, after the reorder and the test | KILLED | 1 |
| L42 | share formed as `(q''A)/3` | SURVIVED |  |
| L42b | same, after the test checks WHICH gate fired | KILLED | 1 |
| L43 | first boundary facet counted twice | KILLED | 16 |
| L44 | first tetrahedron counted twice | KILLED | 12 |
| L45 | the material enters the prepared-load identity | KILLED | 1 |
| L46 | a canonical load persists a facet id | KILLED at compile time | n/a |

## What the probes found

**F1 — a dead finiteness scan (L27; production code removed).** The first
version ended with a pass over the whole load vector that refused any non-finite
entry. L27 deleted it and every test passed. The reason is structural: each
share is checked finite before it is added, and the addition itself is refused
when the sum overflows, so no entry can become non-finite and the scan could
never fire. Dead code that looks like a safety check is worse than none, so it
was deleted and the argument written beside the place it used to be.

**F2 — the element-list uniqueness check had no test (L18; test added).**
Nothing gave the volumetric seam a repeated or misordered element list, although
the surface seam had that test. `RepeatedAndMisorderedTetrahedraAreRefused`
now does. The rerun killed it.

**F3 — the order of the value check and the target check had no test (L26b;
test added).** With the value check disabled no test noticed. The order is the
contract (a bad number is reported as a bad number, not as a bad face), so
`ValuesAreCheckedBeforeTargetsAreResolved` pins it. L26c is killed.

**F4 — the running-sum overflow gate could not be reached (L41; formulas
reordered, test added).** The share was formed as `(q'' A) / 3`, so any input
big enough to overflow the running sum had already overflowed the product. The
gate was unreachable, not untested. The two shares are now `q'' (A/3)` and
`q''' (V/4)`: the division comes first, the product cannot overflow where the
sum would, and the gate fires. A rectangle with `q'' = 1e308` is the test.

**F5 — the order of the division was classified but not verified (L42; test
tightened).** Putting the old form back still returned `NonFiniteResult`, just
from the other gate. The two overflow cases now assert the message of the gate
that fired: "share of facet" for the first, "superposing facet" and not "share
of facet" for the second. L42b is killed.

**F6 — three probes did not compile (L12, L14, L26; method).** `-Werror` refuses
the unused name the mutation leaves behind. Each was reformulated so every name
is still used (L12b, L14b, L26b) and rerun; L26b then surfaced F3.

**F7 — L28 had to be re-anchored.** The text it used as an insertion point went
away with the dead scan (F1). It was rerun on the final text as L28b.

## What was re-established on the final text

Two source lines changed after batch 1: the two share formulas (F4) and the
deleted scan (F1). Every probe that touches either was rerun on the final text:
L01b to L04b and L06b to L10b (formulas), L28b (anchor), L41b and L42b (gates).
The other probes mutate lines that did not change, and the tests only gained
cases since, so their kills still hold. The file restored after each run was
searched for every mutant fragment and for the original lines.

## Equivalent mutants considered and not run

Reordering the three corner additions of a facet (floating-point addition of
the same three values into three different entries is independent of order);
accumulating per-load totals in a different order from the vector (only the
reported total moves, by at most a few ulps, which the 1e-13 tolerance already
bounds). Recorded so a reader does not count them as gaps.
