# P19-ARCH-001 - adversarial review

Three attacks before the freeze: an **independent reviewer** given the scope and the code but not the author's conclusions; **ten mutation probes** of
the boundary and the two architecture checkers; and the **author's own questions**. All findings were disposed before the freeze; none was left
as a defect in the code.

## 1. The independent reviewer (read-only; ran the two checkers on the real tree and on every fixture)

It verified, by running them: the real tree passes both checkers; every fixture yields exactly one violation with the expected message; layer 42
is free; `thermal` -> `cfd` and `structural` -> `cfd` are allowed by the strictly-lower rule; the link checker catches both spellings of a
target. It verified, by reading the source, every factual claim the ADRs make about the existing code (no viscosity in P15; `ConsumerKind` has
seven values and none for CFD; `DynamicViscosity` and `KinematicViscosity` exist; `tetrahedralBoundary` exists; `requireDensity` refuses a
missing, non-finite or non-positive density). It found **no blocker**, six major and seven minor findings:

| id | finding | disposition |
|---|---|---|
| F1 major | the fluid's authority was stated four incompatible ways ("analysis names a material id", "assigned to the body", ...) while P15 has **one material per document** | **fixed**: ADR-058, ADR-060, `AUTHORITY_MODEL.md` and `SCOPE.md` now say v1 runs on a document whose single assignment is the fluid; per-body materials recorded as a prerequisite of any fluid-and-solid workflow (including CFD -> structural in one document) |
| F2 major | ADR-051 had said a third caller would justify extracting the shared seam; ADR-060 copied the boundary a third time without discharging that | **decided and recorded** in ADR-060: not extracted now (it would change P16, a qualified module, inside an architecture milestone); proposed to P19-MESH-001; until then the drift is guarded by a test comparing the CFD boundary with **both** the thermal and the structural one, step for step |
| F3 major | "an unresolved reference is a refusal, never re-bound" and "every boundary facet has exactly one condition" were in no ADR | **fixed**: both are constraints of ADR-058 (no default wall, no default outlet) |
| F4 major | the scope could not produce its own oracles: no moving wall (Couette, lid-driven cavity), a pressure inlet only "if qualified" (fully developed Poiseuille), and the header called "no inlet or no outlet" an input problem, which wrongly refuses pressure-driven and closed-cavity flows | **fixed**: moving wall and static-pressure inlet are in the supported set; the header now names the real problems (pressure datum missing or overdetermined, prescribed fluxes that do not balance) |
| F5 major | pressure datum, compatibility, gravity convention and the meaning of mass balance were open | **decided** in ADR-058: reference pressure required iff nothing fixes `p`, refused if overdetermined; incompatible prescribed flux refused, never rescaled; `p` is static gauge pressure with the hydrostatic part inside it (reference height canonical with gravity); mass balance = cell continuity residual and global boundary imbalance, relative to throughput |
| F6 major | "unsupported physics is refused" contradicted "beyond the laminar regime is a validation concern" | **fixed**: exceeding the laminar bound is a named refusal at validation, never a warning, never a quietly enabled model |
| F7 minor | `geometryRevision()` asserted nowhere; the model kept no material id | **fixed**: `CfdModel::materialId()` added; both asserted (mutations CP05, CP06 now killed) |
| F8 minor | the roster test was not tied to the enum; the viscosity tripwire was a name heuristic; check order was pinned only geometry-before-mesh; a claim of "identical counts" was not tested | **fixed**: the roster is tied to the enum (the value after the last must be unnamed); the tripwire also asserts the consumer COUNT; a new case reports the first of two simultaneous failures in four combinations (killed CP01, CP02, CP04); the claim was removed. *Recorded, not fixed*: a test using another control's map, and `is_constructible` checked for one signature only |
| F9 minor | two older zero-match minimums were diluted by the new tests | **fixed**: raised to the new counts (6 and 6) |
| F10 minor | the link checker was a deny-list of two names | **fixed**: an allow-list for `cfd` (core, features, meshing, linalg, eigen, compiler options), with a third fixture (`cfd-links-io`) |
| F11 minor | ADR-058 overstated what P16 provides (interior faces, ownership of boundary triangles, FV metrics) | **fixed** in the text |
| F12 minor | documentation drift: fixture count, the checker's message text, "reported as stale", "one lookup" | **fixed** |
| F13 minor | v1 has no energy equation, so CFD -> thermal cannot be produced in v1 | **fixed**: ADR-058 and ADR-059 say only CFD -> structural is real in v1; the layer still keeps the other open |
| suspected | the cross-preset bit-identity claim cannot hold for a nonlinear iterative solve | **fixed**: ADR-058 limits it to what is assembled and summed in a fixed order |
| suspected | the (document, regenerator, mesher) triple is not bound; a moved-from `CfdModel` keeps its pointers | recorded; identical to the qualified thermal and structural boundaries (P18-QUAL-001 D3) |
| expert verdict | collocated pressure-velocity coupling on tets is the right call; the face flux is the conserved quantity and needs a momentum-weighted (Rhie-Chow type) interpolation | **added** to ADR-058 |

