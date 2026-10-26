# P18-QUAL-001 - limitations and owner decisions

None of these is a wrong number. Each is a place where the implemented behaviour is narrower than a reader of the brief might assume, a guard is convention rather than mechanism, or a design
choice belongs to the owner. They are listed so P18 is qualified **with its limits written down** rather than discovered.

## Owner decisions

| id | the decision | today |
|---|---|---|
| D1 | the brief says "no numerical-equality resurrection: an edit, a stale result, an undo or a recreate with equal values - the source-generation policy decides". The code has two regimes (ADR-055): the validation report is keyed by the analysis revision and never revives; the derived chain (loads, boundary, system, solution, POST, BALANCE) is keyed by the content of its inputs and describes the document again when the content returns | after an undo or a typed-back value the chain is current and the report is stale; a fresh solve of that content gives the same bits (asserted). Results of one analysis are "current" for another with an equal definition on the same mesh and material. Pinned by `P18Qualification_AManualEditBackFollowsTheSameTwoRegimesAsUndo` and the earlier cases. To adopt the uniform rule, add the analysis id and revision to the chain's source records; the pinning tests would fail first. |
| D-D | the default `TransientSettings::output` is `FinalOnly`, and the balance refuses a multi-step `FinalOnly` history | a transient with default settings and more than one step can never be accepted; an accepted transient needs `EveryStep` |
| D-K | the transient balance starts from the resolved initial state | the heat implicitly injected at t = 0 by a prescription that differs from the initial temperature is not in the balance; reported as `InitialConditionOverriddenByPrescription` |
| D-C | `Viewer::displayOverlay(ThermalOverlay)` does not refuse `incompleteTargets > 0` | the overlay carries the count so a caller can refuse to present it as the whole target; there is no caller in the repository |
| D2 | `NamedBoundarySet` as a thermal target | loads and conditions take a `FaceName` only; the brief lists both |
| D7 | the JSON parser collapses a repeated key (last wins) for every object type; the data-model `validate` accepts `FaceName{}` | pinned by tests; changing either changes the whole format |
| time grid | `kTimeGridTolerance = 1e-9` snaps `t_end / dt` to an integer; `TimeGrid::integratedSpan()` is read nowhere outside the module | documented and tested, not disclosed to the user |

## Conventions that are not mechanisms

| id | what | recorded because |
|---|---|---|
| D3 / ADR 51.15 | a `ThermalModel` borrows `Mesher` storage and must not outlive a remesh or an edit | every production path rebuilds it on every call; no guard exists |
| D4 | a mesh's identity is its `MeshStamp` plus its node count | reachable only with hand-built meshes |
| D6 | the architecture gates are regex scans of text with `//` comments removed | a token after a string containing `//`, or inside `/* */`, escapes them; they are the sole enforcement of ADR-053 to ADR-057 ownership |
| D-I | `CheckThermalCliOwnership` rule 13 misses `+=`, raw-double accessor arithmetic and `std::accumulate`; a formula in a non-`Thermal*` CLI file is scanned by no CLI rule | the current CLI is clean |
| D9 | `ThermalValidationReport` is an aggregate with public mutable members | `status()` re-derives; `accepted()` ignores the report's currency by design |
| D10 | `kSymmetryTolerance` and `kEffectiveSymmetryTolerance` are two literals with one value; the solver defaults exist in three places | the defaults are pinned together; the symmetry pair is not nameable by a test |
| D11 | not gated: "thermal links no Eigen target", "no Qt or OCCT in the four viewer files", "thermal holds no second sparse kernel", `CheckTargetDependencies` rules 2 and 3 | hold by absence and review |
| C5.1 / C5.5 | a hidden anonymous member in a command class would pass every gate | the size-independence evidence is the payload test |

## Things the suite cannot reach

| id | what |
|---|---|
| QP15 / B6.10 | the `TargetProblem::WithoutFacets` branch of the chain is defensive: P16 never hands an empty set |
| B8.4 | no test would fail on a dense `Ndof x Ndof` temporary; the guarantee is the absence of any dense type in the API and of any `n*n` allocation in `src/thermal` |
| D3 (B) | the anchor classifier is row-relative; a Robin anchor weaker than 1e-10 of its row would be refused as a missing datum rather than solved |
| B10.8 | no absolute (W) per-step transient solver residual; only the two normalised ratios |
| C2.7 | no transient cancellation-trap test (the steady one is built on the real model) |
| A2a.4 | no test compares the faceted area of a curved face with the CAD area; Q follows the facets |
| D-L | TV_POST does not recompute `q = -k grad T`; the guard is the private constructor of `ThermalPostResult` plus source equality |
| D-J | `ThermalResult` / `ThermalResultSlot` have no production caller; about twenty tests assure a path the product does not take |
| C4.7b | staleness on screen is caller-driven; a thermal overlay presentation has no stale mark |
| C4.3 | the unit test compares silhouettes within 3 px; it fails on a missing or doubled conversion, not on a 1% error |
| C7.3 | a true stale geometry or mesh cannot reach a one-shot CLI process; the analogues are tested |
| D8 | one type for absolute temperature and for a temperature difference; the `>= 0 K` rule is runtime-only |
| D5 | ADR-049 (one translation unit), ADR-052 (two test names that do not exist), ADR-057 (17 rules and 73 fixtures; it is 21 and 102) are out of date; ADRs are not edited by a qualification |

## Scope, unchanged

Linear conduction on Tet4 with constant isotropic k, rho and cp; steady and backward-Euler transient; surface heat flux and volumetric generation; prescribed temperature and convection; one solid.
Radiation, phase change, contact resistance, anisotropy, temperature-dependent properties, nonlinear boundary conditions, CFD, thermo-mechanical coupling, moving sources, adaptive meshing and
higher-order elements are refused or absent, never approximated (ADR-049).

## From the second reviewer (`ADVERSARIAL_REVIEW.md`)

| id | what | note |
|---|---|---|
| F1 | **the balance gate refuses correct small-step transient runs** (a rounding term `eps |T| |C| / dt` is missing from the transient arithmetic scale) | the first item to fix when the owner allows the policy to change; reproduced at dt <= 0.003 s on a 1.6e-4 m^3 part and at dt <= 3 s on a 0.25 m^3 steel bar; the workaround is a larger step |
| F2, F3 | no bound on a temperature or on the user's residual tolerance | owner decisions |
| F4, F5 | the consistent mass undershoots a source-free transient by up to 6%; a one-free-node mesh is accepted with a result 15% off the closed form | known properties of the model, stated in ADR-049 and ADR-053 |
| F6 | `worstBalanceResidual` in the validation metrics is dimensionless | a payload rename is a CLI change |
| F7 | the policy fingerprint omits five acceptance-relevant constants | change together with F1, so the fingerprint moves once |
| F9 | the architecture checkers' bypasses (unmasked `//` stripping, name-only rule 4, narrow numeric regexes) | the real limit of the ownership gates |
