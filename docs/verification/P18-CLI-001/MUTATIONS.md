# P18-CLI-001 — mutation campaign

## Method

A probe edits one production file in place (a `Find` text replaced by a `Replace` text, each read from a file; a probe whose `Find` does not occur is *not applied*, never counted), rebuilds `bettercad_tests` (debug-ext, `-Werror`), runs `ThermalCli_*` and `ThermalRun_*` (105 test cases, 6013 assertions) **and** the ownership gate `CheckThermalCliOwnership.cmake` on the mutated tree, then **restores the file** and records whether the restore is byte-identical. A probe is *killed* when the tests fail or the gate fails, or when the mutant does not compile *because the gate's rule would also have rejected it* (CR14, CR15). A probe that does not compile for an unrelated reason is **void**, not a kill. The five production files' git blob hashes were recorded before the first probe (`qualification/mutations/cli-premutation-hashes.txt`) and re-checked after the last: all five are identical.

Driver, per-probe script, premutation hashes, the progress log and each probe's `Find`/`Replace`/log are in `qualification/mutations/`.

## Result

**45 probes, 45 killed, 0 survivors, 0 void** — after two things the first pass exposed (below).

| | |
|---|---|
| killed by the tests and the gate | 16 |
| killed by the tests alone | 22 |
| killed by the gate alone | 5 |
| killed by the gate (the mutant does not compile either) | 2 |

Seven probes are killed by the gate alone (CR03, CR04, CR14, CR15, CR18, CR20, CK04). That is deliberate and is the reason the gate exists: they are defects a behavioural test **cannot** see, either because the input that exposes them cannot be produced through the shipped executable or because the defect is a second implementation that happens to give the right answer.

* **CR03 / CR04** (an id derived from a position): the mesher numbers nodes and elements from 1 without gaps, so `index + 1` equals the real id on every mesh the CLI can be given. `MeshBuilder` *does* allow sparse ids, and the brief forbids relying on the coincidence, so the gate pins how an id becomes a number (rule 20) and how an ordinal is found (rule 21).
* **CR14** (a banner on stdout), **CR15** (an extremum bypassing the finite-checking writer), **CR18** (the non-finite guard removed): the core refuses every overflow before a non-finite value can reach a printer, so no document makes the guard observable; the writer's behaviour is unit-tested on hand-built input (CO01/CO02 are killed by tests) and the *use* of the writer is pinned by the gate's exact counts.
* **CR20** (a residual recomputed in the CLI): gives the right number, so only the structure shows it.
* **CK04** (`accepted()` = the validator only): the validator already rejects a run that did not complete, so the two definitions agree on every input; the header spelling is pinned (rule 19) because the brief says exactly what success is.

## The first pass, kept for the record

The first campaign (45 probes, 2026-10-25 16:51–18:54) ended with **3 survivors and 1 void**. None was a hole in the CLI's behaviour; each was resolved as follows, and the four probes were then re-run (19:00–19:13).

| Probe | First result | Cause | Resolution | Re-run |
|---|---|---|---|---|
| CR03 | survived | equivalent on every reachable mesh (contiguous ids), see above | gate rule 20 + 2 fixtures | killed by the gate |
| CR04 | survived | same | gate rule 21 + 2 fixtures | killed by the gate |
| CE07 | survived | **my probe was inert**: the replacement kept the `return` and appended a dead `else if (false)` — it changed nothing | probe rewritten to give a missing `--face` a default face (the real defect); the existing tests that give `--flux` without `--face` and expect exit 2 kill it | killed by the tests (3 cases failed: `ThermalCli_InvalidLoadFailsAtomically`, `ThermalCli_ACommandLineFaultHasOneExitCodeWhateverTheDocumentHolds`, `ThermalCli_ABatchWithAnUnreadableLineWritesNothing`) |
| CK05 | void | **my probe did not compile**: `(void)stage; (void)error;` left `run` unused under `-Werror=unused-parameter` | probe rewritten with `(void)run;` | killed by the tests (12 cases failed) |

Two of the four were mistakes in the probes, not gaps in the product, and are reported as such rather than counted as kills of the original text. The two real gaps led to **gate rules 20 and 21** (`CheckThermalCliOwnership.cmake`) and four new fixture trees (102 violating fixtures in all, 104 ctest entries for the gate). Because the gate changed after the first pass, the whole gate fixture set was regenerated and re-verified (each tree must produce **exactly one** violation naming its rule), and the gate's pass on the real tree was re-run.

