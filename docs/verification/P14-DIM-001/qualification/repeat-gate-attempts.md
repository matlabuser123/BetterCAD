# The Debug determinism repeat: four attempts, and what each is worth

The Debug `--repeat until-fail:5` stage failed on its first run. This records
every attempt after it, what each one is evidence of, and what it is not, so
that the audit trail does not have to be reconstructed from timestamps.

## Run A — the qualification harness

```text
launched  run-qualification.cmd -> qualify.cmd, under chcp 65001
log       ctest-repeat-debug.log
recorded  qualification-times.txt: "repeat debug exit 8"
result    FAIL
```

```text
cli.assembly.batch  ***Failed
  exit code: 1 (expected 0)
  stderr: bettercad-cli batch: cannot replace
          '.../build/debug/tests/cli-output/assembly/built.bcad':
          Permission denied
```

Nine dependent tests were then `Not Run`, because they read the file that test
writes. **A valid run of the gate, and it failed.**

The CLI produced no wrong answer: it could not open the file for writing. The
build tree is under `OneDrive\Desktop\CAD\build\`, and OneDrive opens files to
sync them, as does the virus scanner on newly written files.

## Run B — direct ctest, wrong code page

```text
launched  ctest --preset debug ... --repeat until-fail:5, from Bash
log       ctest-repeat-debug-second.log
result    1879/1880 passed
```

**Not a valid run of the gate.** It was launched from Bash, where `chcp 65001`
does not reach the child console, so it ran under code page 437 — and
`cli.new.unicode-path` fails under 437 and passes under 65001 (verified
separately on this tree, twice). That is an error in how it was launched, not
a result.

**It is still evidence of one thing, and the thing that mattered at the time:**

```text
cli.assembly.batch   5 / 5 PASSED
```

The failure under investigation did not reproduce. That is what justified
running the gate again rather than declaring the milestone BLOCKED on one
observation.

## Run C — a malformed invocation; no result of any kind

```text
launched  cmd /c "chcp 65001 > nul && ctest ... & echo EXIT=%errorlevel%"
          from Bash
output    The system cannot find the path specified.
          EXIT=0
log       NONE — the file was never created
```

**A non-result.** The `&` and the quoting did not survive the Bash-to-`cmd`
translation, so `ctest` never started. Proved by three things, not by
assertion:

1. **No log file exists.** The redirect target
   `ctest-repeat-debug-third.log` was absent afterwards; `tail` reported
   "No such file or directory". A ctest run that started would have created
   it before running anything.
2. **The output is a shell error, not a test error.** "The system cannot find
   the path specified" is `cmd` failing to resolve the command, and there is
   no ctest banner, no test count and no summary line.
3. **It returned in under a second.** The gate takes about sixteen minutes;
   nothing that could be called a run finishes that fast.

This attempt is recorded because it happened, and it is counted neither as a
pass nor as a failure, because no test was executed.

## Run D — the valid rerun

```text
launched  PowerShell, chcp 65001 confirmed, same filter and flags as Run A
log       ctest-repeat-debug-third.log
result    PASS -- 100% of 1880 tests passed, ctest exit 0

cli.assembly.batch passed all five repeats, and nothing else failed. The
permission failure did not reproduce under the conditions that produced it.
```

## What is NOT being done

The gate is not being re-run until it goes green. Run A is a failure and stays
in the evidence as one. Run B was invalid as a gate but is kept for its
diagnosis. Run C executed nothing. Run D is the one controlled rerun, and it passed.

## What this leaves

The Debug determinism repeat is a PASS on Run D, with Run A's failure kept in
the evidence rather than erased. The failure was a filesystem permission
error, not a wrong answer, and it did not reproduce: `cli.assembly.batch`
passed 5/5 in Run B and 5/5 again in Run D, which itself passed 1880/1880.

It is nonetheless a real intermittent hazard, and its cause is structural: the
build tree lives under OneDrive. **The remediation -- moving build and test
output off the synchronised directory -- is recorded as the next infrastructure
decision and was NOT done here**, because the build directory is set in
`CMakePresets.json`, which is inside the frozen qualified tree. Changing it
would void this qualification and alter a file three milestones have been
qualified against, so it needs its own decision and its own requalification.
