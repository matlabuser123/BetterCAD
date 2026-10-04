# P16-VIZ-001 — viewer architecture audit, part 2

```text
DATE:     2026-10-04
AT:       d9c8d53
SUBJECT:  VIEWER_AUDIT.md described a repository with NO viewport. That is no
          longer true. This re-answers the same question against what
          INFRA-VIEWER-001 built, and decides what P16-VIZ-001 may reuse.
```

[VIEWER_AUDIT.md](VIEWER_AUDIT.md) is kept unchanged. It was accurate on
2026-10-03 and it is the reason this milestone was blocked; it is not a
description of the tree being worked on now.

## What exists now

```text
OBJECT                      PURPOSE                  CANONICAL OWNER
renderer::Viewer            a V3d view: display,     none -- all of it is
                            camera, visibility,      viewer-derived
                            picking, offscreen render
renderer::PresentationId    viewer-local handle for  none; invalidated when
                            one displayed thing      the presentation goes
renderer::DisplayedObject   {presentation, object,   none
                            visible, selected}
renderer::RenderedImage     a read-back framebuffer  none
                            plus a coverage measure
app::ViewportWidget         Qt host: native window,  none
                            mouse -> camera/pick
AIS_Shape (in the adapter)  CAD solid display        geometry::Body
```

```text
OBJECT                      SELECTION PATH                        REUSABLE?
renderer::Viewer            pickAt -> SelectMgr_EntityOwner        YES, extend
                            -> AIS object -> ObjectId
PresentationId              handed out by display()                YES, as is
DisplayedObject             keys on ObjectId                       PARTLY
ViewportWidget              left click -> pickAt -> picked()        YES, extend
AIS_Shape                   whole-shape selection, mode 0          NO, for mesh
```

**The framework is suitable and will be extended, not duplicated** — which is
part 1's own instruction and §2 of the brief. Three specific gaps:

```text
1  Viewer::display takes (ObjectId, const geometry::Body&). A mesh is not a
   Body, so displaying one needs a sibling entry point -- not a second viewer.

2  DisplayedObject::object is an ObjectId, i.e. CAD identity. A mesh
   presentation is not a CAD object; what identifies it is the feature it was
   meshed FROM plus which canonical mesh it shows. That needs a distinct
   presentation kind, so "hide the CAD" and "hide the mesh" are separable
   (brief §87).

3  AIS_Shape's selection is whole-shape, mode 0. Mesh picking needs primitive
   granularity, and the translation from a picked primitive to a NodeId or an
   ElementId has to be explicit (brief §22, §66).
```

## What does NOT exist, and what follows

```text
NO CLIPPING OR SECTION PLANE in the 3D viewer
    INFRA-VIEWER-001 recorded this as a carried limitation.
    Graphic3d_ClipPlane IS available in the rebuilt dependency, so this is an
    absence of code, not of capability. It bears on interior element
    inspection (brief §52-55), which the checklist marks OPTIONAL.

NO TEXT IN THE 3D SCENE
    FreeType is deliberately off, so OCCT draws no glyphs in the view. An
    ElementId label (brief §20) therefore cannot be drawn IN the scene; it
    belongs in a Qt panel, which is what the brief's own wording ("optional
    on-screen label") permits.

NO MODEL TREE OR PROPERTY PANEL
    apps/bettercad is a window, a menu bar, a status bar and the viewport.
    Panels for mesh summary, element inspection and boundary sets (brief
    §68-71) have nowhere to dock yet; they are this milestone's to add.
```

## MeshVS_Mesh: available, and deliberately NOT used

The rebuilt dependency ships `libTKMeshVS`, and OCCT's `MeshVS_Mesh` with a
`MeshVS_DataSource` is the obvious-looking way to display a mesh.

**It is the wrong choice here, for the reason this milestone exists.** A
`MeshVS_DataSource` is a second mesh model: its own node and element
numbering, its own notion of what an element is, its own accessors. Anything
displayed through it is displayed through *that* model rather than through
`meshing::Mesh`. The brief's first automatic-fail condition is "GUI owns
independent editable copy of mesh", and its required architecture is a single
canonical owner with a derived, disposable view.

