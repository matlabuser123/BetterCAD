# The first qualification run, which failed and voided the first freeze

Kept because `../../FREEZE.md` cites it. Three files, 12 KB:

```text
run-qualification.out     the harness's own verdict sentence
qualification-times.txt   every stage's exit code and the two tree readings
the-failing-gate.txt      the one failing gate's captured output, all three
                          presets, with the compiler command lines stripped
```

```text
QUALIFICATION FAILED: 3 stage(s) failed.
compile_fail.cfdids.a-result-cannot-be-built-without-its-factory
    1 of 6903, identical in debug-ext, release-ext and debug-shared-ext
the launcher reported "completed (exit code 0)"
```

**THE FULL LOGS OF THIS RUN ARE NOT KEPT.** Three ctest logs at 1.6 MB and two
repeat logs at 3.9 MB, about 13 MB, none of which is cited: the run's only
finding is the one gate above, and its output is here in full. The second run's
logs sit in the parent directory and are the ones the evidence reconciles
against. `CLAUDE.md` asks that large generated files not be committed without a
reason, and "a run that was voided" is not one.

What the gate found, why widening its regex was refused, and what the blast
radius missed are in [FREEZE.md](../../FREEZE.md), last section.
