# P17-QUAL-001 — the qualified tree is the committed tree

```text
RESULT:    PASS
CANDIDATE: a99f0b6c332c4c122d495c55b99df598a083d86a  (P17-REFMOD-001's
           commit, == origin/main at the freeze) plus the working tree:
           tests/CMakeLists.txt and tests/cli/ZeroMatchGuard.cmake modified,
           tests/qualification/P17StructuralQualificationTests.cpp new, the
           evidence directory and the documentation amendments new --
           git status --short recorded at the freeze in
           qualification/prefreeze.txt
RAN:       Thu 15/10/2026 19:16:23 -> 23:06:20  =  3h49m57s
STAGES:    17, every one exit 0, 0 stage(s) failed
HARNESS:   qualify.cmd exit 0, "Qualification passed: every stage exited 0."
           qualify.cmd git hash-object d313a64070718c44fae290ac042fe259d1a03c8b,
           byte-identical to P16-SIZE-001's and every milestone's since;
           verify-harness.cmd 11855b8ff891093b021d1e0351761283d0eedd24, run
           before the launch (qualification/verify-harness.out: PASS)
MACHINE:   the hasib machine, as P17-CLI-001 and P17-REFMOD-001
```

Stage numbers, the warning audit and the zero-match table are in
`FINAL_REGRESSION.md`; this file is the identity claim.

## The method, stated exactly

A full `HEAD^{tree}` comparison would be a lie here, and the brief is right
to warn about it: writing this evidence directory changes the tree *after*
the tests ran. So the identity that is claimed is the one that is true and
is the one that matters --

```text
THE EIGHT-PATH SOURCE/TEST FINGERPRINT

    apps  include  src  tests  examples  cmake
    CMakeLists.txt  CMakePresets.json
```

-- computed by `qualify.cmd`'s `:trees` from a **scratch git index**
(`GIT_INDEX_FILE`), seeded with `read-tree HEAD` and then `add -A` over
those eight paths, so it captures the working tree exactly as it stood.
Everything that can reach a compiler, a linker or a test is inside it.
`docs/`, `deps/`, `TODO.md`, `ROADMAP.md` and `README.md` are outside it,
which is what allows evidence to be written after the run without
invalidating it. This is the convention every BetterCAD qualification since
P11 has used.

## The three readings, which must agree

```text
| WHEN                                | apps     | include  | src      | tests    | examples | cmake    | CMakeLists | Presets  |
| frozen, before the first build      | a95876bc | c9696d26 | f3c1245d | 90f93c52 | a955cf65 | 5a382115 | 3ec3c10f   | 3229f0f8 |
| recorded by the harness after the   | a95876bc | c9696d26 | f3c1245d | 90f93c52 | a955cf65 | 5a382115 | 3ec3c10f   | 3229f0f8 |
|   last test of the last preset      |          |          |          |          |          |          |            |          |
| the closeout commit's own subtrees  | see the commit record below                                                             |
```

Full values:

```text
apps               a95876bc6dbf843d4e0cec114059bc196fa16175
include            c9696d26cd8583a90cdddd00a51ee654fc3ba72b
src                f3c1245dabbfda68008db3660f3d15a73e8e607f
tests              90f93c52dcc88b2ff8061ba33c64f688472a4c38
examples           a955cf6511ce715fcb95bc06ef82b877535869e6
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e
```

**Before == after**, taken by the harness at 19:16:23 and again at
23:06:20. **Seven of the eight are byte-identical to the P17-REFMOD-001
freeze** -- everything but `tests`, which moved from 3568eb0e to 90f93c52
by one new test file, one registration line and one zero-match filter.
That is the fingerprint-level proof that this milestone changed no
production file and no example: `apps`, `include`, `src` and `examples` are
the same objects P17-REFMOD-001 qualified, and the four before it in
`apps`, `include`, `src`, `cmake`, `CMakeLists.txt` and `CMakePresets.json`
(P17-CLI-001's freeze).

The first two readings are not claims this file makes about the run -- they
are recorded inside `qualification/qualification-times.txt` by the thing
that performed it. The third is recorded below after the commit, with
`git rev-parse <commit>^{tree}:<path>` for each of the eight.

## Source immutability during the run

```text
19:16:23   qualification started, fingerprint recorded
19:16:27 - 23:06:09   three presets configured, CLEANED, built, no-op
                      rebuilt and tested, then two repeat stages
23:06:20   fingerprint recorded again -- identical
```

No source, config or test file was edited in that window. What was edited
during and after it is documentation: this directory, the eight ADR
amendments, `docs/architecture.md`, the two P17-VIZ-001 addenda, `TODO.md`,
`ROADMAP.md` and `README.md` -- every one outside the fingerprint by
construction, and none configured, compiled, linked or read by a test.

## What moved after the P17-REFMOD-001 freeze, and why it is admissible

```text
tests/qualification/P17StructuralQualificationTests.cpp   NEW, 3 final gates
tests/CMakeLists.txt                                      +1 source line
tests/cli/ZeroMatchGuard.cmake                            +1 filter (+ comment)
docs/architecture/decisions/ADR-03{4,5,6,7,8}, ADR-04{2,3,7}   dated amendments
docs/architecture.md                                      layer table respaced
docs/verification/P17-VIZ-001/README.md, REMAINING_SCOPE.md   dated addenda
docs/verification/P17-QUAL-001/                           this directory
TODO.md, ROADMAP.md, README.md                            documentation following evidence
```

The ADR amendments deserve saying out loud: they are documentation changes
made *because* the audit found the documents disagreed with the code, and
CLAUDE.md requires documentation to follow evidence. Had the audit instead
required a change to `src/` or `tests/`, the qualification would have been
void and the candidate re-frozen and re-run. One test defect in this
milestone's own new file was found and fixed BEFORE the freeze (T1 in
`ADVERSARIAL_REVIEW.md`), so there was one candidate and one run.

