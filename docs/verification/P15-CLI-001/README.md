# P15-CLI-001 — Headless Engineering Data Workflows

**STATUS: PASS.** Qualified across three presets, 2764/2764 each, 0 warnings, 35932
test executions, 0 failures — on the SECOND attempt. The first is kept at
[qualification/attempt-1-void/](qualification/attempt-1-void/README.md): every preset
passed and both repeat stages caught a test of mine that was not idempotent. Full
detail at the end.

## TASK

P15-CLI-001. Make the material and mass-properties work of P15-MASS-001 through
P15-PERSIST-001 usable without a GUI, as an **adapter over the qualified core**.

The gate: `CLI uses core APIs + engineering values identical to core + diagnostics
structured + failures propagate + no CLI-only material semantics`.

## SCOPE

Authorized by TODO.md: 17 items. Nothing outside them was built. P15-REFMOD-001 and
P15-QUAL-001 were not started.

## BASELINE

```text
HEAD / origin/main   0e595357c22d13dffb2e64fbcf63a6bf0802e505 (equal)
HEAD^{tree}          a0204beab4cc8141c134fbfcc76f0b2e958df34f
working tree         clean
predecessors         11 of 11 PASS, 178 ticked boxes
suite                2716 tests
```

## ARCHITECTURE

The audit came first, before any design decision and before any code, and it is the
substance of this milestone: see **[AUDIT.md](AUDIT.md)**. The finding was that
`apps/bettercad_cli/` already had every mechanism this work needs.

```text
existing mechanism                       reused as
------------------------------------     --------------------------------------
Cli.hpp ExitCode{0,1,2} + kCommands      five report verbs, added to the table
Edits.hpp EditCommand spine              seven edit verbs, in a third table
EditRegistry.cpp                         joins the third table -- and that alone
                                         makes them BATCH verbs
EditSupport::execute(Document&, Command&) routes edits through P15-CMD-001's commands
BatchCommand.cpp                         unchanged in substance
Arguments.cpp parseSiValue               already dimension-generic; exposed as
                                         parseQuantity<D>
Selectors.hpp resolveObject              narrowed to resolveMaterial
bettercad_add_process_test + fixtures     the multi-process E2E chain
```

**New files: three.** `MaterialVocabulary.{hpp,cpp}` (the codes), `MaterialReports.cpp`
(five reports), `MaterialEdits.cpp` (seven edits). No new framework, no second document
model, no second parameter system, no second unit parser.

Four decisions the audit forced, each recorded there with its reasoning:

1. **Ambiguity is the core's policy, surfaced.** `findMaterialsByDesignation` already
   returns every match and refuses to pick one. The CLI reports it; it does not invent
   it. This also fixes the selector grammar, because an object *name* is unique by
   `validateIdentifier` and can never be ambiguous, while a *designation* is free text
   and legitimately can be.
2. **The persisted property keys are the machine vocabulary, and are NOT shared by
   lifting the table.** `kMechanicalKeys` has 9 entries for 11 kinds because ADR-027
   stores no derived modulus, so no key exists and a file cannot claim one. The CLI
   needs all 11. A shared table would have had to contain `shear_modulus`, turning that
   structural guarantee into a convention. Two tables, and a test asserting they agree
   through the real writer.
3. **No `degC`.** A `UnitScale` is a ratio with no offset. An affine conversion written
   in `Arguments.cpp` would exist only in the CLI.
4. **No whole-part mass total.** Summing inertia tensors needs Huygens to a common
   point, a derivation core does not offer; writing it here would be a CLI-only
   engineering result.

## BLAST RADIUS

Wider than the material subsystem, which is why the regression filter is total.

```text
changed                        who else goes through it
---------------------------    ------------------------------------------------
EditFailure (+ code field)     EVERY assembly and drawing edit verb
runEdit (AssemblyEdits.cpp)    EVERY single-shot edit, all three domains
BatchCommand.cpp               EVERY batch script
Arguments.cpp parseSiValue     EVERY length and angle on EVERY command line
EditRegistry.cpp               the one verb listing `help` prints, `batch` looks up
Cli.cpp kCommands              the help text, which a process test asserts
Selectors.hpp/.cpp             +resolveMaterial only; no existing resolver touched
```

