# P18-QUAL-001 - ADR audit (brief sections 9-12)

Nine ADRs carry P18: ADR-049 through ADR-057. The clause-by-clause table - **127 normative clauses**, each with its code evidence, the exact names of its covering tests and gates, a status
(IMPLEMENTED / DRIFT / VIOLATED) and a coverage verdict (COVERED / PARTIAL / GAP) - is `audit/AUDIT_A_adr_authority_currentness_dof.md`, section A1. This page records what it found.

## What the ADRs are (the brief's one-line labels were approximate)

| ADR | decides | clauses |
|---|---|---:|
| 049 | the **scope** of the first thermal solver: linear conduction, constant isotropic k (and rho, cp), Tet4, one scalar T per node, steady and backward-Euler transient, two load kinds, two boundary-condition kinds, a long refusal list. The one-solid limit is one constraint inside it. | 20 |
| 050 | the thermal module at **layer 45**, below structural (50), so a thermo-mechanical coupling can flow structural -> thermal and never the reverse | 11 |
| 051 | **input validated once**: `requireThermalModel` is the only boundary; possession of a `ThermalModel` is the proof | 15 |
| 052 | the generic symmetric sparse solve **moves to `linalg`** (layer 5), the decision ADR-050 left open | 12 |
| 053 | **thermal acceptance in one place**; P16 only measures | 14 |
| 054 | thermal visualisation is a **read-only** view of derived data | 16 |
| 055 | commands edit **one analysis definition**; history holds intent only | 15 |
| 056 | persistence is **one more object type**; the file holds intent only | 14 |
| 057 | **one production entry** `runThermalAnalysis`; the CLI is an adapter | 10 |

## Result

| | count |
|---|---:|
| clauses | 127 |
| IMPLEMENTED | 122 |
| DRIFT (a clause the implementation or the text no longer matches) | 5 |
| VIOLATED | **0** |
| coverage COVERED | 114 |
| PARTIAL | 7 |
| GAP | 3 |
| not applicable | 3 |

**The five drifts, none a defect in the code.** 49.13 (the ADR leaves a boundary Celsius conversion open; the implementation has none and the gates forbid one); 49.20 (the verification
paragraph says `src/thermal` holds one translation unit - true at P18-ARCH-001, false now: 20); 51.15 (a `ThermalModel` "must not outlive an edit" is a convention); 52.12 (the ADR names two tests,
`architecture.targets.thermal-link-graph` and `.linalg-link-graph`, that do not exist: the real names are `architecture.targets.link-graph`, `.valid`, `.thermal-links-structural`); 57.7 (the
ADR says the CLI gate has 17 rules and 73 fixtures; it has 21 rules and 102). The text of ADR-049, -052 and -057 should be updated in a documentation change; the ADRs are not edited by a qualification.

**The three GAPs.** 50.5 (structural MAY include thermal - the reason layer 45 exists - had no fixture): closed; the `valid` architecture tree now includes a thermal header from the structural layer and
`architecture.checker.valid` passes. 51.2 (`ThermalModel` is not default-constructible, copyable or buildable from its parts): closed by
`P18Qualification_PossessionOfAThermalModelIsTheOnlyProof`, six `static_assert`s. 51.15 (a model must not outlive a remesh or an edit): not closed - it is a lifetime convention, every production path
rebuilds the model on every call (`ThermalAnalysisRun.cpp`, `ThermalValidation.cpp`, `ThermalInspection.cpp`), and a guard would be a new API; recorded as **D3** in `LIMITATIONS.md`.

**The seven PARTIALs.** 49.14 (no runtime test that a temperature-dependent or anisotropic material is refused: none is representable, so none can be offered), 50.8 / 50.9 / 52.11 (the "thermal links no Eigen"
and "no second sparse kernel" rules hold by absence and review, not by a gate), 52.4 (the P17 wrapper is mechanical; the P17 suite is outside this audit), 52.7 (`CheckTargetDependencies` rules 2 and 3
have no negative fixture), 54.1 (nothing gates "no Qt or OCCT in the four thermal view files"; the search found none). All recorded in `LIMITATIONS.md`.

## Decisions the ADRs left to the owner, still open and still pinned by tests

* the JSON parser collapses a repeated key (last wins) for every object type - `ThermalPersistence_ARepeatedKeyInTheFileIsLastWinsAsEverywhereInTheFormat`;
* the data-model `validate` accepts `FaceName{}` while the commands and the file refuse it;
* a `final_only` transient of more than one step cannot be fully accepted - `ThermalValidation_AFinalOnlyHistoryOfSeveralStepsCannotBeFullyAccepted`;
* the two currentness regimes (ADR-055) - `CURRENTNESS.md`.