## Limits

* Mutants are injected into the five new thermal-CLI files only. The qualified thermal core, and the shared CLI files that merely register the verbs, are not mutated here: their own milestones qualified them.
* The suite run per probe is `ThermalCli_*` + `ThermalRun_*` + the gate in debug-ext. The workflow (`cli.thermal.workflow`) and the process tests are not re-run per probe (cost: ~1 min each); they are run in full in the three-preset qualification.
* A kill by the gate is a kill of the *text*, not of the behaviour. Where the behaviour could not be reached by any test the table says so.

## All 45 probes
| Probe | The defect injected | Killed by |
|---|---|---|
| CR01 | the temperature minimum and maximum are swapped | KILLED by tests |
| CR02 | the minimum tie-break is the DESCENDING id | KILLED by tests and gate |
| CR03 | a node id is the ordinal plus one (NodeId == index + 1) | KILLED by gate |
| CR04 | an element is found by id minus one instead of the stored ordinal | KILLED by gate |
| CR05 | the printed heat-flux vector has its x sign inverted | KILLED by tests and gate |
| CR06 | the gradient magnitude is reported squared | KILLED by tests and gate |
| CR07 | thermal-validate exits 0 whatever the verdict | KILLED by tests and gate |
| CR08 | thermal-balance exits 0 whatever the verdict | KILLED by tests and gate |
| CR09 | success means a solution exists (solver success), not that the analysis is accepted | KILLED by tests and gate |
| CR10 | the result commands never refuse | KILLED by tests |
| CR11 | a load whose face does not resolve reports faceResolves true | KILLED by tests and gate |
| CR12 | thermal-validate always reports stoppedAt as null | KILLED by tests |
| CR13 | the stored times are generated as frame times a nominal step | KILLED by tests and gate |
| CR14 | a progress banner is written to stdout before the solve payload | KILLED by the gate (the mutant does not compile either) |
| CR15 | an extremum value bypasses the finite-checking writer | KILLED by the gate (the mutant does not compile either) |
| CR16 | the reaction count is off by one | KILLED by tests |
| CR17 | a refusal is reported under the first finding of ANY severity | KILLED by tests |
| CR18 | the non-finite guard of thermal-solve is removed | KILLED by gate |
| CR19 | thermal-validate always reports accepted true | KILLED by tests and gate |
| CR20 | a heat-balance residual is recomputed in the CLI as a sum of the printed terms | KILLED by gate |
| CR21 | the validation checks are listed in reverse order | KILLED by tests |
| CR22 | the analysis start time is the first STORED time | KILLED by tests |
| CR23 | the extremum tie-break is deleted from the maximum | KILLED by tests and gate |
| CE01 | a surface flux is made positive on the way in (abs) | KILLED by tests and gate |
| CE02 | a convection coefficient is made positive on the way in (abs) | KILLED by tests and gate |
| CE03 | a time step is clamped to a floor | KILLED by tests and gate |
| CE04 | a refused command is reported as success | KILLED by tests |
| CE05 | a missing id is reported under the duplicate-id code | KILLED by tests |
| CE06 | a non-numeric id is read as 0 instead of being a usage error | KILLED by tests |
| CE07 | a surface flux without a face is given a default face | KILLED by tests |
| CE08 | a volumetric generation silently accepts a face | KILLED by tests |
| CE09 | an analysis without --mode is transient | KILLED by tests |
| CE10 | an unparseable face falls back to the empty reference | KILLED by tests |
| CE11 | a prescribed temperature silently accepts an ambient | KILLED by tests |
| CE12 | an id above the file cap is accepted by the command line | KILLED by tests |
| CE13 | time settings no longer need --output (the data model default is taken) | KILLED by tests |
| CE14 | a name clash is reported as a duplicate id | KILLED by tests |
| CK01 | the validator is handed nothing derived | KILLED by tests and gate |
| CK02 | every failure is handed to the validator as a solve failure | KILLED by tests |
| CK03 | accepted() is a completed chain only (solver success) | KILLED by tests and gate |
| CK04 | accepted() is the validator only | KILLED by gate |
| CK05 | the refusal of a link is not recorded | KILLED by tests |
| CK06 | a transient analysis is run through the steady links | KILLED by tests |
| CO01 | a non-finite number is written through to the payload | KILLED by tests |
| CO02 | a non-finite number is replaced by null but not remembered | KILLED by tests and gate |
