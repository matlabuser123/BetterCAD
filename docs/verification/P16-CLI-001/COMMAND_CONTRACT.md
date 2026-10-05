# P16-CLI-001 — the command contract

```text
SUBJECT:  every mesh command, the core API behind it, its exit codes, and what
          it does and does not write
```

## 1. The command matrix

| Command | Core API | Success | Failure | Structured? | Mutates? | Needs a mesh? |
| --- | --- | --- | --- | --- | --- | --- |
| `mesh-control-add` | `CreateMeshControlCommand` | 0 | 1 refused, 2 usage | no (one line) | **yes**, saves | no |
| `mesh-set-global-size` | `SetGlobalMeshSizeCommand` | 0 | 1 refused, 2 usage | no | **yes**, saves | no |
| `mesh-local-add` | `AddLocalMeshSizingCommand` | 0 | 1 refused, 2 usage | no | **yes**, saves | no |
| `mesh-local-remove` | `RemoveLocalMeshSizingCommand` | 0 | 1 refused, 2 usage | no | **yes**, saves | no |
| `mesh-settings` | `MeshControl::definition`, `orderedLocalSizing`, `orderedBoundarySets` | 0 | 1, 2 usage | `--json` | no | no |
| `mesh-generate` | `Regenerator::regenerate` → `Mesher::generate` | 0 | 1, 2 usage | `--json` | no | builds one |
| `mesh-info` | `VolumeMesh` accessors, `Mesher::currency` | 0 | 1, 2 usage | `--json` | no | yes |
| `mesh-quality` | `Mesher::quality` (`MeshQualityReport`) | 0 | 1, 2 usage | `--json` | no | yes |
| `mesh-validate` | `meshing::validate(const Mesh&)` | 0 valid | **1 if not data-valid**, 2 usage | `--json` | no | yes |
| `mesh-boundaries` | `meshing::resolveBoundarySet`, `Mesher::map` | 0 | 1, 2 usage | `--json` | no | yes |

The four mutating verbs are `EditCommand`s in the registry, so each is also a
`batch` verb in the same one-transaction-or-nothing script as a mate verb,
with nothing added to the batch driver. The six reports are handlers in
`kCommands` and **write nothing**.

## 2. Two decisions worth stating

### Every query generates the mesh it needs, and says so

A generated mesh is never persisted (P16-PERSIST-001), so a one-shot process
has nothing to load. The only honest way to answer "how many elements" is to
build the mesh now from the intent just read, and every structured payload
carries

```json
"meshSource": "generatedNow"
```

so a reader cannot mistake it for something restored. What would have been
wrong is persisting a mesh to make these commands look stateful across
processes — exactly what the derived-state rule forbids, and the end-to-end
script asserts the document is byte-identical after a generate.

**A consequence, stated rather than hidden:** `mesh-info` always reports
`"state": "current"`. The stale states (`staleIntent`, `staleGeometry`) are
within-session conditions — a mesh held while its intent moves — and a
one-shot process always generates from the intent it just loaded, so it cannot
observe one. They exist in core and are tested there (P16-CMD-001's
invalidation matrix). This is a consequence of not persisting a mesh, not a
gap in the CLI, and it is recorded as a known limitation.

### `mesh-validate` gates; `mesh-quality` does not

```text
mesh-validate  structural: connectivity, degenerate and inverted elements
               -> exit 1 when the mesh is not data-valid
mesh-quality   element SHAPE under the control's own thresholds
               -> exit 0 with the counts, whatever they are
```

`mesh-validate` follows `validate`'s own precedent, which P15-CLI-001 recorded:
*"a report that gates is only usable from a script if the gate reaches the
exit status."* `mesh-quality` does not gate because whether a mesh is good
enough is the engineer's policy; a thin element is not a broken one, and
conflating the two would make a warning look like corruption.

## 3. The exit-code matrix

Measured, not asserted. `ExitCode` is the CLI's existing enumeration:
`Success = 0`, `Failure = 1` (ran, reported a problem), `UsageError = 2` (the
command line was invalid).

| Scenario | Exit | Stdout | Diagnostic code |
| --- | --- | --- | --- |
| `mesh-settings` on a valid document | 0 | the report | — |
| `mesh-set-global-size 5mm` | 0 | one line + "Wrote …" | — |
| `mesh-set-global-size -- -1mm` | **1** | **empty** | core: "size is -0.001 m, which is not positive" |
| `mesh-set-global-size -- 0mm` | **1** | empty | core: "size is 0 m, which is not positive" |
| `mesh-set-global-size -- nanmm` | **2** | empty | `'nanmm' is not a number` |
| `mesh-set-global-size -- 1zz` | **2** | empty | `unknown unit 'zz' in '1zz'` |
| `mesh-local-add` on a face that already has one | **1** | empty | core: "already exists" |
| `mesh-local-add node:4 2mm` | **2** | empty | names the `face:<feature>:<role>` form |
| `mesh-local-remove` on a face with no control | **1** | empty | core: "no local mesh sizing control" |
| `mesh-control-add` on a document with no body | **1** | empty | `no_body` |
| any report with no meshing control | **1** | empty | `no_mesh_control` |
| any report on a missing or malformed document | **1** | empty | `document_not_loaded` |
| `mesh-generate` with an unresolvable local control | **1** | empty | `mesh_generation_failed` + core's "does not resolve" |
| `mesh-validate` on a data-valid mesh | 0 | the report | — |
| `mesh-boundaries` on a face that resolves to nothing | **0** | `"resolved": false` | — |
| `mesh-frobnicate` | **2** | empty | unknown command + usage |