Nothing in `src/`, `include/` or `io/` was modified. **`MaterialJson.cpp` was
deliberately not touched** — see ARCHITECTURE 2.

## IMPLEMENTATION

Twelve verbs. Five report and write nothing; seven edit through the load-apply-save
spine.

```text
material-list           the document's materials, and which is assigned
material-show           metadata and all 16 properties; --provenance adds citations
material-effective      what the part is made of, and whether the assignment resolves
mass-properties         volume, mass, centre of mass, both inertia tensors, per body
material-completeness   what a material has and lacks, per consumer or in general

material-create         CreateMaterialCommand
material-clone          cloneMaterial            (carries the copy semantics)
material-set            setMaterialDefinition / setMaterialMechanical / ...Thermal
material-unset          removeMaterialProperty   (removes the citation with the value)
material-assign         AssignMaterialCommand
material-unassign       RemoveMaterialAssignmentCommand
material-delete         DeleteMaterialCommand
```

The four verbs that have a P15-CMD-001 command object use it, through
`EditSupport::execute`, so the CLI inherits that milestone's preconditions and its
all-or-nothing execution. The three that do not go through the qualified *function*
instead, and that is the same principle rather than an exception: each of those carries
a coupling a command object would not — a clone mints a new identity while copying every
property and the origin key; a removal takes the provenance with the value; a value
change clears the citation that described the old number. Rebuilding a whole
`MaterialDefinition` here and handing it to `EditMaterialCommand` would mean
reimplementing those three couplings in the CLI. The rule is not "always use a command
object"; it is **never restate what a qualified function already decides**.

Selector grammar:

```text
7                     an object ID           digits only, and no name can be
Steel                 an object NAME         unique per document -> NEVER ambiguous
designation:Steel     a DESIGNATION          exact, and duplicates are legitimate
```

A bare number is **SI** for every property. One rule, so `210` cannot mean 210 GPa
where `235` means 235 MPa.

## TESTS

48 added; 2716 -> 2764.

```text
tests/cli/MaterialCliTests.cpp   28 in-process, driving cli::run
tests/CMakeLists.txt             20 process tests, on the real executable
examples/scripts/build_material.txt   a committed five-edit transaction
```

### CLI/core equivalence — the differential harness

Each of these does the same thing **twice**, once through the core API in memory and once
through the CLI and a file, and requires the two to agree. A test that only checked the
CLI produced something plausible would pass just as happily if the CLI had its own idea
of what a material is.

| Operation | Core path | Compared on |
| --- | --- | --- |
| create | `createMaterial` | `materialIds` equal, `MaterialDefinition ==` |
| edit | `setMaterialMechanical` | `MaterialDefinition ==` |
| unset | `removeMaterialProperty` | `MaterialDefinition ==`, and the property is Unknown |
| clone | `cloneMaterial` | new ID, and the source's density unchanged after editing the clone |
| assign / unassign | `assignMaterial` / `removeMaterialAssignment` | `MaterialAssignment ==` |
| delete | `removeMaterial` | state Unresolved, intent still the deleted ID |
| effective | `materialAssignment` | the state string is core's `toString` |
| property query | `MechanicalProperties` | every printed number `==` core's, exactly |
| derived G, K | `derivedShearModulus` / `derivedBulkModulus` | `==` core's, exactly |
| mass properties | `partMassProperties` | volume and mass `==` core's, exactly |
| completeness | `materialCompleteness` | state, and every missing property core names |

The comparisons are **exact**, not within a tolerance, and that is a consequence of the
output format: the CLI prints shortest-round-trip text, so the double recovered from its
stdout is the double the core computed. A tolerance would have hidden a unit error;
exact equality cannot.

### Exit codes

