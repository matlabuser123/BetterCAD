# P18-BC-001 — mutation testing

```text
SUBJECT:  does the suite fail when the boundary kernel, the preparation, the target
          resolution, the provenance or a canonical header is changed in a way the
          brief names?
METHOD:   one textual mutation of production code, rebuild the test targets in
          debug-ext, run every ThermalBC case in its OWN process through ctest, record,
          restore the file. mutate.ps1 does exactly that, so a mutant that aborts
          or crashes is counted case by case. After every batch the restored files
          were compared by blob hash with the hashes taken before it started.
RUNS:     70 runs in four batches (52 + 8 + 5 + 5)
DISTINCT: 58 mutations
RESULT:   58 killed, 0 survived, 0 void at the end
          (one survivor and six voids on the way, each resolved, see below)
```

## How a run ends

| Verdict | Meaning |
| --- | --- |
| KILLED | at least one ThermalBC case failed, aborted or crashed, or the test target no longer compiled because a `static_assert` in a test refuses the mutant |
| SURVIVED | every case passed: a gap in the tests, an equivalent mutant, or dead code |
| VOID | the mutant did not compile for a reason unrelated to the tests (`-Werror` refuses the unused name it leaves behind); it proves nothing and is rewritten |

## The brief's mutation list, and which probe answers each

| Brief | Probe | Cases failing (of 92) |
| --- | --- | --- |
| force prescribed `T` to zero | B01 | 5 |
| store Celsius | B02 | 5 |
| skip node deduplication | B03 | 3 |
| first conflicting `T` wins | B04 | 6 |
| last conflicting `T` wins | B05 | 6 |
| (conflicting `T` averaged) | B06 | 6 |
| Robin: diagonal and off-diagonal all `hA/9` | B14b | 15 |
| Robin diagonal lumping `hA/3 I` | B15b | 17 |
| omit `Qconv` | B16b | 21 |
| negate `Qconv` | B17 | 17 |
| negate `Kconv` | B18 | 22 |
| multiply `Kconv` by `k` | B19 (a factor of the test conductivity, 45) | 20 |
| divide the matrix by the wrong factor | B20 | 21 |
| divide the source by the wrong factor | B21 | 17 |
| use mm² area | B22 | 20 |
| omit mesh-source provenance | B43 | 6 |
| accept same counts as the same mesh | B44 | 4 |
| persist `NodeId`s or facet IDs in a canonical BC | B51 | compile time |
| nearest-face fallback | B41 | 5 |
| empty target to success | B09, B10 | 2 each |
| include the material revision in the geometric BC source | B50 | 1 |
| reject a no-Dirichlet BC set at the BC layer | B13 | 8 |
| add a penalty diagonal | B52 (representative, see below) | 24 |
| source-only convection (matrix omitted) | B54 | 24 |
| drop one facet | B55 | 9 |
| double-count one facet | B56 | 12 |

B52 is representative, not literal: there is no global matrix in this milestone to
add a penalty to, so the probe puts an enormous number on the diagonal of the
local Robin matrix, which is the nearest thing that exists. The real protection
against a penalty is structural: a constraint is a `Temperature` in a
`ThermalConstraintMap`, the prepared type has no matrix, no penalty and no
right-hand side (compile-fail cases), and the scope test asserts that the
convection terms of a face carrying a prescribed temperature are bit-identical to
those of the face alone.

B51 adds a mesh-local member (`std::uint32_t cachedNode`) to the canonical
prescribed temperature. It does not run to a verdict because it does not compile:
the `static_assert` in `ThermalBoundaryTests.cpp` (the record is a face and a
temperature) and the one in P18-DATA-001's `ThermalDataTests.cpp` both fire. That
is a kill at compile time, recorded as such and not as a VOID.

## Every run

