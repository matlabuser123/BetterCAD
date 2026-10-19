# P18-ASSEMBLY-001 — mutation probes

```text
SUBJECT:  deliberate defects applied to the production source, each run against the
          thermal assembly tests, to show the tests can fail
RESULT:   75 distinct mutations, 74 killed, 1 survived (an equivalent, explained),
          0 void; 5 test gaps found and closed
```

## Method

Each probe is a textual replacement in `src/thermal/ThermalSystem.cpp` or
`src/thermal/ThermalSparse.cpp` (the only two production files of the milestone),
followed by a build of `bettercad_tests` in `debug-ext` and a run of every
`[thermal][assembly]` case, then an unconditional restore. The SHA-256 of both files
before the first probe and after the last were equal (`f16b0f21...` and
`5c203857...`), and `mutate.ps1` restores in a `finally` block. Scripts and every log are
under `qualification/`. A probe counts as **killed** if the run exits non-zero. Eight
were killed by an abnormal exit (the test binary aborted on a dereferenced empty
`optional` or an out-of-range index, which is the mutation breaking its contract louder
than an assertion would); they are marked in the notes below and are not hidden.

Probes A01..A61 and B01..B13 follow the brief's list; C01 is a combination. The first
probe, A01, ran against the broader `[assembly]` tag (520 cases, including P17's) and was
killed by 12; the rest ran the milestone's own tag.

## Results

| ID | Mutation | Verdict | Test cases failing |
| --- | --- | --- | --- || A01 | conduction contribution overwrites instead of summing | KILLED | 12 of 520 cases failed |
| A02 | capacity contribution overwrites instead of summing | KILLED | 9 of 58 cases failed |
| A03 | prepared heat input omitted | KILLED | 10 of 58 cases failed |
| A04 | convection source overwrites instead of summing | KILLED | 7 of 58 cases failed |
| A05 | convection source omitted | KILLED | 7 of 58 cases failed |
| A06 | convection matrix omitted | KILLED | 9 of 58 cases failed |
| A07 | convection facet rows rotated | KILLED | 28 of 58 cases failed |
| A08 | element columns reversed against rows | KILLED | 49 of 58 cases failed |
| A09 | node handle minus one used as the row | KILLED | 2 of 58 cases failed |
| A10 | convection facets not reserved in the K pattern | KILLED (survived the first pass; closed by a new test, see below) | 1 of 61 cases failed |
| A11 | duplicate columns kept in the layout | KILLED | 44 of 58 cases failed |
| A12 | row columns not sorted | KILLED | 44 of 58 cases failed |
| A13 | only the upper triangle of Ke scattered | KILLED | 50 of 59 cases failed |
| A14 | element order not recorded | KILLED | 3 of 59 cases failed |
| A15 | steady always (no capacity ever) | KILLED | abnormal exit (-1073740791): the test binary crashed |
| A16 | recorded mode always steady | KILLED | 2 of 59 cases failed |
| A17 | mesh stamp not recorded | KILLED | 5 of 59 cases failed |
| A18 | node count not recorded | KILLED | 5 of 59 cases failed |
| A19 | body not compared | KILLED (survived the first pass; closed by a new test, see below) | 1 of 61 cases failed |
| A20 | mesh stamp not compared | KILLED | 2 of 59 cases failed |
| A21 | node count not compared | KILLED (survived the first pass; closed by a new test, see below) | 1 of 61 cases failed |
| A22 | geometry not compared | KILLED | 1 of 61 cases failed |
| A23 | mode not compared | KILLED | 2 of 61 cases failed |
| A24 | material revision not compared | KILLED | 3 of 61 cases failed |
| A25 | material id not compared | KILLED | 1 of 61 cases failed |
| A26 | loads not compared | KILLED | 2 of 61 cases failed |
| A27 | convections not compared | KILLED | 2 of 61 cases failed |
| A28 | prescribed temperatures enter the source | KILLED | 2 of 61 cases failed |
| A29 | loads not recorded in the source | KILLED | 1 of 61 cases failed |
| A30 | material revision not recorded | KILLED | 2 of 61 cases failed |
| A31 | geometry not recorded | KILLED (survived the first pass; closed by a new test, see below) | 1 of 62 cases failed |
| A32 | body not recorded | KILLED (survived the first pass; closed by a new test, see below) | 1 of 62 cases failed |
| A33 | model-level numbering check removed | KILLED | 2 of 61 cases failed |
| A34 | prepared load staleness not checked | KILLED | 2 of 61 cases failed |
| A35 | prepared boundary staleness not checked | KILLED | 1 of 62 cases failed |
| A36 | conductivity edit not detected | KILLED | 1 of 62 cases failed |
| A37 | transient capacity never requested | KILLED | abnormal exit (-1073740791): the test binary crashed |
| A38 | convection dof range check removed | KILLED | abnormal exit (-1073740791): the test binary crashed |
| A39 | heat vector length check removed | KILLED | abnormal exit (-1073740791): the test binary crashed |
| A40 | empty mesh accepted | KILLED | 1 of 62 cases failed |
| A41 | rejected element skipped | KILLED | abnormal exit (-1073740791): the test binary crashed |
| A42 | convection matrix negated | KILLED | 9 of 62 cases failed |
| A43 | convection source negated | KILLED | 7 of 62 cases failed |
| A44 | capacity halved | KILLED | 11 of 62 cases failed |
| A45 | diagonal epsilon added to K | KILLED | 16 of 62 cases failed |
| A46 | elements visited in reverse order | KILLED | 2 of 62 cases failed |
| A47 | non-finite classified as inconsistent | KILLED | 1 of 62 cases failed |
| A48 | wrong conductivity (doubled) passed to the kernel | KILLED | 11 of 62 cases failed |
| A49 | capacity computed with density ignored | KILLED | 12 of 62 cases failed |
| A50 | lookup returns the neighbour for an absent pair | KILLED | 6 of 62 cases failed |
| A51 | symmetry error always zero | KILLED | 1 of 62 cases failed |
| A52 | non-finite entry check removed from the matrix | KILLED | 2 of 62 cases failed |
| A53 | column bound off by one | KILLED | 1 of 62 cases failed |
| A54 | repeated column allowed | KILLED | 1 of 62 cases failed |
| A55 | last row start not checked | KILLED | abnormal exit (-1073740791): the test binary crashed |
| A56 | symmetry bound strict | KILLED | 1 of 62 cases failed |
| A57 | matrix product keeps only the last term | KILLED | 9 of 62 cases failed |
| A58 | heat total keeps only the last entry | KILLED | 6 of 62 cases failed |
| A59 | heat vector finite check removed | KILLED | 1 of 62 cases failed |
| A60 | non-square matrix reports symmetric | KILLED | 1 of 62 cases failed |
| A61 | symmetry gate not applied to K | SURVIVED | all passed |
| B01 | Ke added to C | KILLED | 12 of 62 cases failed |
| B02 | Ce added to K as well | KILLED | 12 of 62 cases failed |
| B03 | last element omitted | KILLED | abnormal exit (-1073740791): the test binary crashed |
| B04 | first element counted twice in K | KILLED | 11 of 62 cases failed |
| B05 | combined and per-load heat input both added | KILLED | 3 of 62 cases failed |
| B06 | penalty-enforced Dirichlet on unknown 0 | KILLED | 25 of 62 cases failed |
| B07 | entries below a threshold dropped | KILLED | 11 of 62 cases failed |
| B08 | transient with a missing capacity silently becomes steady | KILLED | abnormal exit (-1073740791): the test binary crashed |
| B09 | C / dt added into K | KILLED | 12 of 62 cases failed |
| B10 | convection matrix recomputed as a half off-diagonal | KILLED | 9 of 62 cases failed |
| B11 | matrix size taken from the largest handle | KILLED | 53 of 62 cases failed |
| B12 | convection matrix counted twice | KILLED | 10 of 62 cases failed |
| B13 | convection source counted twice | KILLED | 7 of 62 cases failed |
| C01 | upper-only Ke scatter with the symmetry gate removed | KILLED | 19 of 62 cases failed |

## What the probes found: five test gaps, all closed

The first pass left five survivors. Each was a real gap in the *tests*, none a defect in
the production logic:

| Survivor (first pass) | Why it survived | New test that kills it |
| --- | --- | --- |
| A10 convection facets not reserved in the K pattern | on a valid P16 mesh every facet pair is already in a tetrahedron, so no test ever presented a facet that is not | `ThermalAssembly_ConvectionFacetOutsideAnyElementExtendsThePattern` |
| A19 body not compared in `staleReasons` | the provenance comparison had only whole-record and a few single-field cases | `ThermalAssembly_StaleReasonsNameEachFieldAlone` (every field alone, then all together in enum order) |
| A21 node count not compared | the stamp identifies the builder, and no test grew a mesh after a snapshot | `ThermalAssembly_SameBuilderLaterSnapshotIsNotTheSameMesh` and the one above |
| A31 geometry not recorded | the mutation edits `sourceFor`, which builds **both** the recorded and the current source, so both sides lost the field and still agreed | `ThermalAssembly_BlockSourceRecordsExactlyItsInputs` (checks every recorded field against the inputs, not against a second call) |
| A32 body not recorded | same | same |

After the tests were added all five are killed, and the complete earlier set was *not*
re-run (the new tests only add assertions); the survivors' first-pass logs are kept as
`mutation-A..-pass1.log`.

## The one survivor: A61, and why it is an equivalent

A61 removes the symmetry gate from the production call of `fromCsr` for `K`. A correct
scatter writes the same local numbers into both triangles in the same order, so `K` is
exactly symmetric and the gate has nothing to refuse: no valid input can reach it. What
the gate is *for* is a defective scatter, and that is shown in two ways:

- **A13** (only the upper triangle of `Ke` scattered) is killed **by the gate**: the
  message is `the assembled conductance matrix was refused: max |A(i,j) - A(j,i)| = 1`.
- **C01** (A13 *and* the gate removed together) is killed by the independent dense
  oracle and the analytical cases, 19 of 62 cases failing. So the gate is not the only
  protection, and removing it does not let a wrong matrix through.

`AssemblyProblem::InconsistentSystem` is therefore reachable only from a defective
scatter (A13 fires it); `ThermalSparseMatrix::fromCsr` itself has every one of its six
problems tested directly with hand-written layouts.

## The brief's list, mapped

| Brief item | Probe |
| --- | --- |
| global index = `NodeId.value()` | A09 (handle minus one as the row) |
| matrix size = max NodeId + 1 | B11 |
| scatter K / C / Q with assignment instead of `+=` | A01, A02, A04 (the heat input starts at zero, so only the second Q source can be overwritten) |
| swap K and C scatter | B01 (Ke into C), B02 (Ce into K), B09 (C / dt into K) |
| omit one element / double one element | B03, B04 |
| omit convection matrix / source | A06, A05 |
| double convection matrix / source | B12, B13 |
| add both combined and individual load vectors | B05 |
| add epsilon to K diagonal | A45 (`1e-12`), killed |
| penalty-enforce Dirichlet | B06 |
| symmetrize K after scatter | **equivalent**: if the scatter is correct, symmetrising is the identity; if it is wrong the oracle comparisons and the gate catch the wrong values (C01) |
| assemble only upper triangle | A13, C01 |
| drop entries below threshold | B07 |
| accept same dimensions as source compatibility | A17, A18, A20, A21 (stamp and count), A33 to A35 |
| omit material source from provenance | A24, A25, A30, A36 |
| assemble transient with zero C when rho/cp missing | B08, A37 |
| use dense global matrix | not mutable by a one-line edit; audited instead (README.md, SPARSE_SMOKE.md) |
| introduce dt into C assembly | B09 |
| recompute the Robin matrix incorrectly in assembly | B10, A07 |

## Notes on individual rows

- **Killed by abnormal exit** (the binary aborted): A15, A37 (steady forced, so `capacity()`
  is empty where the tests dereference it), A38 and A39 (a removed range check lets an
  index run past a vector), A41 (a skipped element), A55 (the last row start unchecked),
  B03 (the last element omitted), B08 (a transient request resolved as steady). They are
  kills, but a crash is a blunter signal than an assertion.
- **Comment-only change after the probes.** After the last probe one comment was added to
  `ThermalSystem.cpp` (the numeric pass's `elementDofs` result is valid by construction,
  see ADVERSARIAL_REVIEW.md, reviewer question 3). No code changed. The three-preset
  qualification ran on the final text.
