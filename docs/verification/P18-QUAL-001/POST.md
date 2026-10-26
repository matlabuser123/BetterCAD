# P18-QUAL-001 - result recovery (POST) audit (brief sections 42-44)

**Verdict: the temperature is the solver's vector verbatim; the gradient and heat flux are recovered per element; Fourier's sign is exercised on all three axes.**
Row-by-row: `audit/AUDIT_C_post_balance_validation_viz_commands_persistence_cli.md` section C1 (7 rows, 7 COVERED).

```text
T          the solver's vector, copied bit for bit (ThermalPostState.temperatures)
grad T     = G Te per tetrahedron, K/m, constant within a Tet4 (one value per element by construction)
q          = -k grad T, W/m^2, component by component (HeatFlux{} - k * gradient.x / .y / .z), |q| from the components
```

| property | tests |
|---|---|
| nodal T is the solver's vector, bit for bit; kelvin stays kelvin | `ThermalPost_NodalTemperatureRemainsKelvinAndFollowsTheSolver` (bit_cast equality), `ThermalPost_SteadySlabOnTheBlockFollowsFourierLaw` |
| cardinality equals `Ndof`; all values finite and absolute | `ThermalPost_RefusesWhatItCannotRecoverAndPublishesNothing` (wrong size 3, 5, 0; NaN, +-inf; -0.5 K refused, 0 K allowed), `ThermalPost_ALateStateBelowAbsoluteZeroFailsTheWholeHistory` |
| the gradient is exact for an affine field, independent of the recovery | `ThermalPost_XLinearFieldGradient` / `_Y` / `_Z`, `_AffineFieldGradientAndFourierFlux` (162 jittered tetrahedra), `_GeneralTetIndependentGradientOracle`, `_MultiTetAffinePatchUsesGenuinelyDifferentTetrahedra` |
| Fourier's sign on every axis, not magnitude only | `ThermalPost_FourierLawSign`, `_AffineFieldGradientAndFourierFlux` (`q . grad T = -k |grad T|^2`, `q x grad T = 0`, `dot(q, g) < 0`), `_HeatFluxMagnitudeIsEuclideanAndKeepsTheVector` (q = (3, 4, 12) gives 13, no negative zero); mutation QP08 (the z sign lost) killed by 28 tests |
| covariance: translation, rotation, geometry scaling, temperature offset | `ThermalPost_TranslationInvariant`, `_RotationCovariant`, `_GeometryScalingDividesTheGradientAndTheFlux`, `_TemperatureOffsetLeavesGradientAndFluxUnchanged` |
| sparse node and element ids are never array indices | `ThermalPost_NodalTemperatureUsesThermalDofOrderForSparseNodeIds`, `_ElementResultsAreKeyedByOrdinalAndNotByRawElementId` |
| the result is bound to the exact solution, mesh and material it was recovered from | `ThermalPost_RecoveryIsRefusedForEveryStaleInput` (load, prescribed temperature, convection, solver settings, conductivity - "old temperatures are not post-processed with the new k" - model older than a material edit, steady-vs-transient confusion), `_RemeshStalesTheResultEvenWhenCountsAndIdsAgree`, `_AFailedRecoveryNeverMakesAnOldResultCurrent`; mutation QP18 (a material edit not noticed) killed by `ThermalPost_RecoveryIsRefusedForEveryStaleInput` |
| every accepted field is finite; one invalid tetrahedron fails the whole recovery | `ThermalPost_RefusesNonFiniteDerivedValues`, `_OneInvalidTetrahedronFailsTheWholeRecoveryAndIsNeverSkipped`; the result type is built only through a private constructor after all gates |

## Notes recorded

* **D-L** the validator's TV_POST rule verifies finiteness, cardinality and bit-exact temperature equality; it does **not** re-derive `q = -k grad T`
  from the presented arrays. A finite, wrong `q` with matching provenance would pass TV_POST. The protection is the private constructor of
  `ThermalPostResult` plus source equality (`ThermalValidation_RejectsSameIdsNewMeshSource`), not a recomputation. Owner decision
  (`LIMITATIONS.md`): recomputing would make the validator a second recovery, which ADR-053 forbids.
* **D-J** `ThermalResult` / `ThermalResultSlot` (P18-DATA-001) have no production caller; their finiteness checks protect nothing at run time.
* The isotropic, uniform, temperature-independent conductivity is the only material model; no test claims otherwise.
