# P18-VALID-001 — scope audit

```text
IN:      one authoritative acceptance decision for a P18 thermal analysis, as a deterministic structured report built from the qualified evidence of every predecessor
OUT:     everything else
STARTED: nothing beyond this milestone — P18-VIZ-001, P18-CMD-001, P18-PERSIST-001, P18-CLI-001, P18-REFMOD-001, P18-QUAL-001 and any later P18 milestone are NOT started
```

## Audit

| Question | Answer | Evidence |
| --- | --- | --- |
| Does the validator solve, assemble, recover or balance? | **NO** — it judges what exists | ownership gate rule 2; `ValidationIsReadOnly` (the mesh stamp, the document revision, the system, solution, recovered fields and balance are unchanged by three validations) |
| Does it rerun the solver if a result exists? | NO | `validateThermalAnalysis` takes the derived state as views and has no solver call; compile-fail `THERE_IS_NO_SOLVE_AND_VALIDATE_IN_THE_VALIDATOR`, `THE_DERIVED_STATE_HAS_NO_SOLVER` |
| Does it duplicate load, boundary-condition, element, recovery or Robin formulas? | NO | the gate forbids the names of every one of them in the file |
| Does it recompute a mesh metric? | NO | no `evaluateMeshQuality` / `evaluateTetQuality`; only `summaries`, `direction`, `unitOf` |
| Does it change P16? | **NO** | no meshing file is touched (FREEZE.md) |
| Does it change any qualified thermal, linalg or structural file? | **NO** | the untouched list in FREEZE.md, compared by blob |
| Does it add a residual or balance tolerance? | NO | gate rule 3; `ResidualIsJudgedAgainst…`, `BalanceIsJudgedBy…` |
| Does it reject by a temperature range, a heat-flux magnitude or a margin? | NO | no physical range exists in the material model and no design requirement in the inputs; min/max T and peak |q| are INFORMATION in the metrics |
| Does it introduce thermal allowables or margins? | NO | out of scope; none exist |
| Does it persist anything or put the policy into canonical analysis state? | NO | the policy is application constants, not user intent; the report is derived and never persisted; compile-fail `THERE_IS_NO_VALIDATION_PERSISTENCE`, `THERE_IS_NO_POLICY_OBJECT_TO_EDIT` |
| Does it make a command, an undo step or a CLI verb? | NO | P18-CMD-001, P18-CLI-001 |
| Does it draw, colour or choose a component? | NO | P18-VIZ-001; the report's source has no display field (compile-fail) |
| Does it start the reference models or the P18 qualification? | NO | P18-REFMOD-001, P18-QUAL-001 |
| Does it depend on the GUI? | NO | `ThermalValidation.hpp` includes core, meshing and thermal headers only; layering 0 violations |
| Does it use a clock, a pointer, an unordered container, a thread or a random number? | NO | gate rule 5; source audit |

## Limitations recorded rather than hidden

* **Real P16 meshes are single-solid** (carried constraint 4), so multi-region/multi-solid acceptance is not claimed. The disconnected-component rules are shown on synthetic systems through
  the same function the model-level decision calls.
* **`h = 0` convection is not a valid condition in this version** — the canonical definition refuses it — so the validator's "h > 0" is the current policy, not an extra rule.
* **Prescribed temperatures are constant in time** in this version (P18-TRANSIENT-001), so the initial-versus-prescription override at `t = 0` is an information finding and not a
  failure.
* **A `FinalOnly` transient of more than one step cannot be fully accepted**: its intermediate states are not stored, so it cannot be balanced step by step (P18-BALANCE-001 refuses it).
  It can be SOLVE-accepted; the full analysis needs `EveryStep`.
* **The thermal mesh bounds are a gradient-recovery accuracy guarantee at a stated reference condition**, not a discretisation-error bound and not a conditioning bound. The reference
  gradient (14 K/m on a 307 K level) is conservative; a weaker gradient has a larger baseline error on every mesh, a perfect one included.
* **The dihedral angles carry no failure bound**, because no law was measured against them.
* **A balance PASS is not a solve PASS**, and neither proves the physics; accuracy against analytical solutions belongs to the reference models.
* **No sanitizer coverage** — this MinGW toolchain ships no ASan/UBSan (carried constraint 6).
