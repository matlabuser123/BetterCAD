# P18-VIZ-001 — Mutation campaign

Each probe edits ONE production line (a sign, a comparison, a guard, a unit factor, a key field, a classifier row), rebuilds `bettercad_tests` in debug-ext, runs every `[thermal][viz]` case
(97 at the first pass, 100 at the second) and restores the file. The verdict is read from the test output, never from an exit code. The driver
(`qualification/mutations/mutation-driver-viz.ps1`) and the one-probe runner (`mutate-t.ps1`) are in this directory with the progress logs of both passes and the hashes of the five mutated files before each
pass; **after each pass every file hashed equal to its pre-campaign hash** (restore-hash verification). The campaign ran detached and no source or test file was edited while it ran.

## Result

**75 probes. 71 killed. 4 survived and are shown to be unreachable. 0 voids and 0 not-applied remain.**

| Pass | Probes | Outcome |
|---|---|---|
| 1 | 75 | 61 killed; 3 more killed by an **abort** (the mutated binary tripped a `std::vector` bounds assertion after printing its failures: `VIZ02`, `VIZ03`, `VIZ16`); 7 survivors; 2 did not compile (`VIZ33`, `VIZ35`); 1 not applied (`VIZ54`) and 1 whose target text holds double quotes was lost to the command line (`VIZ41`) |
| 2 | 7 | the three test gaps and the four repaired probes: `VIZ09`, `VIZ14`, `VIZ74` (survivors, closed by three new tests) and `VIZ33`, `VIZ35`, `VIZ41`, `VIZ54` (probes repaired: a branch that keeps the helper used, a runner that reads the text from files so quotes survive) — **all 7 killed** |

