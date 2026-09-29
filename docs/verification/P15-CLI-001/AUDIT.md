# P15-CLI-001 — CLI architecture audit

Done before any design decision and before any code, because the brief forbids a second
CLI framework and because the four milestones before this one each found the work
already done.

## Baseline

```text
HEAD / origin/main   0e595357c22d13dffb2e64fbcf63a6bf0802e505 (equal)
HEAD^{tree}          a0204beab4cc8141c134fbfcc76f0b2e958df34f
working tree         clean
predecessors         11 of 11 PASS, 178 boxes ticked
suite                2716 tests
```

## What exists

`apps/bettercad_cli/` is 3363 lines across 19 files, carrying P10, P13-CLI-001 and
P14-CLI-001. It is not a sketch of a CLI; it is a CLI with a spine.

| Existing pattern | Reusable? | Required P15 extension | Risk |
| --- | --- | --- | --- |
| `Cli.hpp`: `ExitCode{Success=0,Failure=1,UsageError=2}`, `run()`, `kCommands` table | **Yes, unchanged** | add report verbs to the table | None. Additive entries in a `constexpr std::array`. |
| `Edits.hpp`: the edit spine — `EditCommand{name,usage,summary,apply}`, single-shot = batch of one | **Yes, unchanged** | a `materialEditCommands()` table | None. P14 already added a second table this way. |
| `EditRegistry.cpp`: joins the tables into one listing and one lookup | **Yes** | join a third table | None — the file exists to be extended. |
| `EditSupport.hpp::execute(Document&, Command&)` | **Yes — mandatory route** | material edits run P15-CMD-001 command objects through it | Bypassing it would restate validation. Forbidden by ADR-009 and by this brief. |
| `BatchCommand.cpp`: transactional script driver | **Yes, no change at all** | none — a verb in the registry is a batch verb | None. Registration alone gives scripting. |
| `Arguments.hpp/.cpp`: `parseArguments`, `OptionSpec`, `parseSiValue` | **Yes** | expose a dimension-generic `parseQuantity<D>` | Low. `parseSiValue` is *already* generic — `parseLength`/`parseAngle` are four-line wrappers over it. |
| `Selectors.hpp::resolveObject` — ID-or-name, sets provably disjoint | **Yes** | `resolveMaterial` narrowing it; a `designation:` form | None. `resolveComponent` is the narrowing pattern, verbatim. |
| `bettercad_add_process_test` + `$<TARGET_FILE:bettercad_cli>` + ctest fixtures | **Yes** | a material fixture chain | None. |
| `tests/cli/CliRunner.hpp`: in-process `cli::run` | **Yes** | the equivalence harness drives it | None. |

**Nothing in this milestone needs a new CLI mechanism.** Every requirement in the brief
maps onto a mechanism that is already qualified.

## Four findings that change the design

### 1. The core already refuses first-match. The CLI only has to report it.

The brief requires a name selector to answer NotFound / one / **Ambiguous** and never to
pick the first match. `features::findMaterialsByDesignation` already returns **all**
matches, and says why:

> Returns ALL of them, because a designation is not identity and duplicates are
> legitimate. A caller that finds two has an ambiguity to resolve or to show the user;
> **it is never resolved here by picking one.**

So Ambiguous is not a policy the CLI invents — it is the core's policy, surfaced. This
also settles the grammar, because the two selector spaces behave differently and must not
be conflated:

```text
7                     an object ID           digits only, and no name can be
Steel                 an object NAME         unique across objects AND parameters,
                                             re-checked on every rename -> NEVER ambiguous
designation:Steel     a DESIGNATION          free text, duplicates are legitimate
                                             -> 0 = not found, 1 = it, >= 2 = AMBIGUOUS
```

An object name cannot be ambiguous and that is a property of `validateIdentifier`, not a
convention this parser invents. A designation can be, and P15-PERSIST-001 already has a
document holding two materials that share one. `designation:` follows the prefix
convention the selector grammar already uses for `datum:`, `face:`, `object:`.

### 2. The persisted property keys are the stable machine vocabulary — but the sets differ

`src/io/json/MaterialJson.cpp` holds `kMechanicalKeys` and `kThermalKeys`:
`density`, `youngs_modulus`, `poisson_ratio`, `yield_strength`, ...
Exactly the machine codes the brief asks for, already stable, already qualified.

The obvious move is to lift that table into `core/materials/` and have both read it.
**Rejected**, and the reason is a qualified invariant:

`MechanicalPropertyKind` has **11** values; `kMechanicalKeys` has **9**. `ShearModulus`
and `BulkModulus` are absent *by design* — ADR-027 derives G and K and never stores them,
so no key exists and a file therefore **cannot claim one**. That absence is the structural
guarantee, and P15-PERSIST-001 tests it.

The CLI needs all 11, because it must print derived G and K labelled as derived. A single
shared table would have to contain `shear_modulus`, and the structural guarantee would
become a convention. So:

* `MaterialJson.cpp` is **not touched**. Its 9 entries stay the guarantee.
* The CLI carries its own code table, covering all 16 kinds.
* A test asserts the CLI's code is **byte-identical** to the persisted key for every kind
  that has one. Drift is then impossible without the suite failing, and ADR-027's
  protection is not weakened.

Two tables with a proof beat one table with a hole in an invariant.

### 3. There is no degC, and the CLI must not invent one

A `Unit` scale is a ratio — `{numerator, denominator}`, multiplicative. There is no offset
anywhere in the unit system, and `K` is the only temperature unit in a catalog of 54.

That is not an oversight to paper over. An affine degC conversion implemented in
`Arguments.cpp` would be a conversion that exists **only in the CLI** — precisely the
CLI-only semantics this brief forbids. Absolute temperatures stay absolute, in kelvin,
exactly as P15-THERM-001 persisted them. If degC is wanted it is a unit-system milestone.

### 4. Mass properties are per-feature, and the CLI must not aggregate

`partMassProperties(document, regenerator, feature)` answers for **one** body-producing
feature. Core provides `shiftedFromCentroid` and `transformed`, but **no aggregation over
bodies**.

Summing masses is trivial; summing inertia tensors is not — it needs Huygens to a common
point, and that is a derivation core does not offer. Writing it in the CLI would be a
CLI-only property derivation, which the brief forbids by name. So the command reports
per-feature and prints **no total**. A whole-part total is a core milestone, not a
formatting decision.

## Consequence

New files, following the names already in use (`AssemblyEdits`/`AssemblyReports`,
`DrawingEdits`/`DrawingReports`):

```text
apps/bettercad_cli/MaterialEdits.cpp      mutating verbs, via P15-CMD-001 commands
apps/bettercad_cli/MaterialReports.cpp    reporting verbs
```

Modified, additively: `Cli.cpp` (report verbs), `EditRegistry.cpp` (join the third
table), `Selectors.hpp/.cpp` (`resolveMaterial`), `Arguments.hpp/.cpp`
(`parseQuantity<D>`), `CMakeLists.txt`.

**No new framework, no second document model, no CLI-only semantics.**
