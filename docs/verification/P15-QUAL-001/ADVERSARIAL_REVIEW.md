# P15-QUAL-001 — final adversarial review

A **fresh cross-milestone review**, not a summary of the thirteen that preceded it. Each
earlier review attacked one milestone's diff. This one attacks the *boundaries between them*,
because that is where a fact can hold in every milestone's own tests and still fail when
identity, deletion, persistence, undo and the CLI are put in one sequence.

Read against the final committed tree `8d4b23c` / `1f6f2951`.

## The twenty-two questions

| # | Attack | Outcome |
| --- | --- | --- |
| 1 | Can identity ever become name-based? | **No.** An assignment holds a `MaterialId`. A rename changes the name and nothing else — the ID, the assignment, the whole definition, the mass, the inertia and every property are byte-identical afterwards. Nothing resolves by designation, name, position or similarity; `resolveMaterial` in the CLI resolves a *selector* to an ID at the moment of use and never stores one. |
| 2 | Can deleted A rebind B after any combination of save/load/undo/CLI? | **No — and this is the gate, not inherited evidence.** A and B share a designation with *different densities*; after deleting A the assignment is Unresolved-A through two round trips, a CLI query and the creation of a third same-designation material, and `effectiveMaterial()` is nullptr throughout. Only undoing the delete restores it, under the original ID. **Mutation-tested: making a dangling assignment rebind fails 22 assertions.** |
| 3 | Can Unknown ever become zero or a default? | **No.** `PropertyState` is Known / Unknown / Derived. One `render()` produces `UNKNOWN` in the CLI, mutation-tested in P15-CLI-001. A mass with no density is refused naming the property; FEA with no nu reports the Poisson ratio missing; transient conduction with no k reports the conductivity. The no-fabrication search over all P15 production code returned one hit, classified below. |
| 4 | Can same-dimension properties substitute for each other? | **No, and the type system is no help.** E, G, K and the four strengths are all `Quantity<pressure>`; P15-CLI-001 discovered that when two overloads collided. Three independent name tables keep them apart — the enumerator, the persisted key, the CLI code — and a gate gives the five stored ones five different values and requires each to survive on its own slot through the file and the CLI. A UTS does not substitute for a yield strength in any requirement set. |
| 5 | Can derived G/K become authoritative? | **No, structurally.** ADR-027 gives them no slot. The persisted key table has 9 entries for 11 kinds *because of that*, and P15-CLI-001 declined to lift that table into core precisely so the absence stays structural. After a load both are still `isDerived()` and not `isKnown()`, while E and nu come back supplied. |
| 6 | Can mass or inertia become authoritative persisted state? | **No.** A document with the mass, elastic constants, general report and consumer completeness all computed is saved and contains twelve forbidden tokens none of them, nor the computed numbers. Everything recomputes after a load. |
| 7 | Can mechanical and thermal density diverge? | **No.** There is one density property, in the mechanical block, and the transient-conduction requirement reads *that* one — asserted equal to `requireDensity` to 1e-15. There is no second slot to diverge from. |
| 8 | Can a clone edit mutate the library source? | **No, twice over.** The library entry is a compiled-in constant with no property values at all. A clone of an import, edited across metadata, mechanical, thermal, provenance *and* a removed property, leaves the source definition byte-identical and the library entry unchanged, through a file. |
| 9 | Can undo restore a new MaterialId instead of the old identity? | **No.** `CreateMaterialCommand` wraps `AddObjectCommand`, which restores the ID; the allocator only counts up so an ID is never reissued. The five-command chain redone gives the *same* `MaterialId`, and undoing a delete makes the assignment resolve again — which it could only do to that one material. |
| 10 | Can persistence lose provenance? | **No.** Records are keyed by property NAME, so no reordering can reattach one. Three different source kinds on three properties of RM-MAT-01 are re-checked after a load by kind, reference **and** date, so a swap changes an answer rather than shuffling equal things. |
| 11 | Can old files gain fabricated materials? | **No.** A pre-P15 document loads with zero materials and no default; version 1 still loads; version 3 is refused by name. A golden-text test asserts the exact bytes of a document with no materials, so no pre-P15 file's representation moved by a byte. |
| 12 | Can the CLI bypass core validation? | **No — checked, not assumed.** Every mutating call in `apps/bettercad_cli/Material*.cpp` is one of nine core APIs or four P15-CMD command objects. A search for direct writes to `.mechanical.`, `.thermal.`, `.provenance.` or `setDefinition` outside those paths returns nothing. The CLI holds no OCCT and no JSON header either. |
| 13 | Can the CLI and core disagree numerically? | **No.** Each number is parsed out of the CLI's output and compared for **exact equality** with the core's double. Exact is possible because the CLI prints shortest-round-trip text; a tolerance would have hidden the unit error that mutation M4 injected in P15-CLI-001. |
| 14 | Can a malformed load partially mutate a document? | **No.** Load returns a new Document, so it is atomic by shape rather than by care. A test loads a malformed file into a live document and requires the caller's document untouched. 19 malformed cases plus non-finite tokens, a `1e400` overflow, duplicate IDs, a missing ID and an unknown future field that is rejected rather than ignored. |
| 15 | Can configuration or occurrence resolution change an assignment unexpectedly? | **No.** ADR-026 makes an assignment configuration-independent; switching none → Tall → Plain → Tall → none leaves the identity, the assignment and the designation unchanged. There is no per-occurrence assignment because `ComponentDefinition` has no material field, and a compile-fail case proves the field is absent rather than unused. |
| 16 | Can stale geometry produce a current mass result? | **No.** A 200 mm bore through a 120 mm cylinder makes the regeneration produce nothing, and the mass is refused with `'Bore' (hole, object:6) has no body` — not the previous answer. Separately, a configuration that overrides a parameter makes the mass refuse, because the carried regeneration defect would otherwise give the base configuration's volume. |
| 17 | Can assembly inertia omit transforms? | **N/A — there is no assembly inertia.** No aggregation exists in `include/` or `src/`. The *mechanism* is covered on a single body: a 90° rotation moves the axis moment from zz to yy and the parallel-axis shift produces Ixz = −0.735 kg m², both validated against independent closed form. |
| 18 | Can hollow geometry be treated as a bounding volume? | **It was — and a reference model caught it.** RM-MAT-03 found `mass-properties` reporting the un-bored blank (3392920 mm³, 26.46 kg) before the real tube (1884955 mm³, 14.70 kg). Fixed in P15-REFMOD-001 with `features::resultFeatures()`; mutation-verified regression test. The integration itself was always right — `validate` reported the correct single body — so this was a reporting defect, and the worse kind: a right number beside a wrong one. |
| 19 | Can the test harness mask a failure? | **No.** `qualify.cmd` exits with the *number* of failed stages, and `verify-harness.cmd` is its regression: pointed at a non-existent preset it must exit non-zero, and it does (2 stages, exit 2). Each process test asserts its own exit code; there is no pipeline, no `|| true`, no shell. |
| 20 | Can stale evidence or logs be mistaken for current qualification? | **Guarded, and it has bitten this phase.** P15-PROV-001, P15-PERSIST-001 and P15-CLI-001 each voided a passing or failing run and re-ran from clean; P15-CLI-001's void attempt is kept under `attempt-1-void/` *with its diagnosis* rather than deleted. Every log in this milestone's `qualification/` was written by the run whose times file sits beside it, and the harness records the candidate commit and the eight tree IDs in that same file. |
| 21 | Can a zero-test filter report PASS? | **No, twice.** The test presets set `noTestsAction: error`, so `ctest --preset X -R <no match>` exits 8 — measured, not assumed, and the inherited claim that a *bare* `ctest -R` does the same is false, which P15-CLI-001 corrected. On top of that, `qualify.cmd` counts the selected tests itself and records the number; a zero is a failed stage. |
| 22 | Can post-qualification file edits invalidate the evidence or tree equality? | **Avoided rather than argued.** The nine gates were committed and pushed *before* the qualification, so the harness's candidate is an already-pushed commit and its eight tree IDs are that commit's. Only `docs/` and `TODO.md` change afterwards, and neither is in the fingerprint — checkable: the only occurrences of `docs/` in the build system are two comments, and `TODO.md` appears in no build file. |

