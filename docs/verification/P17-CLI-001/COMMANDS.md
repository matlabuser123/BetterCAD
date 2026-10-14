# P17-CLI-001 — the command inventory

```text
ELEVEN COMMANDS: six reports and five edits, following the P16 pattern
exactly -- which is ten mesh-* commands made of six reports and four edits.
```

Names were chosen **after** the audit, not before. See [AUDIT.md](AUDIT.md) §3
for the measurement: the module name is the prefix and the verb, where there
is one, comes last.

## The inventory

| Command | Purpose | Core API called | Mutates? | Solves? | Output |
| --- | --- | --- | --- | --- | --- |
| `structural-settings` | the canonical analysis definition, and which targets resolve | `findStructuralAnalysis`, `features::checkFaceName` | no | **no** | prose / `--json` |
| `structural-validate` | is it ready to solve, in the core's staged order | `validateStructuralAnalysisForSolve` | no | **no** | prose / `--json` |
| `structural-solve` | solve and summarise | `Mesher::generate`, `solveStructuralAnalysis` | no | yes | prose / `--json` |
| `structural-displacement` | nodal displacement summary | the same, then `RecoveredFields::displacements()` | no | yes | prose / `--json` |
| `structural-stress` | element stress summary | the same, then `RecoveredFields::elements()` | no | yes | prose / `--json` |
| `structural-reactions` | support reaction summary | the same, then `SupportReactions` | no | yes | prose / `--json` |
| `structural-analysis-add` | create the analysis | `CreateStructuralAnalysisCommand` | **yes** | no | prose |
| `structural-load-add` | add one load | `AddStructuralLoadCommand` | **yes** | no | prose |
| `structural-load-remove` | remove one load by id | `RemoveStructuralLoadCommand` | **yes** | no | prose |
| `structural-restraint-add` | add one restraint | `AddStructuralRestraintCommand` | **yes** | no | prose |
| `structural-restraint-remove` | remove one restraint by id | `RemoveStructuralRestraintCommand` | **yes** | no | prose |

Every one appears in `--help`, and a test asserts that for all eleven.

## The five edits are also batch verbs, for free

The edit spine (ADR-009): registering a verb in `structuralEditCommands()`
makes it both a single-shot command and a `batch` script line, "in the same
one-transaction-or-nothing script as a mate verb, with nothing added to
`BatchCommand.cpp`". `StructuralCli_TheEditsAreScriptableThroughBatch` tests
both halves, including that a script whose second line fails writes nothing at
all.

A single-shot edit loads, applies and **saves in place, only on success** —
`runEdit`'s contract. There is no `--save` flag and no CLI-side persistence
code; the save is `io::saveDocument` through the normal document path.

## Four result commands, one helper

Per the brief's §197, the load-regenerate-mesh-solve sequence is written
**once**, in `solve()`, and shared by `structural-solve`,
`structural-displacement`, `structural-stress` and `structural-reactions`. It
is orchestration and owns no rule:

```text
io::loadDocument                            the file
Regenerator::regenerate  -> the REPORT      not only the Result -- see below
structuralAnalysisIds    -> exactly one     or a named refusal
Mesher::generate                            because solveStructuralAnalysis
                                            does not, by its own contract
validateStructuralAnalysisForSolve          the staged report, for the
                                            diagnostic AND so the solver is
                                            not invoked for a refused model
solveStructuralAnalysis                     the production entry, which gates
                                            again on its own terms
```

**The regeneration REPORT, not only its Result.** P16-CLI-001's mutation M6
found that checking the `Result` alone let a broken model through: a feature
that fails to build is a *successful* regeneration carrying `failed`,
`blocked` or `cycles`. The CLI then reported "no analysis", which is true and
is not the reason. Each kind keeps its own diagnostic code.

## What the CLI accepts as a target

One grammar, the shared one: `face:<feature>:<role>[:<entity>]`, parsed by
`parseFaceReference` — the same function drawings, mates and local mesh sizing
use. Gravity takes no face, because it acts on the body.

```text
ACCEPTED    face:Solid:end_cap
            face:Extrude001:side:6

REFUSED     node:1   element:1   facet:1   triangle:9
            1        0.02,0.015,0.02      nearest:0,0,0

AND NO OPTION EXISTS   --node  --element  --facet  --at
```

`StructuralCli_TheCliAcceptsNoMeshLocalTarget` checks all eleven spellings and
that the file is byte-identical after each refusal. The absence of the options
is checked rather than asserted: an unknown option is a usage error, so it is
provable.

**`NodalForceLoad` is therefore not creatable from the CLI**, and that follows
rather than being enforced: it is the one qualified load form that names a
`NodeId`, there is no option that could supply one, and ADR-048 refuses to
persist it — so a CLI that could add one would be a CLI that produced
unsavable documents.

## Units

Through `parseQuantity`, the one dimension-generic parser, so a structural
value is read by exactly the code that reads a length and "cannot acquire a
second spelling, a second unit table or a second dimension check"
(P15-CLI-001).

```text
--pressure 2.5MPa      the same as --pressure 2500000
--traction 0,0,-1.5MPa the same as 0,0,-1500000
--gravity  0,0,-9.80665
```

A bare number is Pa for a pressure or traction and m/s² for gravity, and every
usage string says so. A unit of the wrong dimension is refused by the
catalogue. `nan` and `inf` are refused by the parser as "not a number" —
earlier than the canonical validator, which still owns the rule for a value
that *does* parse.

## Two things deliberately not added

**No load or restraint EDIT command.** The checklist asks for add and remove
only, and the brief's §20 says editing stays available through the core command
API. `EditStructuralLoadCommand` and `EditStructuralRestraintCommand` exist and
are qualified; they are simply not exposed here. Remove-then-add through
`batch` is one transaction, so nothing is lost.

**No solver-settings command.** `SetSolverSettingsCommand` is qualified, but
exposing a tolerance on the command line would invite a second opinion about
numerical acceptance, and P17-SOLVE-001 chose both numbers with a recorded
reason. `structural-settings` reports them; nothing in the CLI changes them.

## One core rule the CLI cannot reach

`validate(StructuralRestraint)` refuses an empty component mask — "a restraint
that holds nothing at zero is not a weaker restraint, it is a record whose
intent cannot be acted on". **No CLI spelling produces one**: every token the
parser accepts adds a component, and an empty string is refused as a malformed
argument (exit 2) before any restraint exists.

So that core refusal is unreachable through this surface. It still guards the
API, the GUI and the file reader; it is recorded here rather than left looking
like a CLI test gap. `StructuralCli_AFailedMutationLeavesTheFileExactlyAsItWas`
pins the CLI's own behaviour — exit 2, file unchanged — and says why it is 2
and not 1.
