# P17-QUAL-001 — determinism

```text
SUBJECT:  whether the same document gives the same structural answer on
          every run, in every preset, and after a round trip through the file
METHOD:   the harness's repeat stages; the suite's in-process repetition over
          fresh documents; the final gates' fingerprint comparisons; and the
          code read for every source of non-determinism the brief names
```

## The repeat stages

```text
repeat release-ext   1138 tests x 5, back to back   exit 0, 100% passed, 1101.68 s
repeat debug-ext     1138 tests x 5, back to back   exit 0, 100% passed, 1232.44 s
```

The filter selects every structural, meshing, DOF, element, reference-model,
GUI and structural-CLI test (`run-qualification.cmd`), plus this
milestone's three final gates; `compile_fail` is excluded because each case
is a real compilation. The selected count, 1138, is the harness's own
measurement and reconciles as 1135 + 3.

## In process, over fresh documents

```text
StructuralReference_IsDeterministicOverRepeatedSolves
    five fresh documents each of RM-FEA-01, 02, 03, 05, 06 (placed) and
    07 (root) -- built, regenerated, meshed and solved from nothing shared --
    one fingerprint each, bit for bit; a changed pressure changes it
StructuralReferenceSuite_EveryModelSolvesOrIsRefusedAsDeclared
    all twelve documents give the same outcome, and the two refusals the
    same diagnostic text, over three attempts
StructuralReference_SaveLoadAndResolveReproducesEverySolutionBitForBit
    save, load, regenerate, mesh, solve: the same fingerprint, every
    solvable document
P17Qualification_AFailedRegenerationNeverYieldsACurrentResult
    RM-FEA-09 solved, broken, recovered at its original depth: the same
    fingerprint 0x665456e4bf19f4fe to the last bit
P17Qualification_EveryDependencyEditStalesTheResultInOneSequence
    the solve after an undo agrees with the solve before the undone edit
    bit for bit; a solver-settings edit gives identical numbers
StructuralSolve_IsDeterministicAcrossRepeatedSolves
StructuralPost_IsDeterministicAcrossRepeatedRecoveries
StructuralReaction_IsDeterministicAcrossRepeatedRecoveries
StructuralLoad_IsDeterministicOverRepeatedPreparation
StructuralSystem_IsDeterministicAndFollowsP16ElementOrder
MeshDofMap_NumberingIsIdenticalOverRepeatedBuildsOfTheSameMesh
StructuralDof_IsDeterministicAcrossARemeshOfTheSameReferenceModel
StructuralData_StateAndStaleReasonsAreDeterministic
StructuralValidation_OrdersFindingsDeterministically
StructuralCli_ARepeatedCommandGivesTheSameBytes
```

## Across presets

`CROSS_PRESET.md`: the runner's 164 non-timing lines, the suite's ten
fingerprints and 337 measurement lines, the three gates' fingerprints and
the three CLI payloads are identical in debug-ext, release-ext and
debug-shared-ext, and identical to P17-REFMOD-001's run of the same
production code.

## Why, read from the code

```text
no unordered container        grep unordered_map|unordered_set in
                              src/structural + include/bettercad/structural:
                              0 uses (2 comments explaining the choice)
no wall clock, no randomness  grep std::chrono|time(nullptr)|rand(|random: 0
element order                 P16's ascending ElementId, asserted
node ordinal                  the mesh's own enumeration, copied not sorted
sparse pattern                sorted and uniqued per row; each (row, col)
                              once, so setFromTriplets has nothing to reduce
solver                        SimplicialLDLT, single-threaded; OpenMP is not
                              enabled anywhere in cmake/ (grep: 0)
findings                      ordered by severity, code, element (stable_sort)
extrema                       ascending NodeId / ElementId tie-break, stated
payloads                      no timestamp, path, address or duration
```

## Verdict

```text
determinism   PASS -- 1138 x 5 in two presets, bit-exact fingerprints over
              fresh documents and across presets, and no source of
              non-determinism in the module
```
