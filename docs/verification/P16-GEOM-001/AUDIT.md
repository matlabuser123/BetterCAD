# P16-GEOM-001 — regeneration, authority and healing audit

Done before any production change. Every claim is about **this tree** and names the file and
line it came from.

## 1. The regeneration states the repository actually exposes

`features::NodeState`, in full — four values, and the repository really does distinguish
`Failed` from `Blocked`, so this milestone preserves that distinction rather than collapsing
it:

```text
UpToDate      "Inputs unchanged since the item was last built."
Regenerated   "Rebuilt in the last pass."
Failed        "Rebuilding failed, or the item is part of a dependency cycle."
Blocked       "Not rebuilt because something upstream failed or is missing."
```

No state was invented. There is no `Stale` state, which is the whole problem — see §3.

## 2. What happens to a body when regeneration fails

A correction to the obvious reading, and it matters because the brief's central worry rests on
it.

`Regenerator::body()` is documented as "Body produced by @p object in the **latest successful
build**, if any", which reads like a last-known-good cache that would survive a failure. It is
not. `src/features/Regenerator.cpp` erases the body in both failure paths:

```cpp
const auto fail = [&](ObjectId id, Error error) { ...; bodies_.erase(id); ... };
const auto block = [&](ObjectId id)             { ...; bodies_.erase(id); ... };
```

`RegeneratorTests` states it as a property — "Failed and blocked items have no (stale) result;
unrelated items keep theirs" — and asserts `regenerator.body(m.extrude1) == nullptr` with
`state(...) == Blocked`.

So:

```text
Can an old successful body remain accessible after a failed regeneration?   NO
```

P15-MASS-001 found the same thing and kept a guard anyway, documented as **"UNREACHABLE TODAY,
and kept deliberately … what it guards against is a mass computed from geometry that no longer
follows from the document. That is worth a lookup even at a probability of zero."** This
milestone keeps its equivalents on the same grounds.

## 3. The real hazard: `state()` is not a currency check

This is the gap nothing in the repository covered, and it is sharper than the one the brief
anticipated.

`state()` reports **what the last pass did**. It does not report whether the stored result is
still implied by the document. After an edit with no regeneration in between:

```text
state(feature)         UpToDate          <- the last pass's verdict, still true OF THAT PASS
body(feature)          the old body      <- valid, closed, positive volume
builtRevision(feature) != revisionOf     <- the only thing that knows
```

The Regenerator computes exactly this comparison internally, at `Regenerator.cpp`:

```cpp
if (built == builtRevisions_.end() || built->second != document.revisionOf(id) || unhealthy) {
    changed.insert(id);
}
```

and `builtRevisions_` was **private with no public accessor**. The information existed; nothing
outside could ask for it.

### The trap inside the gap

Comparing the feature's **own** revision is not enough, and a guard built that way would have
looked right and been wrong:

```text
edit a SKETCH  ->  the sketch's revision moves
                   the EXTRUDE's revision does NOT move
                   the extrude's body is nonetheless stale
```

The Regenerator handles this with `graph.downstreamOf(changed)`, and `downstreamOf` includes
its seeds (`DependencyGraph.cpp:56`). So currency has to walk the graph, which is what
`isCurrent()` does.

### Why it is one shared helper and not a second copy

`regenerate()` and `isCurrent()` now both call one private `dirtySources()`. A second copy of
the rule would be a second definition of staleness, and the two would drift the first time
either was touched. The only thing `regenerate()` adds is a term it alone can know — a
parameter expression that failed in the pass it is running — and that addition is visible at
the call site rather than buried.

`isCurrent()` therefore omits that one term, which can only make it answer *current* where a
pass would have found more work, never *stale* where a pass would have found none. The header
says so.

## 4. The authoritative body source

The path P15-MASS-001 uses, reused here unchanged in shape:

```text
owner of the current body     features::Regenerator, in its private bodies_ map
how it is queried             regenerator.body(ObjectId)
how currency is proven        NOT by state(). By builtRevision + the dependency
                              graph, which is what isCurrent() now does
```

`partMassProperties(const Document&, const Regenerator&, ObjectId)` is the precedent for the
signature, the const-ness and the order of its guards.

### An observation about mass properties, recorded and not acted on

`partMassProperties` checks the configuration override, then nullptr, then `Failed`/`Blocked`,
then volume. It does **not** check built-revision currency. So a caller that edits a sketch and
asks for mass properties without regenerating gets a mass computed from the previous
geometry.

That is outside this milestone, which authorizes the meshing boundary and not a change to P15
behaviour. It is recorded here, and `Meshable_...` tests demonstrate that the *meshing* boundary
refuses the case. Whether mass properties should refuse it too is a scope decision, not
Claude's.

## 5. Geometry validity: what BetterCAD already defines

`features::Validation::checkBody` (`src/features/Validation.cpp`) already defined body
eligibility, in this order:

```text
isEmpty           -> "produced an empty body"
solids == 0       -> "the body has no solid"
!isValid()        -> "the body fails the kernel's validity check"
no massProperties -> the kernel's own message
volume not > 0    -> "the body's volume is not positive"
```