## The mutation probes and the restore

Seven probes edited `src/structural/*.cpp` and `apps/bettercad_cli/StructuralReports.cpp`
between 18:17 and 18:40, each restored by the probe script and checked by
`git status --short` afterwards (`MUTATION.md`). The pre-freeze build and
full ctest at 18:41-19:14 (4009/4009) ran on the restored tree, and the
qualification then cleaned and rebuilt every preset from it -- which is why
`src` reads f3c1245d, P17-REFMOD-001's value, at both ends of the run.

## The fresh-binary proof, per preset

```text
preset             absolute binary                                            bytes          sha256
debug-ext          C:/Users/hasib/bcdev/bc-build/debug-ext/bin/
                     bettercad-cli.exe                                        368 170 949    45fc3e3b7b754ef9...
                     bettercad_example_reference_models.exe                   372 836 927    ca1319d16ae015d7...
                     bettercad_tests.exe                                    1 164 005 833    a19be7ab293de98f...
release-ext        C:/Users/hasib/bcdev/bc-build/release-ext/bin/
                     bettercad-cli.exe                                         10 113 338    f2080ba2a70533e9...
                     bettercad_example_reference_models.exe                     9 496 172    f92edb18f862dbb9...
                     bettercad_tests.exe                                       48 768 245    9a74c54d6d45c161...
debug-shared-ext   C:/Users/hasib/bcdev/bc-build/debug-shared-ext/bin/
                     bettercad-cli.exe                                         41 310 182    48e0cb00a79945f5...
                     bettercad_example_reference_models.exe                    52 417 896    de3c3c16bd0f2c13...
                     bettercad_tests.exe                                      831 050 621    715fb87c6cccd2e8...
```

Full hashes in `qualification/postrun/summary.txt`. The runner and the CLI
are `$<TARGET_FILE:...>` in every process test, so a PATH lookup is
impossible by construction; no installed copy of BetterCAD exists on this
machine. The CLI and runner binaries have exactly the sizes P17-REFMOD-001
recorded in every preset (their sources did not change; only the embedded
git revision stamp differs), and the test binaries grew by the one new
object. For debug-shared-ext the ten `libbettercad_*.dll` modules sit
beside the three executables in their own `bin/`, with the Qt, OCCT and
compiler runtime DLLs.

## The commit record

Filled in after `BetterCAD: qualify P17 structural FEA` was made, with the
eight subtree hashes read from the commit itself:

```text
commit             (see below)
apps               a95876bc6dbf843d4e0cec114059bc196fa16175   == frozen
include            c9696d26cd8583a90cdddd00a51ee654fc3ba72b   == frozen
src                f3c1245dabbfda68008db3660f3d15a73e8e607f   == frozen
tests              90f93c52dcc88b2ff8061ba33c64f688472a4c38   == frozen
examples           a955cf6511ce715fcb95bc06ef82b877535869e6   == frozen
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   == frozen
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   == frozen
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   == frozen
```

The comparison itself is recorded in the final report and in `TODO.md`'s
PASS entry; anyone can re-check it against whatever `HEAD` is current:

```bash
git fetch origin
for p in apps include src tests examples cmake CMakeLists.txt CMakePresets.json; do
  echo "$p $(git rev-parse "origin/main^{tree}:$p")"
done
```

Every line must equal the table above. If one does not, the tree that is
published is not the tree that was qualified, and this milestone's PASS
does not apply to it.
