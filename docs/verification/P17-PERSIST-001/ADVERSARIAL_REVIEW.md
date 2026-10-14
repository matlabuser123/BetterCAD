# P17-PERSIST-001 — adversarial review

```text
RESULT:              PASS
QUESTIONS ASKED:     33 from the brief's own list, plus the reading pass
CREDIBLE FINDINGS:   13 -- all in the TESTS or the EVIDENCE, none a
                     production defect
PRODUCTION DEFECTS:  0
REMAINING BLOCKERS:  0
```

Thirteen things were wrong and were fixed. **None of them was in the
serializer.** Six were found by reading and seven by a test failing, and the
two sets are disjoint — the same split P17-CMD-001 measured, and worth
recording again because it keeps arguing for doing both.

## The brief's attack list, answered

| Attack | Answer |
| --- | --- |
| Can a `NodeId` leak into the schema? | No. The one type holding one refuses the save; the reader refuses the key by name. Probes P12, P13. |
| Can an `ElementId` or a facet id become authority? | No type in the definition holds one. |
| Can a `DofIndex` be serialized "because it is convenient"? | Not reachable: the writer's only argument is the document object. |
| Can `PreparedStructuralLoads` be written to speed reload? | Not reachable, and nothing prepares anything in `io`. |
| Can a `ConstraintSet` be serialized? | *Nameable* through `StructuralRestraint.hpp` → `StructuralDof.hpp`; **not written**, and the forbidden-key scan checks it. Stated as partial reachability rather than claimed isolation. |
| Can `K`/`F` be serialized as a cache and become authority? | `StructuralSystem.hpp` is not reachable from this TU. |
| Can displacement/stress/reaction arrays appear after a solve? | No — four-state byte identity, and the arrays are unreachable. |
| Can save-before-solve and save-after-solve differ? | No. Byte-compared, four states, 4426 bytes each. |
| Can only the file size be compared while the bytes differ? | The test compares the strings; sizes are recorded as supplemental and labelled as such. |
| Can a loaded solved document appear current without re-solving? | No, structurally: a `StructuralResult` does not live on a document object. |
| Can a `GeometryReference` be replaced by mapped facets? | No: `faceNameToJson` writes the selector, and nothing in `io` can resolve one. |
| Can an unresolved reference silently rebind? | **Not writable.** The reader has no `Document`, `Regenerator`, `Mesher` or geometry. Tested against all six real faces as well. |
| Can a malformed reference be treated as merely unresolved? | No — three malformed cases refused, one well-formed-unresolved case accepted, in one test. |
| Can duplicate ids overwrite an earlier entry? | No. Refused by name, with the index: `loads[1].id`. Probe P6. |
| Can an unknown solver type fall back silently? | No. `unknown solver algorithm 'pardiso'`. |
| Can a missing solver setting pick up a library default? | No — every field is always written, so an absent one is malformed. Three cases. Probe P2. |
| Can display units (mm, MPa) leak into canonical storage? | No. The units test checks for `2500000.0` and against `"MPa"`, `"kN"`. Probe P3. |
| Can float serialization lose enough precision to alter the re-solve? | No. Four awkward values compared bit for bit, including `0.1 + 0.2`. Probe P4. |
| Can hash iteration make the file order nondeterministic? | There is no map, no hash and no sort in the serializer. |
| Can loading allocate new `LoadId`s? | No. Probe P7. |
| Can a loaded id allocator later collide? | `LoadId`/`RestraintId` are not document-allocated; collision is by **refusal**, and it is tested against ids that came from a file. |
| Can an empty/no-restraint analysis be wrongly refused at load time? | No — it loads, and `P17-VALID` refuses the *solve*. The brief's §113 distinction. |
| Can a failed solve alter the saved bytes? | No — state D of the four. |
| Can a stale result alter the saved bytes? | No — state C, reached by remeshing, which touches no document object. |
| Can viewer deformation scale alter structural persistence? | No P17-VIZ state is in the definition; there is nothing to write. |
| Can undo/redo history leak into the schema? | No. Two paths to one state, identical bytes, different revisions. Probe P1. |
| Can load → save → load keep old runtime ids or pointers? | No pointer or handle is written. The destroy/load test keeps nothing but the file. |
| Can a fresh re-solve bypass `P17-VALID`? | No — the test calls `solveStructuralAnalysis`, the single production entry. |
| Can a malformed file partially mutate the open document? | No — `documentFromJson` builds a fresh document. Checked twice: fingerprint and re-saved bytes. |
| Can zero filtered tests be reported as PASS? | The probe harness refuses a red baseline, records the baseline count (64), and every probe reports the test names that died. |
| Can a stale binary generate the evidence? | Every probe rebuilds; the run ends with a restore **build**; the qualification runs from clean in three presets. |

## The six found by reading

**R1 — the forbidden-key scan would have hit the sketch's own `constraints`.**
Scanning the whole file for `"constraint` matches `"constraints"`, which every
sketch writes. Caught before it could fail, and fixed by scoping the scan to the
structural section — which is the right question anyway.

