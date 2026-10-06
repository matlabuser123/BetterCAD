# P16-QUAL-001 — the qualified tree is the committed tree

```text
SUBJECT:  proving that the code and tests which passed qualification are the
          code and tests that were committed -- precisely, and without claiming
          a raw git tree identity that would not be true
```

## The method, stated exactly

A full `HEAD^{tree}` comparison **would be a lie here**, and the brief is right
to warn about it: writing this evidence directory changes the tree *after* the
tests ran. So the identity that is claimed is the one that is true and is the
one that matters —

```text
THE EIGHT-PATH SOURCE/TEST FINGERPRINT

    apps  include  src  tests  examples  cmake
    CMakeLists.txt  CMakePresets.json
```

— computed by `qualify.cmd`'s `:trees` from a **scratch git index**
(`GIT_INDEX_FILE`), seeded with `read-tree HEAD` and then `add -A` over those
eight paths. So it captures the working tree exactly as it stood, dirty or not,
and is independent of what is staged or committed.

Everything that can reach a compiler, a linker or a test is inside it.
`docs/` and `deps/` are outside it, which is what allows evidence to be written
after the run without invalidating it. This is the convention every BetterCAD
qualification since P11 has used.

## The three readings, which must agree

```text
| WHEN                              | WHOLE FINGERPRINT                        |
| frozen, before the first build    | 8ab30a31695e79c3d02fe222898f8b1ec6274565 |
| recorded by the harness after the | 8ab30a31695e79c3d02fe222898f8b1ec6274565 |
|   last test of the last preset    |                                          |
| recomputed before the commit      | 8ab30a31695e79c3d02fe222898f8b1ec6274565 |
```

Component for component, at all three readings:

```text
apps               b49722470580d4fe6c2bb2b3594bb3885eee10db
include            528df72c31684e7cde3cec361e04341e81949708
src                182a584b6c20bd106613b180ed50fd2a4172992f
tests              dfbb30038fa052fbfa7de13d57d805b543a0ee80
examples           75ce1c6852e03011f350d6806f4e8d5f3207d63e
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e
```


### What the whole fingerprint is a function of

Worth stating exactly, because a reader who recomputes it from `origin/main`
will get a different number and be right to ask why. The harness builds its
scratch index with `read-tree HEAD` and *then* `add -A` over the eight paths,
so the value is a function of **the eight paths plus whatever base tree HEAD
pointed at when it ran**. Throughout this qualification HEAD was `d7db08b`,
P16-REFMOD-001's commit, and it never moved during the run — which is why all
three readings agree.

```text
read-tree d7db08b  +  add -A over the eight paths, from the tree as pushed
  -> 8ab30a31695e79c3d02fe222898f8b1ec6274565     reproduced exactly

read-tree origin/main  +  the same add -A
  -> a different value, necessarily: the base now carries this evidence
     directory, which is the whole reason evidence is kept outside the
     fingerprint
```

So the single value is a convenient summary, meaningful relative to the base it
was taken against. **The invariant is the eight component hashes**, which are
pure functions of content and which the pushed revision carries unchanged —
verified against `origin/main^{tree}` after the push, path by path. Those are
the rows in the table above, and they are what the claim rests on.

**Nothing that can affect an executable or a test moved during the run**, and
nothing moved between the run and the commit. The harness reads the fingerprint
itself, at both ends, so the first two readings are not claims I make about the
run — they are recorded inside `qualification-times.txt` by the thing that
performed it. The third was recomputed by the same `read-tree` + `add -A`
method immediately before `git add`.

## What moved after the freeze, and why it is admissible

```text
docs/architecture/decisions/ADR-031-*.md   the amendment the ADR audit required
docs/verification/P16-QUAL-001/            this evidence directory
docs/verification/P16-SIZE-001/            a dated count correction (F6)
TODO.md  ROADMAP.md  README.md             documentation following evidence
```

All of it is documentation. `docs/` is outside the fingerprint by construction;
`TODO.md`, `ROADMAP.md` and `README.md` sit at the repository root and are not
among the eight paths, are not configured, compiled, linked, installed, or read
by any test. `tests/CMakeLists.txt` and `tests/cli/ZeroMatchGuard.cmake` **are**
inside the fingerprint, and were frozen with everything else.

The ADR amendment deserves saying out loud: it is a **documentation** change
made *because* the audit found the document disagreed with the code, and
CLAUDE.md requires documentation to follow evidence. Had the audit instead
required a change to `src/` or `tests/`, the qualification would have been void
and the candidate re-frozen and re-run. That is not hypothetical — it is
exactly what happened to the first candidate, below.

## Source immutability during the run