| Situation | Exit | stdout | stderr |
| --- | --- | --- | --- |
| query answered, state good | 0 | the report | — |
| query answered, state not ready | 1 | the report | code + diagnostic |
| query could not answer (no such material, ambiguous, file) | 1 | **empty** | code + diagnostic |
| command line unreadable (bad property, bad consumer, arity) | 2 | empty | problem + usage |
| edit refused by the document | 1 | empty | code + diagnostic |
| mass unavailable for a body | 1 | the bodies it could do | code + diagnostic |

The decision the brief asks to be recorded: **`Incomplete` is exit 1, not exit 0 with a
status field.** It follows `validate`, which exits 1 when a document has errors, because
a report that gates is only usable from a script if the gate reaches the exit status.
"Could not answer" stays distinguishable from "answered: not ready" through the streams
— the first prints nothing to stdout — and `mass-properties` has a second, per-body
shape which is stated rather than smoothed over. See ADVERSARIAL_REVIEW.md F5.

Machine codes: `material_not_found`, `material_ambiguous`, `not_a_material`,
`bad_material_selector`, `no_material_assigned`, `material_unresolved`,
`material_assignment_invalid`, `document_not_loaded`, `feature_not_found`, `no_bodies`,
`mass_properties_unavailable`, plus `missing_property:<code>`,
`inconsistent_values`, `orphan_provenance`, `provenance_incomplete` from core's own
`IssueKind`. Every one names something core already distinguishes.

### End-to-end, across process boundaries

One process mutates and saves; later, separate processes read the file back. Everything
shared between them is bytes on disk. The binary is `$<TARGET_FILE:bettercad_cli>` — the
absolute path of the executable this preset just built, so there is **no PATH to be
ambiguous** and no installed or stale copy that could answer.

| Step | Process | Asserted |
| --- | --- | --- |
| 1 | `batch build_material.txt` | 5 edits, one transaction, exit 0, every line's output |
| 2 | `material-list` | both materials, their IDs, the assignment |
| 3 | `material-show SteelCold` | 275 MPa — the clone was edited |
| 3 | `material-show Steel` | 235 MPa — the source was not |
| 4 | `material-show Steel` | G and K present, labelled `(derived)`; hardness `UNKNOWN` |
| 5 | `mass-properties` | density, volume and mass against **closed-form geometry** |
| 6 | `material-completeness --consumer thermal_steady` | `ready`, exit 0 |
| 6 | `material-completeness --consumer thermo_mechanical` | `incomplete`, exit 1, `missing_property:thermal_expansion` |
| 7 | `material-clone` then `material-show designation:…` | ambiguity refused, both named, exit 1 |
| 8 | `material-set … density 0` then `material-show` | refused, and the designation is unchanged **on the file** |

## INDEPENDENT VALIDATION

Two independent references, neither taken from production code.

**A box, closed form.** For `a x b x c` at density rho: `V = abc`, `m = rho abc`, the
centroid at `(a/2, b/2, c/2)`, and `Ixx = m(b^2 + c^2)/12`. A 100 x 50 x 20 mm box at
7850 kg/m^3 is checked on all four, in process.

**The committed example plate, closed form.** `plate.bcad` carries its dimensions as
parameters: width 100 mm, height 50 mm, thickness 20 mm, hole radius 10 mm. So

```text
V = 100 x 50 x 20 - pi x 10^2 x 20 = 93716.81469282041 mm^3
m = 7850 kg/m^3 x V                = 0.7356769953386403 kg
```

worked out from the model's own numbers. The CLI prints `93716.81469282042 mm^3` and
`0.7356769953386403 kg` — the volume differs from the closed form by 1 ulp and the mass
agrees to every digit printed. That is the process test's assertion.

**The derived elastic constants, closed form.** From E = 210 GPa and nu = 0.3,
`G = E/(2(1+nu)) = 80769.230769... MPa` and `K = E/(3(1-2nu)) = 175000 MPa`. Both are
asserted in the process test regex.

## ADVERSARIAL REVIEW

**[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md)** — 30 attacks, 5 findings, all
resolved, and 1 known limitation recorded.

