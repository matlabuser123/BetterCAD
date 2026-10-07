# P17-LOAD-001 — adversarial review

```text
SUBJECT:  an attempt to disprove the load model before an assembly and a
          solver consume it
QUESTIONS: 30 from the brief, plus 4 of the reviewer's own
FINDINGS: 4 -- 0 production defects, 2 test defects of mine (one of them
          twice), 1 diagnostic defect, 1 unreachable branch kept with its
          reason
GATE-BLOCKING: 0
```

## Findings

### F1 — my refinement test was vacuous, twice (FIXED)

The brief's §67 asks that the same canonical load on the same CAD face give the
same continuum resultant at a coarse and a fine discretisation. Two drafts
failed to test that at all.

```text
draft 1   varied the sizing with MeshedReference::requireWith(), which builds a
          VolumeMesh the Mesher never HOLDS. requireStructuralModel found no
          mesh and refused -- correctly -- so the test failed for a reason that
          had nothing to do with loads
draft 2   drove the document's own MeshControl, which is the right path, but
          refined a PLANAR face of RM-MESH-03 and measured 40 facets at every
          level. "The resultant is unchanged" was true because NOTHING changed
```

Draft 2 is the serious one: it **passed**. A plane is exactly representable, so
no deflection or sizing change retriangulates it — which is the recorded
`mesh-visualisation-traps` lesson arriving in a new place, and the
`convergence-levels-must-differ` lesson arriving for the second time.

The fixture is now RM-MESH-02's cylindrical wall, where refinement is real, and
the test asserts the levels **differ** — strictly more facets and strictly more
loaded nodes — **before** comparing any resultant:

```text
0.40 mm deflection ->  72 facets
0.10 mm            -> 100 facets
0.025 mm           -> 200 facets
```

The claim is also stronger than invariance now. An inscribed polygon's area
approaches the true area from below, so the resultant must **converge** to
`t A_analytic` with the error falling monotonically — and separately, at every
level the resultant is exactly `t` times the facet area, which is the statement
that the integration is right whatever the mesh.

### F2 — the diagnostics said "load load:4" (FIXED)

`LoadId`'s formatter emits `load:4`, and the messages were written
`std::format("load {}: ...", load.id())`. Minor, and fixed rather than left,
because a diagnostic that reads `load load:4: its target face names no face` is
the kind of thing that erodes trust in the rest of the output.

### F3 — one branch has no test, and I could not construct one (KEPT, RECORDED)

`LoadProblem::TargetWithoutFacets` fires when a `FaceName` resolves to a CAD
face and the mapping attributed **no boundary facet** to it. P16 names that
state — `MappingIssueKind::FaceWithoutFacets`, "for an ordinary meshable face
it means the attribution chain lost it" — and reports it, so it is a state P16
believes can occur.

**I could not reach it in a test.** `GeometryMeshMap` is complete for every
committed reference model (P16-QUAL-001), so a resolved face always has facets,
and producing one that does not would need a defective attribution I have no
way to build through the public API.

It was kept rather than deleted, and that is the opposite of what
P17-ARCH-001 did with its three unreachable values. The difference is why:

```text
P17-ARCH's values were impossible because ANOTHER TYPE'S INVARIANT excluded
  them -- Mesher::generate refuses to hold an invalid mesh -- so nothing could
  ever return them
TargetWithoutFacets is a state P16 ACTIVELY REPORTS. Deleting P17's handling
  would make the fallthrough "resolved, no facets, integrate over nothing" --
  that is, a silent zero load, which is exactly what the brief's §15 and §72
  forbid
```

So the branch exists, is justified, and is **untested**. Recorded as a gap in
the evidence rather than counted among the reachable values — the honest claim
is "seven of the eight `LoadProblem` values have a test", not "every value is
reached".

A second, smaller case of the same shape: `run()` checks
`load.target().has_value()` before resolving a face load, and that check is
unreachable today because the nodal and gravity branches `continue` above it
and both remaining payloads always carry a face. It is kept because
`target()` returns an `optional` and a fifth payload without a target would
otherwise fall through silently. Noted rather than presented as covered.

### F4 — two test-authoring errors worth recording as a pattern

`structural::Traction3D` does not exist — the type is in `namespace bettercad`,
because `core/math/Vector.hpp` is core's. And `Document::modifyObject`'s mutator
must **return** the `Result`, not consume it with a `REQUIRE`. Both were
compile errors, caught immediately, and neither reached a passing state. They
are listed only because both came from writing against an API from memory
instead of reading it, which is the habit the four previous milestones' findings
keep pointing at.

## The brief's 30 questions

**Can a boundary facet ID become canonical load authority?** No, and it is a
compile-time assertion rather than a review: mirror structs declare the
permitted members of `SurfaceTractionLoad` and `PressureLoad` in order, so an
added field changes the `sizeof`, and neither is constructible from an
`ElementId`. `grep` confirms there is no `ElementId` outside comments in the
canonical header.