**R2 — an explicit `"loads": []` normalises away on the first save.** The writer
omits an empty collection, so a hand-written file carrying `[]` is accepted and
then written back without it. Not a defect — the same property P16's
`boundary_sets` has, and the brief's §142 contemplates it — but it was
undocumented and untested. Now
`StructuralPersistence_AnExplicitEmptyCollectionNormalisesThenIsByteStable`.

**R3 — there is no `LoadId` allocator to restore.** The brief's §83 asks about
one. `Id.hpp` says `LoadId` and `RestraintId` "deliberately do NOT widen" to
`ObjectId`, so they are not document-allocated: the caller chooses the id and a
collision is prevented by refusal. The thing worth proving was that the refusal
still works against ids that came from a *file*, which was untested. Now
`StructuralPersistence_ALoadedIdCannotBeReusedByALaterCommand`.

**R4 — three assertions were weak.** `report.status() != Accepted` was being used
to mean "the solve must not proceed" — and it passes on
`AcceptedWithWarnings`, which *does* proceed. Tightened to `== Rejected` in all
three places.

**R5 — and one was the wrong claim.** The re-solve comparison asserted
`validation.status() == Accepted` on both runs. That would pass if one run
warned and the other did not — the exact equivalence the test exists for. It now
records the original verdict and finding count and requires the reloaded run to
match both.

**R6 — two probes would have been VOID.** P5 as written used
`std::ranges::sort` and P9 used `std::abs`, neither of whose headers the
serializer includes. A mutation that does not compile is a void result, not a
kill, so both were rewritten to need no new include — P5 reverses the iteration
instead of sorting, P9 uses a conditional negation.

## The seven found by a test failing

**T1** — `toString(LoadKind::SurfaceTraction)` is `surface_traction`, not
`surface traction`; my non-vacuity substring was wrong.

**T2** — `restraints` contains `strain` and `relative_residual_tolerance`
contains `residual`, so the forbidden-name scan reported two false positives
against the section's own legitimate keys. Fixed by matching the opening quote,
and the test now *also* asserts both of those keys are present, so the quoting
rule is shown to be a precision fix and not a hole.

**T3 — a box cannot test mesh refinement.** `fineElements > coarseElements`
failed with `24 > 24`: a block's boundary is planar and exactly representable,
so 20 mm and 5 mm give the same mesh. P16-SIZE-001, P16-CMD-001 and
P16-PERSIST-001 each recorded this and it was in my own standing notes, and I
used a block anyway. Now an r10 × 20 mm cylinder, 499 → 1313 elements.

**T4 — `MeshStamp::generation` is 0 for a fresh mesher's first mesh.** The
assertion `generation != 0U` was meant to prove the re-solve bound to fresh
derived state and was simply wrong. Replaced with a `staleReasons` check against
`currentResultSource`, which asks the real question — is this result current for
the mesh that exists now — through P17-DATA-001's own comparison.

**T5 — two documents never have identical bytes.** A `Document` generates a
fresh UUID on construction and writes it into the header, so three
cross-document byte comparisons were failing on the header rather than on
anything P17 does. Fixed with one shared helper that compares the structural
section, used in all three and documented at the helper.

**T6 — `1e999` is refused by the parser, not by the validator.** I had written
in the serializer that `validate(StructuralLoad)` "is also what refuses a
non-finite value: `readNumber` checks `is_number()` and would accept an
infinity written as 1e999". The measurement says otherwise: nlohmann rejects the
out-of-range literal outright, so an infinity never becomes a number. The
comment was corrected to say the validator is the second line and not the first
— and P16 had recorded the same thing ("a non-finite global size cannot even be
expressed"), which I had read and then written the opposite of.

**T7 — a hardcoded `"feature": 6` is not the fixture's body.** It is harmless in
a rejection payload, which fails before any id is consulted — but the one
*positive* case compared the decoded reference against the fixture's face and
failed. That case now uses the fixture's own id; the rejection payloads keep the
arbitrary literal with a note saying why it cannot matter.

## What was looked for and not found

```text
a second file format or linked subdocument          none
a second reference encoding                         none
a second validation rule duplicating the core's     none
a gratuitous version bump                           none
migration code for a schema that never shipped      none
a sort that would break command semantics           none
a map or hash in the serializer                     none
a result serializer anywhere in BetterCAD           none
an export macro on a header-defined constexpr       none (checked in the
                                                    pre-freeze gates)
a guard the happy path cannot reach                 see below
```

**On unreachable guards**, which cost P17-VIZ-001 four of its fifteen gaps: the
one candidate here is `enumName`'s `"unknown"` fallback, reachable only by an
enumerator absent from a name table. It cannot be tested by calling the writer
— every enumerator *is* in its table, which is the condition being guarded —
so the guard is backed by per-enumerator round-trip tests instead: both analysis
modes, all seven non-empty restraint masks, all three persistable load kinds and
the one solver algorithm are each put through a save and load in turn. A table
size check could not catch a *wrong* entry; these do.

The other candidate, `refuseExtras`, **is** reachable and is reached: three
cases drive it (a pressure carrying a traction, a gravity load carrying a face,
and the per-kind key union that `requireObject` cannot narrow).
