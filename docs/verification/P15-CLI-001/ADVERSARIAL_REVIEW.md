# P15-CLI-001 — adversarial review

An attempt to disprove the milestone, not to describe it. Read against the final
diff: `apps/bettercad_cli/` (+3 files, 5 modified), `tests/cli/MaterialCliTests.cpp`,
`tests/CMakeLists.txt`, `examples/scripts/build_material.txt`.

The gate is **CLI semantics == core semantics**, so every attack below is a way the
CLI could have acquired an opinion of its own.

## Attacks

| # | Attack | Outcome |
| --- | --- | --- |
| 1 | The CLI keeps material identity by name or designation rather than by ID | **Held.** `resolveMaterial` returns a `MaterialId` and nothing else ever leaves it. A material assigned by name and then renamed stays assigned. |
| 2 | An ambiguous designation resolves to the first match | **Held.** Refused, listing every match with its ID. Mutation-tested (M1). |
| 3 | A designation is matched case-insensitively or trimmed | **Held, but untested — F1.** Fixed by adding four folding and padding cases. |
| 4 | A missing value prints as 0 | **Held.** One `render()` produces `UNKNOWN`. Mutation-tested (M2). |
| 5 | A derived modulus is presented as supplied | **Held.** `(derived)` comes from `MaterialProperty::isDerived()`, and a supplied value is checked NOT to carry it. |
| 6 | A derived modulus can be set, and is then stored | **Held twice.** Refused by name, citing ADR-027. Mutation-tested (M6) — and *removing* the guard does not even compile (`-Werror=unused-function` on the refusal helper). |
| 7 | The CLI computes a mass itself, bypassing P15-MASS-001 | **Held.** Exactly one call site: `features::partMassProperties`. No OCCT header, no `GProp`, no `TopoDS` anywhere under `apps/bettercad_cli/`. |
| 8 | The CLI aggregates bodies into a whole-part total | **Held by omission, deliberately.** Summing inertia tensors needs Huygens to a common point, which core does not offer; writing it here would be a CLI-only derivation. No total is printed, and AUDIT.md finding 4 records why. |
| 9 | A body that fails is masked by a body that succeeds | **Held.** `status` is only ever assigned `Failure`, never back to `Success`. |
| 10 | The configuration-override guard is in-process only | **Held, but untested — F2.** A mass request under a configuration that overrides a parameter must still be refused through the CLI. Fixed by adding that test. |
| 11 | A non-ASCII designation is mangled on the way in or out | **Held, but untested — F3.** Fixed; an accented and a micro-sign designation now round-trips command line -> file -> selector. |
| 12 | A deleted material leaves a stale mass rather than a refusal | **Held, but untested — F4.** Fixed. |
| 13 | A failed edit half-writes the file | **Held.** Atomicity is the spine's: `runEdit` saves only on success. Proved on the FILE (byte comparison in-process, and a second process re-reading in `cli.material.atomic.wrote-nothing`). Mutation-tested (M5). |
| 14 | A not-ready completeness report exits 0 | **Held.** Mutation-tested (M3). |
| 15 | "Could not answer" is indistinguishable from "answered: not ready" | **Held, with a contract that has two shapes — recorded, see F5.** |
| 16 | A usage error carries a machine code that implies a document state | **Held.** `coded()` drops the code when the failure is a usage error. |
| 17 | The CLI invents a default when data is missing | **Held.** No property acquires a value it was not given. The only generated value is an object NAME (`Material1`), from the existing `freeName`, which is not engineering data. |
| 18 | The CLI has its own property vocabulary, drifting from the file | **Held by a test through the real writer.** Every storable code is asserted to be a key in a saved document; the derived pair is asserted absent from it. |
| 19 | Lifting the persisted key table into core to share it | **Rejected before writing it.** `kMechanicalKeys` has 9 entries for 11 kinds *because* ADR-027 stores no derived modulus; a shared table would need `shear_modulus` and would turn that structural guarantee into a convention. AUDIT.md finding 2. |
| 20 | A second CLI framework, argument parser or document model | **Held.** No new mechanism: the command table, the edit spine, the batch driver, the selector grammar and the process-test harness are all the existing ones. |
| 21 | An affine `degC` conversion implemented in the CLI | **Held by refusal.** A `UnitScale` is a ratio; there is no offset anywhere. Temperatures are kelvin, and adding `degC` here would be a conversion existing only in the CLI. AUDIT.md finding 3. |
| 22 | A dimensionless property silently accepts an angle | **Found and fixed while writing.** `parseSiValue` against a dimensionless unit would have taken `0.3 rad` for a Poisson ratio and turned `0.3 deg` into 0.0052, because the catalog's only dimensionless entries are angles. `parsePlainNumber` refuses any trailing symbol. Tested. |
| 23 | A hardness is stored without its scale | **Held.** A bare number is refused, naming the four scales: 60 HRC and 60 HRB are different hardnesses. |
| 24 | `--configuration` on a material report implies materials are configuration-dependent | **Held by omission.** Only `mass-properties` takes it, because only geometry is configuration-dependent. ADR-026 makes an assignment configuration-independent and no material command offers the option. |
| 25 | A stale or ambient binary answers the process tests | **Held.** `$<TARGET_FILE:bettercad_cli>` is the absolute path of the binary this preset built; no PATH lookup exists to be ambiguous. The harness's no-op rebuild stage proves nothing was left to build. |
| 26 | A filtered test run reports PASS having executed nothing | **Held.** Per-preset `ctest` is unfiltered; the repeat filter is `[A-Za-z]`, which matches every test. Counts are recorded in README.md and cross-checked against the suite total. |
| 27 | Report order comes from a container's iteration | **Held.** Materials in ascending ID from the document's ordered map; properties from a `constexpr` table in enumeration order; issues in core's defined order. Tested by building two materials in reverse name order. |
| 28 | Provenance changes a number | **Held.** The same values print with and without `--provenance`; and editing a value clears the citation that described the old one, which is `setMaterialMechanical`'s rule, not the CLI's. |
| 29 | An uncited value gets a fabricated source | **Held.** It prints `none`. Not a blank, which would read as a formatting fault. |
| 30 | The batch driver needed material-specific code | **Held.** `BatchCommand.cpp` gained nothing but the shared diagnostic-code line. A verb joined to the registry is a batch verb; the committed script proves it with five material edits in one transaction. |

