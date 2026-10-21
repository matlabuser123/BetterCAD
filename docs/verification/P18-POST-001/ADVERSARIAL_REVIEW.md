# P18-POST-001 — adversarial review

```text
METHOD:   34 questions a hostile reviewer would ask of the recovery; each answered with a test, a mutation probe, an audit or a stated limit
RESULT:   no production defect. Six findings: three test gaps (closed), one redundant branch (removed), one invalid probe (corrected), one cosmetic alignment (fixed).
          No gate-blocking finding.
```

## The questions

| # | Question | Answer and evidence |
| --- | --- | --- |
| 1 | Does the recovery reimplement P18-ELEM's `G`? | No. `ThermalPost.cpp` calls `gradientFrom` once and `computeTet4Geometry` once; it contains no Jacobian / adjugate / cofactor / shape gradient (source audit; architecture rule 2; fixture `recovery-rewrites-geometry` fails the gate). |
| 2 | Is `Te` gathered in the tetrahedron's own node order? | Yes: `elementDofs` order, corners in the same order. Tests: `PreservesTetLocalNodeOrder…`, sparse-id test (two namings of one tetrahedron). **PM01** (sorted) killed by 18 cases. |
| 3 | Is a raw `NodeId` ever used as an index? | No. Handles 7, 42, 104, 9001 map to unknowns 0..3; the result arrays have size 4. **PM02, PM02b** killed. |
| 4 | Is a raw `ElementId` ever used as an index? | No. `ordinalOf` is a binary search; the fixture's tetrahedra are ids 2 and 3. **PM29, PM29b** killed (PM29b only after the test was strengthened; finding F1). |
| 5 | Is the component order really `[x, y, z]`? | `T = x`, `T = y`, `T = z` and a mixed-sign affine field. **PM03, PM04** killed (15, 12 cases). |
| 6 | Is the sign right? Could a test be written to pass either way? | Heat flows toward lower temperature: `qz = +4500` when the bottom of a slab is hot, `-4500` reversed; `q . grad(T) = -k |grad(T)|²` checked independently over 162 elements. **PM05, PM06** killed (17, 14). |
| 7 | Is `k` used exactly once? | Scaling test: gradient bit-identical for `k = 2.5` and `10`, flux x4. **PM07** (no `k`), **PM08** (`k²`) killed. |
| 8 | Does `q` contain a convection term? | No: with a film on the end cap, film exchange 2671.25 W/m² vs conductive `|q|` 3.27e-10 W/m² at t = 0 and `max |q + k grad T|` = 0 over all states. |
| 9 | Does `q` contain generation? | No: a 60 W insulated block heats by 69.2 K while `max |q|` = 9.58e-10 W/m². |
| 10 | Does `q` contain storage `C dT/dt`? | No: uniform heating has `dT/dt != 0` and `max |q|` = 1.79e-09 W/m². |
| 11 | Is the temperature Kelvin? A Celsius offset cancels in every gradient. | Tested on its own: 350 K / 280 K come back as 350 K / 280 K, bit for bit. **PM09** killed by the three cases that look at `T` itself (and by none that look only at gradients). |
| 12 | Is a field below absolute zero accepted? | Refused: `temperature_below_absolute_zero`. `0 K` accepted. **PM18** killed. This closes the limitation both solvers recorded. |
| 13 | A transient history that goes below 0 K late: is the early part published? | No: the whole history is refused and the first bad state is named (state 5 of 11). **PM28** (state reported as 0) and **PM40** (only the first state checked) killed. |
| 14 | Can an invalid tetrahedron be skipped? | No: the mirror tetrahedron fails the whole recovery and is named. **PM13** killed. |
| 15 | Are the three finiteness gates (gradient, flux, magnitude) each reachable? | Yes, one section each: `1e308 K` across a 1 mm tetrahedron overflows `G Te` (`non_finite_temperature_gradient`, element named); `k = 1e300` with a finite `1e9 K/m` gradient overflows the flux (`non_finite_heat_flux`); `k = 1e300` with `1.5e8 K/m` components keeps every component finite (`1.5e308`) while `hypot` is infinite (`non_finite_heat_flux_magnitude`). **PM14, PM15, PM16** each killed by its own case. |
| 16 | Is the recovery atomic? | The arrays are built in temporaries and published by one private constructor after every check; a failing recovery returns no object. Tests: the invalid-tetrahedron and late-state cases assert that nothing is returned. |
| 17 | A remesh with the same counts and ids: is the old recovery stale? | Yes, by the mesh stamp: 9 nodes and 12 tetrahedra with equal ids in both generations are different meshes. **PM22** (counts only) killed. |
| 18 | New conductivity with old temperatures? | Three defences (CURRENTNESS.md): the solution is stale, the model is refused, the held result is stale. **PM23, PM25, PM35** killed. |
| 19 | Is the result persisted, canonical or in the command history? | No: no serialisation symbol in either file (source audit: 0), no setter, and RESULT_MODEL.md's table. It is bound to its source only. |
| 20 | Can a viewer recompute the fields? | Not without turning four tests red: the ownership gate scans 497 files outside the thermal module (0 violations) and fails on the `recovery-gui-recomputes` fixture. |
| 21 | Can a held result be mutated? | No setter and no non-const accessor: six compile-fail cases (`…ARE_READ_ONLY`, `THE_SOURCE_IS_READ_ONLY`). |
| 22 | Is the magnitude consistent with the vector, and Euclidean? | `(3, 4, 12)` gives 13 (L1 would be 19); the stored magnitude equals the norm of the stored vector to 1e-12. **PM11, PM12** killed. |
| 23 | Is a zero gradient `+0` or `-0`? | `+0`: `signbit(q.x) == false` on an exactly constant field. **PM30** (`-(k g)`) killed. |
| 24 | Is anything smoothed? | No. Element-wise values only; compile-fail `THE_RESULT_HAS_NO_NODAL_HEAT_FLUX` and `…NO_SMOOTHED_FIELD`; **PM32** (each element averaged with its predecessor) killed by three cases. |
| 25 | Is it deterministic, across presets too? | 25 and 20 repeats, 0 differences; bit fingerprints compared across debug-ext, release-ext and debug-shared-ext (DETERMINISM.md). |
| 26 | Does a sparse identity space blow the memory? | No: arrays are `O(Ndof + Ntet)`; the sparse-id test has arrays of size 4 for handles up to 9001. |
| 27 | Does it scale? | 729 nodes / 3072 tetrahedra (affine, exact), 900 nodes / 4118 tetrahedra (real fine cylinder, steady slab to 1.68e-06 W/m² against 225 000). |
| 28 | Does it mutate its inputs? | `RecoveryDoesNotMutateItsInputs`: solution, system and mesh stamp equal before and after; inputs are `const`. |
| 29 | Is a `ThermalResult` (with a heat balance) returned by mistake? | No: the type is different and says so; compile-fail `A_POST_RESULT_IS_NOT_A_THERMAL_RESULT`, `…NO_HEAT_BALANCE`, `…NO_REACTIONS`, `…NO_BOUNDARY_FLUX`. |
| 30 | Are the transient states in the solution's order, with its times? | Yes, including the shortened last step (16 states, the final one at 100 s). **PM26** (reversed), **PM27** (every state at the first time) killed. |
| 31 | Is mode confusion caught both ways? | A steady result asked with transient settings says "steady solution"; the reverse says "transient history". **PM37, PM38** killed (PM37 only after finding F2). |
| 32 | Is the conductivity rule applied at the solution seam, not just the kernel? | Now yes, tested for both seams. **PM39** killed (after finding F3). |
| 33 | Is anisotropic or temperature-dependent conductivity silently accepted? | It is not representable: `k` is one scalar `ThermalConductivity`, and ADR-049 refuses the other cases upstream (P18-MAT). Nothing here widens that. |
| 34 | Does the milestone start anything it was told not to? | No heat balance, no boundary flux, no `q . n`, no colour, no command, no persistence, no validation suite: SCOPE.md and the compile-fail group. |

