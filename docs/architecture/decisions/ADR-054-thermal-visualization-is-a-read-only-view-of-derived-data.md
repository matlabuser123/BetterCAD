# ADR-054 — Thermal visualization is a read-only view of derived data

```text
STATUS:    Accepted
DATE:      2026-10-23
MILESTONE: P18-VIZ-001
TOUCHES:   ADR-053 (the verdict this one displays and never re-derives), ADR-051 (the post and balance data it draws),
           ADR-050 (the layer boundary), ADR-045 (P17's visualization precedent), ADR-030 (derived state carries its source),
           ADR-036 (possession is the proof)
```

## Context

By the end of P18-VALID-001 the thermal module produces, for an analysis the document defines, an ordered chain of derived objects: a mesh, a DOF map, prepared loads and boundary conditions, a solution, a post
result (nodal temperature in K, element gradient in K/m, element heat flux in W/m^2 and its stored magnitude), a heat balance and a validation report. Every one carries the source it was derived from. Nothing yet
shows any of it. A viewer that shows a temperature field is persuasive in exactly the way a wrong one is dangerous: a colour is read as a fact, and a stale colour looks like a current one.

P17-VIZ-001 solved the same problem for the structural kernel as a HEADLESS layer in the renderer (a result view, a contour, an inspection and an overlay builder, with the Qt application as a thin shell), and
recorded the method. P18 inherits the method. It does not inherit the types: a structural channel, field and legend are bound to the structural result, and the thermal module (layer 45) cannot depend on
the structural one (layer 50) in either direction.

## Constraints

* The renderer (70) may link the thermal module (45); the thermal module may not know the renderer.
* The core mesh stays in SI metres. There is exactly one metre-to-millimetre conversion in the project, `toModelLength` in the OCCT presentation adapter, and a thermal view must pass through it unchanged.
* A temperature is Kelvin, a gradient is K/m and a heat flux is W/m^2 everywhere a value is stored or inspected. A display range is VIEW STATE and may clamp a colour, never a value.
* `NodeId` and `ElementId` are sparse. They are never array indices.
* A view cannot decide that a result is current: the core's own comparison of sources decides, and it is recomputed on every call.
* Nothing in the view may be a second thermal authority: no recovery, no Fourier law, no balance, no validation rule, no copy of a canonical property.

## Options

### A — the GUI keeps the result and a flag

The application would hold a result and a `stale` boolean it sets on edit. Rejected: the flag is a second, hand-maintained answer to a question the core answers by comparing sources; the one that is forgotten
after a remesh with the same counts and the same ids is exactly the picture that ships a wrong temperature.

### B — extend the structural result view with a thermal case

Rejected: the structural types are the structural milestone's frozen surface (their channels, units and fields are the structural result's), a shared enumeration would couple two modules the architecture keeps apart,
and every structural test would then also be a thermal test.

### C — a separate headless thermal view layer that READS the core, over the P16 mesh view and the P17 viewer

Chosen.

## Decision

**Option C**, with these clauses.

1. **Four headless files, no Qt and no OCCT.** `ThermalResultView` (channels, scalar fields, the classifier, extrema), `ThermalContour` (bands, legend, colour buffers, a cache), `ThermalInspection` (display state,
   banner, node and element inspection, navigation targets, playback) and `ThermalOverlay` (heat-flux vector glyphs, applied-flux arrows, prescribed-temperature and convection symbols, boundary-set records with
   their classifier cardinality). The only additions outside them are additive: a `ThermalOverlay` presentation kind and `Viewer::displayOverlay(ObjectId, const ThermalOverlay&)` in the renderer.
2. **Channels describe themselves with the core's descriptors.** The unit and the location (node or element) of a channel are read from `thermal::descriptorOf`. The gradient magnitude is the display norm of the
   STORED vector; the heat-flux magnitude is POST's stored value; neither is recomputed from material data.
3. **Element fields are element-wise, unsmoothed.** A nodal temperature contour is interpolated across a triangle's corners; an element quantity gives every corner of a boundary triangle its owning tetrahedron's value
   (`MeshSource::VolumeBoundaryByTetrahedron`). A view that names no tetrahedra refuses an element channel rather than colour it by a node.
4. **Currentness is the core's.** The display state is a pure function of the document, the stored result, the last failure and the report: `Current` only when `isCurrent` says so, `Stale` with the SET of moved
   dependencies (`staleReasons`) otherwise, and the validation badge is the core's verdict while the report is current and `Stale` once it is not. Fields are unavailable unless the result is current and its validation did not
   fail; a warning stays drawn. A result is never placed on a mesh whose stamp it was not recovered on, even with identical counts and identical ids.
5. **The classifier cardinality is explicit.** Every field and every boundary glyph reports `expected`, `classified` and `rendered`. A zero-classified target is rejected, a partial one is `Partial` and is never
   `Complete`, an unresolved reference is kept and never rebound to the nearest face.
6. **Vectors are stored as the core stored them.** The recovered heat flux q is drawn from the stored vector at the element centroid, never sign-flipped and never normalised in storage; its display length is a view
   scale. An applied flux q'' is drawn at the facet centroid with its head along the direction heat TRAVELS (for a positive q'', into the body: the direction of -n_out). The two are distinct overlays in distinct colours.
7. **Playback is a cursor over stored states.** A frame IS a stored state and its time is the stored time of that state (never `frame * dt`: the last step is a shortened remainder). No interpolation, and every field
   of a frame is bound to the same state.
8. **Caches are keyed on the whole source.** Result source, mesh stamp, frame, channel, band count and range. A key that omits one is a stale colour waiting for the right edit.
9. **One ownership gate.** `CheckThermalVizOwnership.cmake` fails any renderer thermal file or application that names thermal physics, converts a unit itself, indexes with a raw identifier, is non-deterministic,
   writes through a const view, declares a stale flag of its own, computes a norm outside the two documented display-norm sites, or drops the binding it must read from the milestone that owns it.

## Consequences

* The Qt application has no result UI yet (as at P17-VIZ-001). The GUI smoke of this milestone is the headless layer plus offscreen viewer renders of the production display path. Wiring the panels is a later,
  separate decision that cannot change a number.
* `std::hypot` with a NaN component returns the OTHER components' norm in the libstdc++ shipped here. `gradientDisplayNorm` therefore tests finiteness first and returns NaN; `HeatFlux3::magnitude()` in the core
  has the same property and is untouched (POST validates finiteness before storing a flux).
* A change to a channel, a legend, a cardinality rule or the unit boundary changes the contract fingerprints in `FREEZE.md` and is a new milestone.
