# P17-VIZ-001 — the display surface's qualification freeze

```text
RESULT:  PASS. Every stage exited 0, 0 stages failed.
         qualify.cmd exit 0
         16:35:19 -> 19:48:49 on 2026-10-12, 3h13m

         THIS IS THE THIRD ATTEMPT. Two were aborted deliberately, and
         ABORTED.md in qualification-display/ records why.
```

## The qualified trees

Recorded by `qualify.cmd` **before the first build** and recomputed after the
last test run. All eight identical:

```text
apps              b49722470580d4fe6c2bb2b3594bb3885eee10db
include           240bcc208b0dc4aedb146609fe0399dbed578953
src               8f2f75bd14104b422f827592468186df3b345476
tests             d8250b5d068bcc1fe8c66088565707bb0c82bc30
examples          75ce1c6852e03011f350d6806f4e8d5f3207d63e
cmake             5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt    3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

Cross-checked by a **second, independent method** — `find | sha1sum` over each
path, recorded before the run and recomputed after — which also reports all
eight unchanged. The tree that was qualified is the tree that is committed.

Qualification candidate: `b04a9e158ff4cb8e2841db16e3914ac8731d94a6`.

## Stages

| preset | configure | clean | build | no-op rebuild | ctest |
| --- | --- | --- | --- | --- | --- |
| debug-ext | 0 | 0 | 0 | 0 | **3806 / 3806** |
| release-ext | 0 | 0 | 0 | 0 | **3806 / 3806** |
| debug-shared-ext | 0 | 0 | 0 | 0 | **3806 / 3806** |

```text
build warnings   0 in all three, with -Werror on
build errors     0 in all three
StructuralViz_   47 run and passed in EACH preset
```

**3806 reconciles from both ends**: 3792 at `b04a9e1` plus the fourteen cases
this increment added, with the viz set going 33 → 47. The commit message for
the previous run had a placeholder here rather than a prediction, which is why
the figure is the run's and not a guess — an earlier draft would have said
3798.

The **no-op rebuild** is the stage that proves the tested binaries came from
the frozen tree rather than an earlier build. It mattered more than usual
here: two aborted attempts had each left a partial build tree behind after
their clean stage.

## Determinism

| preset | selected | result |
| --- | --- | --- |
| release-ext | 951 | **951 / 951** |
| debug-ext | 951 | **951 / 951** |

Five repeats each. 951 was counted with `ctest -N` before the run and the
harness's own count agrees — it records `repeat <preset> selects 951 test(s)`
and treats a filter matching nothing as a failed stage, twice over.

Two presets because a verdict that depended on optimisation level would be a
defect: `-O0` and `-O2` reassociate floating point differently, and every value
this milestone publishes is a double.

## The error log

268 lines, **every one** a git `CRLF will be replaced by LF` warning about
pre-existing working-tree files. **Zero non-warning lines.**

The first qualification's `.err` held 270, and the difference is accounted for
rather than waved at: the only delta is `src/renderer/CMakeLists.txt`, present
twice there and absent here, because this milestone normalised that file to LF
and git emits the warning once per invocation while `qualify.cmd` fingerprints
the tree both before and after. 270 − 2 = 268.

## Pre-freeze checks

Seven, cheap before expensive:

```text
git diff --check                                clean
no mixed line endings in anything touched       4 CRLF, 3 LF, none mixed
no export macro on a constexpr or inline        0
ctest -R StructuralViz_ --repeat until-fail:5   47/47 x 5
the repeat filter recounted                     951
debug-shared-ext build + run                    links clean, 47/47
the full default target built                   so gui.* has an executable
```

The last one is there because an aborted clean deletes every build output, and
a later targeted build of `bettercad_tests` alone leaves `bettercad.exe`
missing — all ten `gui.*` tests then fail with *"exit code no such file or
directory"*, which reads exactly like a regression and is not one.

## And the probes ran BEFORE this run

83 probes over four batches, 74 killed, 7 unreachable, 2 equivalent mutants, 0
void, 0 compiler kills. Fifteen real test gaps found and closed.

The last batch alone found **four** gaps in code an earlier attempt at this run
was already certifying. That is what the two aborts bought, and it is why the
ordering is now recorded as a rule: the review and the probes come before the
qualification, not beside it.