| Probe | Mutation | Verdict | Failing cases |
| --- | --- | --- | --- |
| B01 | prescribed temperature forced to zero | KILLED | 5 |
| B02 | prescribed temperature stored in celsius | KILLED | 5 |
| B03 | node deduplication skipped | KILLED | 3 |
| B04 | first conflicting temperature wins | KILLED | 6 |
| B05 | last conflicting temperature wins | KILLED | 6 |
| B06 | conflicting temperatures averaged | KILLED | 6 |
| B07 | condition attribution keeps only the first | KILLED | 3 |
| B08 | conflict counted per request not per unknown | KILLED | 1 |
| B09 | empty dirichlet target is a success | KILLED | 2 |
| B10 | empty convection target is a success | KILLED | 2 |
| B11 | facet uniqueness checks removed | KILLED | 1 |
| B12 | numbering checks removed | KILLED | 4 |
| B13 | no dirichlet set rejected | KILLED | 8 |
| B14 | robin matrix centroid quadrature | VOID (did not compile) |  |
| B15 | robin matrix diagonally lumped | VOID (did not compile) |  |
| B16 | robin source omitted | VOID (did not compile) |  |
| B17 | robin source negated | KILLED | 17 |
| B18 | robin matrix negated | KILLED | 22 |
| B19 | robin matrix multiplied by a conductivity | KILLED | 20 |
| B20 | robin matrix divided by the wrong factor | KILLED | 21 |
| B21 | robin source divided by the wrong factor | KILLED | 17 |
| B22 | robin area in square millimetres | KILLED | 20 |
| B23 | robin source ambient in celsius | KILLED | 19 |
| B24 | robin matrix takes the ambient | KILLED | 19 |
| B25 | robin share formed as product then quotient | SURVIVED |  |
| B26 | robin matrix overflow gate removed | VOID (did not compile) |  |
| B27 | robin source overflow gate removed | VOID (did not compile) |  |
| B28 | negative coefficient accepted in the kernel | VOID (did not compile) |  |
| B29 | nan coefficient passes the kernel check | KILLED | 2 |
| B30 | ambient ignored when the coefficient is zero | KILLED | 1 |
| B31 | zero area kernel accepted | KILLED | 1 |
| B32 | hand and ambient swapped | KILLED | 3 |
| B33 | corner order sorted in the facet | KILLED | 1 |
| B34 | zero area facet accepted by the seam | KILLED | 1 |
| B35 | identical convections deduplicated | KILLED | 1 |
| B36 | duplicate condition ids accepted | KILLED | 1 |
| B37 | invalid identity accepted | KILLED | 1 |
| B38 | temperature value check removed | KILLED | 1 |
| B39 | zero coefficient accepted by preparation | KILLED | 1 |
| B40 | ambient value check removed | KILLED | 1 |
| B41 | nearest face fallback | KILLED | 5 |
| B42 | unresolved treated as resolved | KILLED | 5 |
| B43 | mesh stamp omitted from the source | KILLED | 6 |
| B44 | same counts taken as same mesh | KILLED | 4 |
| B45 | conditions omitted from the comparison | KILLED | 1 |
| B46 | body omitted from the comparison | KILLED | 1 |
| B47 | geometry omitted from the comparison | KILLED | 1 |
| B48 | describes numbering ignores the stamp | KILLED | 1 |
| B49 | describes mesh ignores the stamp | KILLED | 3 |
| B50 | material enters the prepared boundary identity | KILLED | 1 |
| B51 | canonical prescribed temperature persists a node | KILLED at compile time | n/a |
| B52 | huge diagonal added to the robin matrix | KILLED | 24 |
| B14b | robin matrix centroid quadrature | KILLED | 15 |
| B15b | robin matrix diagonally lumped | KILLED | 17 |
| B16b | robin source omitted | KILLED | 21 |
| B26b | robin matrix overflow gate removed | KILLED | 1 |
| B27b | robin source overflow gate removed | KILLED | 3 |
| B28b | negative coefficient accepted in the kernel | KILLED | 3 |
| B25b | robin share formed as product then quotient | KILLED | 1 |
| B53 | robin matrix formed as product then quotient | KILLED | 1 |
| B54 | robin matrix omitted source only | KILLED | 24 |
| B55 | one convection facet dropped | KILLED | 9 |
| B56 | one convection facet counted twice | KILLED | 12 |
| B57 | node id value used as the unknown | KILLED | 9 |
| B58 | unresolved condition silently skipped | KILLED | 5 |
| B14c | robin matrix centroid quadrature | KILLED | 15 |
| B18b | robin matrix negated | KILLED | 23 |
| B04b | first conflicting temperature wins | KILLED | 6 |
| B35b | identical convections deduplicated | KILLED | 1 |
| B43b | mesh stamp omitted from the source | KILLED | 6 |

