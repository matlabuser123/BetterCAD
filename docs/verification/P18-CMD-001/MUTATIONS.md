# P18-CMD-001 — mutation campaign

```text
44 PRODUCTION MUTANTS  -> 44 killed. 2 SURVIVED on the first run and led to a new test (below). 0 survivors at the end. 0 left unclassified.
14 GATE MUTANTS        -> 14 killed. 1 SURVIVED on the first run and led to a new fixture (below).
Every probe restored its file; the eight-path tree and the sha256 of the mutated source were identical before and after (below).
```

## Method

A mutation is a one-line change to the code under test. A test suite that is worth anything fails when the code is wrong; a mutant that the suite does not fail is a statement the tests make no claim about.

* **Driver:** [mutations/mutation-driver-cmd.ps1](qualification/mutations/mutation-driver-cmd.ps1) reads the `Find` and `Replace` text from files (so double quotes survive a native command line), applies the change with
  [mutate-t.ps1](qualification/mutations/mutate-t.ps1), rebuilds `bettercad_tests` (`debug-ext`, `-Werror`), runs `ThermalCommand_*` with a timeout, **reads the verdict from the test output**, and restores the file.
* **Verdicts:** `KILLED` = a test case fails (or the runner crashes after failed assertions, which is also a failed run); `SURVIVED` = "All tests passed"; `VOID` = the mutant did not compile (the probe is wrong, not the suite).
* **Nothing else moved:** no `src`, `include`, `tests`, `apps` or `cmake` file was edited while a campaign ran.
* **Restore proof:** the `git` tree of the eight frozen paths, built from a scratch index, was `78c2ed422d2db7799e32f86febe53f20c642989a` before the first probe and again after the production campaign
  ([cmd-premutation-freeze.txt](qualification/mutations/cmd-premutation-freeze.txt), [cmd-postmutation-freeze.txt](qualification/mutations/cmd-postmutation-freeze.txt)); the sha256 of `ThermalCommands.cpp`
  (`679CE388…E5E9D`) was the same before the campaign and after the last pass. Each probe's own line says `restored`.
* **Passes:** pass 1 ran all 44; the verdict logic then needed one extension (below), so the 3 that had no verdict line and the 7 that did not compile were corrected and re-run in pass 2 and pass 3; pass 4 re-ran the two
  survivors after their test was added. The progress files of every pass are kept beside the driver.

### Two things that went wrong in the harness, and what was done

1. **No verdict line.** A mutant that scrambles a vector can crash the Catch2 runner *after* it has printed failed assertions and *before* it prints its summary. `mutate-t.ps1` called that `UNKNOWN`. It now
   records "assertions failed and the run ended without a summary line" as `KILLED`, because a run that prints `FAILED:` and dies is a failed run; CMD01–CMD03 were re-run under the corrected logic.
2. **Probes that did not compile.** Replacing `if (const Result<void> named = f(x); !named.has_value()) {` by `if (false) {` removes the variable that the next line uses, and `-Werror` refuses an unused
   parameter or loop variable. Those mutants never ran, so they proved nothing; the probes were rewritten to keep every variable alive and re-run (CMD23, CMD32, CMD33, CMD35, CMD37, CMD38, CMD39).

## The production mutants