## Findings

| # | Kind | Finding | Disposition |
| --- | --- | --- | --- |
| F1 | test gap | The ordinal lookup was not distinguished from "raw id minus one, clamped" (PM29b survived): the fixture looked up the last tetrahedron only, for which the two agree. | Every tetrahedron is now looked up through all three accessors and compared with the array at its ordinal; the two tetrahedra are required to differ. PM29b killed. |
| F2 | test gap | The mode check of `requireCurrent(steady)` was observable only through the *message* of a refusal that would have happened anyway (PM37). | The message and `isCurrent` are asserted. PM37 killed. |
| F3 | test gap | The conductivity rule at the two solution seams was unobserved (PM39). | Both seams are tested with 0, a negative value, NaN and infinity. PM39 killed. |
| F4 | redundant code | Both solution seams contained a check that the solution's numbering describes the map presented. After the stamp comparison and `map.describes(mesh)` it cannot be false, so no test could reach it. | Removed, rather than left as unreachable, untested code. The behaviour it guarded is the `numbering_mismatch` of `checkNumbering`, which is tested. |
| F5 | tooling | Probe PM10 computed a value and discarded it (an equivalent mutant). | Counted VOID; replaced by PM10b, which applies the factor. |
| F6 | cosmetic | The continuation line of `recoverThermalElementFields` was misaligned after the rename from `recoverElementFields` (a structural function already owns that name). | Aligned in the header and the source; whitespace only. |

## Limits, stated

* **The recovered gradient is discontinuous across element faces** (a linear tetrahedron has a constant one). That is the engineering result of the element, not an error; a smoothed
  display is P18-VIZ-001's and may not replace it. Neither a patch recovery nor a nodal average is offered here.
* **A body that is physically below 0 K cannot be recovered.** Only a non-physical sink produces one, and the policy is to refuse rather than publish a field that would be coloured
  as if it were valid. A user who wants such a field has the solver's result, which is unchanged.
* **Reflection is not a transform test.** A reflected mesh inverts every tetrahedron and is refused by P18-ELEM, as the invalid-tetrahedron case shows.
* **The cross-preset claim is for this tree and these three presets.** It is measured by the post-run script, not argued.
* **The copy of the nodal temperatures costs `Ndof` doubles per state.** The solver's result remains the source of truth; the copy makes the post result self-sufficient.
