# P19-TURB-ARCH-001 — the qualification freeze

```text
FROZEN:    2026-11-10
MILESTONE: P19-TURB-ARCH-001 -- Turbulence Architecture
ADR:       ADR-079
```

`CLAUDE.md`: *"Freeze the source and test tree and record its identity before
qualifying. … If source or tests change after the freeze, the qualification is
void and is run again from clean."* This file is that record, written before the
harness was launched.

## The frozen surface

Eight paths, the same eight every milestone freezes:

```text
apps  include  src  tests  examples  cmake  CMakeLists.txt  CMakePresets.json
```

`TODO.md`, `ROADMAP.md` and everything under `docs/` are OUTSIDE it. They cannot
reach the executable or the tests, which is what makes it possible for this file
to exist at all and for the README's REGRESSION and QUALIFICATION sections to be
filled from the harness's logs afterwards.

## The eight identities, as frozen

Read through a scratch `GIT_INDEX_FILE` so the working tree's own index is never
touched: `git read-tree HEAD`, `git add -A` over the eight paths, then
`git write-tree --prefix=<dir>/` for each directory and `git ls-files -s` for the
two files.

```text
apps               fc398a3d2e524178cf38e8b667236379d8381086
include            4759b76839047d95cf096ae80bf071c52c7e3edb
src                c33a7b6fdbe7cb1b4deb66727152525e85dd4a48
tests              bf65a34a2027d1c3ceff8a3e8b3216c538a9165d
examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e
```

**`git rev-parse HEAD:<path>` IS NOT USED HERE, AND THAT IS DELIBERATE.** Before
the commit it reads the PREVIOUS commit's tree and would agree with itself for
the wrong reason. It is used only AFTER the commit, as the independent final
reading. `P19-STEADY-001` and `P19-TRANSIENT-001` both record that distinction
and this freeze keeps it.

## What moved, and it is one subtree

Read directly with `git rev-parse HEAD:<path>` against the committed tree at
`9d89f4d5002e0724323cc7340ea30c3cb7dc8419`:

```text
apps               fc398a3d   unchanged
include            4759b768   unchanged
src                c33a7b6f   unchanged
examples           4da234eb   unchanged
cmake              5a382115   unchanged
CMakeLists.txt     3ec3c10f   unchanged
CMakePresets.json  3229f0f8   unchanged

tests              47658ea5 -> bf65a34a
```

**SEVEN OF EIGHT BIT-IDENTICAL, AND THE ONE THAT MOVED IS `tests`.** That is not
a convenience; it is the milestone's central claim measured in hashes. `ADR-079`
decides the turbulence architecture **without types** — no mode field, no model
enumerator, no turbulence variable, no wall-treatment record, no result array, no
flow-regime query — so `include` and `src` being bit-identical to the committed
tree is the proof that the decision was really taken, and not merely written
down. `apps` unchanged says no GUI or CLI surface was touched. `examples`
unchanged says no reference model moved. `cmake` and the two root files unchanged
say no build policy changed.

The whole deliverable inside the frozen surface is therefore:

```text
tests/architecture/CheckTurbulenceOwnership.cmake     new, four rules
tests/architecture/fixtures/turbulence-*/             new, eleven trees
tests/architecture/CMakeLists.txt                     +12 registrations
tests/cfd/CfdTurbulenceArchitectureTests.cpp          new, 4 cases
tests/compile_fail/CfdTurbulenceMisuse.cpp            new, 14 gates
tests/compile_fail/CMakeLists.txt                     +14 registrations
tests/CMakeLists.txt                                  +1 source
tests/cli/ZeroMatchGuard.cmake                        +3 minimums
```

## The figures, counted off the frozen tree

By `ctest -N` and off the built binary, never predicted. `P19-GEOM-001`'s freeze
recorded getting that wrong once, and `P19-TRANSIENT-001`'s voided second attempt
was caught by a count of five.

```text
full suite, per preset   6995 = 6965 + 4 unit.CfdTurbulenceArchitecture_
                                      + 12 architecture.turbulence-ownership
                                      + 14 compile_fail.cfdturb
repeat set, per preset   3543 = 3527 + 4 + 12
repeat EXECUTIONS       17715 = 3543 x 5, per repeat log
architecture              217 = 205 + 12
compile_fail             1090 = 1076 + 14
```

The repeat filter is **unchanged**, and it was checked with `ctest -N -R` over the
filter rather than by reading the regex: `unit.CfdTurbulenceArchitecture_` matches
its existing `unit\.Cfd` branch and the twelve ownership tests match
`architecture`. `compile_fail` stays excluded from the repeat, as in every
milestone.