Four were true coverage gaps: a designation must match exactly and not be folded; the
configuration-override refusal must hold *through the CLI* (a guard that only holds
in-process is not a guard); a non-ASCII designation must survive command line -> file ->
selector; and deleting the assigned material must make the mass a refusal. None was
broken; all four now have a test.

The fifth was a contract that has two shapes, now stated instead of being claimed as
one rule.

One defect was found during implementation, before any test existed to find it: parsing
a dimensionless property against a dimensionless unit would have accepted `0.3 rad` as a
Poisson ratio and turned `0.3 deg` into 0.0052, because the only dimensionless entries
in the unit catalog are angles.

### Mutation testing

Six mutations, each built against the real tree; every one was caught.

```text
M1  ambiguity branch disabled (take the first match)     1 test failed
M2  render() returns "0" instead of "UNKNOWN"            1 test failed
M3  completeness gate always returns Success             2 tests failed
M4  volume printed in cm^3 while labelled mm^3           3 tests failed
M5  runEdit saves the document before reporting failure  3 tests failed
M6  bulk_modulus silently accepted instead of refused    3 tests failed
```

Deleting the derived-property guard outright **does not compile** — the refusal helper
becomes unused under `-Werror=unused-function`. The compiler holds that guard before any
test does.

A process finding is recorded there too: my first mutation run piped the revert step to
`/dev/null`, and one revert was *correctly refused* for being ambiguous while the
refusal went unseen, leaving a mutation in the tree for three builds. A mutation revert
has to be verified, never assumed.

## FAILURE PATHS

Every one reports a code and reaches the exit status: no such material; an ambiguous
designation; an object that is not a material (naming what it is); a document that will
not load; no material assigned; an assignment to a deleted material; a material with no
density; a body whose regeneration failed; an active configuration that overrides a
parameter; a derived modulus someone tried to set; an unknown property code; an unknown
consumer; a property value with the wrong dimension; a unit on a dimensionless property;
a hardness with no scale; an odd number of `<property> <value>` words.

**Never a zero.** A mass that could not be computed is a refusal naming the reason, and
a property nobody supplied prints `UNKNOWN`.

## PERSISTENCE

Nothing new is persisted: this milestone adds no field, no key and no schema change. The
format is P15-PERSIST-001's, unaltered, and `MaterialJson.cpp` is untouched. What is
tested is that the CLI's writes go through it correctly — every equivalence test above
saves with the CLI and loads with `io::loadDocument`, and the E2E chain crosses a process
boundary through the file.

One test reads the saved file **as text** and asserts that the CLI's code for each
storable property is a key in it, and that `shear_modulus` and `bulk_modulus` are in
neither the file nor the persisted vocabulary while still being printed by the CLI.

## DETERMINISM

Materials in ascending ID from the document's ordered map; properties from a `constexpr`
table in enumeration order; issues in core's defined order. The order test builds two
materials in reverse name order so that an order coming from insertion would show.
Numbers are formatted with `std::format("{}", double)`, which is locale-independent and
shortest-round-trip. Two identical invocations produce byte-identical output.

No wall-clock, no random seed, no unordered iteration, no temporary path order.

## PERFORMANCE

Not a performance milestone; no claim is made. The 20 process tests take 3.4 s in total.

## KNOWN LIMITATIONS

1. **No multi-body mass test.** Every mass test has one body, so "one body fails,
   another succeeds" is reasoned (`status` is never reset to `Success`) and not
   exercised. Recorded, not claimed.
2. **No whole-part total**, by decision (ARCHITECTURE 4). A total needs core to provide
   aggregation.
3. **No `degC`**, by decision (ARCHITECTURE 3). Temperatures are kelvin.
4. **A configuration override still refuses a mass**, because the carried P15-MASS-001
   regeneration defect is unchanged. The CLI inherits the refusal, and a test now pins
   it there.
5. **The library has no data**, so `material-create` and the example script define
   materials by hand. There is no `material-import` verb because there is nothing to
   import.