## Findings

### F1 — the no-fabrication search's single hit, classified

```text
src/features/material/Materials.cpp:71
    material.setDefinition(definition).value_or(false)
```

**Not a fabricated engineering value**: `false` is a *changed* flag, not a property. **Not a
swallowed error either**: `setMaterialDefinition` runs `validateMaterialDefinition` on the same
definition, with the same pure function, *before* `modifyObject` is called, so the inner call
cannot fail if the outer one passed.

But "unreachable" was my claim, and a claim is not a gate. So it is now one:
`P15Qual_ARejectedDefinitionIsReportedAndNeverASilentNoChange` requires an **error** — not
`false` — for `nu = 0.5` and for a negative density, requires the material to be unchanged in
both cases, and requires a genuine no-op to return `false` **without** an error. That last
assertion is the one that makes the distinction meaningful: if a rejection and a no-op both
reported `false`, no caller could tell them apart.

### F2 — my own audit misread its input, and that is worth recording

The first pass of the predecessor matrix reported `P15-ARCH-001` as absent from both TODO.md
and ROADMAP.md, and therefore `every row resolves: False`. The repository was right: the
milestone is in TODO.md under `# DONE — P15-ARCH-001`, and my matcher looked for the bare
heading.

Recorded because the failure mode matters more than the slip. **An audit that silently
mis-reads its own input is worth less than one that says where it slipped** — and a phase gate
that had accepted "row missing → BLOCKED" without looking would have blocked P15 over a regex.

### F3 — two things the brief asked for that cannot exist, verified in the headers

Not findings against the product, but against the assumption that a brief's fixture is always
constructible:

* **Two materials both NAMED "Steel"** (§9). Object names are unique across objects and
  parameters and re-checked on every rename. The gate uses the DESIGNATION, which is free text
  and legitimately duplicated, with different densities so a rebind changes the mass.
* **A supplied G inconsistent with E and nu** (§11). ADR-027 gives a supplied shear modulus no
  slot; that absence is a qualified invariant. The inconsistency the product *does* detect is
  an ultimate tensile strength below the yield strength, which reports `Invalid` and is never
  silently corrected.

Both are recorded in the evidence rather than approximated, and in both cases the substitute
fixture is *stronger* than the one asked for: a duplicate designation with differing densities
is a sharper rebind test than a duplicate name would have been, because it changes an
engineering answer.

## Production defects

```text
found in this milestone      0
found by this phase's own reference suite and fixed in it   1
  mass-properties listed consumed intermediate bodies as results (P15-REFMOD-001)
```

The one defect the phase found in itself was found by a *fixture*, not by a review — RM-MAT-03
was the first thing in the repository to combine a feature chain with a mass. Twelve qualified
milestones and 2764 tests had not.

## Conclusion

22 attacks, 3 findings, **0 new production defects**. One finding is a claim turned into a
gate, one is an audit of mine that mis-read its input, and one is two brief fixtures that the
architecture makes impossible — each replaced by a stronger one and recorded, not approximated.
