# P16-DATA-001 — adversarial review

Every attack the milestone brief lists, answered against the implementation rather than
against the design. An attack answered by a test names the test; one answered by a type names
the compile-fail case; one answered by reading says what was read.

```text
ATTACKS              23 from the brief, plus 5 raised here
FINDINGS             7
PRODUCTION DEFECTS   0
SHARED-TOOLING
DEFECTS              1 -- in the qualification harness, latent since P15-QUAL-001
FIXED                7
REMAINING            0 open; 1 deferred with a documented reason (topological duplicates)
```

Five of the seven were found **while building**, which is where they are cheap. Two are worth
reading. **F3** is a fact about tetrahedra that would mislead anyone trying to repair an
inverted mesh, and my own test had it wrong. **F7** is a defect in the qualification harness
itself that had been latent for three phases and cost this milestone a two-hour re-run.

---

## Findings

### F1 — A compile-fail case that could not fail

**Attack.** Does every compile-fail case fail *for the reason claimed*?

`TET_WITH_TRIANGLE_CONNECTIVITY` asserted that `addTetrahedron({n1, n2, n3}, region)` is a
compile error. **It is not.** A braced list with too few elements is valid aggregate
initialisation of `std::array<NodeId, 4>` and zero-fills the fourth. The case would have
compiled, and the suite would have reported a failure that said nothing about safety.

**Resolution.** Removed, with a comment at the site saying why it cannot exist, and replaced
by `MeshBuilder_RejectsAnElementWithAnUnderfilledConnectivityList`, which pins that the zero
handle aggregate initialisation produces is refused as the invalid handle. Too *many*
elements is a genuine compile error and remains as a case.

### F2 — A diagnostic regex that did not match the diagnostic

**Attack.** The same question, applied to the case that survived.

`triangle-with-tet-connectivity` asked for `could not convert`. GCC 16.1.0 emits `cannot
convert` followed by the brace-enclosed-initializer-list wording. The case failed to compile
exactly as intended and the harness still refused to pass it — which is the harness working:
a compile-fail test that only checks "the build failed" proves nothing about *why*.

**Resolution.** The regex now matches the real message and names the arity it is about.

### F3 — Reversing a tetrahedron's connectivity does NOT invert it

**Attack.** Build a mesh with two inverted tetrahedra and check that validation reports both.

It reported one. The second element — the reference tetrahedron with its four handles
reversed, `(1,2,3,4)` to `(4,3,2,1)` — has **positive** volume, and the production code was
right to accept it.

Reversing a 4-tuple is the permutation `(1 4)(2 3)`: **two transpositions, an even
permutation, so the determinant's sign is preserved.** The intuition that "reversed" means
"inside out" is simply false for a tetrahedron.

This is worth more than a fixture correction, because it is a trap in the domain: **a mesher
author who "repairs" inverted elements by reversing their connectivity changes nothing at
all.** Only an odd permutation flips the sign.

**Resolution.** The fixture now uses odd permutations, with a comment recording the error so
it is not reintroduced. A dedicated test,
`SignedVolume_SignFollowsThePermutationParityNotTheApparentReversal`, pins all four cases
against the hand-computed reference: full reversal (even, preserved), one swap (odd,
flipped), 3-cycle (even, preserved).

### F4 — A latent missing include

`issueLess` uses `std::tuple` and `MeshValidation.cpp` did not include `<tuple>`. It compiled
through a transitive include and would have broken on any standard-library reshuffle. Found
by reading the file against the facilities it uses, not by the compiler. Added; the unused
`<cmath>` was removed at the same time.

### F5 — A test that asserted nothing in one branch

`Validate_OrdersIssuesByKindThenByHandle` called `.error()` on a `Result` that might have held
a value — undefined behaviour had the insertion succeeded, and no assertion at all about the
ordering it was named for. Rewritten to assert the exact expected order of two issues of
different kinds, the later-added one reported first.

### F6 — Three semantics left accidental

The brief requires each of these to be decided explicitly rather than by omission. A grep
established that all three were undocumented — verified absent, not assumed present:

```text
topological duplicate elements   two tetrahedra on the same four nodes
mesh equality                    exact representation, or semantic equivalence?
thread safety                    what may be done concurrently?
```

**Resolution.** All three documented at the declaration. Duplicates are **not** rejected, and
the header says why: which of the two is right is not answerable from connectivity alone, one
ordering may be inverted, detecting the pair needs a canonical key over node sets that is a
whole-mesh question, and refusing it at insertion would make adding an element cost more as
the mesh grows. It is P16-QUALITY-001's, recorded so the silence is not read as an oversight.

