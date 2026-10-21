# P18-TRANSIENT-001 — adversarial review

```text
METHOD:   every question below was asked of the code as an attack: how would a wrong integrator, a stale
          input, a regularised matrix or a steady rule in disguise get through? Each is answered by a test,
          a mutation probe, an audit or a stated, measured limit. The questions are the milestone's own
          traps first (1-10), then the ones the reviewer added.
```

## The traps the milestone names

| # | Question | Answer and evidence |
| --- | --- | --- |
| 1 | Does a pure-Neumann transient model succeed, where the steady one is refused? | Yes. `PureNeumannTransientSucceedsWhereSteadyIsRefused` (steady: `missing_temperature_datum`; transient: four steps, stored energy +10 J per step exactly); the real block with 60 W and no condition (`InsulatedBlockWithGenerationSucceedsAndGainsEnergyExactly`, 69.232899 K over 100 s). Mutation TA21 (the steady datum rule applied to K, as the steady solver applies it) is killed by 10 cases, these two among them. |
| 2 | Does a disconnected positive-capacity model succeed? | Yes, with nothing prescribed and with one region anchored (steady: `unanchored_thermal_region`). `DisconnectedPositiveCapacityRegionsSucceed`: each body evolves from its own initial state, region 1's mean exactly constant, region 2 rising 2.5 K/s. |
| 3 | Is a region with no capacity at all refused, and a node with no capacity but a conduction path not? | `RegionWithNoCapacityAtAllIsRefused` (`singular_system`, step 1, the message says capacity; a `1e-30` capacity too); `ZeroCapacityConnectedByConductionSolves` (`[300, 300]` by hand). |
| 4 | Is the capacity the CONSISTENT matrix? | The non-uniform oracle `T[1] = [310, 320]`; a lumped `C` gives `[306, 324]` (differs by 4 K). Mutation E05 (off-diagonal `C` dropped) is killed by 36 cases. |
| 5 | Is it `C/dt`, not `C*dt`, and the history `+ (C/dt) T[n]`? | One-DOF `4600/12`; E01 (`C*dt`) killed by 35 cases; H01 (sign) 38; TH02 (no history) 42; H03 (`* dt`) 31; TH04 (not divided) 35; TH05 (no `Q`) 32. The physical residual alone refuses a `C*dt` or flipped-sign candidate whose own system residual is `1e-10`. |
| 6 | Is the nonzero Dirichlet value exact, and does it enter the history? | `NonzeroDirichletMatchesDenseOracle` (five prescribed sets, six states each against the test's own elimination), prescribed unknowns equal to the bit at every state; `PrescribedDominatesTheInitialTemperatureAtTimeZero` (`1000/3`, not `300`); TA01 (no override) killed by 8 cases. |
| 7 | Is a failed step ever retried, shortened or regularised? | No. A failed step fails the run with its step and time; there is no `settings` member for retry, adaptivity, regularisation, lumping or a method (compile-fail, 19 `no member` cases); E06 (epsilon on the diagonal) is killed by 19 cases; source audit clean. |
| 8 | Is `t_end` hit exactly, and is the last step its own? | `dt = 0.3, t_end = 1` is `0.3, 0.3, 0.3, 0.1` with `t_4 == 1.0`; `LastPartialStepUsesItsOwnTimeStep` (the nominal step for the last would move the answer by more than 0.5 K); G04, G05, TA05, TA07b killed. |
| 9 | Does floating-point noise in `t_end/dt` create a sliver step? | `0.3/0.1 = 2.9999999999999996` is three steps (G01 killed); the tolerance edge is tested at `0.5e-9` and `2e-9` (G02 killed); times are products (G03 killed). |
| 10 | Can a result be published from a failed run, and does an old result stay stale? | No: one private constructor, one friend; `FailedRunDoesNotCreateACurrentResult` (R1 stale, attempt failed, no current result); `AFailureAtALaterStep…` (step 2 of 2 fails, no history). |

## The reviewer's questions

| # | Question | Answer and evidence |
| --- | --- | --- |
| 11 | Can a solver's "success" pass a wrong step? | No: both residuals are computed from the original arrays after the solve. Wrong finite candidates refused: perturbed free temperature, stagnant, wrong `dt`, explicit Euler, `C*dt`, steady, Crank-Nicolson. A06 (acceptance skipped) killed by 3 cases. |
| 12 | Is the physical residual redundant? | For a correct build yes in value, and it is deliberately stricter when the step is small. Mutation V04 SURVIVED the first test set (it is an equivalent mutation if only large steps are tried); the finding is closed by `PhysicalResidualIsStricterThanTheReducedOneForASmallStep` (reduced `1.7e-10` passes, physical `1.7e-7` refuses) and V04 is then killed (TV04). |
| 13 | Is the free-row restriction right? | The residual at a prescribed unknown is the boundary heat (above 1 W in `…ExcludesTheBoundaryHeat…`, while the free rows are below `1e-13`). R04 (all rows counted) killed by 29 cases. |
| 14 | What is the normalization, and is it scale free? | `||r|| / (||C||∞||ΔT||/dt + ||K||∞||T|| + ||Q||)`; scaling `K, C, Q` by `1e-6` and `1e6` leaves the answer and the residual unchanged; the exact value `40/1560` is pinned (R05, R06 killed). |
| 15 | Can a finite-checking shadow hide a missing check? | Found by probing: V02 (candidate finiteness) and V03 (previous-state finiteness) SURVIVED the first test set because a later check gave the same classification. Closed by asserting the message that names the right unknown / the previous temperature and a NaN at a prescribed unknown (which would otherwise be `constraint_not_preserved`); TV02 and TV03 then killed. |
| 16 | Are the settings the user's? | TA14/TA15 (floor and tolerance replaced by constants) killed: a floor of `0.999999` refuses the chain at step 1; a tolerance of `1e-30` refuses at step 1; `1.0` accepts. |
| 17 | Is the effective matrix the union of the patterns and symmetric? | `EffectiveMatrixIsTheUnionOfThePatterns` (nine entries, two patterns each lacking two), E03 killed; asymmetric `C` → `inconsistent_system` (E04 killed); overflow → `non_finite_input`, never clamped. |
| 18 | Does the order of refusals leak? | `SettingsAreRefusedBeforeStalenessIsExamined`: five bad settings against a stale (steady) system each report themselves; TS03b, TS04, TS05 killed. |
| 19 | Are K and C interchangeable by mistake? | No: `ConductanceMatrix` and `CapacityMatrix` are distinct types in the three entry points; two compile-fail cases. |
| 20 | Is anything a function of time? | No member of a load, a condition or the settings carries one (three compile-fail cases); the solve is defined for numbers. |
| 21 | Is a stale input ever solved? | Mutations P11-P17 (current mode, stale system, stale boundary, wrong numbering) are all killed; each edit stales exactly its own reason (`EditsStaleExactlyWhatTheyChange`, `StaleReasonsNameEachFieldAlone` covering all thirteen reasons). A specific-heat edit stales the system although `K` is unchanged. |
| 22 | Is a remesh with identical counts and ids caught? | Yes: the stamp differs; the old system, boundary, numbering and constraints are each refused with their own problem (`RemeshStalesTheSolutionAndRefusesTheOldPieces`). |
| 23 | Does it mutate its inputs? | No: matrices, heat vector, constraints, the initial vector, the model, the system, the boundary and the document revision are unchanged after three runs (two tests). |
| 24 | Is it deterministic across runs and presets? | 30 + 20 repeated runs are bitwise equal; fingerprints are recorded and compared across the three presets in [DETERMINISM.md](DETERMINISM.md). |
| 25 | Can the history exhaust memory or the step count overflow? | The grid is O(1); `kMaxTimeSteps` and `kMaxStoredValues` refuse before the first step; the quotient overflowing to infinity is refused; TA08 (guard removed) does not finish within 240 s. |
| 26 | Does it call any steady-solver logic it should not? | `analyseAnchoring` is not called (source audit: 0 calls). Applied to the effective matrix it would be harmless (every row of `C/dt + K` sums to a positive number, so every region is anchored; TA03 is killed only through the no-capacity message); applied to `K`, as the steady solver does, it refuses the pure-Neumann model (TA21, killed). The reduction `reduceThermalSystem` IS reused, deliberately: one definition of the Dirichlet partition. |
| 27 | Was anything qualified changed? | No. `ThermalSolve.{hpp,cpp}`, `SymmetricSolve.{hpp,cpp}` and `StructuralSolve.cpp` are byte-identical to HEAD (recorded in [FREEZE.md](FREEZE.md)). |
| 28 | Is the sparse path really sparse, and is Eigen contained? | No dense `n x n` anywhere in the production files (audit); 216 unknowns with `nnz(K) = 2446`, 900 unknowns with `11 506`; Eigen is not named in the header or the source. |
| 29 | Does the shared-library preset export everything? | Built early and run (62 then 72 cases passing); the three presets are qualified. |
| 30 | What happens at the edge of representable steps? | Stated in the open: a step so small that the temperature change is below the rounding of the temperature (`dt/tau ~ 1e-12`) is refused by the physical residual, with a message saying so; `1e-6` and `1e-3` pass; a user tolerance of `1.0` accepts it. |

## Findings

| # | Kind | Finding | Disposition |
| --- | --- | --- | --- |
| F1 | test gap | V02, V03: a missing finiteness check was shadowed by a later check with the same classification. | Messages asserted; both killed. |
| F2 | test gap | V04: the physical-residual threshold looked redundant against the reduced residual. | It is stricter for small steps; a test pins that and documents it; killed. |
| F3 | redundant code | `NotTransientSystem` tested the mode AND the capacity, but a system's mode is derived from its capacity, so the second test could not be reached; found when a test could not construct the case. | One test kept, the invariant stated in a comment and a test. |
| F4 | unreachable branch | The physical residual returned `infinity` for a zero denominator over a nonzero numerator; each term of the denominator bounds the numerator, so it cannot occur. | Replaced by the proof in a comment (`return 0.0`). |
| F5 | limitation, documented | Tiny steps (see 30). | Message improved to say so; in [TRANSIENT_RESIDUAL.md](TRANSIENT_RESIDUAL.md). |
| F6 | tooling | The mutation harness (a) read a process exit code that `Start-Process` can leave empty, so three survivors were first logged as kills; (b) reused ids of earlier milestones, so 18 probes were skipped silently; (c) hit `-Werror=unused-parameter` on six reformulated probes. | (a) verdicts are read from the test output; (b) the skipped 18 were rerun under new ids; (c) the six were reformulated. See [MUTATIONS.md](MUTATIONS.md). |

**No production defect.** Two were redundant or unreachable code found while testing, removed or proven; two were
test gaps, closed; one is a documented numerical limit; one is tooling. No gate-blocking finding.
