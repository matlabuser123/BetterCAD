# P18-CLI-001 — CLI / core equivalence

## The claim

For the same document, every number a thermal CLI command prints **is** the number the core computed, bit for bit, and every edit a CLI command makes **is** the edit the P18-CMD command object makes, byte for byte on the canonical file. The CLI adds no result of its own.

## How the results are compared

The oracle is the core itself, called by the test through `thermal::runThermalAnalysis` on the file the CLI will read (`coreAnswer(file)`), and — for the transient — the core's own arrays (`post.states()`, `core.transient->timeOf(i)`, `core.balance`). The CLI numbers come from a separate invocation that parsed the `--json` output with the strict reader (`tests/cli/MiniJson.hpp`), and are compared with `bitsOf(double)`, i.e. **all 64 bits**, not a tolerance.

The entry itself is checked the same way against an independent chain: `ThermalRun_*` (20 tests) compares `runThermalAnalysis` with the test-support derivation `vztest::derive`, which calls each qualified link directly, field by field and bit by bit — so the chain the CLI reaches is the chain the viewer and the earlier milestones' tests qualify, not a rewritten one.

| Quantity | CLI source | Core value it must equal | Test |
|---|---|---|---|
| minimum / maximum temperature (steady) | `thermal-solve`, `thermal-temperature` | the extrema of the core's final-state temperature array | `ThermalCli_SteadyCoreEquivalence` |
| largest gradient magnitude | `thermal-gradient` | max over the core's `gradients[]` of the norm | same |
| largest heat-flux magnitude | `thermal-heat-flux` | max over the core's `heatFluxMagnitudes[]` | same |
| Dirichlet reaction, worst normalised residual | `thermal-balance` | `balance->finalState().terms…`, `balance->worstNormalizedResidual()` | same |
| validation status | `thermal-validate`, `thermal-solve` | `thermal::toString(core.validation.status())` | same |
| node count | `thermal-solve` | `dofMap->nodeCount()` | same |
| every stored time, and the range of every stored state | `thermal-temperature` history | `post.states()[i].time`, `transient->timeOf(i)`, the min/max of `states()[i].temperatures` | `ThermalCli_TransientCoreEquivalence` |
| balance step count, state count | `thermal-balance` | `transient->grid().stepCount()`, `balance->stateCount()` | same |
| add load / remove load / add BC / remove BC / create analysis | the five edit verbs | the same P18-CMD command object executed on a copy of the same baseline, saved with `io::saveDocument` | `ThermalCli_AddLoadCoreEquivalence`, `…RemoveLoad…`, `…AddBoundaryCondition…`, `…RemoveBoundaryCondition…`, `…AddAnalysis…` — the two canonical files must be **the same bytes** |

Extrema are the only values the CLI selects rather than copies, and the tie-break is part of the contract (lowest stored id for the minimum and the maximum), tested on hand-built unordered input (`ThermalCli_ExtremumTieBreakIsTheLowestIdWhateverTheOrder`) and mutation-probed (MUTATIONS.md, CR02).

## What the equivalence tests cannot show, and what covers it

They prove agreement with the core on the cases they build. That the CLI **cannot** disagree on others is not argued from tests but enforced structurally: the ownership gate (`CheckThermalCliOwnership.cmake`) fails a CLI file that contains arithmetic on a result, a floating-point literal, a threshold, a status decision, a material property, an id used as an index or a second chain. 102 fixture trees prove that each rule fires on its one defect. See CLI_ARCHITECTURE.md and the source-audit output (`qualification/source-audit.ps1`).

## A finding recorded here

The core had no production orchestrator: the only code that ran the whole chain was the test-support `derive`. P18-CLI-001 therefore adds `thermal::runThermalAnalysis` (ADR-057). It is **additive** — no existing thermal file changed — and is held to the same standard as the CLI: it calls each link once, in order, and does no arithmetic (gate-enforced). It is reported as a finding and not hidden as "CLI plumbing".