6. **No locale is exercised**; the audit of number formatting is by construction.
7. **No compile-fail cases were added.** The CLI's interface is text, so its misuse is a
   runtime diagnostic rather than a type error, and the type-level guarantees it depends
   on are already pinned by P15-MECH-001's and P15-CUSTOM-001's compile-fail suites.
8. **An ad-hoc `ctest -R` by hand is not protected against a zero-match filter** the way
   the harness is — the protection comes from `noTestsAction: error` in the test preset.
   Measured, and recorded in the harness comments.

## FULL REGRESSION

Qualified on the **second attempt**. The first is kept, with its diagnosis, at
[qualification/attempt-1-void/](qualification/attempt-1-void/README.md) — it is evidence
that the determinism gate works.

```text
preset             configure  clean  build  no-op rebuild  suite       warnings
debug-ext              0        0      0          0        2764/2764      0
release-ext            0        0      0          0        2764/2764      0
debug-shared-ext       0        0      0          0        2764/2764      0

repeat release-ext, [A-Za-z], until-fail:5   exit 0   13820 = 2764 x 5, 0 failures
repeat debug-ext,   [A-Za-z], until-fail:5   exit 0   13820 = 2764 x 5, 0 failures
qualification finished 14:45:40, 0 stage(s) failed          (11:15:36 -> 14:45:40, 3h 30m)
```

2764 = the 2716 of P15-PERSIST-001 plus the 48 added here. All three presets discover the
same 2764, and both repeat stages show exactly **13820 passing executions** — 2764 x 5,
with nothing skipped, counted from the logs rather than inferred from the summary line.

**35932 test executions in total, 0 failures.**

### Tests selected, executed and passed

The brief requires this recorded rather than implied, and a zero-test filter must not read
as PASS.

```text
stage                        selected   executed   passed
debug-ext ctest                  2764       2764     2764
release-ext ctest                2764       2764     2764
debug-shared-ext ctest           2764       2764     2764
repeat release-ext               2764      13820    13820
repeat debug-ext                 2764      13820    13820
                                          ------   ------
                                           35932    35932
```

The `selected` figures for the repeat stages are the harness's own, written to
`qualification-times.txt` by the guard added in this milestone
(`repeat release-ext selects 2764 test(s)`). A zero there is counted as a failed stage.

### The tests carrying this milestone's claims, in all three presets

```text
cli.material.batch                    -- 5 material edits in one transaction   passed x3
cli.material.mass                     -- mass against closed-form geometry     passed x3
cli.material.exit.ambiguous           -- a designation refused, both named      passed x3
cli.material.completeness.incomplete  -- the gate reaching the exit status      passed x3
cli.material.atomic.wrote-nothing     -- a refused edit wrote nothing           passed x3
cli.material.ambiguous.clone          -- the test attempt 1 caught              passed x3
MaterialCli_PropertyCodes_AreTheKeysTheDocumentFormatAlreadyUses               passed x3
MaterialCli_MassProperties_MatchTheCoreApiAndClosedFormGeometry                passed x3
MaterialCli_Selector_RefusesAnAmbiguousDesignationInsteadOfPickingOne          passed x3
MaterialCli_MassProperties_RefuseUnderAConfigurationThatOverridesAParameter    passed x3
MaterialCli_Show_PrintsUnknownAndNeverZero                                     passed x3
```

### Why attempt 1 was void

Every per-preset stage passed — 2764/2764 in all three presets — and **both repeat stages
failed on one test**, `cli.material.ambiguous.clone`, three hours in.

The defect was mine, and so was the misunderstanding behind it:
**`--repeat until-fail:N` re-runs each test N times BACK TO BACK, not the whole set N
times**, and a required fixture does not re-run between those passes. The log shows it
plainly — `Start 2559 ... Passed` immediately followed by `Start 2559 ... Failed`, with
`the name 'Steel3' is already used in this document`. `material-clone` is the one test in
this milestone that mutates rather than reading or being refused, and it was the only
producer without its own `COPY_FROM`.

Fixed with the pattern every other producer here already used, and verified under the gate
that caught it: 385/385 CLI and material tests, five times each, under code page 65001, in
17 seconds. **That check now sits in the pre-freeze routine**, because it costs seconds and
this cost three hours.

