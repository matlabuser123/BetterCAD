# INFRA-VIEWER-001 — the qualification freeze

```text
SUBJECT:  the identity of the tree that was qualified, recorded BEFORE the
          qualification started
DATE:     2026-10-03
FROZEN:   15:56 local
```

**The tree that is qualified must be the tree that is committed.** This file
exists so that claim can be checked rather than believed: the fingerprint
below was taken before the first preset was configured, and `qualify.cmd`
records the same eight tree IDs again after the last test run.

## The fingerprint

Eight paths, as `qualify.cmd`'s `:trees` computes them — from a scratch git
index, so a dirty working tree is captured exactly as it stands rather than as
`HEAD` has it.

```text
apps               40cedc927b030d484dd68b7d1bce7860be75e811
include            cc90240ab3e9cdfa084196ca9d35686d102d8efc
src                015afd935975b906d3253ff312cbf5476181921f
tests              7d40189fcea6073233c97d2c80ae420e3e06b5f2
examples           9187931fa6824e98d3da6296cdc40af6ca4b8607
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e

whole fingerprint  335f7fc176224ffd74db687b6097a664ee4d4fde
frozen from        e7d90e9  (HEAD at the start of the milestone)
```

`deps/` and `docs/` are **outside** the fingerprint, as `INFRA-NETGEN-001`
established. That is what makes the OCCT rebuild a toolchain change rather
than a change to the qualified tree, and it is why this evidence directory may
still be written to after the freeze. Nothing under the eight paths above may
move; if it does, the qualification is void and is run again from clean.

## The gates that were cleared BEFORE the freeze

In this order, cheapest first. **This ordering is the whole point:** three of
`P15`'s qualifications were lost to running a cheap check after an expensive
one, and one of these gates did fail the first time.

```text
git diff --check, whitespace, tabs, column limit     clean
15 mutations, one uninterrupted run                  15 killed by a TEST,
                                                     0 compiler-only,
                                                     0 survived
verify-harness.cmd (does a failed stage report?)     PASS, exit 2
the new tests x5, debug-ext                          23/23, 56 s
debug-shared-ext configure + build + targeted run    0 warnings, 39/39, 50 s
full debug-ext suite                                 FAILED 3150/3151, then
                                                     3151/3151 once explained
```

## The failure that was not a defect

The full suite's first run failed one test, `cli.new.unicode-path`:

```text
command  : bettercad-cli.exe new ".../Plåt ✓.bcad" --force
exit code: 0 (expected 0)
stdout   : Created .../PlΓö£├æt ╬ô┬ú├┤.bcad  (document 'PlΓö£├æt ╬ô┬ú├┤', ID ...)
```

The document was created, the exit code was right, and only the stdout
comparison failed — on text transcoded twice. `qualify.cmd`'s first line is
`chcp 65001` for exactly this test, and the run had been launched from a
PowerShell child at the OEM code page.

**It was re-run rather than explained away.** Under code page 65001 the same
suite gives `100% tests passed out of 3151`, exit 0, in 1402 s. An
understood-but-unverified failure is not a cleared gate, and the cost of
checking was 23 minutes against a qualification measured in hours.

## Revision

First issue, 2026-10-03.