### F7 — The qualification harness could not express this milestone's own subject

**Attack.** Run the regression.

All three presets passed — 2875/2875 each, clean builds, proven no-op rebuilds — and the run
then died with `else was unexpected at this time`, exit 255, **before the determinism stage**.

`%REPEAT%` is substituted when cmd **parses** the enclosing `for ... do ( ... )` block, not
when it executes. This milestone's subject needs an alternation over ten test-name prefixes, so
the filter contains `|`, `(` and `)` — which closed the block early and orphaned the `else`.

The defect has been in the harness since P15-QUAL-001 and survived three phases only because
every earlier subject was a single word: `architecture`, `cli.material`. Nothing before needed
an alternation. Its failure mode is the expensive one: it destroys the **last** gate after all
the costly ones have passed, and leaves a times file showing three green presets and no
determinism line at all — which reads like an omission rather than a failure.

**Resolution.** `!REPEAT!` — delayed expansion, substituted after the block is parsed. Three
uses, with the reason recorded in the harness header so it propagates to every milestone that
copies the harness forward. `verify-harness.cmd` was re-run and still passes, and the exact
filter that broke it was proven to parse against a nonexistent preset, which cost seconds
rather than another full build.

This is not a production defect — no shipped code is affected — but it is a defect in shared
infrastructure that every future milestone depends on, and it was found by being the first
subject that needed more than one word.

---

## The brief's attacks, answered

**Can `ObjectId` implicitly become `NodeId`?** No — `object-id-as-node-id` and
`sketch-id-as-node-id`. And not the reverse: `node-id-as-object-id`,
`element-id-as-object-id`, `region-id-as-object-id`. These are ADR-031's central invariant and
are the reason the handles are not `bettercad::Id`, which *would* have widened.

**Can `NodeId` implicitly become `ElementId`?** No — `node-id-as-element-id`,
`element-id-as-node-id`, and they do not even compare (`compare-node-and-element`).

**Can the same `NodeId` be inserted twice?** No, and it is *unrepresentable* rather than
merely rejected: a supplied handle must be strictly greater than the last, so a repeat and an
out-of-order handle fail identically with `AlreadyExists`
(`MeshBuilder_RejectsARepeatedNodeHandleAndLeavesTheMeshUnchanged`,
`..._RejectsAnOutOfOrderNodeHandle`). The first test also checks the *original* node is
untouched: no last-write-wins.

**Can missing `NodeId` connectivity silently resolve by vector position?** No. `findNode` is a
binary search over ascending storage by identity, and the source comment forbids
`nodes_[id.value()]` at the point where someone would write it.

**Can sparse IDs index the wrong node?**
`Mesh_ResolvesSparseHandlesByIdentityAndNeverByPosition` builds nodes 1, 4 and 10, checks each
resolves to its own coordinates, and checks 2, 5 and 11 resolve to nothing.

**Can repeated Tet4 nodes survive?** No —
`MeshBuilder_RejectsAnElementNamingTheSameNodeTwice`, for both a tetrahedron and a triangle,
reported as a connectivity defect rather than indirectly as a zero volume.

**Can a coplanar Tet4 survive because only ID uniqueness is checked?** No —
`Validate_RejectsACoplanarTetrahedronDespiteFourDistinctHandles`. Its connectivity is
impeccable; the geometry is what fails.

**Can an inverted Tet4 become valid because volume uses `abs()`?** There is no `abs` in the
module: a grep for `abs|fabs` matches only the comment forbidding it. The one `sqrt` is in
`triangleArea`, where the quantity is genuinely unsigned.

**Can a reflection remain falsely positive?** No — `SignedVolume_ChangesSignUnderAReflection`.
A reflection has determinant −1, and the test requires the sign to flip and the magnitude to
match.

**Can NaN coordinates sneak through comparisons?** No.
`MeshBuilder_RejectsANonFiniteCoordinateOnEveryAxis` covers NaN, +Inf and −Inf on each of x, y
and z — nine cases — and checks the builder is unchanged after each. `validate()` re-checks,
so a mesh is never trusted merely because its builder was.

**Can Infinity produce a seemingly positive volume?**
`Validate_RejectsANonFiniteDeterminantFromExtremeButFiniteCoordinates` uses coordinates of
1e300, each finite, whose triple product overflows. The code tests `!isFinite(volume)`
**before** comparing against zero, precisely because an infinity answers "greater than zero"
with a yes.

