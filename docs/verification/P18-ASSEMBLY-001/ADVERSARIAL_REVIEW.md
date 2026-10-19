# P18-ASSEMBLY-001 — adversarial review

```text
SUBJECT:  the brief's 35 attack questions and six more of the reviewer's, each answered
          with a test, a probe or an audit
RESULT:   0 production defects; 5 test gaps (found by mutation, closed); 0 gate-blocking
```

Answers say **where the claim is enforced**; "probe" ids are in MUTATIONS.md.

## The brief's questions

| # | Attack | Answer | Evidence |
| --- | --- | --- | --- |
| 1 | Can `NodeId.value()` become a global equation index? | No. Every row is a `ThermalDofIndex` from `ThermalDofMap::elementDofs` or a facet's bound unknowns. The only `.value()` calls in `ThermalSystem.cpp` are on `ThermalDofIndex` (audit below) | probe A09 killed; sparse-id tests; compile-fail A_NODE_ID_IS_NOT_A_ROW |
| 2 | Can max `NodeId` determine the dimension? | No. `rows = map.dofCount()`. A mesh with max handle 4 294 967 294 and four nodes gives 4 x 4 | `ThermalAssembly_LargeSparseNodeIdDoesNotControlMatrixSize`, `_SparseNodeIds`; probe B11 killed (53 of 62 failed) |
| 3 | Can `ElementId` become a matrix row? | No. It is recorded in `elementOrder()` and in messages only | compile-fail AN_ELEMENT_ID_IS_NOT_A_ROW; audit |
| 4 | Can local DOFs be sorted without permuting local matrices? | No. Local rows are used in the element's own order and never sorted. An even permutation of a tetrahedron's corners gives the same global matrices | `_LocalOrderIsPreservedUnderEvenPermutation`; probes A08 (columns reversed), A07 |
| 5 | Can duplicate sparse entries overwrite rather than sum? | No: one slot per `(row, column)`, `+=` | `_TwoTetDuplicateContributionsSum` (K 2.5, C 6, Q 3); probes A01, A02, A04 killed |
| 6 | Can shared-node Q contributions overwrite? | No | Q(1) = 1 W + 2 W = 3 W; probe A04 |
| 7 | Can `Ke` be added to C by mistake? | No | probe B01 (12 failed) |
| 8 | Can `Ce` be added to K by mistake? | No | probes B02, B09 (12 failed each) |
| 9 | Can `Qconv` be omitted? | No | probe A05; `_ConvectionSourceScattersToCorrectDofs` |
| 10 | Can `Kconv` be omitted? | No | probe A06; `_ConvectionMatrixScattersToCorrectDofs` |
| 11 | Can `Kconv` be added twice? | Not by the scatter. Two *conditions* on one face are two exchanges and do add twice, deliberately | probes B12, B13; `_RepeatedConvectionAddsTwice` |
| 12 | Can the prepared combined `Q` and the per-load `Q` both be added? | No. Only `heatInput()` is read; the per-load contributions are attribution | probe B05 killed; `_BlockHeatResultantsPreserved` asserts the total is below `1.5 x` the expectation |
| 13 | Can one load category be silently dropped? | No | block flux-only, generation-only, loads-only and convection-only assemblies each match; probe A03 |
| 14 | Can a prescribed-temperature penalty be added here? | No. The same case with and without a prescribed temperature assembles to **bit-identical** `K`, `C`, `Q` and source | `_PrescribedTemperaturesNeverEnterTheMatrices`; probe B06 killed (25 of 62 failed) |
| 15 | Can epsilon regularisation be injected? | No | probe A45 (`1e-12` on the diagonal) killed |
| 16 | Can the constant null mode disappear? | No: `K 1 = 2.2e-16` against `max|K| = 6.1` | `_BlockNullModeAndCapacityConservation`, `_PureConductionPreservesConstantNullMode`; probes A45, B06 |
| 17 | Can K/C be post-symmetrised to hide a scatter bug? | No such step exists (audit). A scatter bug that breaks symmetry is refused by the gate (A13) and one that does not is caught by the oracle (C01) | audit; A13, C01 |
| 18 | Can the sparse symmetry check miss a missing transpose? | No: an absent transpose counts as zero | `_SparseSymmetryCheckSeesAMissingTranspose`; probe A51 killed |
| 19 | Can global dense matrices be allocated in production? | No. No `n * n` allocation, no `toDense` (audit). The 22^3 grid would need 113 379 904 doubles dense | audit; `SPARSE_SMOKE.md` |
| 20 | Can one source generation be mixed with another if dimensions match? | No. Compatibility is by value of the source (stamp, count, geometry revision, loads, conditions, material). A foreign numbering with identical counts and ids is refused | `_RejectsMismatchedDofMeshSource`; probes A17 to A27, A33 to A35 |
| 21 | Can a remesh with the same ids keep the system current? | No: 9 nodes, 12 tets and 12 boundary facets in both generations, every id identical, different stamp: stale | `_RemeshStalesAssembledSystem`, `_SameIdsNewMeshIsStale` |
| 22 | Can old prepared loads/BCs assemble after remesh? | No: refused as `dof_map_mismatch` (the first check); independently, stale prepared loads or boundary are refused by their own sources | remesh test; `_RejectsStalePreparedLoad`, `_RejectsStalePreparedBC` |
| 23 | Can stale material values assemble? | No: the material is resolved afresh on every call and `k` is compared with the model's | `_RejectsStaleMaterialSource`; probes A36, A24, A30 |
| 24 | Can steady assembly wrongly require rho/cp? | No: a `k`-only material assembles steady | `_SteadyKOnlyMaterialSucceeds` |
| 25 | Can transient assembly silently set C = 0 if rho/cp are missing? | No: `material_unavailable`, no system | `_TransientRequiresCapacityProperties`; probes B08, A37 |
| 26 | Can time step leak into base C? | No: `dt` is not an input and `C / dt` is not formed; a time-step edit does not stale the system | `_EditsStaleExactlyWhatTheyChange`; compile-fail THE_SOURCE_HAS_NO_TIME_STEP; probe B09 |
| 27 | Can solver tolerances alter K/C/Q? | No: the model-level entry reads only mode, loads and conditions | `_EditsStaleExactlyWhatTheyChange`, `_DefinitionOverloadReadsOnlyModeLoadsAndConditions` |
| 28 | Can assembly recompute q''/q'''/Robin formulas and diverge from upstream? | No: it reads `heatInput()` and the facet's bound `term`. The audit finds no `/ 3`, `/ 4`, `/ 12`, `/ 20` or area product in the file; a mutant that recomputed a Robin entry differently (B10) is killed | audit; B10, A07 |
| 29 | Can boundary internal faces receive exterior convection accidentally? | That is **P18-BC-001's** question, answered there (its facets are P16 boundary facets resolved from a `FaceName`). The seam here takes facets as data and does not re-decide. On the real block the end-cap convection contributes exactly 2 facets | block test: `convectionFacetCount() == 2`; limitation noted below |
| 30 | Can a NaN local contribution propagate into the global matrix? | No. The element kernel refuses a non-finite result (P18-ELEM), and the matrix and the heat vector refuse a stored NaN or infinity | `_AllGlobalEntriesFinite`, `_HeatVectorRefusesNonFinite`, `_SparseMatrixLayoutIsValidated` |
| 31 | Can overflow create Inf but assembly still succeed? | No: three huge convection copies each representable, the sum overflows, `non_finite_system`; probe A47 shows the classification matters | `_SeamRefusals` (overflow section) |
| 32 | Can one failed contribution leave a partially accepted system? | No. The result is `std::expected<ThermalSystem, AssemblyFailure>`; nothing is published on failure, and the inputs are not mutated | every refusal test; `_AssemblyDoesNotMutateItsInputs` |
| 33 | Can deterministic order depend on unordered maps? | No unordered container exists in either file (audit); order is the mesh's ascending `ElementId`, the condition order, and each condition's facet order | audit; 30 and 50 repeat tests, bit fingerprints |
| 34 | Can the large-mesh smoke secretly use dense storage? | No: `nnz / Ndof^2 = 1.31e-3` at 10 648 unknowns; the dense form (907 MB of doubles) is never formed | `SPARSE_SMOKE.md` |
| 35 | Can zero targeted tests still report PASS? | No: the zero-match guard asserts a minimum per group and a filter known to match nothing | `ZeroMatchGuard.cmake`, 15 filters for this milestone |

## The reviewer's additional questions

| # | Attack | Answer |
| --- | --- | --- |
| R1 | Is the system valid for a `Result` that could be empty, dereferenced unchecked? | In the numeric pass `map.elementDofs(tet)` is dereferenced without a check. It is safe by construction (the symbolic pass called the same pure function on every element and returned on the first refusal). A comment now says so; a runtime check would be a dead branch, which an earlier milestone's lesson rules out. **No defect.** |
| R2 | Can a `ThermalSystem` outlive its mesh and be misread? | It holds no pointer or reference: values only. The remesh test captures the old system by value, remeshes, and uses it against the new mesh to show it is refused |
| R3 | Does a copied or moved system compare equal? | `operator==` is defaulted over every array and the source; the determinism test compares 30 and 50 assemblies bitwise |
| R4 | Is the relative symmetry gate blind when `max|K|` is huge? | The bound is `1e-13 x max|K|`, so an asymmetry of that relative size is accepted; a correct scatter is exactly symmetric (0.00e+00 in every test), so the gate sits ten orders above the observed value and far below any defect |
| R5 | Does `thermalAssemblyFailure` cost a full assembly? | Yes: it runs the same function and discards the system. It exists for diagnostics and tests, not a hot path. Recorded as a limitation |
| R6 | What if the document changes between preparing and assembling? | Material is resolved at assembly. Loads and conditions are compared with their prepared sources; a document edit that changes a canonical load is refused as `load_source_mismatch` |

## Audits

```text
unordered containers in either file         none (the two mentions are comments)
formula constants in code                   none (the only division-like text is total() summing)
penalty / epsilon / pin / threshold / prune none; the only tolerance is kSymmetryTolerance = 1.0e-13, a gate
.value() on a NodeId in ThermalSystem.cpp   none; every one is a ThermalDofIndex
n * n allocation or toDense in production   none
linear-algebra library linked or included   none (Eigen appears only in comments)
```

## Findings

| # | Finding | Class | Resolution |
| --- | --- | --- | --- |
| F1 | five survivors on the first mutation pass (A10, A19, A21, A31, A32) | test gaps | four tests added; all five now killed |
| F2 | `inconsistent_system` is reachable only through a defective scatter | design note | covered by A13 and `fromCsr` direct tests; stated in the header |
| F3 | the seam trusts convection facets as data | scope | P18-BC-001 owns interior/exterior; stated in SCOPE.md |
| F4 | `thermalAssemblyFailure` runs a full assembly | limitation | documented |
| F5 | unchecked `elementDofs` dereference in the numeric pass | reviewer note | safe by construction; comment added, no code change |

No production defect was found in this milestone, and the shared-library preset
(`debug-shared-ext`) was built **early**, before the mutation work, because the previous
milestone's only production defect was a missing export annotation: the new tests linked
and passed there on the first build.