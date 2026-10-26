# P18-REFMOD-001 — Audit of what existed, and what the reference models had to work around

Written **before** the reference documents were designed, and extended as measurements came in. Everything marked *measured* was produced by running the real P16 mesher and the real P18 chain on the real documents; nothing here is taken from a design note.

## 1. What the chain offered (the seams the suite is built on)

| Need | Found | Used as |
|---|---|---|
| One call that runs a document through the whole chain | `thermal::runThermalAnalysis(document, regenerator, mesher, analysis)` (P18-CLI-001, ADR-057) | every document-level test; `Run` in `tests/reference/ThermalReferenceSupport.hpp` |
| A verdict that is not the solver's own | `thermal::validateThermalAnalysis` (P18-VALID-001) — 15 rules, `ValidationCode`, `statusOf(rule)` | `accepted()` is the success criterion; refusals are asserted by **stage** (`ThermalRunStage`) and **code** |
| Recovered fields and the heat accounting | `ThermalPostResult`, `ThermalBalanceResult` (P18-POST/BALANCE) | compared with the oracles; never used to compute an expectation |
| An element kernel callable on a bare corner list | `computeTet4Geometry`, `computeTet4Conduction` (P18-ELEM-001) | RM-THERM-01, compared with a dense oracle that shares no code |
| Algebraic seams | `actest::AsmHand`, `actest::assemble`, `buildThermalConstraintMap`, `solveSteadyThermalSystem`, `recoverSteadyFromSolution`, `steadyFullResidual` | the affine patch (a different temperature at every boundary node — no canonical boundary condition can say that) and the structured-mesh convergence study |
| A structured tetrahedral mesh | `pstest::makeGrid` (Kuhn tetrahedra) | RM-THERM-04 convergence — see §3 |
| Example runner that is also a CLI fixture | `examples/reference_models` (`--structural` precedent, P17-REFMOD-001) | new `--thermal [--out dir]` loop; its exit code is an assertion |
| Strict JSON reader | `tests/cli/MiniJson.hpp` (RFC 8259) | CLI payloads are parsed, never grepped |

## 2. What did **not** exist, and how that was handled

| Missing | Consequence | Handling |
|---|---|---|
| **More than one material per document** (ADR-049: `Document::materialAssignment()` is one slot) | RM-THERM-03 (composite wall) cannot be built | **No document is built.** `RM03_UnsupportedLayeredCapabilityExplicit` shows both reasons hold: a second assignment *replaces* the first (the slab keeps one gradient everywhere), and `meshing::volumeMeshFor` refuses a two-solid body by name (`MultipleSolids`). The closed form is verified against hand arithmetic (`R1 = 0.25, R2 = 0.15 K/W, Q = 250 W, T_interface = 337.5 K`) and a single-material stand-in is shown to be *wrong* (series conductivity 3.125 W/(m K) carries the same 250 W but puts the interface at 360 K, not 337.5 K, with a uniform gradient of −400 K/m instead of −625 / −250). The suite builds neither stand-in. |
| A thermal surface-flux load on a **solving** document in the reference set | none needed | surface flux appears only in the RM-THERM-09 balanced-Neumann document (which must be refused) and in P18-LOAD/BALANCE/CLI suites |
| Mesh refinement that actually converges on a smooth field | see §3 | convergence shown on structured meshes; real-mesh checks use independent-FE equality |
| An independent dense FE solver | — | written in `ThermalAnalytic.hpp` (std-only) |
| A way to ask the CLI for numbers without regexes | — | strict-parse + closed-form comparison, run by `tests/cli/ThermalReferenceWorkflow.cmake` |

## 3. Measured limits of the P16 mesher (the finding that shaped RM-THERM-04 and RM-THERM-07)

All *measured*, release-ext, GCC 16.1, Netgen 6.2.2604, 2026-10-26.

1. **A planar face is two triangles whatever the target.** A global target size does essentially nothing on a box; a local size on a face produces a slab *one target deep* beside that face. A 500 × 500 × 1000 mm block at 100 mm and 50 mm face sizes gives 32 and 449 nodes (RM-THERM-02), but a *box with a size on all six faces* has **no node on the axis**: a *diagnostic variant* of the generation box (80 × 80 × 160 mm, q‴ = 2·10⁵ W/m³, with a size on every face and all six faces held — not the committed `ThermalGeneration`, which holds only its caps) gave a peak temperature that scattered over a 10 K band (301–311 K) from one sizing to the next, with no monotone trend against the analytic value — the field's peak sits on the axis, where no node is.
2. **Rod (R 50, L 100 mm):** RMS error 10–13 K against the closed form, *not falling* with refinement (needle elements, the F3 family of P17-REFMOD-001).
3. **Tube (ri 20, ro 40, L 6 mm; deflection 0.5 → 0.02 mm × outer-wall size 8 → 2 mm; 160–4200 nodes):** hottest node 351.6–358.2 K vs exact **348.411 K**, RMS error 1.9–3.9 K, not converging; interior nodes sit 4–5 mm from each wall; the log-law heat rate is 2.5–4 % stiff. The documented levels are 180 / 229 / 507 nodes: hottest 354.94 / 354.09 / 353.85 K, RMS 3.99 / 3.80 / 3.79 K.
4. **An independent dense FE solve** (own shape gradients from the inverse of [1 x y z], Ke = k V GᵀG, Dirichlet nodes found by *radius*) **equals production to 9.7·10⁻¹³ … 3.8·10⁻¹² K on every committed real mesh (the slab and the three tubes; the exploratory meshes of items 2–3 measured 5·10⁻¹³ … 2·10⁻¹¹ K in the diagnostic runs, whose code is not committed).** The production chain is therefore *correct*; the bias is the mesh. This is the evidence that separates "the solver is wrong" from "the mesh cannot represent the field", and it is why the real-mesh tests assert *equality with an independent solve* and *bounded error against the closed form* and do **not** assert convergence.
5. **Local refinement is a skin.** A 5 mm control on one cap grows the mesh from 9 to 78 nodes and puts 74 tetrahedra (mean size 11.3 mm against 55.4 mm unrefined) in the 10 mm band beside the cap, **but the RMS gradient error near a held cap did not improve** (baseline 1688 → target 1768 K/m): refinement beside a boundary without interior nodes behind it does not resolve the field. Recorded as measured; the test asserts the *size* effect and records the *accuracy* trend (±10 %), it does not assert an improvement that is not there.

**Owner findings from §3** (not fixed here; P16 is qualified and the brief forbids widening a tolerance after seeing a defect — and none of these is a P18 defect):

* **F-R1.** P16 cannot refine a smooth field on a box, rod or tube (no interior nodes beyond a skin). Mesh convergence of the thermal stack is demonstrated on structured meshes, which test the P18 stack, not the mesher.
* **F-R2.** Local sizing does not improve gradient accuracy near a held boundary. Any accuracy claim for refined thermal meshes would need P16 interior sizing.
* **F-R3.** P18 v1 cannot represent a composite wall (one material per document, one solid per mesh).

## 4. Nothing in P18 production was found wrong

No reference model exposed a production defect, so no production file was changed and no earlier milestone needs requalifying for correctness. (The mutation campaign — `MUTATIONS.md` — shows the suite would notice if one were introduced.)
