# P16-CMD-001 — mutation testing

```text
SUBJECT:  whether the milestone's tests actually detect the defects they claim to
HARNESS:  mutations.json + apply.py + mutate.sh, carried from P16-VIZ-001
JUDGED BY: bettercad_tests.exe "[meshcmd]" under debug-ext, and both
           SURVIVORS re-run under "[meshcmd],[meshcontrol]"
```

**Why the filter widens for a survivor.** The main run used `[meshcmd]`, which
is `tests/meshing/MeshingCommandTests.cpp` alone;
`tests/meshing/MeshControlTests.cpp` is tagged `[meshcontrol]` and includes the
probe tests. A mutation killed by a *subset* of the tests is still killed, so
the ten kills need nothing further — but a mutation called **equivalent** must
be checked against every test that could have killed it, or "equivalent" is
just "not covered by the filter I chose". Both survivors were re-run under the
wider filter.

A mutation that **survives** is a gap in the tests, not a harmless variant —
unless it can be shown to be an **equivalent mutant**, meaning no legal
sequence can distinguish it from the original. That exception is used once
here, and the argument is written out rather than asserted.

Each mutation is a single substitution applied to a pristine snapshot, so
mutations cannot compound, and a pattern that is absent or occurs more than
once is refused rather than guessed at.

## How to run it

```bash
export PATH="/c/msys64/ucrt64/bin:$PATH"
export BETTERCAD_BUILD_ROOT=C:/Users/uqhas/AppData/Local/bc-build
bash docs/verification/P16-CMD-001/qualification/mutation/mutate.sh
```

**The lock is removed BEFORE the restore build, so a missing lock does not
mean the run is over.** `restore()` does `rm -f "$LOCK"` first, then restores
the sources, then rebuilds — and that rebuild takes minutes. A second build
started in that window collides on the static libraries:

```text
CMake Error: File can't be removed and still exist: lib\libbettercad_core.a
```

which is reported as `killed by the COMPILER` and is a **void verdict, not a
kill**. It happened here: the first attempt to re-run M3 and M5 produced two
such verdicts, both discarded, and the re-run was repeated after waiting for
`ninja.exe` and `cc1plus.exe` to disappear. Wait for build quiescence, not for
the lock.

**Do not edit the source tree while it runs**, and do not stop it by killing
only its parent shell: it restores every snapshotted file from its `EXIT`
trap, so an edit made during a run is silently reverted, and a concurrent
build of a half-edited tree makes a mutation look "killed by the COMPILER"
when nothing was wrong with it. Both were learned the expensive way on
P16-VIZ-001. The lock file refuses a second concurrent run; it cannot stop a
human, which is what this paragraph is for.

## Results

See [results.txt](results.txt) for the raw verdicts, and
[../../ADVERSARIAL_REVIEW.md](../../ADVERSARIAL_REVIEW.md) §4 for what each
mutation is testing.

### M5 as first written was a faulty mutation, not a surviving one

It survived, and the reason was in the mutation rather than in the tests:

```cpp
  const MeshControlDefinition kept = definition_;
  definition_ = definition;                            // assign first
  if (!validate(definition)) {
      definition_ = kept;                              // ... and roll back
      return std::unexpected(...);
  }
  definition_ = kept;
```

Assigning and then restoring is **observationally identical** to validating
first: nothing between the two statements can see `definition_`, and
`validate` does not throw. So the mutant was equivalent by construction and
could not have been killed by any test.

The intended defect — attack question 11, "can an invalid size mutate state
before failing?" — is that an invalid definition is assigned and **left in
place**. Corrected to touch only the failure path, so the valid path stays
byte-identical and the verdict is about atomicity and nothing else:

```cpp
  if (!validate(definition)) {
      definition_ = definition;                        // and left there
      return std::unexpected(...);
  }
```

Re-run alone, after the main run, and recorded in `results.txt` as a re-run.
This is the third self-authored mutation in this project to turn out faulty
(two on P16-VIZ-001: one a no-op, one unreachable), which is the reason every
surviving mutation here is read against the source before it is called a gap.

### M3 is an equivalent mutant, and here is why

M3 changes `MeshControlEditCommand::redo` from replaying the recorded
after-state to re-deriving it:

```cpp
- Result<bool> reapplied = apply(document, control_, *after_);
+ Result<bool> reapplied = apply(document, control_, *edit(*before_));
```

It survived. That is correct, and not a gap:

1. **Every `edit()` is a pure function of `(before, the command's own stored
   argument)`.** Read against the final diff: `SetMeshControlDefinitionCommand`
   ignores `before` entirely and returns its stored definition; the other six
   copy `before` and change one field, keyed by a `FaceName` or a
   `BoundarySetId` they already hold. None of them reads the document, the
   mesher, a clock, a random source or any global.
2. **`redo` is reachable only with the document holding exactly `before_`.**
   `CommandHistory` populates the redo stack only from `undo`, clears it on any
   new command, and binds to one document; and ADR-030 forbids editing a
   control outside a command.

So `edit(*before_) == *after_` on every legal path, including a multi-step
undo-all/redo-all, and no test could distinguish the two without first
violating the architecture.

The recorded `after_` is still the right implementation, for a reason the
mutation cannot expose: it does not depend on `edit()` remaining pure as the
commands grow, and it cannot fail, whereas a re-deriving redo would have to
handle an error it has no meaningful way to report. That is a robustness
argument, not a behavioural one — which is exactly why it shows up as an
equivalent mutant rather than as a failing test.