and composed them into `BodySummary::valid`.

**This milestone's first implementation duplicated that rule** — the same checks, in a slightly
different order, inside `meshing`. Two definitions of valid geometry, and the weaker one would
eventually have decided what gets meshed. The brief forbids it in as many words: "Do not create
a weaker 'meshing valid' test that accepts shapes core considers invalid."

Resolved by extraction, not by a second rule: `features::bodyDefect(const Body&)` is now the one
definition, `validateDocument()` keeps the sentences for a user, and `meshing` maps the typed
reason. One rule, two presentations.

### Extracting it exposed a defect in the existing rule

The old test was `properties->volume > Volume{}`. **`Inf > 0` is true**, so an infinite volume
counted as a positive one and `BodySummary::valid` would have been `true` for it. `bodyDefect`
requires finite as well, which strictly strengthens `validateDocument()` too.

```text
BRepCheck_Analyzer   used via Body::isValid(), in exactly two places
                     (OcctBody.cpp, OcctSweeps.cpp)
ShapeFix             NOT USED ANYWHERE
ShapeAnalysis        NOT USED ANYWHERE
sewing               NOT USED ANYWHERE
```

## 6. Shape healing

```text
Does BetterCAD heal generated geometry today?   NO
Where?                                         nowhere -- ShapeFix and
                                               ShapeAnalysis appear in no source
                                               file
Does meshing need healing?                     NOT ESTABLISHED, and this
                                               milestone does none
```

So the policy is **rejection, not repair**, and it is the status quo rather than a new rule.
`requireMeshableGeometry` performs no sewing, no tolerance adjustment and no shape
modification; it copies a `Body`, and a `Body` copy shares the kernel shape.

The boundary this cannot guarantee is recorded rather than glossed: a volume-meshing **backend**
may repair geometry internally, and nothing here can prevent that. P16-GEOM establishes what
BetterCAD considers acceptable *before* a backend is invoked. Auditing what a backend then does
is `P16-VOL-001`'s, and it cannot begin until the backend decision is taken.

## 7. Tolerance

```text
closure            TOPOLOGICAL: topology().solids == 0. No tolerance.
positive volume    volume > 0 AND finite. No tolerance.
BRep validity      the kernel's own, via BRepCheck_Analyzer. Its tolerances are
                   the shape's own, set when the shape was built.
```

**No epsilon was introduced**, so §32's prohibition is satisfied by not needing it. "Greater
than zero and finite" has no threshold to tune, and therefore cannot reject legitimate
micro-scale geometry or accept a near-flat solid the size of a building — which is exactly what
a fixed SI threshold would have done.

The tolerances this milestone does **not** own, kept separate per §33: mesh quality thresholds
(`P16-QUALITY-001`), coordinate comparison in the mesh data model (`P16-DATA-001`, which has
none either), and the kernel's own topological tolerances.

## 8. Revision and fingerprint

```text
Document::revision()           monotonic, moves on EVERY effective change
Document::revisionOf(ObjectId) per object
Regenerator::builtRevision()   NEW: what a stored result was built from
buildDependencyGraph()         public, returns the graph and missing references
DependencyGraph::downstreamOf  public, includes its seeds
```

`Document::revision()` is unusable as a geometry stamp, and the reason is a requirement rather
than a preference: §39 demands that a material edit not invalidate geometry, and a density edit
moves the document revision. So `GeometryRevision` mixes the feature's own revision with the
revisions of **every object it transitively depends on**, plus the active configuration — all
logical dependency revisions, no BRep bytes.

This is the narrowing ADR-030 named in advance: its mesh build stamp uses the document revision
as "the outer guard … `P16-GEOM-001` may narrow it with measurement. **It may never widen it.**"

The mix is written out rather than taken from `std::hash`, because `std::hash` is not required
to agree between builds and this value is compared across three presets.

## 9. Copying, locations and future face mapping

```text
does preparation copy the shape?   it copies a Body, which "shares the underlying
                                   kernel shape" (Body.hpp)
can a TopLoc_Location be stripped?  there is no code path that could: this layer
                                   never names a TopoDS_Shape, a TShape or a
                                   Location
is subshape correspondence kept?    yes, for the same reason -- nothing is rebuilt,
                                   so P16-MAP-001's attribution is not disturbed
```

The brief's §25 concern is real for code that extracts a `TShape` and forgets to re-apply the
location. This layer cannot make that mistake, because OCCT is not reachable from it at all —
the architecture check confines OCCT to `src/(.+/)?occt/`, and `src/meshing/` has no such
directory.

## 10. Lifetime

`MeshableGeometry` holds a `Body` **by value**, plus a `GeometryRevision`. It does not hold a
reference to the document or to the regenerator, so it cannot dangle if either changes or is
destroyed, and a later asynchronous mesher cannot dereference state that has moved underneath
it. The snapshot is a value; the document remains the authority; and a result that records the
revision it was built from can be told it is stale without consulting anything that might have
gone away.