| Id | Mutation (applied to `src/thermal/ThermalCommands.cpp`) | First run | Final |
| --- | --- | --- | --- |
| CMD01 | an added load is inserted at the front, not appended | no verdict line (runner crashed; verdict logic then extended) | killed: assertions fail and the runner crashes |
| CMD02 | an added condition is inserted at the front, not appended | no verdict line (runner crashed; verdict logic then extended) | killed: assertions fail and the runner crashes |
| CMD03 | a load edit moves the record to the end instead of replacing it in place | no verdict line (runner crashed; verdict logic then extended) | killed: assertions fail and the runner crashes |
| CMD04 | a condition edit moves the record to the end instead of replacing it in place | same | killed: 1 of 94 cases fail |
| CMD05 | an edit of a load that does not exist creates it | same | killed: 6 of 94 cases fail |
| CMD06 | a duplicate load id is accepted | same | killed: 2 of 94 cases fail |
| CMD07 | a duplicate condition id is accepted | same | killed: 2 of 94 cases fail |
| CMD08 | removing a load that does not exist succeeds silently | same | killed: 3 of 94 cases fail |
| CMD09 | removing a condition that does not exist succeeds silently | same | killed: 4 of 94 cases fail |
| CMD10 | removing a load removes the last load | same | killed: assertions fail and the runner crashes |
| CMD11 | removing a condition removes the first condition | same | killed: 1 of 94 cases fail |
| CMD12 | a load is addressed by its raw id as a position | same | killed: 7 of 94 cases fail |
| CMD13 | the recorded before-state is the after-state | same | killed: 36 of 94 cases fail |
| CMD14 | a refused apply leaves the command staged as executed | SURVIVED | killed: 1 of 95 cases fail |
| CMD15 | a no-op records the requested definition instead of what the document holds | same | killed: 1 of 94 cases fail |
| CMD16 | a no-op is reported as a change | same | killed: 2 of 94 cases fail |
| CMD17 | undo no longer checks that the analysis is as the command left it | same | killed: 1 of 94 cases fail |
| CMD18 | redo no longer checks that the analysis is as the command found it | same | killed: 1 of 94 cases fail |
| CMD19 | undo assigns the after-state (undo does nothing) | same | killed: 39 of 94 cases fail |
| CMD20 | redo assigns the before-state (redo does nothing) | same | killed: assertions fail and the runner crashes |
| CMD21 | a refused apply is reported as success (the command is recorded) | SURVIVED | killed: 1 of 95 cases fail |
| CMD22 | a redo of a create makes a NEW analysis with a new id | same | killed: 3 of 94 cases fail |
| CMD23 | an undo of a create leaves the analysis in the document | void (the mutant did not compile; probe corrected) | killed: 4 of 94 cases fail |
| CMD24 | a create silently drops the loads of its definition | same | killed: 2 of 94 cases fail |
| CMD25 | a solver-settings command lands only the pivot floor | same | killed: 3 of 94 cases fail |
| CMD26 | a transient-settings command keeps the old output policy | same | killed: 5 of 94 cases fail |
| CMD27 | an initial temperature is shifted by half a kelvin on its way in | same | killed: 3 of 94 cases fail |
| CMD28 | a steady analysis accepts an initial temperature (mode not checked) | same | killed: 2 of 94 cases fail |
| CMD29 | a steady analysis accepts transient settings (mode not checked) | same | killed: 2 of 94 cases fail |
| CMD30 | the initial-temperature value is checked before the mode | same | killed: 1 of 94 cases fail |
| CMD31 | the transient-settings value is checked before the mode | same | killed: 1 of 94 cases fail |
| CMD32 | the named-feature rule never fires | void (the mutant did not compile; probe corrected) | killed: 1 of 94 cases fail |
| CMD33 | add-load no longer requires a named feature | void (the mutant did not compile; probe corrected) | killed: 1 of 94 cases fail |
| CMD34 | edit-load no longer requires a named feature | same | killed: 1 of 94 cases fail |
| CMD35 | add-condition no longer requires a named feature | void (the mutant did not compile; probe corrected) | killed: 1 of 94 cases fail |
| CMD36 | edit-condition no longer requires a named feature | same | killed: 1 of 94 cases fail |
| CMD37 | a create no longer requires its loads to name a feature | void (the mutant did not compile; probe corrected) | killed: 1 of 94 cases fail |
| CMD38 | a create no longer requires its conditions to name a feature | void (the mutant did not compile; probe corrected) | killed: 1 of 94 cases fail |
| CMD39 | the command layer drops the record validator of an added load (defence in depth) | void (the mutant did not compile; probe corrected) | killed: 1 of 94 cases fail |
| CMD40 | a missing analysis is reported as an invalid argument | same | killed: 2 of 94 cases fail |
| CMD41 | a diverged undo is reported as not found | same | killed: 1 of 94 cases fail |
| CMD42 | a steady-mode refusal is reported as an invalid argument | same | killed: 3 of 94 cases fail |
| CMD43 | the load description loses its id | same | killed: 1 of 94 cases fail |
| CMD44 | the initial-temperature description shows the wrong unit | same | killed: 1 of 94 cases fail |

## The two that survived, and the test that closes them