`Graphic3d_ArrayOfTriangles` and `Graphic3d_ArrayOfSegments` are also present.
They take batched primitives with no model attached, which is exactly what a
derived render cache should be: vertices and indices, built from the canonical
mesh, discarded when its stamp changes. That also satisfies brief §78 ("few
batched presentation objects rather than N AIS objects for N elements") by
construction rather than by discipline.

```text
DECISION   a custom AIS object over Graphic3d arrays, fed from a derived
           render cache keyed to MeshStamp.
REJECTED   MeshVS_Mesh + MeshVS_DataSource -- a second mesh model.
REJECTED   one AIS object per element -- unusable, and brief §78 forbids it.
REJECTED   displaying the CAD triangulation as the engineering mesh -- an
           automatic fail (brief §86) and the whole point of §6.
```

## Where the canonical data comes from

Everything this milestone displays already exists and is already qualified.
The adapter's job is translation, not derivation.

```text
WANTED                        CANONICAL SOURCE
engineering surface triangles EngineeringSurfaceMesh -> Mesh::triangles()
volume boundary facets        VolumeMesh::mesh() -- "nodes, Tet4 elements and
                              the BOUNDARY Triangle3 elements, in one". The
                              boundary IS the Triangle3 set, so brief §10's
                              "do not implement another Tet face incidence
                              counter" is satisfied by not needing one.
tetrahedra (interior)         Mesh::tetrahedra()
node position                 Mesh::findNode(NodeId)->position, SI, body frame
element connectivity          Mesh::findTriangle / findTetrahedron
signed volume                 meshing::signedVolume -- never recomputed here
quality per element           MeshQualityReport::tets / ::triangles
worst per metric              MeshQualityReport::summaries[metric].worst
quality policy                MeshQualityReport::thresholds -- the GUI holds
                              NO threshold constants of its own (brief §27)
CAD face -> facets            boundaryFacetsOf(map, FaceName)
facet -> CAD face             sourceFaceOf(map, ElementId)
facet -> owning tet           owningTetrahedraOf(map, mesh, facets)
facet -> nodes                boundaryNodesOf(map, mesh, facets)
named set -> facets           resolveBoundarySet(set, map)
mesh currentness              geometryRevision(document, source) compared with
                              VolumeMesh::revision() -- core state, never a
                              GUI timestamp (brief §43)
mesh generation identity      Mesh::stamp(), Mesh::owns(stamp)
```

## The one gap in the canonical data

**`MeshQualityReport` carries no mesh identity.** It has counts, per-element
metrics, per-metric summaries, findings and the threshold policy — and nothing
saying which mesh it describes. `GeometryMeshMap`, by contrast, carries both
`meshStamp()` and `revision()`.

That matters for brief §32 and §99: a report computed on M1 must not navigate
into M2, because `ElementId` values are reused across generations and a numeric
match would silently select a different physical element.

```text
DECISION   the VISUALISATION ADAPTER pairs a report with the MeshStamp it was
           evaluated against, and refuses navigation when the current mesh's
           stamp differs. In scope, and no qualified contract changes.
OBSERVED   this is not only the GUI's problem. ANY caller holding a report
           across a possible remesh has it, P17 included. Putting a MeshStamp
           in MeshQualityReport would be the general fix -- and that is a
           change to P16-QUALITY-001's qualified data model, which this
           milestone does not authorize. Recorded here rather than done
           quietly, and rather than left unsaid.
```

## Result of the audit

```text
REUSE          renderer::Viewer, PresentationId, ViewportWidget, the OCCT
               adapter pattern, and the pick-to-identity translation shape
EXTEND         a mesh presentation kind; a mesh entry point beside display();
               primitive-granular picking
ADD            a headless read-only mesh view adapter (the architectural core)
               and Qt inspection panels
DO NOT BUILD   a second viewer, a second mesh model, a second quality policy,
               a second boundary definition, or a second mapping
```

## Revision

First issue, 2026-10-04, at d9c8d53, after INFRA-VIEWER-001.
