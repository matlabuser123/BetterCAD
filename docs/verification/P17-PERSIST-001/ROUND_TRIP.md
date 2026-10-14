# P17-PERSIST-001 — the round trip, and the re-solve

```text
create -> solve -> save -> DESTROY EVERYTHING -> load -> remesh -> re-solve
                                                      -> compare
```

## The destroy is real

`StructuralPersistence_AFullDestroyAndLoadRoundTripReSolvesEquivalently` is
the central qualification test, and the thing it is careful about is that
nothing crosses the boundary but the file. The original document, regenerator,
mesher, mesh and solve outcome all live inside a scope that ends before the
load, and the only things carried out are plain `double`s, `size_t`s and a
`std::string` fingerprint:

```cpp
{
    AnalysedPart part;                      // document, regenerator, mesher
    ...
    REQUIRE(io::saveDocument(part.document, file).has_value());
}
// Every object above is destroyed here. No pointer, no document, no mesh
// and no result survives into the block below.

Result<Document> loaded = io::loadDocument(file);
```

The reload then uses a **fresh** `Regenerator` and a **fresh** `Mesher`, so
there is no cache for a result to be found in.

## The path after the load is the production path

```text
io::loadDocument                            restores intent, nothing else
features::Regenerator  -> requireReport     the normal P12 regeneration
meshing::Mesher::generate                   the normal P16 mesh path
structural::solveStructuralAnalysis         the normal P17-VALID entry
```

No private shortcut, no injected result, and nothing that bypasses validation.
`solveStructuralAnalysis` is the single production entry and its own header
states that it "never calls `Mesher::generate`" — so the mesh had to be
generated explicitly, which is what makes "load does not auto-remesh" visible
rather than assumed.

## The canonical intent compares equal before anything is regenerated

The fingerprint is checked **first**, before the regeneration and before the
solve, so a pass cannot be explained by derived state agreeing:

```cpp
CHECK(fingerprint(onlyAnalysis(*loaded)) == originalIntent);
```

The fingerprint reads the mesh control, the mode, every load in order with its
target and value at `{:.17g}`, every restraint in order with its mask, and all
three solver settings. It reads no revision, no mesh, no result. Seventeen
significant digits distinguish every distinct double, so a value that
round-tripped to the nearest neighbour would show rather than compare equal.

The whole definition is *also* compared through its own `operator==`, which is
field-complete and `= default`ed — so a member added to
`StructuralAnalysisDefinition` later without being serialized fails the
round-trip test even if the hand-written fingerprint forgot it.

## The measured comparison

Fixture: 40 × 30 × 20 mm steel block (E = 210 GPa, ν = 0.3), 20 mm global mesh
with a 6 mm control on one side face, 2.5 MPa pressure on the end cap, fixed
support on the start cap, `relativeResidualTolerance = 1.23456789e-8`,
`pivotFloor = 2.5e-11`.

| Metric | Original | Reloaded and re-solved | Difference | Tolerance | PASS |
| --- | --- | --- | --- | --- | --- |
| nodes | 19 | 19 | 0 | **exact** | ✓ |
| tetrahedra | 70 | 70 | 0 | **exact** | ✓ |
| free equations | 45 | 45 | 0 | **exact** | ✓ |
| strain energy | 3.159077485944374e-4 J | same | 0 | 1e-12 rel | ✓ |
| max \|u\| | 2.3767488610034445e-7 m | same | 0 | 1e-12 rel | ✓ |
| max von Mises | 2586035.5147892255 Pa | same | 0 | 1e-12 rel | ✓ |
| reaction R_z | 2999.9999999999982 N | same | 0 | 1e-12 rel | ✓ |
| normalized residual | 3.141e-17 | 3.141e-17 | 0 | both < 1.23456789e-8 | ✓ |
| validation status | *(recorded)* | **the same value** | — | equality | ✓ |
| validation findings | *(recorded)* | the same count | 0 | exact | ✓ |
| constrained nodes | *(recorded)* | the same count | 0 | exact | ✓ |
| prepared load resultant | *(recorded)* | same | 0 | 1e-12 rel | ✓ |

**The node and element counts are equalities, not bounds**, because P16's
regeneration from identical intent is deterministic. The brief asked not to
*rely* on that (§58) and to record it if it held (§59) — it holds, and it is
recorded as additional evidence rather than as the gate. The gate is the
canonical intent plus the engineering semantics.

**The validation status is compared for EQUALITY, not against `Accepted`.** The
first version of that check asserted `== Accepted`, which would have passed if
one run warned and the other did not — the exact equivalence the test exists
for. It now records the original verdict and the finding count and requires the
reloaded run to match both.

**R_z = 2999.9999999999982 N against an exact 3000 N.** 2.5 MPa over the
40 × 30 mm end cap is 3000 N exactly, and the measured reaction is that to
within 6e-16 relative — which is the equilibrium identity P17-REACTION-001
qualified, holding across a save and reload.

## The reference binds to the same geometry, not merely to an equal record

`StructuralPersistence_AReloadedFaceLoadResolvesToTheSameGeometry` is the half
that matters physically. A `FaceName` comparing equal proves the record
survived; it does not prove the record still *means* the same face. So this
test measures the prepared load resultant and the constrained node count after
the reload — both of which are produced by P16's mapping resolving the loaded
reference against a freshly generated mesh — and requires them to match.

## Result currentness after the load

```text
original had a solved result                      YES
any result array serialized                       NO
a fresh load reports a current solved result      NO
solve requested                                   YES
a new result generated                            YES
the new result is current against the reloaded
  document's own source                           YES (staleReasons empty)
```

The "no" in the middle is **structural rather than defended**: a
`StructuralResult` does not live on a document object, so a loaded analysis has
nowhere to carry one. `StructuralPersistence_ALoadedAnalysisHasIntentAndNoCurrentResult`
asks the production path and gets a refusal for want of a current mesh, and
then checks that the fresh `Mesher` still holds nothing — which is what "no
auto-solve and no auto-remesh on load" means in this architecture.

The final check uses `staleReasons` against `currentResultSource`, which is
P17-DATA-001's own comparison, and requires it to be empty: the new result is
bound to the mesh that exists *now*.

> An earlier form of that check asserted `source().mesh.generation != 0` and was
> simply wrong. A `MeshStamp`'s generation counts rebuilds from one source and
> is 0 for the first mesh a fresh `Mesher` makes — which is exactly what happens
> here. It is recorded because the wrong version looked more convincing than the
> right one.

## Post-load integration

Two tests prove the load restored a live document object rather than a frozen
value:

```text
..._CommandsUndoAndRedoExactlyAfterALoad     add a load, add a restraint,
                                             undo both, redo both, remove one
                                             by identity -- all through
                                             P17-CMD-001's commands on the
                                             loaded document
..._ALoadedIdCannotBeReusedByALaterCommand   a load whose LoadId came from the
                                             FILE still collides correctly
..._AnObjectCreatedAfterALoadCannotCollide   the document's ObjectId allocator
                                             is restored from
                                             last_allocated_id
```

On the brief's §83: `LoadId` and `RestraintId` deliberately do **not** widen to
`ObjectId` — `Id.hpp` says "the two IDs below deliberately do NOT widen, and
that difference is the whole of this milestone's identity model" — so they are
not document-allocated and there is no allocator to restore for them. The
caller chooses the id and a collision is prevented by **refusal**, not by
allocation. What needed proving was that the refusal still works against ids
that came from a file rather than from this session, and it does.
