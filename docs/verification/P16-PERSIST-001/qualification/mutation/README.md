# P16-PERSIST-001 — mutation testing

```text
SUBJECT:  whether the persistence tests actually detect the defects they claim to
TARGET:   src/io/json/MeshControlJson.cpp
JUDGED BY: bettercad_tests.exe "[persist]" under debug-ext
RESULT:   12 mutations, 12 killed, every one BY A TEST, 0 survivors
```

| # | Mutation | Verdict | Failing assertions |
| --- | --- | --- | --- |
| M1 | a local control's face reference is dropped on write | killed | 6 |
| M2 | lengths written in millimetres instead of SI metres | killed | 14 |
| M3 | a local size read with the wrong unit scale | killed | 16 |
| M4 | a boundary set given a fresh identity on load | killed | 7 |
| M5 | local controls written in stored order, not canonical | killed | 1 |
| M6 | boundary sets written in stored order, not canonical | killed | 1 |
| M7 | reading skips the core's validation entirely | killed | 29 |
| M8 | a repeated quality metric silently keeps the last | killed | 1 |
| M9 | an absent target size written as a zero | killed | 5 |
| M10 | a local control with an unreadable face is skipped | killed | 2 |
| M11 | the linear deflection written as the default | killed | 2 |
| M12 | the threshold policy dropped on write | killed | 9 |

## M11 was first killed only by the compiler, which is not the same thing

Written to put defaults in **both** deflections, it left the `surface`
parameter unused and `-Werror=unused-parameter` rejected it — so the mutation
was never evaluated by a test at all. A results file records that as "killed
by the COMPILER", which reads almost identically to a real kill.

Rewritten to default only the linear deflection and keep the parameter used,
it compiles and dies on two assertions in
`MeshingPersistence_SurfaceDeflectionsRoundTripExactly`. The weaker verdict is
left in `results.txt` above the re-run rather than deleted.

## M5 and M6 each die on exactly one assertion

Worth stating rather than glossing. `Json` is `nlohmann::ordered_json`, so a
file's order is its insertion order, and the only thing standing between
canonical ordering and a document whose bytes depend on the order somebody
happened to click is the edit-order half of
`MeshingPersistence_SerializationIsByteIdenticalAndIndependentOfEditOrder`.
One assertion is sufficient; it is also exactly one.

## The first run was orphaned and left a mutation applied

`mutate.sh` died with the session **without its EXIT trap firing**, leaving M3
applied to the production file — `Length::fromSi(*size / 1000.0)` — and a
stale lock whose pid no longer existed.

Git could not see it: the file is new and therefore untracked, so
`git status` shows `??` whatever the contents and `git diff` shows nothing.
Caught by diffing the working file against `pristine/`, restored, rebuilt and
re-verified before continuing.

M1 and M2 keep their original verdicts — each mutation is applied to a
pristine snapshot and judged independently, so they do not depend on the rest
of the loop. M3 had no verdict (its `run.log` holds only a header) and was
re-run with the rest.

## How to run it

```bash
export PATH="/c/msys64/ucrt64/bin:$PATH"
export BETTERCAD_BUILD_ROOT=C:/Users/uqhas/AppData/Local/bc-build
bash docs/verification/P16-PERSIST-001/qualification/mutation/mutate.sh
```

`run-remaining.py` runs a sub-range instead, and is the one to prefer: it is
launched **detached** so a session ending cannot orphan it, and it restores in
a `finally` block rather than a shell trap.

**Afterwards, whatever happened, check the tree:**

```bash
M=docs/verification/P16-PERSIST-001/qualification/mutation
diff -q "$M/pristine/src__io__json__MeshControlJson.cpp"         src/io/json/MeshControlJson.cpp || python $M/apply.py restore
```

Do not trust the lock's absence — it is removed before the restore build — and
do not trust its presence either, as a dead run leaves it behind.