**A negative value needs `--`.** `-1mm` begins with a hyphen, so the argument
parser reads it as an option and reports exit 2. `--` ends option parsing — the
POSIX convention the parser already implements — and then the value reaches
P16-SIZE-001's validator and comes back with its own diagnostic at exit 1.
Both paths are non-zero and both leave the document unchanged; both are in the
matrix because pretending only one exists would misdescribe the tool.

**Reporting "unresolved" is a successful query.** Observation is not
operation: `mesh-boundaries` on a face with no facets in this mesh exits 0 and
says so, while `mesh-generate` with a control on such a face exits 1 because
it needed the region. The brief asks for that distinction and it is the
behaviour.

## 4. Units in and out

**In:** `parseLength(text, units::mm)` — the CLI's own parser, the one every
other command uses. A bare number is millimetres; `10mm`, `0.01m` and `1cm`
all become the same `Length`, asserted together in one test. There is no
mesh-only quantity syntax.

**Out:** a value and its unit, never a bare number.

```json
"globalTargetSize": { "value": 0.008, "unit": "m" }
"meshVolume":       { "value": 9.374866560399053e-05, "unit": "m^3" }
```

A size written as `0.008` with no unit anywhere is the shape of a
factor-of-1000 mistake nothing downstream could catch. Mutation **M8**
removes the unit.

**Absent is a state, not a number.** No global size means BetterCAD's
scale-relative default, which prints as `"globalTargetSize": null` and
`Global target size: BetterCAD default` — never as a number this CLI invented.
Mutation **M1** invents one.

## 5. Geometry references

`face:<feature>:<role>[:<entity>]`, parsed by `parseFaceReference`, which
routes to `Selectors.cpp`'s existing `parseNamedFace` — **the same grammar
drawings and mates use**. Roles: `start_cap`, `end_cap`, `side`,
`hole_bottom`, `counterbore_floor`, `chamfer`, `spotface_floor`.

Exported from `Selectors.hpp` for this milestone rather than reimplemented,
because a second reader would be a second contract and `face:Block:side:5`
would eventually mean two things.

**Output is re-feedable.** A face prints as `face:Extrude001:end_cap` — the
feature's own name, which `resolveObject` accepts — and not as `label()`'s
`Extrude001 (object:6)`, which reads well and is not a selector. That was a
defect in the first version, found by looking at the output.

**A `NodeId` cannot be a sizing target.** There is no syntax for one; `node:4`
is refused at exit 2 with a message naming the face form. A node and an
element belong to one generation, and a persistent control keyed to one would
be wrong at the next remesh.

## 6. What the CLI does not contain

Checked by search, case-sensitively, over `apps/bettercad_cli/`:

```text
nglib        0        radiusRatio   0        determinant   0
Ng_[A-Z]     0        dihedral      0        nearest       0
Netgen       0        aspectRatio   0        std::sqrt     0
```

and `meshing` links Netgen **PRIVATE**, so the CLI cannot reach the backend
even transitively. The volume comes from `VolumeMesh::tetrahedralVolume`, the
quality from `MeshQualityReport`, the structure from `meshing::validate`, the
mapping from `meshing::resolveBoundarySet`. There is no mesh arithmetic in the
CLI to drift from the core's.

There is also no CLI default, tolerance or threshold. The only numeric
literals in `MeshingEdits.cpp` are the `units::mm` default for the quantity
parser — shared with every other command — and the arities of the argument
lists.

## 7. Honouring a warning the core wrote down

`MeshQuality.hpp` says of `QualityFinding::metric` and `value`:

> ABSENT when the finding is about the element as a whole — a structural
> refusal, where no metric was computed at all. Optional rather than a default
> enumerator, because naming `TetVolume` with a value of 0 for an inverted
> element whose volume is NEGATIVE would be fabricated data in a structured
> field, and a GUI or **a CLI reading the payload instead of the message would
> believe it**.

So the structured output emits `null` for an absent metric, value or
threshold, and never a default. The warning was written for this milestone
before it existed, and it is followed rather than rediscovered.