**THESE FIGURES ROSE TWICE AFTER REVIEW.** The launcher's first draft carried
6991 / 3539 / 214, counted off the tree before an adversarial review and a
mutation analysis. Between them they added a fourth runtime case (the nested
settings records), a zero-scan fixture, a `virtual` fixture and a bare-symbol
fixture. Corrected in place in the launcher's header, with the correction stated
there, and counted again here off the frozen tree.

## The subsystem arithmetic, which a passing suite cannot make

A green run says every assertion held. It does not say the right number of
assertions ran.

```text
[cfd]             755077 assertions in 701 test cases
                  = 755043 + 34   and   697 + 4        EXACT
[cfd][turbarch]       34 assertions in   4 test cases  the new suite
[cfd][transient]    1813 assertions in  34 test cases  IDENTICAL to baseline
[cfd][steady]       1263 assertions in  19 test cases  IDENTICAL
[linalg]            2560 assertions in  29 test cases  IDENTICAL
[structural]      271414 assertions in 415 test cases  IDENTICAL
[thermal]         595622 assertions in 1168 test cases IDENTICAL
```

Five tags identical to baseline is the check that a tests-only change really was
tests-only, and `[cfd][steady]` and `[cfd][transient]` being bit-identical is the
check that this milestone touched neither qualified solver. A pass/fail count
cannot see an assertion that stopped running; these can.

The post-gate **binary readout** is the stronger reading and is taken as well: it
reads the per-tag identity from each of the three QUALIFIED executables, which is
the tree that was actually run rather than the tree that was frozen.

## Pre-freeze state of the gates, on debug-ext

Every one of these was run on the frozen tree before the freeze:

```text
[cfd][turbarch]                 4 cases, 34 assertions, PASS
compile_fail.cfdturb            14 of 14 PASS
  + the control target          compiles with no BETTERCAD_CF_* macro
architecture.turbulence-*       12 of 12 PASS
cli.mesh.zero-match-guard       PASS with the three new minimums, 207 s
the checker, by hand            real tree 600 files / 0 violations; eleven
                                fixtures each exactly as registered
release-ext                     builds, 0 error / 0 warning
debug-shared-ext                builds, 0 error / 0 warning
```

The last two were built deliberately before freezing, and twice: once before the
review and again after it changed two test files. A configuration-specific
compile error found afterwards would void a multi-hour run, and
`P19-STEADY-001` has paid that price once.

### The gates were made to fail on purpose before they were believed

```text
M1   widen kCfdVariables only            caught ALREADY, by -Werror=switch
M2   widen both tables consistently      caught ALREADY, same two switch errors
M2b  widen both AND fix both switches    the new count pin is the ONLY failure
M3   a fourth treatment member           the new mirror and its binding both fire
M4   canonicalText emits a turbulence    the absence test fails at runtime
     line
```

`M1` and `M2` are why this milestone's G1 claim is narrower than its first draft:
the variable table was never "frozen by a comment", and the correction is in
`ADR-079` with these logs behind it. Each mutation was applied to a
copy-protected file, built, and restored with its content verified by `sha256`.

**AND THE RESTORE EXPOSED A HAZARD WORTH RECORDING.** `shutil.copy2` preserves
mtime, so a byte-identical restore left the sources looking OLDER than the
objects built from the mutated ones; ninja ran three edges and the linked binary
still held a four-variable `isRecognised`. Two of this milestone's own tests then
failed, which read exactly like a product defect and was not one. Both scripts
now `os.utime` after every restore.

## The harness

Carried forward unchanged. Five files are **byte-identical** to
`P19-TRANSIENT-001`'s, by raw `sha1sum`:

```text
cf6c1f112d71586098a78652091d0375c8d04051  launch.cmd
59910328ab43e07b9e0c474f969994ad483d3f68  preflight-space.cmd
b76d9325e8131c265443540a9ac914e236731a66  preflight-space.ps1
244b38de8feac0475af7cdc56ba9415d27d8dd29  qualify.cmd
afd0197d4f4726db7f533c2f27f4f6e069ba83c6  verify-harness.cmd
```

`run-qualification.cmd` is the only one that differs, and only in its `rem`
header: the executable half — from `@echo off` to the final `exit /b` — was
compared against `P19-TRANSIENT-001`'s programmatically and is **identical at
1306 bytes**, asserted by the script that wrote it so that a harness change could
not pass as a header change. Its own `git hash-object` is
`6db73894c3629d262e55f839f26fb4535ab4d11a`.

