# P17-PERSIST-001 — Structural Analysis Persistence

```text
AUTHORIZED 2026-10-14 by an explicit scope decision from the owner. TODO.md
had recorded "NOT AUTHORIZED: P17-CMD-001 passing will not be permission to
start it", which blocked CLAUDE from starting it unprompted -- not the owner
from authorizing it.
```

## Evidence

| Document | What it holds |
| --- | --- |
| [AUDIT.md](AUDIT.md) | the architecture audit: the capability table, and the four questions the brief asked me to decide that the project had already decided |
| [SCHEMA.md](SCHEMA.md) | every persisted field, the six schema decisions and why, and the version decision |
| [AUTHORITY.md](AUTHORITY.md) | the persist/do-not-persist table, and why the exclusion is **structural** rather than maintained |
| [NODAL_FORCE_POLICY.md](NODAL_FORCE_POLICY.md) | the one genuinely new decision: four candidates, and the **measured** cost of the one chosen |
| [BYTE_IDENTITY.md](BYTE_IDENTITY.md) | the hard gate — **four** states of one document, byte for byte |
| [UNRESOLVED_REFERENCES.md](UNRESOLVED_REFERENCES.md) | preserved, not rebound — and why a rebind cannot be written here at all |
| [ROUND_TRIP.md](ROUND_TRIP.md) | create → solve → save → destroy → load → remesh → re-solve, with the comparison table |
| [MALFORMED_INPUTS.md](MALFORMED_INPUTS.md) | 34 rejections, 2 deliberate acceptances, and where each rule lives |
| [BACKWARD_COMPATIBILITY.md](BACKWARD_COMPATIBILITY.md) | why the version does not move, and the fixture matrix |
| [DETERMINISM.md](DETERMINISM.md) | the mechanism, the measurements, and the committed fixtures' hashes |
| [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) | 33 attacks answered; **13 findings, 0 production defects**, 6 found by reading and 7 by a failing test |
| [MUTATION.md](MUTATION.md) | the probes, and the two the brief asked for that turned out to be **impossible to write** |
| [FREEZE.md](FREEZE.md) | the three-preset qualification |
| [ADR-048](../../architecture/decisions/ADR-048-structural-analysis-persistence-is-one-more-object-type-and-a-nodal-force-load-refuses-the-save.md) | one more object type; and a nodal force load refuses the save |

## The shape of the milestone

**The audit was the valuable part, again, and it said build one file.** Every
architectural question the brief raises already had an answer in the tree with
a recorded reason:

```text
file format, version rule, unknown-field policy, face-reference encoding,
atomic save, the save-failure path, the derived-state argument, enum file
keys, array-not-object for collections, path-named diagnostics
```

So P17 persistence adds **one object type to the existing format**: no second
file, no version bump, no migration code, no new validation framework, no new
reference encoding, no new determinism mechanism, and no change to any
structural production file.

```text
production change     1 new file   src/io/json/StructuralAnalysisJson.cpp
                      3 modified   ObjectJson.hpp (2 declarations + 1 include)
                                   DocumentJson.cpp (2 dispatch branches)
                                   src/io/CMakeLists.txt (1 source, 1 link)
structural module     UNCHANGED -- not one file
tests                 1 new file, 38 cases, 40 sections
fixtures              1 new hand-written document
```

## The one genuinely new decision

Everything else in this milestone followed a precedent. This had none:
**`NodalForceLoad` refuses the save.**

It is the only canonical payload holding mesh-local state — a `MeshStamp` and a
`NodeId` — and ADR-031, P17-LOAD-001's qualified declaration and the brief all
say independently that neither may reach a file. They agree, so there was no
conflict to resolve, only a consequence to choose: write it (a dead load
forever), write it without the stamp (the forbidden rebind, in the mesh
dimension), drop it silently (data loss), or refuse the save.

The save is refused, atomically, naming the load and saying what to do instead.
**The cost was measured rather than estimated**: `NodalForceLoad` has zero
callers in `apps/` and zero in `src/` outside its own definition. It exists only
in tests, which is exactly what P17-LOAD-001 said it was for — so the refusal
guards a path that does not exist yet, which is the cheapest moment to put a
guard in.