**Can a `NodeId` become permanent CAD load identity?** Only through
`NodalForceLoad`, which is **declared** mesh-local, carries the `MeshStamp`,
and is refused against any other mesh. The policy is Policy A as the brief
prefers, and `LOAD_SCHEMA.md` records what it is for and what it is not. A
durable point load would need a canonical CAD **vertex** reference, which P16
does not provide — recorded as a limitation rather than faked.

**Can load targeting geometrically classify faces instead of using P16-MAP?**
No. Searched over the implementation: no `tolerance`, no `nearest`, no
`distance`, no `centroid`, no normal-similarity test, no surface-kind test.
`meshing::boundaryFacetsOf` is the only resolver, and facet geometry is read
only after it answers.

**Can a drilled-hole wall be targeted through an unofficial tolerance
workaround?** No, and the refusal is tested **against its own control**: a
reference aimed at an unnamed face is `TargetUnresolved`, while RM-MESH-03's
hole wall — the same geometric kind, named because its circle is in the profile
sketch — is accepted with facets and a positive area. That contrast is what
makes the refusal a P16 naming limitation rather than a P17 defect.

**Can unresolved load references silently produce zero load?** No;
`TargetUnresolved`, with a diagnostic naming the load and saying "Nothing
nearby is substituted". Mutation probe M8 bypasses the check and is killed by
two tests.

**Can ambiguous references choose the first match?** There is no ambiguous
state to choose from. P16's `MappingState` has exactly `Resolved` and
`Unresolved`, and its header says a `FaceName` "can be ambiguous ... and that
is exactly why this layer does not map through one". The brief's
`LoadTargetAmbiguous` would be a value nothing returns.

**Can stale `G1/M1` mapping be used for current `G2/M2`?** No, and not by a
check here: `prepareStructuralLoads` takes a `StructuralModel`, and possession
of one proves the mesh is current and the map came from the same lookup
(ADR-036). `InputProblem::MeshStale` is where that refusal lives, and
P17-ARCH-001 tests it. Adding a second gate would be a branch nothing can
reach.

**Can remesh reuse old facet IDs?** The canonical load never holds one, so
there is nothing to reuse. The remesh test asserts the mesh stamp changed and
the physical resultant did not, and that the canonical record is
byte-identical across the remesh.

**Can the same numeric facet index on `M2` fool the load system?** It cannot
reach the load system — a facet index appears in no load record. The analogous
question for the one mesh-local load type **is** tested: after a remesh the
handle is still a valid node of the new mesh, and the load is refused anyway,
because the `MeshStamp` decides.

**Can pressure sign be reversed?** M3 reverses it and is killed by three tests.
The convention is asserted positively (end cap, normal `+Z`, force along `-Z`),
negatively (start cap, same scalar, force along `+Z`), and as a sign rather
than a magnitude.

**Can pressure use global direction instead of the surface normal?** The two
caps of one block under one scalar give opposite forces, which a
global-direction pressure cannot produce. And the transform test shows the
pressure resultant rotating as `R F` with the body.

**Can traction accidentally follow the surface normal?** M5 projects it onto
the normal and is killed by four tests, including the transform test that
requires the traction resultant to be unchanged component-for-component under
rotation.

**Can a triangle receive `p A / 3` and then be multiplied by area twice?** The
area vector carries direction and magnitude together, the unit normal is
recovered by dividing by the magnitude, and the area is applied once by
`facetNodalForce`. Asserted against the squared answer explicitly: the
magnitude must differ from `p A^2` by more than a tenth of `p A`. M11 (dropping
the half) is killed by eight tests.

**Can distributed loads preserve total force but not moment?** This is the
sharpest question in the brief and it has a measured answer: **M2 does exactly
that**, and it is caught only through the moment. See `MUTATION_PROTECTION.md`.

**Can shared-node facet contributions overwrite each other?** M6 overwrites and
is killed by ten tests — the broadest kill in the set, which is the evidence
that `Accumulator::add` is on the path for every load type.

**Can local refinement change the physical resultant?** No, and the test is
now non-vacuous — see F1. The distribution changes (72 to 200 loaded nodes) and
the resultant converges.

**Can load insertion order change the final vector?** No. The nodal field
accumulates into a `std::map`, so the emitted order is ascending whatever the
input order, and reversing the list gives the same field node for node to
1e-14 of the scale.

**Can unordered iteration make cross-run forces nondeterministic?** M7 replaces
the `std::map` with an `unordered_map` and is killed by the determinism and
order-independence tests. There is no unordered container in the module.

**Can negative pressure be clamped accidentally?** No; it is signed by design
and the suction case is tested. A clamp would silently change the user's model.

**Can NaN pressure enter the right-hand side?** No. Every load value is checked
finite **before** any integration — force components, traction components,
pressure and acceleration each have a section — so nothing non-finite reaches a
force.

**Can gravity silently default density?** No. There is no 7850 anywhere in the
module; `DensityMissing` is reported and the diagnostic points at the analysis
**mode**, which is the thing the user changes. M9 bypasses the check and the
process dies, which is reported as the crash it is rather than as a clean
assertion.