**Can triangle orientation be lost by sorting NodeIds?** No —
`MeshBuilder_NeverReordersElementConnectivity` gives handles in the order 3, 1, 4, 2 and
requires exactly that order back.

**Can Tet connectivity be reordered silently?** The same test. Nothing in the module sorts an
element's handles, and both element declarations say why: the sign is the only evidence of a
generator's inversion, and normalising it destroys that evidence. ADR-032 requires rejection,
not repair.

**Can `unordered_map` iteration alter element order?** There is no unordered container in the
module. `MeshValidation.cpp` uses `std::map` with a comment saying why.
`Mesh_EnumeratesNodesAndElementsInHandleOrderEveryTime` rebuilds 8 times and compares the full
handle sequence, the bounds and the validation report;
`Validate_ProducesAnIdenticalReportOnRepeatedRuns` compares reports 8 times including their
*messages*.

**Can solver-facing consumers mutate coordinates?** No — `mutate-node-through-mesh`.

**Can P17 obtain mutable topology?** No — `add-element-through-mesh`. Every accessor returns
`std::span<const T>`, and `build()` returns by value.

**Can equal coordinates cause silent node merging?** No —
`Mesh_TwoNodesAtTheSamePositionStayTwoNodes`. Identity and position are different concepts,
and two coincident nodes may be a contact interface or a disconnected region; the data model
does not get to decide.

**Can backend-specific IDs become canonical IDs?** There is no path: `integer-to-node-id`
blocks the implicit route, `NodeId::fromValue` is explicit and named, and the API audit
confirms no backend name appears anywhere in the module.

**Can `gp_Pnt` leak through the public API?** No. The public headers' entire BetterCAD include
closure is `Error.hpp`, `Point.hpp`, `Units.hpp` and the generated `Export.hpp`, and no OCCT
identifier appears in any of them.

**Can the bounds of an empty mesh fabricate a valid zero box?** No —
`Bounds_OfAnEmptyMeshAreAbsentRatherThanAZeroBox` requires `nullopt`, and in the same test
requires a mesh holding one node at the origin to return a real zero-extent box, so the two
stay distinguishable.

**Can a duplicate element overwrite another?** `ElementId`s come from one monotonic counter
shared by both element kinds, so two elements cannot carry the same one. There is deliberately
no explicit-`ElementId` overload to abuse.

**Can invalid insertion partially mutate the mesh?** Every rejection returns before touching
storage, and every failure test checks the count is unchanged — non-finite coordinate,
repeated handle, out-of-order handle, invalid handle, missing node, repeated node, missing
region, under-filled list.

**Can a very large coordinate overflow be reported as valid?** Covered above, and the answer
is a refusal rather than a silent `false`.

---

## Attacks raised here, beyond the brief

**Can a handle from one mesh be used against another?** Both meshes have a `node:1` and they
are different nodes. `Mesh_RefusesAHandleFromAnotherMeshRatherThanReinterpretingIt` checks
`owns()` refuses the other mesh's stamp, the default stamp, and — importantly — the *same*
`MeshId` with a bumped generation, which is what a remesh produces.

**Can `build()` return a mesh that later changes?** No: it returns by value, and
`Mesh_CopyKeepsTheSameTopologyAndHandlesAndIsNotARemesh` pins that a copy is the same topology
with the same handles and stamp — a value copy, not a remesh.

**Is a thin element rejected as degenerate?** No, deliberately —
`Validate_AcceptsAThinTetrahedronBecauseQualityIsNotDataValidity` uses a 1e-12 m sliver and
requires it to be data-valid. There is no tolerance anywhere in the validation file, because a
tolerance here would silently become a quality threshold nobody chose. Degeneracy is exactly
zero or non-finite; thinness is P16-QUALITY-001's.

**Does validation stop at the first problem?**
`Validate_CollectsEveryIssueRatherThanStoppingAtTheFirst` requires two inverted tetrahedra and
one degenerate one to be reported together. This is the test F3 came out of.

**Is the report's order really by kind rather than by insertion?**
`Validate_OrdersIssuesByKindThenByHandle` adds the degenerate tetrahedron **first** and the
degenerate triangle **second**, and requires the triangle to be reported first because its
kind sorts earlier.

---

## What this review did not cover

Nothing in this milestone generates a mesh, so nothing here shows that the model can represent
a real discretisation of a real body — only that what it does represent, it represents
coherently, and refuses to represent incoherently. The first mesh from geometry is
`P16-SURF-001`'s, and the first from a volume backend is `P16-VOL-001`'s, which is gated on a
dependency decision that is not Claude's to take.
