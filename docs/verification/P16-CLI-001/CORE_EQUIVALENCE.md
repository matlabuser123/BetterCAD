# P16-CLI-001 — core / CLI equivalence

```text
SUBJECT:  that the CLI and the core produce the same answer for the same
          input, field by field, and how that is proved in a separate process
```

## 1. Why this is the milestone's hard gate

The CLI is an adapter. The risk is not that a command prints nothing — that
would be obvious — but that it quietly grows a semantics of its own: a default
size, a tolerance, a threshold, a unit, a rounding. Each of those would make
the headless result differ from the GUI's while both looked right.

So the gate is equality, not plausibility, and it is checked twice: in process
against the core API, and out of process against the real executable.

## 2. In process, field by field

`MeshingCli_EveryExposedFieldMatchesTheCore` builds one document, computes the
answer through the **core API** directly, then runs the **CLI** over the same
file and compares.

Fixture: a cylinder r6 × h20 mm, global target 4 mm, a local control of
1.5 mm on the end cap — curved, because a block's planar boundary barely
responds to a target size and would make a sizing difference invisible.

| Field | Core | CLI | Comparison |
| --- | --- | --- | --- |
| node count | `VolumeMesh::mesh().nodeCount()` | `mesh-info --json` `nodeCount` | exact |
| element count | `mesh().tetrahedra().size()` | `elementCount` | exact |
| boundary triangles | `mesh().triangles().size()` | `boundaryTriangleCount` | exact |
| mesh volume | `VolumeMesh::tetrahedralVolume().si()` | `meshVolume.value` | `WithinRel 1e-12` |
| invalid elements | `MeshQualityReport::invalidElements` | `mesh-quality --json` | exact |
| warning elements | `warningElements` | same | exact |
| failure elements | `failureElements` | same | exact |
| data validity | `meshing::validate(mesh).dataValid()` | `mesh-validate --json` `dataValid` | exact |

**The volume tolerance is 1e-12, not an engineering tolerance.** Both paths
read the *same* `tetrahedralVolume()`; the only thing between them is
`std::format` writing the double and `std::stod` reading it back. A difference
larger than round-trip noise would mean the CLI had recomputed something, which
is precisely what this test exists to catch — so a loose tolerance here would
defeat it.

The test additionally requires `elements > 100`, so an accidental agreement on
small numbers is not plausible.

## 3. Out of process, on the real executable

An in-process test cannot prove the executable works: argument parsing from a
real command line, document loading from a real path, the runtime closure
loading, stdout being clean enough to parse, and the exit code reaching the
shell. Every one of those has broken something in this project before.

```text
cli.mesh.settings             exit 0, "value": 0.008 in the payload
cli.mesh.info                 exit 0, "state": "current"
cli.mesh.validate             exit 0, "dataValid": true
cli.mesh.quality              exit 0, "invalidElements": 0
cli.mesh.boundaries           exit 0, the named set "fixed_end"
cli.mesh.settings.no-control  exit 1, "no_mesh_control" on stderr, stdout EMPTY
cli.mesh.workflow             21 steps, every exit code asserted
cli.mesh.zero-match-guard     the filters discover tests; the counter is real
```

Each names the binary through `$<TARGET_FILE:bettercad_cli>` — an absolute
path — never a bare `bettercad-cli` that `PATH` could resolve to something
stale. Because each preset builds its own, running these under a preset is the
fresh-binary proof for that preset.

## 4. Human and structured output agree

`MeshingCli_HumanAndJsonAgree` renders the same core result both ways and
compares them: the prose's `Nodes: N` must equal the payload's `nodeCount`,
`Elements: N Tet4` must equal `elementCount`, and `mesh-validate`'s `PASS`/`FAIL`
must agree with `dataValid` — including the exit code, which must be the same
for both renderings.

A tool whose prose said PASS while its payload said `false` would be worse
than having only one of them.

## 5. Determinism

`MeshingCli_OutputIsDeterministic` runs all five reports twice and requires
byte-identical stdout. The local controls are added in an order that is *not*
the canonical one first, so a report that leaked insertion order would differ
from a report that sorted.

Everything the reports iterate is ordered by construction:

```text
local controls    MeshControl::orderedLocalSizing()   sorted by FaceName
boundary sets     orderedBoundarySets()               sorted by BoundarySetId
quality metrics   MeshQualityReport::summaries        std::map keyed by metric
quality findings  MeshQualityReport::findings         core's own order
JSON fields       insertion order (JsonValue)         the caller's sequence
```

Nothing iterates an unordered container. Mutation **M7** substitutes the
stored vector for the ordered accessor.

## 6. Save / load / remesh

`MeshingCli_SettingsSurviveASaveAndLoadAndRemeshTheSame`, and the end-to-end
script's step 11, are the headless form of P16-PERSIST-001's gate:

```text
set intent through the CLI
mesh-settings --json    ->  S1
mesh-info --json        ->  M1
copy the file (what a second process opens)
mesh-settings --json    ->  S2    must equal S1 byte for byte
mesh-info --json        ->  M2    node count, element count and volume
                                  must equal M1's
```

The script then asserts the saved document contains no `tetrahedra`, no
`"nodes"`, no `connectivity` — so the equality above came from the intent
round-tripping, not from a mesh having been stored. And it removes the local
refinement and requires `mesh-info` to **change**, which proves the intent is
reaching the mesher rather than the answer being cached.

Mesh identities are explicitly not part of the comparison. A `NodeId` or an
`ElementId` belongs to one generation; the semantics are the counts, the
volume and the validity.

## 7. What equivalence does not cover, and why

`mesh-info` always reports `"state": "current"`. The stale states exist and
are tested in core (P16-CMD-001's invalidation matrix), but a one-shot process
always generates from the intent it just loaded and so cannot hold a mesh
while its intent moves. That is a consequence of not persisting a mesh rather
than a difference between the CLI and the core — the two agree about every
state the CLI can reach.
