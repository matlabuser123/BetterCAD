# P18-CLI-001 — the commands, and the core API each one calls

```text
executable        bettercad-cli      (target bettercad_cli;  <build>/bin/bettercad-cli.exe)
shape             bettercad-cli <command> <file.bcad> [arguments]      the document is always the first argument
naming            thermal-<noun> for a report, thermal-<noun>-add / -remove for an edit   (structural-*, mesh-* and material-* are the siblings)
structured        --json on a report                                    the project's JsonValue; a quantity is {"value", "unit"}
units             a bare number is SI; a unit is accepted through the one catalogue:  500W/m^2   1e5W/m^3   25W/(m^2 K)   350K   0.3s
                  there is NO Celsius: the catalogue has none, and a CLI-only conversion would be a thermal-only parser
```

## The inventory (the brief's §217)

| The brief's action | Command | Document-changing? |
| --- | --- | --- |
| inspect | `thermal-settings <file.bcad> [--json]` | no |
| create the analysis | `thermal-analysis-add <file.bcad> [--name <name>] [--mode steady\|transient] [--initial-temperature <K>] [--time-step <s>] [--end-time <s>] [--output final-only\|every-step]` | yes |
| add a surface heat flux | `thermal-load-add <file.bcad> <id> --flux <W/m2> --face <face>` | yes |
| add a volumetric generation | `thermal-load-add <file.bcad> <id> --generation <W/m3>` | yes |
| remove a load | `thermal-load-remove <file.bcad> <id>` | yes |
| add a prescribed temperature | `thermal-boundary-add <file.bcad> <id> <face> --temperature <K>` | yes |
| add convection | `thermal-boundary-add <file.bcad> <id> <face> --convection <W/(m2 K)> --ambient <K>` | yes |
| remove a boundary condition | `thermal-boundary-remove <file.bcad> <id>` | yes |
| steady / transient solve | `thermal-solve <file.bcad> [--json]` (the analysis' own mode decides) | no |
| temperature summary | `thermal-temperature <file.bcad> [--json]` | no |
| gradient summary | `thermal-gradient <file.bcad> [--json]` | no |
| heat-flux summary | `thermal-heat-flux <file.bcad> [--json]` | no |
| heat-balance report | `thermal-balance <file.bcad> [--json]` | no |
| validation report | `thermal-validate <file.bcad> [--json]` | no |

A face is `face:<feature>:<role>[:<entity>]` — `face:Extrude001:end_cap`, `face:Extrude001:side:5` — the grammar drawings, mates, local mesh sizing and structural loads already use. **There is no option that could carry a node, a facet, an element or a degree of freedom.** `--node`, `--facet`, `--element`, `--tet` and `--dof` each fail as an unknown option (exit 2), which is how their absence is *proved*.

Ids are the caller's, a positive integer, stable across save and load. A load and a boundary condition each have their own id space (`ThermalLoadId`, `ThermalBoundaryConditionId`), and neither is a position in a list: removing id 40 from a three-load analysis works, and removing the middle load leaves the others' ids and order unchanged.

**The transient fields are never defaulted.** A transient analysis needs `--initial-temperature`, `--time-step` and `--end-time`; a steady one takes none of them; either mistake is refused by the *data model* (`validate(ThermalAnalysisDefinition)` is mode-complete) with its own message, and the file is untouched. Switching an existing analysis between steady and transient is not a verb: that is a whole-definition edit the command layer does not have, and the brief says not to mutate the mode behind a user's back.

## Which core API each command calls (the brief's §219)

```text
CLI action                    Core owner                                                       Where it is called
------------------------------------------------------------------------------------------------------------------------------
Create the analysis           P18-CMD   CreateThermalAnalysisCommand                           ThermalEdits.cpp  applyAnalysisAdd
Add a load                    P18-CMD   AddThermalLoadCommand                                  ThermalEdits.cpp  applyLoadAdd
Remove a load                 P18-CMD   RemoveThermalLoadCommand                               ThermalEdits.cpp  applyLoadRemove
Add a boundary condition      P18-CMD   AddThermalBoundaryConditionCommand                     ThermalEdits.cpp  applyBoundaryAdd
Remove a boundary condition   P18-CMD   RemoveThermalBoundaryConditionCommand                  ThermalEdits.cpp  applyBoundaryRemove
Validation of every edit      P18-DATA  validate(ThermalLoad / ...Condition / ...Definition)   reached THROUGH the command, never called by the CLI
Atomic load-edit-save         the edit spine (ADR-009) + P18-PERSIST-001's atomic save        Edits.hpp  runEdit  (shared with every edit verb)
Load a document               P18-PERSIST-001  io::loadDocument                                ThermalReports.cpp  prepare
A face               parseFaceReference (shared grammar) -> FaceName, preserved if unresolved  Selectors.cpp
Check whether a face resolves features::checkFaceName   (settings only: a REPORT, never a rebind)  ThermalReports.cpp
Mesh                          P16       meshing::Mesher::generate                              ThermalReports.cpp  analyse
Run the analysis              thermal::runThermalAnalysis  (ADR-057)                            ThermalReports.cpp  analyse
   requireThermalModel / buildThermalDofMap / prepareThermalLoads / prepareThermalBoundary / assembleThermalSystem      the chain,
   solveSteadyThermal | solveTransientThermal / recoverSteadyThermal | recoverTransientThermal /                       in the order the
   recoverSteadyBalance | recoverTransientBalance                                                                       entry freezes
Temperature, gradient, flux   P18-POST   ThermalPostResult  (ThermalPostState: temperatures, gradients, heatFluxes, heatFluxMagnitudes)
Heat balance                  P18-BALANCE  ThermalBalanceResult / ThermalHeatBalance / IntegratedEnergy
Acceptance                    P18-VALID    validateThermalAnalysis -> ThermalValidationReport;  exit status = ThermalAnalysisRun::accepted()
```

## What the CLI does itself, and nothing else

It **selects and formats**: the node or element carrying an extremum of an already-qualified array (ascending id on a tie), the norm of a stored component triple (the same `hypot` `HeatFlux3::magnitude()` is), the JSON field names, the prose. It computes no thermal quantity. [CLI_ARCHITECTURE.md](CLI_ARCHITECTURE.md) states the boundary, and `tests/architecture/CheckThermalCliOwnership.cmake` enforces it.

## Examples (real output; `examples/models/thermal_plate.bcad`, a transient plate with a hole)

```text
> bettercad-cli thermal-solve thermal_plate.bcad
Solved transient: 81 nodes, 127 tetrahedra, 41 free equations, 19 steps to 2.375 s
  worst residual:     1.568e-16
  min T:              317.23 K at node 6
  max T:              345.125 K at node 1
  heat balance:       6.377e-14 (worst normalized residual)
  validation:         PASS

> bettercad-cli thermal-solve singular.bcad --json        (no prescribed temperature, no convection)
[stdout is empty]
bettercad-cli thermal-solve: no_temperature_datum: <the validator's sentence>          exit 1

> bettercad-cli thermal-boundary-add plate.bcad 3 face:Extrude001:end_cap --temperature 20degC
bettercad-cli thermal-boundary-add: --temperature: unknown unit 'degC' in '20degC'
Usage: bettercad-cli thermal-boundary-add <file.bcad> <id> <face> --temperature <K> | --convection <W/(m2 K)> --ambient <K>   exit 2
```