This is the SIXTY-NINTH milestone the harness has run with its executable content
unchanged, and it was self-checked before this run: `verify-harness.cmd`
deliberately fails two stages and confirmed the driver counts them, reporting
*"PASS: a failed stage gave qualify.cmd exit 2, and a zero-match repeat filter
was counted as a failed stage."*

### An inherited defect in `verify-harness.cmd`, carried forward unfixed

`verify-harness.cmd` contains a literal `0x08` byte where `\bc-build` was meant,
now in 75 of the 99 copies committed across the lineage, entered 2026-09-30
with the
P15 materials work. It fires only when `BETTERCAD_BUILD_ROOT` is undefined, which
`run-qualification.cmd` always defines, so **no gate in any milestone has ever
evaluated it**. Carried forward byte-for-byte rather than fixed: repairing it
across the lineage is a scope decision for the owner, not something to fold into
a feature milestone. `TODO.md` records it as an open finding.

## Two reviews ran before this freeze, and both found real defects

Neither was the author's to judge alone, and between them they changed the
deliverable substantially — which is why this freeze is later than it would
otherwise have been.

```text
a mutation / test-gap analysis   10 surviving mutations, 4 gates that changed
                                 meaning instead of failing, 6 tautologies
an adversarial review            2 false MEASURED claims in ADR-079, plus
                                 attribution and scope findings
```

What was acted on and what was not is in the README's ADVERSARIAL REVIEW section
with reasons. The two false measured claims are the ones worth naming here,
because both were in text that claimed to have measured rather than assumed: the
ADR said NINE ownership checkers exist where there are **seven**, and the
checker's false-positive counts were measured over 1830 files including `tests/`
and then quoted against "the 600 scanned files" — wrong in seven of nine rows.
Both are corrected by re-measuring, and one of the review's own corrections was
itself wrong and is recorded as checked rather than conceded.

## What the verdict is, and what it is not

**The verdict is the harness's own two statements:**

```text
1  run-qualification.out        its own sentence about pass or failure
2  qualification-times.txt      its failed-stage count
```

**It is NOT this launcher's exit code.** `P19-MOM-001`, `P19-STEADY-001` and
`P19-TRANSIENT-001` each had a run where the launcher reported exit 0 while the
harness reported failed stages. The exit code is not read.

A clean ctest run prints `100% tests passed out of 6995`. The *presence* of the
substring `tests failed out of` is itself a failure marker, and
`0 tests failed out of 6995` is a string ctest cannot print in a passing run.

## And the shell matters

`P19-TRANSIENT-001`'s second attempt failed in five seconds because it was
launched from a shell that cannot see the dependency prefix, so CMake found
msys64's Open CASCADE 7.9.3 and the project's own supported-range gate refused
it. All three presets were configured as a pre-flight before this run, and all
three resolved `OpenCASCADE_DIR` under the deps prefix.

## What is not frozen

`docs/`, `TODO.md` and `ROADMAP.md`. Nothing in them can reach the executable or
the tests. The qualification's own logs land under
`docs/verification/P19-TURB-ARCH-001/qualification/` while the run is in
progress, which is why they cannot be inside the frozen surface.

`.claude/`, `AGENTS.md` and `P19-PVCOUPLE-001`'s four `.pristine` and log files
are untracked and stay that way, as that milestone's own evidence records.

**AND NEITHER ARE THIS MILESTONE'S OWN FIVE `.pristine` FILES COMMITTED**, on
the same precedent and for a reason worth stating: they are byte-identical
copies of five files under `src/` and `include/`, made by the mutation scripts
themselves — `if not os.path.exists(pristine): shutil.copy2(...)` — so they are
regenerable scaffolding, not evidence. The bodies the mutations were applied to
are recoverable from this commit directly, because `include` and `src` are
bit-identical to it at `4759b768` and `c33a7b6f`. Committing them would
duplicate 116 KB of source into `docs/`, which `CLAUDE.md`'s "do not commit
large generated files without reason" argues against. The mutation LOGS are
committed, and they are what the claim rests on.

## One correction to this file, made after the gate returned

The paragraph above on `verify-harness.cmd` said **73** copies carried the
inherited `0x08` byte. It is **75**, of **99** committed copies, counted three
independent ways after the gate: `grep -qP` per file, `grep -lP` over the whole
list, and a python read of the raw bytes. Corrected in place rather than
quietly, for the same reason `ADR-079`'s nine-versus-seven miscount is recorded
rather than fixed: a number quoted as measured is either measured or it is a
defect, and this file is the one that claims the figures were counted off the
tree. Nothing else in this freeze moved, and `docs/` is outside the frozen
surface, so the qualification stands.
