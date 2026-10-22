# P18-VALID-001 — well-posedness, steady and transient

```text
RULE:     TV_WELL_POSED (V09), after TV_SOURCE_COMPAT
SUBJECT:  the SAME conduction network is a singular problem in steady state and a valid one in time; the rule is chosen by the MODE and the two are never confused
```

## The two rules

| Mode | Rule | Decided by | Not required |
| --- | --- | --- | --- |
| steady | every independent region of the reduced conduction network is anchored to an absolute temperature: a prescribed temperature OR a positive convection coefficient | `analyseAnchoring(reduceThermalSystem(K, Q, free numbering, constraints))` — P18-SOLVE-001's own function | a Dirichlet condition specifically |
| transient | a capacity matrix that is present and finite and strictly positive on every diagonal entry | `system.capacity()` and its diagonal | any datum; connectivity |

A steady conduction matrix has the constant temperature in its null space unless something anchors it; the constant mode is removed by a prescribed temperature or by
`h A` in a row. A transient adds `C / dt` to the diagonal, which removes the null mode: an insulated body with a positive capacity and a valid initial temperature
is well posed, and the steady datum rule applied to it would reject a valid problem.

## The tables the brief asks for

### Steady (`TV_WELL_POSED`)

| Case | Dirichlet | Robin `h > 0` | Expected | Result | Test |
| --- | --- | --- | --- | --- | --- |
| pure Neumann | no | no | FAIL | **FAIL** `no_temperature_datum` | `SteadyPureNeumannFails` (hand network), `SteadyPureNeumannModelFailsEndToEnd` (real block) |
| Dirichlet anchored | yes | any | PASS | PASS | `SteadyDirichletAnchoredPasses`, `SteadyDirichletOnlyPasses` |
| Robin anchored | **no** | yes | PASS | PASS | `SteadyRobinOnlyPasses`, `SteadyRobinOnlyModelPassesEndToEnd` |
| a "convection" of rounding size (3e-16) | no | rounding | FAIL | **FAIL** `no_temperature_datum` (the anchor tolerance, `kAnchorTolerance`, separates rounding from anchoring) | `SteadyRobinOnlyPasses` |
| fully constrained (`Nfree = 0`) | all | any | PASS | PASS (nothing to anchor; `T = Tc`) | `FullyConstrainedSteadyPasses`, `FullyPrescribedSteadyModelPassesOnARealMesh` |
| disconnected, one region unanchored | one | — | FAIL | **FAIL** `unanchored_region` (another region IS anchored) | `DisconnectedUnanchoredSteadyFails` |
| disconnected, every region anchored | one per region | — | PASS | PASS | `DisconnectedAllAnchoredSteadyPasses` |

* **Robin-only is valid.** A model with no prescribed temperature at all but a positive convection coefficient is not rejected for having no Dirichlet condition: the rule is
  "anchored", not "has a Dirichlet" (mutation-checked; the end-to-end Robin-only analysis is accepted with `constrained = 0`).
* **`Nfree = 0` is a success.** All unknowns prescribed means there is no free equation to anchor and no residual to fail; the free residual is empty and accepted
  trivially (`FullyConstrainedSteadyResidualIsNotAFailure`). Treating it as a failure (mutation VAL31) is killed.
* **Disconnected components.** Each independent region is judged; a disconnected body is not rejected for being disconnected, only for an unanchored region. Real P16
  meshes are single-solid (carried constraint 4: P16 refuses a body with more than one solid), so the disconnected cases are shown on synthetic systems through the same
  seam, and that limitation is recorded in the README.
* **The validator and the solver agree on what is singular.** `TheValidatorAndTheSolverAgreeOnWhatIsSingular` runs the real block with and without an anchor: where the
  qualified solve refuses for the lack of a datum, `TV_WELL_POSED` names the same cause; where the solve succeeds, the rule passes.

### Transient (`TV_WELL_POSED`)

| Case | `C` valid | Initial `T` | Expected | Result | Test |
| --- | --- | --- | --- | --- | --- |
| pure Neumann (no condition at all) | yes | yes | PASS | PASS, information `transient_without_datum` | `TransientPureNeumannPasses`, `TransientPureNeumannModelPassesEndToEnd` |
| disconnected components, positive capacity | yes | yes | PASS | PASS (no unanchored-region rule in a transient) | `TransientDisconnectedComponentsWithCapacityPass` |
| no capacity matrix | no | — | FAIL | **FAIL** `capacity_missing` | `TransientMissingCapacityFails` |
| a zero or negative capacity on the diagonal | no | — | FAIL | **FAIL** `capacity_invalid` | `TransientCapacityMustBeFiniteAndPositiveOnTheDiagonal` |
| `rho` or `cp` missing from the material | no | — | FAIL | **FAIL** at `TV_MATERIAL`, `material_incomplete_for_mode`, before any matrix exists | `TransientRequiresConductivityDensitySpecificHeat` |
| invalid initial temperature | yes | no | FAIL | **FAIL** `initial_temperature_invalid` at `TV_TRANSIENT_SETUP` | `RejectsInvalidInitialTemperature` |
| invalid time settings, or a step count past the resource guard | yes | yes | FAIL | **FAIL** `time_settings_invalid` | `RejectsInvalidTimeSettings`, `RejectsTimeSettingsPastTheResourceGuard` |

* **The same system fails the steady rule and passes the transient one.** `TransientPureNeumannPasses` checks both on one matrix (`NoTemperatureDatum` from the steady rule,
  `TransientWithoutDatum` from the transient rule) — which is exactly why they must not be confused. `TransientPureNeumannModelPassesEndToEnd` ends with the steady analysis of
  the same model being refused, on the real block.
* **No zero-capacity fallback.** A node that stores nothing is refused; it is not given the capacity of its neighbour or an epsilon.
* **A transient analysis does not need a datum, a Dirichlet or a convection.** The information finding says why the body is well posed, so the report is not silent about an
  insulated body.

## Order

`TV_WELL_POSED` runs after `TV_SOURCE_COMPAT`: the well-posedness of a stale system is meaningless, so a source-compatibility failure blocks it, and a well-posedness
failure blocks the Solve rules (a solve that "succeeded" on a pure-Neumann model would be an arbitrary level, and the rules after it are not judged on it). The rule is
decided BEFORE a solve exists, so a pre-solve readiness question ("is this model solvable?") has an answer: `inputsAccepted()` plus this rule.
