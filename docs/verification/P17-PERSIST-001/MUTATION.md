# P17-PERSIST-001 — the mutation probes

```text
15 probe runs over 14 distinct mutations.
14 KILLED, 0 survived, 1 void result rerun and then killed.
Plus 2 REACHABILITY measurements, for two mutations the brief asked for that
turned out to be IMPOSSIBLE TO WRITE.

PROBES RAN BEFORE THE QUALIFICATION, not after it. A surviving probe would
have stopped the pre-freeze gates by design rather than being discovered
inside a three-hour run.
```

## The batch

Filtered across **both** persistence suites — `StructuralPersistence` and P16's
`MeshingPersistence`, 64 tests — because the serializer shares `ObjectJson.hpp`
and the dispatch with P16's, and a mutation that broke the shared path should
show there too.

| Probe | Mutation | Verdict |
| --- | --- | --- |
| P1 | the analysis object's `revision()` written into the file | KILLED (28) |
| P2 | `relative_residual_tolerance` not written | KILLED (28) |
| P3 | the pressure divided by 1e6 on the way out (Pa → MPa) | KILLED (9) |
| P4 | the pressure round-tripped through `float` | KILLED (1) |
| P5 | the written load order reversed | KILLED (7) |
| P6 | a duplicate `LoadId` accepted instead of refused | KILLED (1) |
| P7 | `LoadId` reallocated as `*id + 1` on load | KILLED (11) |
| P8 | the restraint mask defaulted to `fixed()` | KILLED (27) |
| P9 | the traction's y component written as its absolute value | KILLED (2) |
| P10 | `validate(StructuralLoad)` not called | KILLED (1) |
| P11 | `validate(StructuralRestraint)` not called | KILLED (1) |
| P12 | the `NodeId` and generation written instead of refusing the save | KILLED (1) |
| P13 | the reader accepting a `nodal_force` key | KILLED (1) |
| P14 | the per-kind key check dropped | **VOID — did not compile** |
| P14b | the same, with the orphaned parameters `(void)`-cast | KILLED (1) |

## P14 — a compiler kill is not a mutation kill

P14 removes the loop inside `refuseExtras`, which orphans both of its
parameters:

```text
error: unused parameter 'keys'      [-Werror=unused-parameter]
error: unused parameter 'kindName'  [-Werror=unused-parameter]
```

**That is a VOID result, not a kill.** The probe never ran, so it says nothing
about the tests — and recording it as a kill would have been the quiet kind of
wrong this instrument exists to avoid. The remedy is the one that worked for
P17-SOLVE-001's four von Mises probes: `(void)`-cast what the mutation orphans
and rerun. P14b then died to
`StructuralPersistence_RejectsMalformedStructuralIntent`, whose three per-kind
cases (a pressure carrying a traction, a gravity load carrying a face, and the
key union `requireObject` cannot narrow) are exactly what that check is for.

This is the second milestone running where `-Werror` turned a one-token
mutation into a void probe. The lesson is standing and was applied twice more
*before* the run: P5 as first written used `std::ranges::sort` and P9 used
`std::abs`, neither of whose headers the serializer includes, so both would
have been void. Both were rewritten to need no new include — P5 reverses the
iteration, P9 uses a conditional negation.

## P1 — the byte gate has teeth, measured

The brief's §206 asks for a demonstration that the solve/no-solve byte-identity
test fails if a generated field is added to persistence. P1 is that
demonstration, in the only form the architecture permits (see below): it writes
the analysis object's `revision()` — a single non-canonical, edit-dependent
field — and **28 of the 64 tests died**, including
`SaveLoadSaveIsByteIdentical`, `TheCommandHistoryDoesNotReachTheFile` and
`SerializationIsByteIdenticalAndIndependentOfEditOrder`.

So the determinism and byte-identity tests are not decorative: a single extra
field in the written object breaks them loudly.

## The two mutations that could not be written

The brief's §131–133 ask for `lastResidual`, `maxVonMises` and a displacement
count to be serialized, and for the before/after-solve comparison to fail. **No
such mutation exists**, and that was measured rather than asserted.

**R-A — attempting to write a result field.** A line writing
`structural::StructuralResult{}.maxVonMises()` into the analysis object:

```text
error: 'StructuralResult' is not a member of 'bettercad::structural';
       did you mean 'StructuralRestraint'?
```

