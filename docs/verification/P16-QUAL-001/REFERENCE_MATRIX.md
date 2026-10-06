# P16-QUAL-001 — final reference matrix

```text
SUBJECT:  every mandatory P16 reference model, every property the final gate
          names, from the frozen candidate tree
VALUES:   PASS / FAIL / N/A only. Unsupported behaviour is never PASS, and a
          property a model does not have is N/A rather than a tick
EXPECTED: 8 model IDs            EXECUTED: 8          DOCUMENTS: 9
```

## The matrix

```text
| Model      | Surface | Volume | Positive | CAD Volume | Mapping | Quality | Save/Load | CLI  | PASS |
|            |         | Mesh   | Elements | Agreement  |         |         |           |      |      |
| RM-MESH-01 | PASS    | PASS   | PASS     | PASS       | PASS    | PASS    | PASS      | PASS | PASS |
| RM-MESH-02 | PASS    | PASS   | PASS     | PASS       | PASS    | PASS    | PASS      | PASS | PASS |
| RM-MESH-03 | PASS    | PASS   | PASS     | PASS       | PASS    | PASS    | PASS      | PASS | PASS |
| RM-MESH-04 | PASS    | PASS   | PASS     | PASS       | PASS    | PASS    | PASS      | PASS | PASS |
| RM-MESH-05 | PASS    | PASS   | PASS     | PASS       | PASS    | PASS    | PASS      | PASS | PASS |
| RM-MESH-06 | PASS    | PASS   | PASS     | PASS       | PASS    | PASS    | PASS      | PASS | PASS |
| RM-MESH-07 | PASS    | PASS   | PASS     | PASS       | PASS    | PASS    | PASS      | PASS | PASS |
| RM-MESH-08 | N/A     | N/A    | N/A      | N/A        | N/A     | N/A     | PASS      | PASS | PASS |
```

### RM-MESH-08: what is N/A and what is PASS

The brief requires these to be separated, and conflating them is how an
expected failure gets recorded as a capability.

```text
N/A, because the model HAS no such property -- it has no body at all:
  Surface            the extrude fails, so no surface is ever generated
  Volume Mesh        nothing is published: heldMeshCount() == 0, mesh() null
  Positive Elements  there are no elements to count
  CAD Volume         there is no body to integrate; the declared analytic
                     volume is the ABSENCE of one, not a fabricated zero
  Mapping            no mesh, so no correspondence
  Quality            no mesh, so no report (quality() returns null)

PASS, because the model's EXPECTED BEHAVIOUR is an explicit refusal and it
behaves that way:
  explicit failure   generation fails with ErrorCode::FailedPrecondition and a
                     diagnostic naming the failing feature and the real cause
                     ("OpenSolid: the profile is open: an edge ends at (0, 0)
                     mm without a neighbour")
  nothing published  no mesh, no map, no quality report, currency
                     generation_failed
  deterministic      the same refusal and the same diagnostic text over 5
                     attempts in process and 3 in the model-stability gate
  Save/Load          the document round-trips its intent and STILL refuses
                     after a fresh load, so the refusal is a property of the
                     document and not of the process that built it
  CLI                five queries, each exit 1, each with the structured
                     diagnostic on stderr and NOTHING on stdout
```

## What each column means, and what proves it

```text
Surface       an EngineeringSurfaceMesh exists, which it cannot unless it is
              closed, manifold and coherently oriented -- P16-SURF-001 refuses
              otherwise. Re-proved per model by the volume mesh existing at
              all, and independently by the suite's own face-incidence audit:
              every undirected face of the tetrahedra used by one or two
              tetrahedra, the one-sided set exactly the stored triangle set

Volume Mesh   a VolumeMesh was published, and its constructor is private with
              generateVolumeMesh its only friend -- so possessing one IS the
              evidence that validation, conformity and volume recovery passed.
              The suite re-checks each of them from the mesh's own data

Positive      the suite computes a determinant per element from the node
Elements      coordinates: 1751 positive, 0 zero, 0 negative, 0 non-finite
              across the eight valid documents, with no abs() anywhere

CAD Volume    four-way: the model's dimensions from its own parameters, through
Agreement     a closed form that cannot call BetterCAD, against the catalog's
              declared volume, against OCCT, against the tetrahedral sum. See
              ANALYTICAL_VALIDATION.md for the figures and the derived bounds

Mapping       GeometryMeshMappingReport::complete() -- every boundary facet
              attributed exactly once, every CAD face represented, nothing
              ambiguous -- plus region-by-region facet counts that SUM to the
              boundary and are pairwise disjoint

Quality       evaluateMeshQuality run on every valid mesh under
              P16-QUALITY-001's own reportOnlyThresholds(); structurally
              valid, 0 invalid, policy satisfied, every metric finite and
              defined. No reference-model threshold invented

Save/Load     all NINE documents round-trip their canonical
              MeshControlDefinition -- body, discretisation, sizing, quality
              policy, boundary sets with their identities -- with no mesh in
              the bytes, an empty mesher after the load, and a regenerated
              mesh identical to the original process's

CLI           fresh processes, the real executable resolved per preset through
              $<TARGET_FILE:>, with the exact counts and SI volume doubles the
              core measures in process
```

## The Save/Load column, and why it is complete

P16-REFMOD-001 round-trips **three** models, chosen deliberately: the block,
the plate whose boundary intent is a hole wall, and the local-refinement model
that carries global sizing, local sizing and a `GeometryReference` together.
That is the right depth for its brief, which named those three.

A final matrix with a Save/Load column needs **every** model, so
`P16Qualification_EveryReferenceDocumentRoundTripsItsIntentAndRegenerates`
provides the breadth — in this milestone's file rather than by editing
P16-REFMOD-001's, so that milestone's evidence keeps saying what it did.

```text
documents round-tripped                9 of 9
regenerating an identical mesh         8 of 8 expected to
RM-MESH-08 still refused after a load  yes
"nodes" / "tetrahedra" / "elements" in any of the nine files   none
```

## The CLI column, per model

```text
| Model      | COMMANDS EXERCISED THROUGH A FRESH PROCESS                   |
| RM-MESH-01 | mesh-settings, mesh-generate, mesh-info, mesh-validate,      |
|            | mesh-quality                                                  |
| RM-MESH-02 | covered by the runner, which meshes it and prints its figures |
| RM-MESH-03 | mesh-generate, mesh-info, mesh-validate, mesh-quality,        |
|            | mesh-boundaries                                               |
| RM-MESH-04 | mesh-boundaries -- the four-region partition                  |
| RM-MESH-05 | covered by the runner                                         |
| RM-MESH-06 | covered by the runner, both placements                        |
| RM-MESH-07 | mesh-settings (the local control in the CLI's own face
|            | grammar), mesh-generate, mesh-info, mesh-validate,
|            | mesh-quality, mesh-boundaries                                 |
| RM-MESH-08 | mesh-generate, mesh-info, mesh-validate, mesh-quality,
|            | mesh-boundaries -- all five exit 1                            |
```

The runner, `bettercad_example_reference_models --meshes`, is itself a fresh
process whose exit code is an assertion: it fails if any model does not build,
does not regenerate as expected, meshes when it must not, fails to mesh when it
must, or publishes a mesh after a refusal. It is registered as a ctest
`FIXTURES_SETUP`, which ctest pulls in even when the run is filtered.

**Where a model is marked "covered by the runner" rather than by a dedicated
`mesh-*` invocation, that is stated rather than papered over.** The three
models the brief names for CLI/core equivalence — RM-MESH-01, RM-MESH-03 and
RM-MESH-07 — carry the full command set with exact figures asserted.
