# P17-PERSIST-001 — the byte-identity gate

```text
THE HARD GATE:  the same canonical intent, saved with and without a solve,
                must produce THE SAME BYTES. Not the same size. Not the same
                object count.
```

P16's equivalent gate was that the file does not grow with mesh density, and it
needed a 64-byte tolerance because the two documents differed in the digits of
one number. **P17's is exact**, because the canonical intent across the two
saves is not merely similar but literally unchanged: it is the same document,
saved twice.

## Four states, not two

A format that wrote a result only when the result was *valid* would pass a
before/after comparison. So `StructuralPersistence_TheFileIsIdenticalWhateverTheSolveHasDone`
compares four states of one document, each reached without touching any
document object:

| State | Solved? | Post / reaction? | Result in memory | Bytes |
| --- | --- | --- | --- | --- |
| A — no result | no | no | none; no mesh generated either | **4426** |
| B — current result | yes | yes | current | **4426** |
| C — **stale** result | yes | yes | stale (new `MeshStamp`) | **4426** |
| D — after a **failed** solve | attempted | no | none; the attempt was refused | **4426** |

```text
bytes(A) == bytes(B)    byte-for-byte
bytes(A) == bytes(C)    byte-for-byte
bytes(A) == bytes(D)    byte-for-byte
```

**The sizes are recorded as supplemental.** Equal sizes would not have proved
anything, which is why the test compares the strings and not their lengths —
the brief's §182, and it is worth saying plainly that the comparison is
`noResult == currentResult` and not `.size() == .size()`.

### How each state is reached without changing the intent

This is the part that makes the test about the file rather than about an edit.

**C, a stale result, from an untouched document.** The mesh is regenerated.
That gives a new `MeshStamp`, which makes the result from B detectably stale
through `staleReasons` — and it changes **no document object at all**, because
a meshing control's definition is untouched by generating a mesh from it. The
test requires `staleReasons` to be non-empty, so the state really is stale and
not merely claimed to be.

**D, a failed solve, from the same untouched document.** A second, fresh
`Mesher` has generated nothing, so `solveStructuralAnalysis` refuses for want
of a current mesh. The document is identical; only the attempt failed.

### Not vacuous

```text
first->largestVonMises.si() > 0.0            a real solve happened
first->result.displacements().size() > 0     it produced real arrays
staleReasons(...) non-empty                  state C really is stale
failed.has_value() == false                  state D really did fail
```

Without these four the test could pass over a document that never solved at
all.

## Independence from the mesh and the result size

`StructuralPersistence_TheFileDoesNotFollowTheMeshOrTheResultSize` is the P16
sibling, in the structural dimension:

```text
coarse   20 mm global     499 elements  ->  structural section 907 bytes
fine    2.5 mm global    1313 elements  ->  structural section 907 bytes
                                            IDENTICAL
```

A 2.6× mesh and a 2.6× result array change the structural section by **zero
bytes**.

**A cylinder, not a block, and that is not a detail.** The first version used
the block fixture and failed with `24 > 24`: a block's boundary is planar and
exactly representable, so 20 mm and 5 mm produce the same 24 tetrahedra and
there is no mesh growth for the file to be independent *of*. P16-SIZE-001,
P16-CMD-001 and P16-PERSIST-001 each recorded the same thing, and this
milestone rediscovered it. The test now builds an r10 × 20 mm cylinder.

**The structural section, not the whole file.** Only the meshing intent differs
between the two documents, so comparing whole files would compare that
difference too. The comparison is scoped to the analysis object's own text, from
its type marker onward.

## State that is not solver state, and is also excluded

`StructuralPersistence_TheCommandHistoryDoesNotReachTheFile` reaches one final
canonical state two ways and requires identical bytes:

```text
path A   setDefinition, directly
path B   add a load; add a decoy; UNDO; REDO; UNDO; add a restraint
```

Path B ends with a four-deep history and a **different object revision**, which
the test asserts to prove the two documents really are distinguishable:

```cpp
CHECK(analysisSectionOf(saveToString(viaCommands.document)) == expected);
CHECK(onlyAnalysis(viaCommands.document).revision() !=
      onlyAnalysis(direct.document).revision());
```

So the revision, the undo depth and the edit count are all absent from the file,
and a strong P17-CMD/PERSIST integration claim comes out of it: the history
path does not leak into persistence.

## A save does not change the document

The brief's §186. `StructuralPersistence_SavingChangesNothingAboutTheDocument`
solves, confirms the result is current, saves, and then requires:

```text
the object's revision        unchanged
the canonical fingerprint    unchanged
the result                   STILL CURRENT -- saving neither staled it nor
                             refreshed anything
```

## Why this is a byte comparison and not a hash

The brief asks for SHA-256 in §101 and §165, and for actual byte comparison in
§183 ("Record both"). The gate here is the **byte comparison**, done inside the
test on the four saved files, which is strictly stronger than comparing digests.

Hashes of the two artefacts that persist on disk are recorded in
[DETERMINISM.md](DETERMINISM.md). The four files above live in a per-test
temporary directory that is removed on teardown, and there is no CLI path that
saves a document after solving it — `P17-CLI-001` is not built, and `solve` does
not write — so producing hashable artefacts for them would have meant either
test-only behaviour, which this project forbids, or a throwaway binary whose
output nothing else checks. The direct comparison is the better evidence and it
is what the gate uses.

## What would make this fail

Measured, not predicted. See [MUTATION.md](MUTATION.md):

- **P1**, which writes the analysis object's `revision()` into the file — a
  single non-canonical field — was killed by 28 of the 64 tests in the filter.

Two of the brief's requested mutations (§131–133: serialize `max von Mises`,
serialize a displacement count) turned out to be **impossible to write** without
changing the serializer's signature, because no solve output is reachable from
it. That is recorded as the measurement it is rather than as a mutation that
"passed".
