# Attempt 1 — VOID

Kept because it is evidence that the determinism gate works, not because it passed.

```text
started   2026-09-29 07:55:29
finished  2026-09-29 11:08:05   (3h 12m)
result    2 stage(s) failed, exit 2
```

Everything else passed. All three presets configured, cleaned, rebuilt with `-Werror`,
rebuilt again to nothing, and ran the whole suite:

```text
debug-ext          configure 0  clean 0  build 0  no-op 0  ctest 0
release-ext        configure 0  clean 0  build 0  no-op 0  ctest 0
debug-shared-ext   configure 0  clean 0  build 0  no-op 0  ctest 0
repeat release-ext selects 2764   exit 8   1 test failed
repeat debug-ext   selects 2764   exit 8   1 test failed
```

Both repeat stages failed on the same test, `cli.material.ambiguous.clone`, and the
defect was mine:

```text
exit code: 1 (expected 0)
stderr:    bettercad-cli material-clone: the name 'Steel3' is already used
```

**`--repeat until-fail:N` re-runs each test N times BACK TO BACK, not the whole set N
times.** The log shows it plainly — `Start 2559 ... Passed` immediately followed by
`Start 2559 ... Failed`. A required fixture does NOT re-run between those passes, so
"the setup will restore the document" is not true, and every test has to survive being
run again immediately, on its own.

`cli.material.ambiguous.clone` was the one test in this milestone that MUTATES rather
than reading or being refused, and it did not copy its own pristine input. The fix is
the pattern every other producer here already used: `COPY_FROM` a template the fixture
builds.

Two things follow, and both are worth more than the fix:

1. **The gate earned its three hours.** Every per-preset `ctest` passed 2764/2764 in
   all three presets. Only the repeat stage found this, which is exactly the class of
   defect it exists to find.
2. **The pre-freeze checklist was missing a cheap step.** Running
   `ctest -R cli.material --repeat until-fail:5` on the new tests takes 17 seconds and
   would have caught this before the freeze. It now goes in the checklist beside
   `git diff --check` and the shared build.
