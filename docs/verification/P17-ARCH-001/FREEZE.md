# P17-ARCH-001 — the qualification freeze

```text
SUBJECT:  the tree that was qualified, recorded before the first build
METHOD:   the eight-path fingerprint every BetterCAD qualification since P11
          has used -- a scratch git index seeded with `read-tree HEAD` and then
          `add -A` over apps include src tests examples cmake CMakeLists.txt
          CMakePresets.json. docs/ and deps/ are outside it by construction,
          which is what lets evidence be written after the run.
```

## The frozen candidate

```text
apps               b49722470580d4fe6c2bb2b3594bb3885eee10db
include            b7aeeba2d1dfb6f0e57a159d4987c2f4c9cfdb9d
src                47a0eb3a9f51941aac846546c09c9946ff80f32f
tests              bb79c89b6d097eef5a7369c557afbf4fed379b41
examples           75ce1c6852e03011f350d6806f4e8d5f3207d63e
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e

WHOLE FINGERPRINT  536e14b585bbd82fc8fcb82c84da680d6860c2cd
```

## What moved, against P16's qualified tree

```text
apps      b49722247  unchanged   examples  75ce1c68  unchanged
cmake     5a382115   unchanged   CMakeLists.txt / CMakePresets.json unchanged

include   528df72c -> b7aeeba2   one new public header
src       182a584b -> 47a0eb3a   one new module (2 files), its registration in
                                 src/CMakeLists.txt, and six module comments
                                 that quoted their own layer number
tests     dfbb3003 -> bb79c89b   one new test file and its registration, the
                                 respaced layer table plus rule 6, five new
                                 fixtures and four test registrations
```

Three paths of eight. The respace touches `tests/` because the layer table
lives in a test script — which is also why its blast radius is the whole
repository rather than the diff.

## Pre-freeze checks, run BEFORE the expensive one

In this order deliberately. All three of P15's voided qualifications came from
running a cheap check after an expensive one, and finding a defect that
invalidated it.

```text
git diff --check                    clean (CRLF notices are the repo's own
                                    normalisation: .gitattributes is
                                    `* text=auto eol=lf`)
over-100-column lines               none introduced in the checker; the four
                                    added to tests/architecture/CMakeLists.txt
                                    are the same long COMMAND form its six
                                    existing ones use
export macro on a header-defined
  constexpr/inline                  none -- grepped, because this fails ONLY in
                                    debug-shared-ext
the SHARED build                    configure 0, build 0, 0 warnings.
                                    10 BetterCAD DLLs including
                                    libbettercad_structural.dll, and the
                                    structural tests pass ACROSS the DLL
                                    boundary: 234 assertions, 10 cases.
                                    No DLL-boundary defect, which is worth
                                    recording because the previous three
                                    milestones each found one
the new tests under --repeat        24 tests x 5, 100% passed. Run again after
  until-fail:5                      the move-only change: 100% passed
full debug-ext suite                3401/3401, 1152 s
zero-match protection               architecture 14, StructuralInput 10, the
                                    repeat filter 579 -- all checked with -N
                                    first
```

The full suite figure of **3401** is P16's 3387 plus this milestone's 10
structural cases and 4 checker self-tests, which is the arithmetic it should
be.

## One ordering note, recorded rather than hidden

The full debug-ext suite above ran on a binary built **before** the last
change of the milestone — `StructuralModel` was made move-only in response to
finding F1 of the adversarial review. That run is therefore a pre-freeze
sanity check and not the qualification. What followed the change: a clean
rebuild (0 errors, 0 warnings) and the 24-test repeat set again, both passing,
and then this freeze.

The authoritative regression is the three-preset qualification below, which
rebuilds every preset **from clean** on the frozen tree.