CMD14 (a refused `apply` leaves the command staged as executed) and CMD21 (a refused `apply` is reported as success, so the command is recorded) both survived the 94-case suite. They sit on the same branch:
*the document refuses a definition that the command's own checks accepted*. Every real edit validates what it changes before deriving a definition, so that branch cannot be reached through any of the ten commands —
which is exactly why nothing exercised it. The branch is not dead code: it is the last line of defence (`setDefinition` is the authority on a valid definition, and a rule added to it later must not be able to
leave a half-executed command behind).

`ThermalCommand_ADefinitionTheDocumentRefusesLeavesTheCommandUnexecuted` reaches it with a test-only command **derived from the edit base** (the base's constructor is protected for exactly that reason) whose `edit`
returns a definition carrying the invalid mesh-control handle. It requires: the command is refused with `InvalidArgument`; through the history nothing is recorded and a pending redo entry survives; driven
directly, `before()`/`after()` are empty, `changedAnything()` is false and its own undo and redo say "not executed"; the document is unchanged. Re-run, CMD14 and CMD21 are both killed.

## What a mutation campaign on this layer cannot do, and what covers it instead

The properties that matter most here are *absences*: the history holds no mesh identity, no DOF index, no solution, no post result, no report. A mutant cannot "add a derived member" and still compile, because the
types are not declared where a command is declared (thirteen compile-fail cases) and the gate forbids the includes. So the absences are checked the only way they can be: by the compile-fail group (52 cases, with
a control that must compile), and by mutating the **gate**.

## The gate mutants

The ownership gate (`CheckThermalCommandOwnership.cmake`) runs against the real tree and 15 fixtures. A rule that is disabled or narrowed must make a fixture test fail; if none does, the rule is not pinned.
Driver: [mutations/gate-mutation-driver-cmd.ps1](qualification/mutations/gate-mutation-driver-cmd.ps1).

| Id | Rule disabled or narrowed | Verdict | Fixture test(s) that fail |
| --- | --- | --- | --- |
| GATE01 | rule 1: a quoted include is allowed | KILLED | quoted-include |
| GATE02 | rule 1: any bettercad header may be included | KILLED | includes-solver |
| GATE03 | rule 1: a third-party header may be included | KILLED | includes-third-party |
| GATE04 | rule 2: derived work may be named | KILLED | solves |
| GATE05 | rule 3: a mesh identity may be named | KILLED | mesh-identity |
| GATE06 | rule 4: a record may be addressed by position | KILLED | positional |
| GATE07 | rule 5: a number may be parsed from text | KILLED | parses-text |
| GATE08 | rule 6: a second authority / non-deterministic construct is allowed | KILLED | stale-flag, unordered |
| GATE09 | rule 7: the one path need not be used | KILLED | bypasses-path |
| GATE10 | rule 8: a second owner of thermal commands is allowed | KILLED | second-owner |
| GATE11 | rule 8: nobody is checked for editing around the commands | KILLED | app-edits, io-edits |
| GATE12 | rule 8: the scope falls back to the renderer only | KILLED | io-edits |
| GATE13 | rule 8: a qualified modifyObject<bettercad::thermal::ThermalAnalysis> is not seen | KILLED (first run: SURVIVED) | qualified-edit |
| GATE14 | rule 8: a direct setDefinition call is not seen | KILLED | io-edits |

GATE13 survived the first run: the widened rule 8 matches `modifyObject<bettercad::thermal::ThermalAnalysis>` with any qualification, but no fixture used a qualified name, so narrowing the pattern back to
`(thermal::)?` changed nothing a test could see. The fixture `thermalcmd-qualified-edit` (a `modifyObject` with the fully qualified type and no `setDefinition`) closes it; it brings the gate to 16 tests.

## Honest limits

* A mutant that is *equivalent* to the original would survive; there were none to classify. CMD39 was expected to be one (the command re-runs the record validator that `setDefinition` also runs) and is not: the
  command's validator produces the specific diagnostic and the error code, and a test reads it.
* The campaign mutates `ThermalCommands.cpp`. The header is not mutated (every header edit rebuilds most of the project); its content is held by the compile-fail group, the schema fingerprint in
  [FREEZE.md](FREEZE.md) and the runtime tests that construct every command.
* The currentness tests (which really solve) are inside the 95 cases that every mutant ran against, so a mutant that, for example, staled the wrong layer would also have been caught there.