## 2. Mutation probes (each edits one file, rebuilds, runs `^(unit\.CfdInput|architecture\.)` through CTest, restores the file byte for byte)

| id | the mutant | verdict | killed by |
|---|---|---|---|
| CP01 | an ineligible body is reported as "no mesh" | KILLED | `CfdInput_RefusesAnIneligibleBodyBeforeLookingAtTheMesh`, `_ReportsTheFirstFailureInTheDocumentedOrder`, `_NamesTheSameReasonAsTheThermalBoundary...` |
| CP02 | mesh currency is never asked | KILLED | five cases, including the stale-geometry and stale-intent cases |
| CP03 | a material without a density is accepted | KILLED | `CfdInput_RefusesAMaterialThatHasNoDensity` |
| CP04 | a stale mesh is reported as "no mesh" | KILLED | four cases |
| CP05 | the model carries no geometry revision | KILLED | `CfdInput_AcceptsACurrentMeshWithAUsableDensity` |
| CP06 | the model carries no material id | KILLED | the same |
| CP07 | a missing material is reported as a bad density | KILLED | `CfdInput_RefusesWhenTheDocumentNamesNoMaterial` |
| CP08 | `cfd` at layer 60 (beside io) | KILLED | `architecture.checker.valid` (thermal and structural may no longer include it), `.cfd-to-structural`, `.cfd-to-thermal` |
| CP09 | `cfd` at layer 47 (above thermal) | KILLED | `architecture.checker.valid`, `.cfd-to-thermal` |
| CP10 | the cfd -> thermal link is not refused | KILLED | `architecture.targets.cfd-links-thermal` |

10 of 10 killed; every file restored. Logs in `qualification/mutation/`.

## 3. The author's questions

| question | answer |
|---|---|
| Can a CFD solver be handed a stale mesh? | No: no CFD function takes a mesh; `CfdModel` is the only way in and it asks P16's currency against the document now. |
| Can a mesh of a changed body pass as current when the counts are equal? | No: currency is the geometry revision and the sizing intent, never a count (`CfdInput_RefusesAMeshOfGeometryThatHasSinceChanged` holds the count equal and is still refused). |
| Can the boundary disagree with the qualified thermal and structural ones? | Not about geometry or meshes: the delegation test walks one document through every state. |
| Can a fluid be defaulted? | No: there is no fluid data in the module, and a missing material is a named refusal. |
| Can the CFD module reach a thermal or structural symbol? | Not by include (layering) and not by link (link graph, deny and allow). |
| Can P16 learn about CFD? | No: `meshing-to-cfd` is refused. |
| Can a mesh backend be called from CFD? | No: `cfd-backend-leak` is refused. |
| Does the boundary claim a viscosity it did not check? | No: the header, ADR-060 and a tripwire test say the opposite and fail when a CFD consumer kind appears. |
| Is any checklist item answered only in prose? | No checklist item of this milestone needs code beyond the boundary and the gates; each is answered in an ADR (see `SCOPE.md`). |
