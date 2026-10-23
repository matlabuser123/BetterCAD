# P18-VIZ-001 — Field mapping

The three scalar channels and the one vector channel, the stored member each reads, and the location where it is defined. The location and the unit of a channel are **read from the core**
(`thermal::descriptorOf(ThermalPostField)`), not restated in the view — a test compares them (`ThermalViz_ChannelsDescribeThemselvesWithTheCoreDescriptors`).

| Channel | Stored source | Location | Unit | Value taken |
|---|---|---|---|---|
| `Temperature` | `ThermalPostState::temperatures[dof]` | node | K | the stored double, bit for bit |
| `TemperatureGradientMagnitude` | `fields.gradients[ordinal]` (`TemperatureGradient3`) | element | K/m | `gradientDisplayNorm` of the stored vector |
| `HeatFluxMagnitude` | `fields.heatFluxMagnitudes[ordinal]` | element | W/m² | the stored double |
| heat-flux vector | `fields.heatFluxes[ordinal]` (`HeatFlux3`) | element | W/m² | the stored vector; direction q/\|q\| formed per glyph |

POST stores no gradient magnitude, so the gradient channel is a **display norm of the stored vector**. It is the only arithmetic in the field layer, it is documented in the header, and it is
tested against a hand-written norm (`ThermalViz_TheGradientMagnitudeIsTheNormOfTheStoredVector`). The heat-flux magnitude is **not** re-derived from the vector: POST's stored value is used
(`ThermalViz_HeatFluxMagnitudeContourUsesElementResult`).

## Parallel arrays and sparse identifiers

* `temperatures` is parallel to `ThermalDofIndex`, and `ThermalDofMap::nodes()` is the ascending node list. `classifyThermalState` refuses an array whose length differs from the node count
  (`RejectsTemperatureCardinalityMismatch`) — it is neither padded with zeros nor truncated.
* The element arrays are parallel to `ThermalPostResult::elementIds()`. `ThermalResultView::of` requires those ids to equal the mesh's tetrahedra **id for id and in order**; equal counts are not
  accepted as proof (`ARecoveredResultIsBoundToItsOwnMeshAndNumbering`).
* Identifiers are sparse in the hand fixtures (nodes 10, 20, 30, 40…; tetrahedra 17..20 after 16 boundary triangles). Every lookup goes through `dofOf`, `ordinalOf` or a binary search of the
  documented-ascending array; the search is guarded by an `is_sorted` check because an unsorted array would return a wrong index rather than an error.

## Absent is not zero

A channel that has no value for an entity — an element channel asked of a node — is **absent** (`std::nullopt`), counted in `skipped`, and the field is refused (`ChannelUnavailable`). A non-finite stored value is
counted in `nonFinite` and the field is refused (`NonFiniteValue`). A field is only ever returned complete: `expected == classified`, `skipped == 0`, `nonFinite == 0`
(`RejectsANonFiniteValueInsteadOfMappingItToAColour`, `AnEmptyDomainIsNotAUniformField`).

## Which view an element channel needs

An element value is constant over its tetrahedron. A boundary triangle takes the value of its **owning tetrahedron**, so an element channel requires a `MeshView` whose facets name tetrahedra
(`MeshSource::VolumeBoundaryByTetrahedron` or `SelectedTetrahedra`); the boundary view of `Triangle3` elements is refused by name (`AnElementChannelNeedsAViewThatNamesTetrahedra`).
All three corners share the element's band; nothing is averaged to a node (`GradientMagnitudeContourUsesElementResult`).

## Finding recorded here

`std::hypot(0, NaN, 0)` is `0` in the libstdc++ shipped with GCC 16.1 (the three-argument form selects the maximum with `std::max`, which returns its first argument when a NaN is compared). A corrupt
gradient would therefore have read as a perfectly quiet one, and the finiteness check downstream would never have seen it. `RejectsANonFiniteValueInsteadOfMappingItToAColour` caught it in the first
run of the contour tests; `gradientDisplayNorm` now tests finiteness first and returns NaN. `HeatFlux3::magnitude()` in the core has the same property; it is untouched (a qualified predecessor) and
unreachable with a corrupt flux because POST validates finiteness before storing one. Mutation `VIZ06` restores the defect and is killed.