## Two claims that are stronger than tests, because they are signatures

```cpp
Result<Json> structuralAnalysisToJson(const structural::StructuralAnalysis& analysis);
```

The writer's only argument is the document object, and a `StructuralResult` does
not live on a document object. **There is no expression it could write that
depends on whether a solve has run.** Leaking one would mean changing this
signature and every caller — which is not an argument but a measurement: two of
the brief's requested mutations (§131–133) could not be written, and
[MUTATION.md](MUTATION.md) records them as unreachable rather than as probes
that passed.

```cpp
Result<std::unique_ptr<structural::StructuralAnalysis>>
structuralAnalysisFromJson(const Json& data, std::string name, std::string_view path);
```

No `Document`, no `Regenerator`, no `Mesher`, no geometry. **There is nothing
for an unresolved reference to be rebound to.** The nearest-face rebind the
brief asks about (§139) cannot be written in this file without first adding a
parameter.

## The gate that got stronger than the brief asked

The brief asks that save-before-solve equal save-after-solve. A format that
wrote a result only when the result was *valid* would pass that. So the test
compares **four** states of one document, each reached without touching any
document object:

```text
A  no result at all, no mesh generated          4426 bytes
B  a CURRENT result                             4426 bytes
C  a STALE result -- remeshed, which changes
   no document object                           4426 bytes
D  after a FAILED solve -- a fresh Mesher that
   has generated nothing                        4426 bytes

byte-for-byte identical, all four
```

And the structural section does not follow the mesh either: 499 vs 1313
tetrahedra, **907 bytes both times**.

## Where the deviations from P16 are, and why each one

This milestone follows P16-PERSIST-001 closely, and departs from it in exactly
three places. Each departure is because P17's data model says the opposite, in
as many words:

| | P16 | P17 | Why |
| --- | --- | --- | --- |
| collection order | sorted | **preserved** | P16-SIZE-001 declares the order meaningless; `StructuralAnalysisDefinition` declares it "THE USER'S AND IS PRESERVED", and P17-CMD-001's probe C8 exists because reordering was a real defect |
| who validates | all of it in `MeshControl::create` | per-record validators called explicitly | `validate(StructuralAnalysisDefinition)` checks the mesh handle and the solver settings and **nothing else** — measured, and P17-CMD-001's probe C11 measured the same asymmetry from the other side |
| can the writer fail? | no | **yes, once** | the nodal force load. `objectToJson` has always returned `Result<Json>`; this is the first writer to use it |

## Totals

```text
runtime tests          38 cases, 40 sections
rejection cases        34, each naming a JSON path; plus 2 deliberate
                       ACCEPTANCE cases, because "malformed" and "unresolved"
                       are different things and so are "invalid" and
                       "incomplete"
mutation               see MUTATION.md
adversarial review     33 attacks, 13 findings, 0 production defects
blast radius           io only. No structural file changed, so no P17
                       predecessor is requalified by this run
```

## Three things that went wrong and were recorded rather than tidied away

**I used a block to test mesh refinement, which cannot work — and I had written
that down before.** `fineElements > coarseElements` failed with `24 > 24`: a
block's boundary is planar and exactly representable, so 20 mm and 5 mm give the
same mesh. P16-SIZE-001, P16-CMD-001 and P16-PERSIST-001 each recorded it, it
was in my own standing notes, and I used a block anyway. Now a cylinder.

**I wrote a comment in the serializer that the measurement then falsified.** It
said `validate(StructuralLoad)` "is also what refuses a non-finite value ...
`readNumber` would accept an infinity written as 1e999". It does not get the
chance: nlohmann refuses the out-of-range literal outright. P16 had recorded
exactly that ("a non-finite global size cannot even be expressed") and I had
read it. The comment now says the validator is the second line, not the first.

**Two of my own assertions were weak, and reading caught them.**
`report.status() != Accepted` was being used to mean "the solve must not
proceed" — but it passes on `AcceptedWithWarnings`, which *does* proceed. And
the re-solve comparison asserted `== Accepted` on both runs, which would pass if
one warned and the other did not: the exact equivalence the test exists for. The
first is now `== Rejected`; the second compares the two verdicts and their
finding counts.