## What the probes found

**F1 — the order of the division had no test (B25; tests added, production
unchanged).** The source is formed as `(h * (A / 3)) * T_inf`, the matrix as
`h * (A / 12)`: the division first, so a product overflows only when the entry
itself would (the order P18-LOAD-001 settled on, for the same reason).
`(h * T_inf * A) / 3` returned the same numbers on every input the suite used, so
B25 survived. Two inputs now separate them: `h A = 3e308` with `h = 1e308` (the
product overflows, `h A / 12 = 2.5e307` does not) and `h T_inf A = 3e308` with
`h = T_inf = 1e154` (`h T_inf A / 3 = 1e308`). B25b and its sibling on the matrix,
B53, are killed.

**F2 — six probes did not compile (B14, B15, B16, B26, B27, B28; method).** The
mutation left a variable or a parameter unused, which `-Werror` refuses. Each was
reformulated so every name is still used (`B14b`, `B15b`, `B16b`, `B26b`, `B27b`,
`B28b`) and rerun against the same suite; all six were killed.

**F3 — the first overflow test could not overflow.** It asked for a matrix
overflow on the 40 × 30 mm cap, whose area is 1.2e-3 m²: `h` would have to exceed
the largest double. The expected gate did not fire, and the test, not the
production code, was wrong. The test now uses the source gate on the real cap
(`h = T_inf = 1e300`) and a kernel call with a 100 m² triangle for the matrix gate.

**F4 — the sparse fixture the brief described cannot be built.** See DIRICHLET.md.
Not a production defect; the fixture was redesigned and the property it was meant
to show (no unknown equals its handle) is still shown.

**F5 — the sample re-run after the export fix (batch 4; no production logic
changed).** The first qualification run failed the shared-library build because one
struct lacked a DLL export annotation (ADVERSARIAL_REVIEW.md, F7), and the fix
touched `ThermalBoundary.hpp`, which the probes mutate. The annotation cannot change
what a mutation does in a static build, but it was not taken on trust: five probes
spanning the kernel (B14c centroid matrix, B18b negated matrix), the constraint
resolution (B04b first-wins), the convection preparation (B35b deduplicated
convections) and the provenance (B43b stamp omitted) were rerun on the final text,
and all five are killed (15, 23, 6, 1 and 6 cases). They are counted in the 70 runs
and are repeats of probes already among the 58 distinct mutations.
## Equivalent mutants considered and not run

Reordering the three corner additions of one facet's local sums (independent
floating-point additions into different entries); storing `Kconv` row-major
against column-major (the matrix is symmetric, and symmetry is asserted bitwise);
taking the absolute value of an area already known positive. Recorded so a reader
does not count them as gaps.

## Not mutated, and why

A scatter that overwrites a shared node instead of adding: this milestone has no
scatter. What it must do is keep every facet's contribution so the assembler can
add them, and B55 (a facet dropped) and B56 (a facet counted twice) are the probes
for that; the overwrite itself is checked in the test-side accumulation
(`SharedFacetNodeContributionsSum`: a shared node's diagonal is 4, not 2).