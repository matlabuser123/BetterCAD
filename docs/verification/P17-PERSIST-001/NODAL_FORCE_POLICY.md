# P17-PERSIST-001 — the nodal force load, and why the save is refused

```text
DECISION: a document containing a NodalForceLoad CANNOT BE SAVED. The save
          fails atomically, naming the load and saying why.
COST:     MEASURED at zero product paths. NodalForceLoad has no caller in
          apps/ or src/ -- it exists only in tests, which is what
          P17-LOAD-001 declared it was for.
```

This is the only thing in P17 persistence that is genuinely new rather than
inherited from P15 or P16. Everything else in this milestone follows a
precedent; this had none.

## The problem

`NodalForceLoad` is one of the four canonical load payloads. Unlike the other
three it holds mesh-local state:

```cpp
struct NodalForceLoad {
    meshing::MeshStamp mesh{};   // which mesh, and which GENERATION of it
    meshing::NodeId node{};      // a handle into that one generation
    Force3D force{};
};
```

## Three authorities, agreeing

There is no conflict to resolve here, which is what makes this different from
P17-CMD-001's version of the same question. All three say the same thing:

```text
ADR-031                  "A NodeId or an ElementId is mesh-local, reused
                         across generations and NEVER PERSISTED"

P17-LOAD-001 (qualified) "MESH-LOCAL BY DECLARATION ... it carries the
                         MeshStamp it was created against and is refused
                         against any other mesh ... What it is NOT: durable
                         point-load intent on the model"

the brief                "NodeId persisted in the structural schema" is an
                         automatic FAIL
```

So the only open question was what should happen to a document that holds one.

## The four candidates

**A. Write it, stamp and all.** Rejected. It puts a `NodeId` and a mesh
generation counter in a file, against ADR-031 and the brief. And it does not
even work: a `MeshStamp` names a mesh generation that cannot exist after a
reload, so P17-LOAD-001's own rule would refuse the reloaded load **for the
rest of the document's life**. The file would carry a permanently dead load
that nothing could fix except editing it away.

**B. Write the node and force but drop the stamp.** Rejected, and this is the
worst of the four. Dropping the stamp is precisely the rebinding the brief
forbids, in the mesh dimension instead of the geometric one — and P17-LOAD-001
refuses a nodal force against "any other mesh, **including one with the same
node count and the same numeric handles**, which a remesh of an unchanged model
produces". Silently reattaching the handle to a new mesh would apply a force at
whatever node happens to hold that number now.

**C. Drop the load on save.** Rejected. Silent data loss, in a format whose
whole character is the opposite — "an unrecognised object type is a parse error
naming it, never a silent skip, so no data is lost".

**D. Refuse the save, naming the load.** **Chosen.**

```text
error: load 12 is a nodal force load, which names a mesh node and the mesh
       generation it belongs to; neither is canonical CAD intent and neither
       can be saved (ADR-031). Apply the force to a CAD face as a traction or
       a pressure.
```

## Why D is affordable, measured rather than assumed

A refusal that made real documents unsavable would be a bad trade. It does not,
and the search says so rather than the reasoning:

```text
NodalForceLoad in apps/        0 matches
NodalForceLoad in src/         1 match -- its own definition
                               (src/structural/StructuralLoad.cpp)
NodalForceLoad in tests/       10 files
```

**There is no product path that can create one.** No GUI command, no CLI, no
application code. It is reachable only through `AddStructuralLoadCommand` from
code that constructs the payload directly, which today means a test fixture.
That is exactly what P17-LOAD-001 said it was for: "element and solver
fixtures, where a test wants a force at a known node."

So the refusal guards a path that does not exist yet, which is the cheapest
moment to put a guard in. The alternative — discovering in P17-CLI-001 that
saving silently dropped a load — costs much more.

## The refusal is atomic, and that came free

`objectToJson` has always returned `Result<Json>`, because `documentToJson`
"fails with InvalidArgument if it contains an object kind the format does not
support". So the failure path already existed; this is the first *writer* to
use it, and the only one in the format that can fail.

`saveDocument` propagates the error before opening its temporary file, so:

```text
io::documentToJson(document)      fails, naming the load
io::saveDocument(document, path)  fails
the file                          DOES NOT EXIST -- not truncated, not partial
the document                      unchanged; the attempt costs nothing but
                                  the save
```

All four are asserted by `StructuralPersistence_ANodalForceLoadIsRefusedRatherThanWritten`,
which then removes the load and saves the same document successfully — so the
refusal is attributable to the nodal force and not to the fixture.

## The reader refuses it too, by name

A hand-written file claiming `"type": "nodal_force"` is refused with a
diagnostic that says what is wrong, rather than falling through to "unknown
load type" — and certainly rather than being read as a zero load, which is the
failure mode the brief's §74 names.

`nodal_force` stays in the writer's name table so the lookup is total and a
diagnostic can name the kind. It is a key the writer never emits and the reader
never accepts, and
`StructuralPersistence_TheReaderRefusesANodalForceLoadByName` holds that half.

## What this costs, stated plainly

**A fixture-only load form cannot be saved.** A test that builds one and then
saves the document will fail, loudly, with a diagnostic that says what to do
instead. That is the intended behaviour and not a limitation to be worked
around later.

**A durable point load is still missing**, and that is P17-LOAD-001's recorded
limitation rather than this milestone's: it would be a load on a CAD **vertex**,
which needs a canonical vertex reference P16 does not provide. Persistence does
not invent one, and in particular does not encode a facet id, a coordinate
query or a nearest-node search to make the unsupported case look supported —
which is the brief's §8, and the reason it is worth saying that nothing here
tried.
