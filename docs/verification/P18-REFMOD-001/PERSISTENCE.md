# P18-REFMOD-001 — Save, load, re-solve

**Item 19.** A saved BetterCAD file is the **canonical intent** and nothing derived. This is the reference-suite check that the contract holds for real reference models — the slab (RM-THERM-02), the convection model (RM-THERM-05) and the lumped transient (RM-THERM-08) — and not only for P18-PERSIST-001's fixtures.

`ThermalReference_Persistence_SaveLoadReSolve`, for each of **six** documents — the slab, the convection model, the lumped transient and (added after the review) the surface-flux slab, the generation box and the held-cap transient slab, so that surface loads, volumetric loads and a transient with two convections are all round-tripped:

1. **`io::saveDocument` → file 1 BEFORE anything is regenerated, meshed or solved** (the document exactly as the builder made it; the review noted that saving after a `Run`, whose constructor solves, would compare two post-solve files).
2. Build, regenerate, mesh, run through `runThermalAnalysis` → accepted. Fingerprint the run: every nodal temperature of every stored state, the stored times, the final gradients and heat fluxes, every balance term and per-node reaction of every step and the worst residual, hashed **bit by bit**. Run **again** and `saveDocument` → file 2. **File 1 and file 2 are byte-identical**: regenerating, meshing and solving (twice) do not write to, or change, the document.
3. The file contains none of `gradient`, `reaction`, `tetrahedra`, `triangle`, `balance`, `solution`, `nodal`, `temperatures`, `heat_flux_field`, `mesh_nodes`, `stored_energy`, `residual_history`. This is a **spot check of vocabulary, not a proof**: the proof is item 2 (the file does not change when a mesh and a result exist), which would fail if any derived datum were written into the document.
4. `io::loadDocument` → a **new** `Run` (regenerate, mesh, solve from nothing). Accepted, the same node and tetrahedron counts, and the fingerprint is **equal bit for bit** (`fingerprint equal: yes` ×3).
5. The reloaded document saved again is **the same bytes** — the round trip is a fixed point.

`ThermalReference_Persistence_EditedFileIsAnotherProblem` is the converse. The slab's cold-cap temperature is edited **in the text of the saved file** (`300.0` → `200.0`), the file loaded and solved: the flux is the *new* closed form `k ΔT/L = 8 × 200 / 1 = 1600 W/m²`. A file is intent; changing the intent changes the answer, and nothing from the earlier solve leaks into the later one.

**What is not claimed.** The CLI-level counterpart — a *different process* loading the file the example program wrote and printing the same numbers — is in `CLI_VALIDATION.md` (the stale-mesh document is edited between two `thermal-heat-flux` processes: 30 mm → 36 mm gives 16 666.67 → 13 888.89 W/m²).

## Byte invariance across presets

The same three files are written by each preset's `bettercad_example_reference_models --thermal --out …` in the qualification; their SHA-256 per preset is compared in `DETERMINISM.md`.