**Can no-gravity load preparation unnecessarily require density?** No — the
density is consulted only when a gravity load is present, and there is a test
asserting that a traction-only set on a no-density material prepares fine.

**Can material edit stale the mesh?** Not this milestone's to change, and
P17-MAT-001 measured it: an `E` edit leaves `Mesher::currency` `Current`, the
`MeshStamp` unchanged and the whole `MeshControlDefinition` equal. A density
edit behaves the same way, and the gravity test changes a density and remeshes
nothing.

**Can load edit stale the mesh?** No. The loads live in
`StructuralAnalysisDefinition`, which is the **analysis** object; the mesh
control is untouched, so nothing in the meshing dependency chain moves.

**Can load edit fail to stale the FEA result?** No, and it needs no new
mechanism: the loads are a field of the analysis definition, so an edit moves
the analysis object's revision, and `StructuralResultSource` already carries
`analysisRevision`. That is what the placeholder `// Loads -- P17-LOAD-001`
anticipated, and the struct's own note says why one counter is better than
three.

**Can load arrow display settings alter the canonical load revision?** There
are no display settings in the schema. The four payloads carry a target and a
physical value; there is no colour, no scale, no label and no visibility. A
presentation field added later would have to go somewhere else, and
`P17-VIZ-001` owns that.

**Can pressure stored as a frozen force vector become wrong after geometry
changes?** It is not stored as a vector — that is the representation decision,
and the transform test is what demonstrates why: the same scalar on a rotated
body gives a rotated resultant, which a frozen vector could not.

**Can P17-LOAD depend on GUI, CLI, persistence or Netgen internals?** No. The
module links `core`, `features` and `meshing` only, the layering check passes,
and nothing in the load headers reaches a viewer, an argument parser, a file
format or a backend.

**Can a filtered test command run zero tests and still be reported PASS?**
Counted with `-N` before every run: `StructuralLoad_` selects 23. And the
repeat filter was verified **from inside the selection** — grepping the
milestone's own tests among the selected 820 and requiring 23 — which is the
check added after P17-DOF-001 found an inherited filter covering 17 of 31.

## Four of the reviewer's own

**Was adding `Traction3D` and `Moment3D` to `core` right, or scope creep?**
`Force3D`'s own comment settles it: it is in `core` "because a force is an
engineering quantity and not a structural-analysis concept — P18's heat flux
will want the same treatment". A traction and a moment stand on exactly that
footing. The cost is real and is stated rather than hidden: it widens this
milestone's blast radius to everything that includes `core`, which is why the
repeat set is the broad one and why the driver's comment says so. The
alternative — a `structural`-local traction — would have put an engineering
quantity in a solver module and left P18 to invent a second one.

**Should the loads have gone into the analysis definition at all?** The
alternative was a separate collection with its own revision. Rejected for the
reason `StructuralAnalysisDefinition`'s own note already gave before this
milestone existed: "an edit to any of them moves the owning object's revision
... Three independent counters would give three chances to forget one." Putting
them there also made the load-change invalidation work with **no new
mechanism** — the one thing in this milestone that required no code at all.

**Is `NamedBoundarySet` being unused a gap?** It is a deliberate omission and
`MAPPING_CONTRACT.md` records three reasons, of which one is substantive: a
named set lives in `MeshControlDefinition::boundarySets`, which is *meshing*
intent, so a load targeting one would make the structural analysis depend on a
collection the mesh control owns — and editing the mesh control's named sets
would then silently change the load. That coupling is not this milestone's to
create. `P17-BC-001` has the natural need (P16's own header uses "fixed_end" as
its example) and can decide where the canonical set should live. Nothing is
unreachable without it, because a set is a union of `FaceName`s and several
loads superpose.

**Does anything here duplicate work P16 or P17-ELEM already does?** The one
real risk was the facet area. `Mesh.hpp` has `triangleArea(p1, p2, p3)`, and
using it would have meant computing an area and then a unit normal separately —
which is how the `2A` factor gets applied twice. `facetAreaVector` returns
both together because a pressure needs exactly that product. So it is not a
duplicate of `triangleArea` but the vector form that one deliberately does not
provide ("a triangle in three dimensions has no sign — it has a normal, which
is the cross product itself"). Gravity, by contrast, reuses
`meshing::signedVolume` directly rather than recomputing a volume, and
P17-ELEM's `Tet4Kinematics` is **not** used, because a body force needs a
volume and not a stiffness — keeping the element kernel independent, as the
brief's §90 requires.

## Result

```text
QUESTIONS:                      30 + 4 = 34
FINDINGS:                       4
PRODUCTION DEFECTS:             0
TEST DEFECTS OF MINE:           2  (F1, vacuous twice and PASSING the second
                                    time; F4, two compile errors from writing
                                    against remembered APIs)
DIAGNOSTIC DEFECTS:             1  (F2, "load load:4")
UNREACHABLE BRANCHES KEPT:      2  (F3 -- TargetWithoutFacets and the
                                    no-target guard, both justified and both
                                    recorded as UNTESTED rather than counted
                                    as covered)
GATE-BLOCKING:                  0
VERDICT:                        PASS
```