## Findings

### F1, F2, F3, F4 — four true gaps in coverage, all closed

Each was a behaviour I had reasoned was correct and had not tested. None turned out to
be broken, and all four now have a test:

* **F1** a designation must match exactly — four folding and padding cases;
* **F2** the configuration-override refusal must hold through the CLI — the carried
  P15-MASS-001 defect must not become answerable just because the question arrived
  from a command line;
* **F3** a non-ASCII designation must survive command line -> file -> selector;
* **F4** deleting the assigned material must make the mass a refusal.

F2 is the one that mattered most. The guard exists because a configuration override
does not currently rebuild geometry, so the volume in hand would be the base
configuration's — and a guard that holds only when called in-process is not a guard.

### F5 — the stdout/stderr contract has two shapes, and saying "one rule" would be false

For a whole-report query the rule is clean, and is tested:

```text
answered            -> report on stdout, exit 0 if good, 1 if not ready
could not answer    -> NOTHING on stdout, diagnostic and code on stderr, exit 1
```

`mass-properties` is a **per-body** report and cannot follow it literally: with two
bodies where one computes and one does not, the one that computed must still appear.
So its contract is:

```text
could not start at all (file, selector)   -> nothing on stdout, stderr, exit 1
started                                   -> every body listed; a body that could not
                                             be computed is marked `unavailable: <why>`
                                             on stdout AND its code goes to stderr;
                                             exit 1 if any body failed
```

Recorded rather than smoothed over. A single stated rule that the commands did not
both obey would be worse than two rules that they do.

### F6 — no multi-body mass test exists

Every mass test here has one body, so the "one body fails, another succeeds" path is
reasoned (`status` is never reset to `Success`) but not exercised. Constructing a
document where one body regenerates and another does not, without weakening anything,
needs a fixture this milestone does not have. **Recorded as a known limitation, not
claimed as tested.**

## Mutation testing

Six mutations, each applied to the real tree, built, and the material CLI tests run.
Every one was caught.

```text
M1  ambiguity branch disabled (take the first match)     1 test failed
M2  render() returns "0" instead of "UNKNOWN"            1 test failed
M3  completeness gate always returns Success             2 tests failed
M4  volume printed in cm^3 while labelled mm^3           3 tests failed
M5  runEdit saves the document before reporting failure  3 tests failed
M6  bulk_modulus silently accepted instead of refused    3 tests failed
```

M6 needed two attempts and the first attempt is itself a result: deleting the refusal
outright **does not compile**, because the helper that produces it becomes unused under
`-Werror=unused-function`. The compiler holds that guard before any test does. The
mutation that does compile — refusing the shear modulus but silently accepting the bulk
modulus — was then caught by the tests.

**A process finding worth more than the mutations.** My first mutation run piped the
revert step to `/dev/null`. The revert of M3 was *correctly refused* — its replacement
text, `return ExitCode::Success;`, occurs five times in the file, so the script would
not guess which one — but the refusal message went to the null device, and the mutation
stayed in the tree for three further builds. It was caught by re-running the tests
after reverting, which is the only reason it did not reach the freeze. **A mutation
revert has to be verified, never assumed**, and the verification is a grep for each
mutation site, which is now what was done.

## Conclusion

Five findings of mine, all resolved: four coverage gaps closed with tests, one contract
documented honestly instead of being stated as something it is not. One known
limitation recorded (F6). One defect found and fixed during implementation (attack 22),
before any test existed to find it.