```text
17:40:08   qualification started, fingerprint recorded
17:40:10 - 20:11:22   three presets configured, CLEANED, built, no-op rebuilt
                      and tested, then two repeat stages
20:11:24   fingerprint recorded again -- identical
```

No source, config or test file was edited in that window. The documentation
written during and after it is listed above.

## Relation to P16-REFMOD-001

This milestone changed **no production code**. Against P16-REFMOD-001's
qualified tree (`c932abc0`), only `tests` moved:

```text
apps, include, src, examples, cmake, CMakeLists.txt, CMakePresets.json
                                                   byte-identical
tests   ce5e6869 -> dfbb3003    one new test file, its registration, and one
                                more filter in the zero-match guard
```

So the six final gates are additive, and every figure the reference models
produce is generated by byte-identical code in both milestones — which is why
the tables in `ANALYTICAL_VALIDATION.md` match P16-REFMOD-001's exactly. They
were nevertheless re-run on **this** candidate, because the final results must
come from the frozen tree rather than be copied forward.

## The first candidate, and why it does not count

A first candidate at `1326a551` (tests `e6173c6f`) passed all three presets and
both repeat stages, `0 stage(s) failed`, 3386/3386 each. The adversarial review
then found an owed `P16-ARCH-001` finding that was covered only by composition
(F5), and closing it meant adding one test — which moves `tests`, which voids
the qualification under this project's own rule. So:

```text
tests   e6173c6f -> dfbb3003    one TEST_CASE added, the deleted-body gate
whole   1326a551 -> 8ab30a31
```

The tree was re-frozen and all three presets were run again from clean, which
is why the suite is 3387 and not 3386. The superseded run's verdict and stage
timings are kept at `qualification/qualification-times-first-candidate.txt` and
`run-qualification-first-candidate.out`; its per-stage logs were overwritten by
the run that counts, which is the correct precedence. **Every figure in this
directory comes from the `8ab30a31` run.**

## Clean-checkout verification

The check writes a pristine tree from `git read-tree HEAD` **alone** — no
`add -A` from the working directory — and configures, builds and runs the whole
suite there. That is what catches a staged-index mistake: a file edited but
never added, or added but never committed, fails in the clean tree while still
passing in the working one.

```text
committed revision  137bad1e0cf609c6c4d6fdbca69e1566b31253bf
committed tree      6a632a7b7bebfb21c712eec7e3822e7774e406b7
working tree at commit time        clean, 0 porcelain lines

files checked out                  3478
build outputs present              0
configure                          exit 0
build                              exit 0, 0 warnings, 596 objects
unit binary                        exit 0
                                   263394 assertions in 2988 test cases
full ctest, code page 65001        exit 0, 100% passed out of 3387
                                   832.35 s, 0 Failed/Exception/Timeout
```

The object count matches the qualification's 596 and the ctest total matches
its 3387, from a tree that contains nothing but the commit.

This check verifies the **committed** revision, which is stronger than
P16-REFMOD-001's scratch-index version but can only run after the commit — so
`qualification/cleantree.txt` and finding F7 land in a follow-up documentation
commit. The qualified source tree is untouched by that: nothing under `docs/`
is configured, compiled, linked or read by a test.

The first attempt reported one failure, `cli.new.unicode-path`, and it was the
**checker** at fault: it ran `ctest` from Git Bash at code page 437, where the
console mangles the CLI's UTF-8 output and the stdout regex misses. Proved by
varying the one thing — 437 fails, 65001 passes, same tree and same binaries —
and recorded as F7 with the second harness defect beside it. Nothing in the
committed tree was changed, and no test was weakened.

## Push verification

```text
git push origin main        d7db08b..95e419b  main -> main
git fetch origin
HEAD                        95e419b336cf9955e7a7cc2e49911d4637b5d491
origin/main                 95e419b336cf9955e7a7cc2e49911d4637b5d491
HEAD == origin/main         YES
git status --porcelain      0 lines
```

And the eight qualified paths read back out of `origin^{tree}` after the push,
each identical to the frozen value in the table above. No force push, no
rewritten history, no tag.

The figures above are the push of `95e419b`. Documentation commits follow it —
this note is in one — and each moves `HEAD`, so chasing the literal value here
would never terminate. What does not move is the claim that matters, and anyone
can re-check it against whatever `HEAD` is current:

```bash
git fetch origin
for p in apps include src tests examples cmake CMakeLists.txt CMakePresets.json; do
  echo "$p $(git rev-parse "origin/main^{tree}:$p")"
done
```

Every line must equal the table at the top of this file. If one does not, the
tree that is published is not the tree that was qualified, and this milestone's
PASS does not apply to it. That is the whole claim, and it is checkable in one
command rather than taken on trust.
