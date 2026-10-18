# P18-LOAD-001 — remesh, re-resolution and currentness

```text
SUBJECT:  what makes a prepared heat input stale, what does not, and the proof
          that the canonical intent re-resolves against a new mesh
SOURCE:   PreparedThermalLoadsSource, staleReasons, isCurrent, requireCurrent
```

## The source a prepared load carries

```text
body        which solid                                            ObjectId
mesh        WHICH MESH AND WHICH GENERATION                        MeshStamp
nodeCount   a stamp identifies the BUILDER, not the snapshot       size_t
geometry    the geometry revision the mapping resolved against     GeometryRevision
loads       the canonical loads it was prepared FROM, BY VALUE     vector<ThermalLoad>
```

A value, not a hash; field-complete equality by `= default`; `staleReasons`
names which field moved, in the order Body, Mesh, Geometry, Loads. The
`ThermalDofMap` source is the mesh stamp plus the count (`describes`), so the
numbering is bound by the same fields.

## What does and does not make it stale

| Edit | Stales the prepared load? | Why |
| --- | --- | --- |
| remesh, same body and controls (new generation) | **YES**, reason Mesh | a new stamp |
| remesh with identical node and element counts | **YES** | currentness is the source, not the length |
| remesh with identical NodeIds and ElementIds | **YES** | the handle values are not the identity |
| CAD parameter edit (depth 20 -> 35 mm) | **YES**, reasons Geometry and Mesh | the geometry revision moved |
| a different solid in the document | **YES**, reasons Body and Mesh | another source |
| load value 100 -> 200 W/m^2, target unchanged | **YES**, reason Loads | the list differs by value |
| load target changed, added, removed, reordered, re-identified | **YES**, reason Loads | likewise |
| **material edit** (k 45 -> 50, cp edited) | **NO** | a prescribed `q''`/`q'''` depends on neither; the vector is bit-identical |
| **solver tolerance edit** | **NO** | not an input of a heat load |
| **boundary-condition edit** (a prescribed temperature added) | **NO** | BCs are P18-BC-001's; not an input here |
| mode / initial condition / transient settings | **NO** | the definition overload reads `loads` only |

The **thermal result** goes stale on a material edit through its own gate
(`ThermalResultSource`, reason Material) and on a load edit (reason Analysis);
`EditStalesThermalResult` and `MaterialEditDoesNotInvalidatePreparedGeometryTarget`
show both, in the same test run, without a solve. Two gates, two dependencies,
neither widened to cover the other.

**Why the analysis revision is NOT in the source.** It moves on any edit to the
definition. Binding a prepared load to it would stale the load for a convection
or tolerance edit that cannot change it, and the brief asks for the minimal
dependency. Binding to the load list by value gives exactly the physics'
dependency. The cost, stated: the prepared object holds a copy of the canonical
loads (a handful of `FaceName`s and doubles); it is provenance, not authority,
and is not persisted.

## The adversarial remesh cases — measured, not assumed

```text
SameCountsNewMeshIsStale   block remeshed: 9 nodes, 12 tetrahedra, 12 boundary facets in
                           BOTH generations; the vector has exactly the 9 entries M2 needs;
                           P1 is stale, describes(M2) is false.
SameIdsNewMeshIsStale      every NodeId, every ElementId and the end cap's facet handles are
                           equal across the remesh; the stamps differ; P1 is stale.
RemeshInvalidates...       P1 stale (reason mesh); P2 prepared on M2 is current and gives the
                           same 0.48 W; a numbering of M1 applied to M2 is refused
                           (DofMapMismatch).
```

## Genuinely different meshes, and the canonical reference unchanged

The brief's section 118 lesson, applied: before anything is compared, the test
**requires** that the two meshes differ.

```text
cylinder, radius 6 mm, length 20 mm      one canonical FaceName{feature, EndCap}

                  coarse (default)      fine (3 mm volume target, 5 um / 5 deg surface)
nodes             156                   900
tetrahedra        605                   4118
boundary facets   140                   572
end-cap facets    34                    142
end-cap area      1.12524019e-4 m^2     1.13061452e-4 m^2          CAD pi r^2 = 1.13097336e-4
volume            2.25048038e-6 m^3     2.26122904e-6 m^3          CAD pi r^2 L = 2.26194671e-6

assertions before comparing:  tetrahedra, nodes AND boundary facets differ; stamps differ
canonical FaceName and load list compared with == before and after preparing:  identical
the facets, nodes and unknowns re-resolve:  34 -> 142 cap facets, 156 -> 900 unknowns
each prepared load is stale against the other mesh; neither is stale against its own
```

The volume target alone left the boundary triangulation unchanged (140 facets
in both) in the first run of this test, because the surface mesh is governed
by deflection, not by the volume size; the fine control therefore tightens the
deflection as well, so that the SURFACE a flux integrates over genuinely
differs. That is recorded rather than hidden.

## Total heat rate is mesh-independent within P16's geometric tolerance

q'' = 1000 W/m^2 on the cap, q''' = 5e6 W/m^3 in the body:

```text
                 surface heat q'' A                 volume heat q''' V
coarse           0.112524019 W                      11.2524019 W
fine             0.113061452 W                      11.3061452 W
CAD              0.113097336 W                      11.3097336 W
mesh vs CAD      -0.507 % (coarse)  -0.032 % (fine)    identical, V = A L for a prism
coarse vs fine   0.475 %                            0.475 %
```

The conversion identities hold at 1e-13 on each mesh separately (`sum Q = q''
sum A_f`, `sum Q = q''' sum V_e`); the difference between the meshes is the
mesh's own discretisation of a circle and nothing else, and the fine mesh is
closer to the CAD value than the coarse one.

## The gate for a stale model

A geometry edit makes the mesh stale, and `requireThermalModel` then returns
**no model** (`MeshStale`), so nothing can be prepared on it at all;
`GeometryEditStalesPreparedLoad` shows that gate, then the stale reasons
(Geometry and Mesh) after a remesh, and the same canonical `FaceName` still
resolving on the edited body with the volumetric heat tracking the new
geometry (35/20 of the original, cap area unchanged).

## A lifetime rule worth stating

`ThermalModel` **borrows** the mesher's mesh and, by its own documentation,
must not survive an edit: after a remesh the old model sees the NEW mesh. The
first version of three tests here compared against the old model after the
remesh and passed or failed for the wrong reason; they now capture the old
stamp, the old numbering and the old prepared loads **by value** before
remeshing. A prepared load and a numbering are values and survive; a model
does not.