## Probes
| Id | Mutation | Result | Cases | Run |
|---|---|---|---|---|
| VIZ01 | node values are bound to the nodes in reverse order | KILLED | 21/97 cases failed, 28 checks failed | pass 1 |
| VIZ02 | the temperature channel is converted to Celsius | KILLED (the mutated binary printed 12 failed checks and aborted on a std::vector bounds assertion) |  | pass 1 |
| VIZ03 | the temperature channel is rescaled in place by 1000 | KILLED (32 failed checks, then a std::vector bounds assertion aborted the run) |  | pass 1 |
| VIZ04 | the heat-flux magnitude is reported in kW/m^2 | KILLED | 5/97 cases failed, 70 checks failed | pass 1 |
| VIZ05 | the gradient channel shows one component, not the norm | KILLED | 4/97 cases failed, 40 checks failed | pass 1 |
| VIZ06 | a NaN gradient component is swallowed by hypot | KILLED | 1/97 cases failed, 1 checks failed | pass 1 |
| VIZ07 | a short temperature array is accepted (zero fill) | KILLED | 1/97 cases failed, 1 checks failed | pass 1 |
| VIZ08 | a non-finite value is mapped instead of refused | KILLED | 1/97 cases failed, 1 checks failed | pass 1 |
| VIZ09 | the lowest value ties resolve to the LAST entity | KILLED | 1/100 cases failed, 1 checks failed | pass 2 |
| VIZ10 | the extreme search ties resolve to the LAST entity | KILLED | 4/97 cases failed, 11 checks failed | pass 1 |
| VIZ11 | the minimum is seeded from zero instead of the first value | KILLED | 9/97 cases failed, 42 checks failed | pass 1 |
| VIZ12 | the result elements are not compared id for id with the mesh | SURVIVED -> UNREACHABLE (see below) | All tests passed (28750 assertions in 97 test cases) | pass 1 |
| VIZ13 | the result is not checked against the numbering | KILLED | 2/97 cases failed, 2 checks failed | pass 1 |
| VIZ14 | the view no longer compares the result source | KILLED | 1/100 cases failed, 2 checks failed | pass 2 |
| VIZ15 | a channel with skipped entities is still a field | SURVIVED -> UNREACHABLE (see below) | All tests passed (28750 assertions in 97 test cases) | pass 1 |
| VIZ16 | an extreme over a partial field is allowed | KILLED (the mutated binary aborted on a std::vector bounds assertion: an extreme over a partial field indexed past the values) |  | pass 1 |
| VIZ17 | the OCCT adapter applies the metre-to-millimetre conversion twice | KILLED | 2/97 cases failed, 2 checks failed | pass 1 |
| VIZ18 | the OCCT adapter applies no conversion (metres drawn as millimetres) | KILLED | 3/97 cases failed, 6 checks failed | pass 1 |
| VIZ19 | a manual range stops clamping the colour (the value is refused) | KILLED | 3/97 cases failed, 5 checks failed | pass 1 |
| VIZ20 | the last band is half open (the maximum falls in no band) | KILLED | 21/97 cases failed, 212 checks failed | pass 1 |
| VIZ21 | a uniform range is divided (0/0) instead of one band | KILLED | 3/97 cases failed, 7 checks failed | pass 1 |
| VIZ22 | the shared band boundaries are not clamped against their predecessor | SURVIVED -> UNREACHABLE (see below) | All tests passed (28750 assertions in 97 test cases) | pass 1 |
| VIZ23 | a legend of another field is accepted | KILLED | 1/97 cases failed, 1 checks failed | pass 1 |
| VIZ24 | a partial field is banded | KILLED | 2/97 cases failed, 2 checks failed | pass 1 |
| VIZ25 | an element channel is drawn on the boundary-triangle view | KILLED | 1/97 cases failed, 1 checks failed | pass 1 |
| VIZ26 | an element value is smoothed: the third corner takes a neighbour element band | KILLED | 1/97 cases failed, 16 checks failed | pass 1 |
| VIZ27 | all three corners of a nodal triangle take the first vertex | KILLED | 3/97 cases failed, 10 checks failed | pass 1 |
| VIZ28 | the cache ignores the displayed frame | KILLED | 1/97 cases failed, 3 checks failed | pass 1 |
| VIZ29 | the cache ignores the result source | KILLED | 1/97 cases failed, 2 checks failed | pass 1 |
| VIZ30 | the cache ignores the display range | KILLED | 1/97 cases failed, 3 checks failed | pass 1 |
| VIZ31 | the cache ignores the view it was built for | KILLED | 1/97 cases failed, 4 checks failed | pass 1 |
| VIZ32 | a failed rebuild leaves the held buffer as the answer | KILLED | 1/97 cases failed, 1 checks failed | pass 1 |
| VIZ33 | any held result is current | KILLED | 8/100 cases failed, 20 checks failed | pass 2 |
| VIZ34 | a failed recompute brings the old result back as current | KILLED | 8/97 cases failed, 16 checks failed | pass 1 |
| VIZ35 | the stale reasons are not reported | KILLED | 3/100 cases failed, 5 checks failed | pass 2 |
| VIZ36 | a stale report keeps its badge | KILLED | 1/97 cases failed, 2 checks failed | pass 1 |
| VIZ37 | the GUI downgrades a failed validation to a pass-with-warnings badge | KILLED | 1/97 cases failed, 1 checks failed | pass 1 |
| VIZ38 | the GUI hides warnings: pass-with-warnings is shown as pass | KILLED | 1/97 cases failed, 1 checks failed | pass 1 |
| VIZ39 | a failed validation no longer makes the fields unavailable | KILLED | 1/97 cases failed, 1 checks failed | pass 1 |
| VIZ40 | a stale result leaves the fields available | KILLED | 4/97 cases failed, 5 checks failed | pass 1 |
| VIZ41 | the stale banner is blank | KILLED | 1/100 cases failed, 1 checks failed | pass 2 |
| VIZ42 | a report of another analysis supplies the badge | KILLED | 1/97 cases failed, 2 checks failed | pass 1 |
| VIZ43 | a report about another result keeps its badge | KILLED | 1/97 cases failed, 2 checks failed | pass 1 |
| VIZ44 | a node is inspected through its raw id as an index | KILLED | 4/97 cases failed, 20 checks failed | pass 1 |
| VIZ45 | an element ordinal is its raw id | KILLED | 3/97 cases failed, 17 checks failed | pass 1 |
| VIZ46 | the gradient magnitude is one component | KILLED | 1/97 cases failed, 12 checks failed | pass 1 |
| VIZ47 | the inspected heat flux is sign-flipped | KILLED | 3/97 cases failed, 47 checks failed | pass 1 |
| VIZ48 | a node of another numbering is inspected | KILLED | 2/97 cases failed, 2 checks failed | pass 1 |
| VIZ49 | an element of another mesh generation is inspected | KILLED | 2/97 cases failed, 2 checks failed | pass 1 |
| VIZ50 | the navigation target of an element is not its centroid | KILLED | 1/97 cases failed, 3 checks failed | pass 1 |
| VIZ51 | a frame time is the frame index times a nominal step | KILLED | 1/97 cases failed, 4 checks failed | pass 1 |
| VIZ52 | non-increasing stored times are accepted | SURVIVED -> UNREACHABLE (see below) | All tests passed (28750 assertions in 97 test cases) | pass 1 |
| VIZ53 | a steady result reports a time | KILLED | 1/97 cases failed, 1 checks failed | pass 1 |
| VIZ54 | a frame outside the stored states can be selected | KILLED | 1/100 cases failed, 2 checks failed | pass 2 |
| VIZ55 | the outward normal is inward (winding reversed) | KILLED | 9/97 cases failed, 902 checks failed | pass 1 |
| VIZ56 | a positive applied flux points out of the body | KILLED | 4/97 cases failed, 302 checks failed | pass 1 |
| VIZ57 | the recovered heat-flux direction is negated | KILLED | 4/97 cases failed, 36 checks failed | pass 1 |
| VIZ58 | the stored heat-flux vector is sign-flipped in the glyph | KILLED | 4/97 cases failed, 86 checks failed | pass 1 |
| VIZ59 | a zero heat flux is given an invented direction | KILLED | 1/97 cases failed, 3 checks failed | pass 1 |
| VIZ60 | a zero applied flux gets an arrow | KILLED | 1/97 cases failed, 2 checks failed | pass 1 |
| VIZ61 | a zero-classified target reads as complete | KILLED | 1/97 cases failed, 4 checks failed | pass 1 |
| VIZ62 | a partial classification reads as complete | KILLED | 1/97 cases failed, 45 checks failed | pass 1 |
| VIZ63 | an unresolved mapping is classified | KILLED | 2/97 cases failed, 4 checks failed | pass 1 |
| VIZ64 | the rendered count is not part of completeness | KILLED | 1/97 cases failed, 7 checks failed | pass 1 |
| VIZ65 | the applied-flux magnitude is signed (a negative flux is refused) | KILLED | 2/97 cases failed, 2 checks failed | pass 1 |
| VIZ66 | identical prescriptions are drawn twice | KILLED | 1/97 cases failed, 2 checks failed | pass 1 |
| VIZ67 | the display threshold is ignored | KILLED | 1/97 cases failed, 2 checks failed | pass 1 |
| VIZ68 | the recovered arrow is drawn with its head at the centroid | KILLED | 3/97 cases failed, 7 checks failed | pass 1 |
| VIZ69 | the named-set cardinality counts every boundary triangle | KILLED | 2/97 cases failed, 4 checks failed | pass 1 |
| VIZ70 | an overflowing arrow length reaches the geometry | KILLED | 1/97 cases failed, 2 checks failed | pass 1 |
| VIZ71 | loads other than the prepared ones are drawn | KILLED | 1/97 cases failed, 2 checks failed | pass 1 |
| VIZ72 | conditions other than the prepared ones are drawn | KILLED | 1/97 cases failed, 2 checks failed | pass 1 |
| VIZ73 | an incomplete target is not counted by the overlay | KILLED | 1/97 cases failed, 3 checks failed | pass 1 |
| VIZ74 | a vector glyph takes its ordinal from the raw element id | KILLED | 1/100 cases failed, 4 checks failed | pass 2 |
| VIZ75 | a result of another mesh generation is drawn | KILLED | 2/97 cases failed, 2 checks failed | pass 1 |