`verify-harness.cmd` was run before and after the qualification and required a non-zero
exit from a preset that does not exist: 2 stages failed, exit 2, and it also confirmed that
a repeat filter matching no tests is counted as a failed stage.

**No replace fault anywhere** — zero occurrences of "Permission denied" or "cannot replace"
across every log of both attempts.

**Qualified tree = committed tree.** The eight tree IDs recorded before the first build and
after the last test run are identical:

```text
apps 5665e20ec8728f05db604fed8efeb9d14f3327c5
include 6d55546edc191bbc2ca37d1db1b17dcb68cd3fcd
src c0c52b09a0497e075aeeb22ff87b97bb53303c3f
tests 3c94c74e9e74c0d2392060f3f81737bec2123e80
examples a22696cb86809d5bc41b587af9990114f7de3526
cmake 7ed703a3031144d26063a1ff02996e7664e29524
CMakeLists.txt 13823e788ed3975792affa9ee4354ffce9479d80
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

`include/` and `src/` are byte-identical to the baseline commit: this milestone changed
nothing below the application layer.

Only `docs/` changed after the freeze, and `docs/` is outside the fingerprint and cannot
affect the executable or the tests.

## WARNINGS

0 in all three builds, with `-Werror` and the project's full warning set.

## RESULT

```text
TASK:            P15-CLI-001
IMPLEMENTATION:  3 new CLI files, 12 verbs, over the qualified core. No new
                 framework, no CLI-only identity, default, validation or
                 derivation. Nothing in src/ or include/ changed.
TESTS:           48 added; 2716 -> 2764
VALIDATION:      a DIFFERENTIAL harness -- eleven operations done through the core
                 API and through the CLI and required to agree EXACTLY -- plus
                 closed-form geometry for the mass (a box, and the committed
                 plate: V = 100x50x20 - pi x 10^2 x 20) and for the derived
                 elastic constants
RESULT:          PASS
EVIDENCE:        this directory; AUDIT.md for the architecture decision;
                 ADVERSARIAL_REVIEW.md for 30 attacks and 6 mutations;
                 qualification/ for all 20 stage logs and the tree fingerprints;
                 qualification/attempt-1-void/ for the run the gate failed
TODO:            updated -- 17 boxes ticked
```

**The audit was the milestone.** `apps/bettercad_cli/` already had the command table, the
edit spine, the batch driver, the selector grammar, the process-test harness — and a
`parseSiValue` that was already dimension-generic, so a density is parsed by the code that
parses a length. Joining a third table to `EditRegistry.cpp` made every material verb a
batch verb with nothing added to the batch driver. Three new files use what was there.

**The most interesting decision was not to share a table.** The persisted property keys are
exactly the machine codes this milestone needed, and lifting them into core looked obvious.
`kMechanicalKeys` has 9 entries for 11 kinds *because* ADR-027 stores no derived modulus —
that absence is the guarantee a file cannot claim one. Sharing would have required
`shear_modulus` in it. Two tables, and a test that asserts they agree through the real
writer, keeps the guarantee structural.

**Exactness was a design decision, not a happy accident.** Printing shortest-round-trip
text means the double recovered from the CLI's stdout *is* the double the core computed, so
the equivalence tests compare for equality rather than within a tolerance they chose
themselves. A tolerance would have hidden the unit error that mutation M4 injected.

**Two of my own errors are recorded rather than smoothed over.** I concluded from a
measurement that the harness's zero-match claim was false; it was true, and I had omitted
`--preset` — the protection comes from `noTestsAction: error` on the test preset. And a
mutation stayed in the tree for three builds because I piped a revert to `/dev/null` and
missed that it had been correctly refused as ambiguous.

## REVISION

Second revision. The first was written against the tree of attempt 1, whose repeat stages
failed; the only source change between them is `tests/CMakeLists.txt`, giving
`cli.material.ambiguous.clone` its own pristine input. No source or test file changed after
the second freeze.
