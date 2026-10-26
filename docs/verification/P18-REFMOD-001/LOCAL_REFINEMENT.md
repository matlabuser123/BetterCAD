# P18-REFMOD-001 — Local-refinement behaviour

**Item 14 (RM-THERM-07).** A 40 × 60 × 100 mm beam, k = 2, uniform generation 10⁵ W/m³, the start cap and the end cap held at 300 K (so the field is symmetric about mid-length). Three documents, identical except for one local mesh control of 5 mm on a cap:

| Document | Control | Nodes | Tets | Boundary triangles |
|---|---|---|---|---|
| `ThermalLocalBaseline` | none | 9 | 12 | 12 |
| `ThermalLocalTarget` | 5 mm on the **start** cap | 78 | 369 | 12 |
| `ThermalLocalMirror` | 5 mm on the **end** cap | 60 | 271 | 12 |

*Why three:* the baseline says what the unrefined mesh does; the target says what the control changes; the mirror is the **same experiment on the other end** — if refinement "worked" only near a cap because of something else about that cap (a numbering accident, a face order), the mirror would not reproduce it.

## What is asserted, and how it is measured

* **The volume band, not the surface triangles.** The refinement of interest is in the *volume* beside the face. The band is the set of tetrahedra whose centroid lies within 10 mm of the cap (twice the control's 5 mm slab, so the band contains it); the size of a tetrahedron is its characteristic length from the oracle's own determinant (never a production figure). Boundary triangles are 12 on all three meshes: a planar face is two triangles per rectangle whatever the target (`AUDIT.md` §3.1), so counting *surface* triangles would show no refinement at all.
* **Band size.** Start band: baseline 0 tets (the 12 baseline tets are long — none has a centroid within 10 mm), **target 74 tets, mean size 11.3 mm** (median 11.3, max 23.6), mirror 4 tets, mean 32.4 mm. End band: baseline 0, target 2 (mean 48.7 mm), **mirror 31 tets, mean 15.7 mm**. The unrefined mesh's mean characteristic size is 55.4 mm. Gate: refined mean `≤ unrefined mean / 3` (measured 4.9× and 3.5×) **and the far band of each refined document still coarser than half the unrefined mean** (48.7 and 32.4 mm against 27.7 mm — so a control that refined everything would fail), at least 8 tets in the refined band, and the control acts where it is put: the start band is finer in `target` than in `mirror` (11.3 < 32.4 mm), the end band finer in `mirror` than in `target` (15.7 < 48.7 mm).
* **The mapping survives.** All three meshes keep the named cap faces resolvable (2 facets each), generated heat is `q‴V = 24 W`, the two caps remove `−12 W` each (`q‴V/2`, `{1e-9, 1e-10}`), the balance closes (`≤ 1e-12`) and the solve is accepted. A control must not break the CAD → mesh mapping, and it does not.

## What refinement did to the accuracy near the held cap — measured, and it is not an improvement

RMS error of the recovered gradient against the exact parabola, near the start / near the end:

| | near start | near end |
|---|---|---|
| baseline | 1688.0 K/m | 1688.0 K/m |
| target (start refined) | **1767.6** | 1594.5 |
| mirror (end refined) | 1649.0 | **1757.0** |

The refined side's error is **+4.7 % (target) and +4.1 % (mirror)** against the baseline: *not better, not materially worse*. The suite asserts the trend within ±10 % and **does not assert an improvement, because there is none to assert.** The mechanism is the one in `AUDIT.md` §3.5: a local size creates a skin of small elements beside the face with nothing behind it, and a gradient recovered from elements whose neighbours are still 50 mm away is no better than before. Per the brief ("do not widen a tolerance after seeing a defect") the first form of the accuracy gate could not have been met either way, and the ±10 % band that replaced it is a **post-measurement guard** (set after seeing +4.7 % / +4.1 %; `TOLERANCES.md` Class E). The two unrefined sides moved the other way (−5.5 % and −2.3 %), so the brief's expectation — refinement *not systematically worse* — is **not met**: where a control was put the gradient error rose slightly. The size guard beside it was also redesigned after the first form (every element ≤ 2 × the control) failed on Netgen's grading. The measurement, not the guard, is the finding.

**Owner finding F-R2** (restated): P16 local sizing is a *geometry-fidelity* control. It does not make a thermal (or structural) gradient more accurate near a boundary. If thermal accuracy near boundaries is wanted, the mesher needs interior sizing.