## What the first pass found

Three survivors were **test gaps**, each closed by a test that fails with the mutation:

* `VIZ09` — the minimum tie resolved to the last entity in the classifier's own record (`field.minimumNode`), while the extreme query was tested. Closed by `ThermalViz_TheLowestValueTieAlsoResolvesToTheFirstEntity`
  (295 K at nodes 7 and 9001: node 7 wins in both).
* `VIZ14` — `ThermalResultView::describes` no longer compared the result source, so a view built for one solve accepted another solve on the same mesh, numbering and counts. Closed by
  `ThermalViz_AViewBuiltForOneResultRefusesAnotherResultOnTheSameMesh`.
* `VIZ74` — a vector glyph's `ordinal` was the raw element id and no test looked at the member. Closed by `ThermalViz_VectorGlyphsCarryTheOrdinalAndNotTheRawElementId` (ids 17..20, ordinals 0..3).

Four survivors are **unreachable**, and are reported as such rather than counted as kills:

| Probe | Why no input reaches it |
|---|---|
| `VIZ12` | a post and a mesh sharing a stamp and a numbering hold the same tetrahedra, and a mesh cannot be edited in place |
| `VIZ15` | a channel's location comes from its own descriptor, so the classifying path never meets an absent value |
| `VIZ22` | the band boundaries are monotone and below the maximum whenever the uniform test has passed; the clamp cannot bind |
| `VIZ52` | the core stores strictly increasing finite times and cannot store others |

Each guard restates a requirement of the brief or the contract a neighbour relies on, and stays. Their "survival" is a property of the types, not of the tests.

Three of the kills are by **abort**: the mutated binary failed several checks and then died on a library bounds assertion. A crash is a failure of the suite, and the printed failures are in the logs, but the run
produced no summary line, which is why the runner reported it as unknown. They are counted as killed and labelled.

## The mutation that turned out to be a test defect

While the documentation was written (not by a probe) a test was found whose last paragraph was inside a comment ([ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md), F2). Un-commenting it exposed a
wrong assertion (a nodal temperature on a slab's exterior facets is identical in every frame, because every corner node is prescribed); it now uses an element channel under a shared range. It was fixed before
pass 2, so the second pass ran against the repaired suite.

## Not mutated

The OCCT adapter was mutated at its one conversion (`VIZ17`, `VIZ18`). The Qt application has no thermal code to mutate. Compile-fail cases and the architecture gate are their own mutation tests (each fixture is a
mutation of a valid tree that must fail).
