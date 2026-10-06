# P16-REFMOD-001 — qualification freeze

```text
SUBJECT:  the identity of the tree that was qualified, so it can be compared
          with the tree that was committed
FROZEN:   01:13 local, 2026-10-06
```

## Why the gates run in this order

Cheapest first. All three of P15's voided qualifications came from doing a cheap
check after an expensive one, and **the `debug-shared-ext` gate has now caught a
real production defect for the third milestone running** — P16-CMD-001,
P16-CLI-001 and this one.

```text
1. git diff --check, tracked AND untracked             seconds
2. grep the changed headers for an exported constexpr  seconds
3. the new tests, 5 repeats                            180 s
4. the reference suite in all THREE presets            2 min
5. the shared build                                    minutes   <- DLL boundary
6. the shared full suite                               19 min
7. the debug-ext full suite                            17 min
8. FREEZE
9. three-preset qualification                          hours
10. clean-tree verification from the frozen index
11. the qualified tree == the committed tree
```

Mutation testing came **before** the freeze too, and so did the adversarial
review: between them they produced five findings, two of which were production
defects. Freezing first would have meant fixing them after the freeze and
re-running everything.

## Pre-freeze gate results

```text
git diff --check                clean. The untracked files were checked
                                separately -- git cannot see whitespace in a
                                file it does not track
exported constexpr              none in MeshReferenceModels.hpp,
                                MeshTestSupport.hpp, MeshSizing.hpp or
                                Analytic.hpp
build debug-ext                 exit 0, 0 warnings
build release-ext               exit 0, 0 warnings
build debug-shared-ext          exit 0, 0 warnings
new tests, until-fail:5         53 tests x 5 back to back, exit 0, 179.73 s
reference suite debug-ext       53 / 53
reference suite release-ext     53 / 53
reference suite debug-shared    53 / 53
full suite debug-shared-ext     3381 / 3381, finished 00:55:44
full suite debug-ext            3381 / 3381, finished 01:12:38
```

**3381 of 3381, with no exception.** Both earlier milestones recorded one
failure here — `cli.new.unicode-path`, a code-page artefact of running CTest
without `chcp 65001`. This run set the code page first, as `qualify.cmd` does,
so the pre-freeze figure and the qualification figure are the same number and
neither needs a footnote.

**Two production defects, found here and fixed before the freeze:**

```text
ResolvedSizing::unresolvedCount()   a public API method of a NON-EXPORTED data
                                    struct, defined out of line -- so not
                                    exported, and unusable from another DLL
                                    since P16-SIZE-001. Nothing had noticed,
                                    because nothing outside bettercad_meshing
                                    had called it. Now inline in the header,
                                    as every other predicate on a report
                                    struct in that module already is
MeshControl::kTypeName at runtime   binding a reference to a dll-imported
                                    static constexpr does not link shared. The
                                    suite uses the literal, which is the
                                    convention MaterialTests.cpp and
                                    DocumentJson.cpp already record and which
                                    MeshControlJson.cpp's static_assert keeps
                                    from drifting
```

Both linked cleanly in **both** static presets and failed only in
`debug-shared-ext`.

**Three cost reductions made before the freeze**, because a reference suite is
paid for in every preset of every future qualification:

```text
RM-MESH-02 convergence   245 s -> 6 s. Its global target was the model's
                         declared 12 mm, which over-refined the volume mesh
                         while contributing nothing to a study of the
                         BOUNDARY's chord error. At 80 mm the boundary governs:
                         the same 140 / 196 / 444 facets, the same volumes to
                         fifteen figures, the same errors
undo/redo                55 s -> 3 s. Its second size was 6 mm, giving 1977
                         tetrahedra, when what the case proves is that each
                         mesh follows the intent current at that point -- which
                         two DISTINGUISHABLE meshes establish. 24 mm gives 361
centroidMm               an O(n^2) in the suite's own support header: it took a
                         mesh and rebuilt the whole node-position map once per
                         element. Now takes the map
```

The whole reference suite is 58 s in debug, down from 103 s, and the five-repeat
gate is 180 s rather than the 25 minutes it was heading for.

## The frozen tree

Eight paths, as `qualify.cmd`'s `:trees` computes them — from a scratch git
index, so a dirty working tree is captured exactly as it stands.

```text
apps               b49722470580d4fe6c2bb2b3594bb3885eee10db   unchanged
include            528df72c31684e7cde3cec361e04341e81949708   moved
src                182a584b6c20bd106613b180ed50fd2a4172992f   moved
tests              ce5e68697fbe9518c68874ccf7812e9c3f2654d0   moved
examples           75ce1c6852e03011f350d6806f4e8d5f3207d63e   moved
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   unchanged
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   unchanged
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   unchanged

whole fingerprint  c932abc0557d9644c33566eda3db63c7ac34b36f
frozen from        a6113de  (HEAD, P16-CLI-001)
```

`apps` is **unchanged**: this milestone adds no CLI command and no GUI surface,
which is the point — the reference suite drives what P16-CLI-001 already built.

`include` and `src` moved for **one production file each**, and the same one:
`ResolvedSizing::unresolvedCount()` moved from `MeshSizing.cpp` into
`MeshSizing.hpp`. There is no other production change in the milestone.

`examples` gained three sources and a link; `tests` gained two files, the
suite's 25 CLI fixtures and two filters in the zero-match guard.