The result type is not reachable from the serializer's translation unit at all.

**R-B — what *is* nameable from it.** Measured by naming four types in turn and
seeing which the compiler accepts:

```text
Document                  nameable   (via DocumentObject.hpp)
meshing::GeometryMeshMap  nameable   (via StructuralLoad.hpp)
structural::StructuralResult      NOT nameable
structural::GlobalStructuralSystem NOT nameable
```

So the claim is stated precisely rather than generously. The result and the
assembled system are unreachable by name. `Document` and `GeometryMeshMap`
*are* nameable — but the reader

```cpp
structuralAnalysisFromJson(const Json& data, std::string name, std::string_view path)
```

holds **no instance of either**, and the writer

```cpp
structuralAnalysisToJson(const structural::StructuralAnalysis& analysis)
```

receives only the document object, which holds no result. Writing solve state,
or rebinding a reference, would require changing a signature and every caller.
That is why §131–133's and §139's mutations are recorded here as unreachable
rather than as probes that passed — a probe that cannot be written is not
evidence that the code is safe, so the *reason* it cannot be written is what
was recorded.

## P10 and P11 — the validator asymmetry, confirmed from the other side

Each died to exactly one test, which is the smallest kill in the batch and
exactly the expected shape:

```text
P10  validate(StructuralLoad) removed
     -> RejectsMalformedStructuralIntent (the invalid-LoadId case)

P11  validate(StructuralRestraint) removed
     -> RejectsMalformedStructuralIntent (the empty-mask case)
```

Both are **load-bearing**, and that confirms from the reader's side what
P17-CMD-001's probe C11 measured from the command side:

```text
validate(StructuralAnalysisDefinition) checks
    the mesh handle        -> StructuralAnalysis::create catches a bad mesh
    validate(solver)       -> and bad settings
but NOT the loads          -> so P10 is LOAD-BEARING and dies
but NOT the restraints     -> so P11 is LOAD-BEARING and dies
```

Had either survived, the right conclusion would have been that
`validate(definition)` covers it and the explicit call is redundant. Neither
did, which is why the reader calls both.

## P5 found a defect in a test, not in the product

Reversing the written load order killed seven tests — and one of them,
`EngineeringValuesSurviveWithoutAUnitConversion`, **SEGFAULTED** rather than
failing:

```cpp
CHECK(after.loads[0].pressure()->magnitude.si() == 2.5e6);
```

With the order reversed, `loads[0]` is the gravity load, `pressure()` returns
`nullptr`, and the dereference crashes. A crash still counts as a kill, but it
takes the whole ctest process with it and tells a later reader nothing — and
under `--repeat` it would lose the rest of the run.

Fixed by requiring each payload by kind before dereferencing it, in that test
and in `ExtremeAndAwkwardFiniteValuesSurvive`, the only other place that
dereferenced unguarded. The suite was rebuilt and re-run green afterwards
(64/64). Every other payload dereference in the file was already preceded by a
`REQUIRE`.

**This is what probes are for.** The mutation was killed either way; the value
was in *how* it was killed.

## Why no equivalent mutants

P17-CMD-001 kept one documented equivalent mutant (its C11). This batch has
none: every mutation that compiled changed observable behaviour and died. That
is a consequence of the serializer having no redundant checks — each validator
call, each duplicate check and each per-kind key check is the only thing
standing behind its own rule, which is also why the smallest kills are single
tests rather than none.

## The protocol, and what it refused

```text
pristine copy hash-verified      8f236003e7b03ab9e68727ffedf0e76e631c9261
                                 before the run, before every probe, and
                                 after the restore
every anchor dry-run first       14/14 unique, before anything was mutated
a red baseline refuses the run   checked: 100% of 64 before the first probe
a mutation that changes nothing  refused as VOID
a mutation that does not compile refused as VOID -- and P14 hit it
the run ends with a restore
  BUILD, not just a restore      a restored source with a mutant binary is
                                 the trap that makes the next failure look
                                 like your fix being broken
the baseline re-run afterwards   100% of 64 again
```

Full logs: `logs/probe-persist.log`, `logs/probe-p14b.log`,
`logs/probe-verdicts.txt`, and `logs/reachability-measurements.txt` for R-A and
R-B with the exact inserted lines and the exact diagnostics.