`docs/` and `deps/` are outside the fingerprint, so the evidence directory may
be written after the freeze. The mutation harness's snapshots were diffed
against the working files immediately before the freeze — because an orphaned
harness once left a mutation applied to an untracked source file, which
`git status` and `git diff` both report as clean, and because **this milestone's
own harness left M5 applied** and was caught only by the one mutated file that
happened to be tracked.

## The harness

Carried **unchanged** from P15-QUAL-001, byte for byte:

```text
git hash-object qualify.cmd   d313a64070718c44fae290ac042fe259d1a03c8b
```

`verify-harness.cmd` beside it is its regression and is meant to be run whenever
it is edited, which it was not. The exact invocation, with this milestone's
blast radius and the reason for every entry in it, is in
`run-qualification.cmd`.

## The qualification

Launched **detached** at 01:15, so no wrapper lifetime bounds it. One run,
uninterrupted, 2 h 38 min.

```text
Qualification passed: every stage exited 0.
qualify.cmd exit 0          (the exit code IS the number of failed stages)

PRESET            CONFIGURE  CLEAN  BUILD  NO-OP REBUILD  CTEST
debug-ext              0       0      0         0           0   3381/3381
release-ext            0       0      0         0           0   3381/3381
debug-shared-ext       0       0      0         0           0   3381/3381

REPEAT (5x each of 692 selected tests, back to back)
release-ext            0                            692/692   686.16 s
debug-ext              0                            692/692   721.74 s

warnings, all three builds   0       (-Werror, 595 objects each)
no-op rebuilds               0 compiles, 0 links -- the binaries tested are
                             the ones just built
shared build                 9 DLLs linked, including libbettercad_meshing.dll,
                             so the reference suite's own process tests had to
                             resolve that closure at load time
```

Timeline: 01:15:31 start, debug-ext 01:59:29, release-ext 02:48:21,
debug-shared-ext 03:30:13, repeats 03:41:40 and 03:53:42, finished 03:53:44.

**3381 of 3381 in every preset, with no exception and no footnote.** The two
preceding milestones each recorded one failure at this stage —
`cli.new.unicode-path`, a console code-page artefact — because their pre-freeze
runs were made outside `chcp 65001`. The harness sets the code page as its first
act, and this milestone's pre-freeze gates did too, so the pre-freeze and
qualification figures agree.

**Fresh-binary proof, per preset.** Every process test and every fixture takes
its executable from `$<TARGET_FILE:...>`, which resolves **per preset**, so each
preset ran the binaries it had just built and nothing came from `PATH`. The
no-op rebuild stage then did 0 compiles and 0 links in each, which is what makes
that statement a measurement rather than an assumption.

## Clean-tree verification

The brief's step 71, and it is a real clean checkout rather than a claim about
one. The same scratch index `qualify.cmd`'s `:trees` builds — HEAD plus the
eight fingerprinted paths taken from the working tree — is written out with
`git checkout-index`, so what is built is **exactly the frozen tree**: no build
output, no untracked stray, nothing git would not carry into a commit.

```text
frozen tree             c932abc0557d9644c33566eda3db63c7ac34b36f
files checked out       3380
build outputs present   0
configure               exit 0
build                   exit 0, 0 warnings, 595 objects
reference suite         28 cases, 23410 assertions, exit 0
refmod CLI fixtures     25 / 25
```

> **A limitation of the environment, not of the tree, and it took two attempts
> to diagnose honestly.** The first run failed at configure: `FetchContent`
> could not download the pinned Catch2 archive. The first reading was
> "transient", because Git Bash's `curl` fetches the same URL with HTTP 200 —
> but the second run gave the real reason:
>
> ```text
> SSL certificate verification failed: certificate signer not trusted
> ```
>
> **CMake's bundled curl has no CA trust anchors here**, so a build root with no
> populated `_deps` cannot fetch the pinned archives at all. The three qualified
> build roots are unaffected because `ninja -t clean` does not touch `_deps`:
> they have carried their hash-verified copies since they were first populated
> and never re-download, which is why every preset configures in seconds.
>
> So the clean-tree configure points `FETCHCONTENT_SOURCE_DIR_*` at those same
> pinned, SHA256-verified sources. What the check is for — that exactly the
> frozen source tree configures, builds and passes from a pristine checkout — is
> unaffected, because the dependencies are pinned by URL and hash *in the tree*
> and no BetterCAD source is substituted. What it does **not** exercise is the
> download path, and that is recorded as a known limitation rather than implied
> to have passed.

## The qualified tree is the committed tree

`qualify.cmd` records the eight paths at both ends of the run, from a scratch git
index. After the last test:

```text
apps               b49722470580d4fe6c2bb2b3594bb3885eee10db   unchanged
include            528df72c31684e7cde3cec361e04341e81949708   unchanged
src                182a584b6c20bd106613b180ed50fd2a4172992f   unchanged
tests              ce5e68697fbe9518c68874ccf7812e9c3f2654d0   unchanged
examples           75ce1c6852e03011f350d6806f4e8d5f3207d63e   unchanged
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   unchanged
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   unchanged
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   unchanged
```

Identical to the freeze, component for component — whole fingerprint
`c932abc0557d9644c33566eda3db63c7ac34b36f`. Nothing that can reach the
executables or the tests moved during the run.

Only `docs/verification/P16-REFMOD-001/` was written after the freeze, and it is
outside the fingerprint by construction.
