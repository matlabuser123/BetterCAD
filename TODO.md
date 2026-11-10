# BetterCAD — TODO

> `[x]` = implemented + tested + independently validated + adversarially reviewed + regression clean + evidence recorded.  
> Qualification milestones require the final qualified tree to match the committed tree.  
> Work top-to-bottom. Stop at failed gates. Never fake evidence.

---

# Status

```text
Current:
           P19 — CFD Integration. AUTHORIZED 2026-10-27 by an explicit
           scope decision ("start P19-ARCH-001"), with the phase plan below.

Prerequisite:
           P18 — Thermal Analysis — QUALIFIED (P18-QUAL-001, 2026-10-27):
           eighteen milestones PASS (345 boxes), 5804/5804 x 3 presets,
           qualified tree == committed tree. Three measured findings stay
           with the owner, unchanged: F1 (the heat-balance gate refuses
           correct transient runs once the time step is small), F2 (no bound
           on a temperature), F3 (no bound on the residual tolerance a user
           may set). P17's F1-F3 stand unchanged beneath them.

Current milestone:
           NONE AUTHORIZED. P19-TRANSIENT-001 passed its gate and finishing a
           milestone is itself a stop condition: the next one is a scope
           decision and not Claude's to make. Nothing below this line is
           authorized by being next, by being small, or by being the obvious
           continuation.

Previous milestone:
           P19-TRANSIENT-001 - Transient Incompressible Solver. AUTHORIZED
           2026-11-10 by the owner's "implement and fully verify" over the
           nineteen-item checklist, which arrived pasted into the instruction as
           the ten P19 checklists before it did, and was diffed against this
           file's own section before any implementation: byte-for-byte
           identical, 19 unticked and 0 ticked -- counted, not assumed -- and
           sha256 1d8018b25bd7ed26 over the section as the previous commit
           carries it. An off-by-one line range was caught and the section
           re-extracted before the comparison was believed.
           PASS 2026-11-10, nineteen of nineteen, ON THE THIRD QUALIFICATION
           ATTEMPT: 6965/6965 x 3 presets, 3527 x 5 = 17635 executions x 2
           presets with the marker-to-progress ratio exactly 5.0000, 17 of 17
           stages exit 0, 0 stage(s) failed, 4h49m, and 82 reconciliation checks
           over the harness's own logs with every predicted figure parsed out of
           FREEZE.md rather than typed in twice.
           THE RECONCILIATION WAS FALSIFIED BEFORE IT WAS BELIEVED. Five
           single-line sabotages of a COPY of the evidence -- a removed Passed
           marker, an altered identity hex, an altered assertion figure, a
           removed verdict sentence, a stage exit changed to 1 -- were each
           required to make the reconciliation fail, and each did, while the
           untouched control still passed. That is this milestone's answer to
           the check P19-STEADY-001 found could not fail.
           NEITHER FAILED ATTEMPT WAS THE TREE, and no product code changed
           between the three. Attempt 1 died 63 minutes in when the machine
           stopped being able to launch processes: 374 of its 375 ctest failures
           are 0xC0000142 and both remaining configures returned it within 30 ms
           of being asked. Attempt 2 was launched from a shell that cannot see
           the dependency prefix, so CMake never put it on CMAKE_PREFIX_PATH and
           find_package(OpenCASCADE) fell through to msys64's 7.9.3, which the
           project's own supported-range gate refused; the cause was established
           by running one probe in BOTH shells -- cmake EXISTS reads YES in one
           and NO in the other for the same absolute path, with LOCALAPPDATA
           identical -- rather than inferred from the message, and the three
           qualified caches contain 0 msys64 references.
           THAT SECOND ATTEMPT ALSO EXPOSED A HOLE WORTH NAMING. With no build
           stage having run, its two repeat stages still reported 100% tests
           passed, off binaries left on disk, and release-ext selected 3522
           instead of 3527 -- exactly the five cases the reviews had added. The
           three build trees were emptied before attempt 3 so that a stage with
           no build behind it fails loudly instead of passing quietly.
           IT ADDED NO NUMERICS: solveCfdTransient calls cfdMassFlux,
           reconstructCfdPressureGradient, assembleCfdMomentum three times and
           advanceCfdCoupling, in that order, every outer iteration of every
           step, and the transient term itself was qualified under P19-MOM-001
           two milestones earlier. No existing declaration changed and
           CfdSteady.hpp and CfdSteady.cpp are untouched -- which [cfd][steady]
           at 1263/19 and architecture at 205, both IDENTICAL to baseline and
           both read off the qualified binaries, are what measure rather than
           assert. [cfd] at 755043/697 minus the new suite's 1813/34 is exactly
           P19-STEADY-001's 753230/663.
           AN ADVERSARIAL REVIEW AND A MUTATION ANALYSIS EACH FOUND THE SAME
           LIVE DEFECT, which the author had found first:
           CfdTransientStepRecord::balance was never assigned, so every step
           record published two zeros where the qualified evaluator's two
           mass-balance measures belong -- "Per-step mass balance" is one of the
           nineteen items -- and the suite passed because the only assertion on
           it was isfinite() and zero is finite. The repair asserts that each
           sampled step's record EQUALS its sample's own metadata, which has no
           tolerance a default can satisfy, and it was verified to fail without
           the fix. Five test cases and about 370 assertions were added after the
           reviews, which is why the frozen counts are higher than the
           launcher's first draft carried.
           Independent validation came from five references: the analytic
           boundary throughput rho u A = 1.1976e-5 kg/s matched to five figures,
           the steady suite's own gravity figure reproduced through the transient
           path (-5.77935e-05 Pa/m, ratio 1.15819), the qualified steady solver
           reproduced by one large step to 1.50592e-07 relative in the same 59
           outer iterations, first-step refinement ratios 1.9834 and 1.9916
           against a first-order prediction of 2, and 240 solve-port calls over
           60 outer iterations = exactly 4, the same ratio as the steady
           solver's 244/61. The two policy items are BUILD failures: thirteen of
           the twenty-eight compile-fail gates are "no member named" gates over
           every record a time profile would have to live in. Fourteen
           limitations are recorded rather than fixed. Evidence
           docs/verification/P19-TRANSIENT-001/ and ADR-078.
           TWO SCOPE DECISIONS ARE OPEN AND REPORTED RATHER THAN TAKEN:
           ADR-078 L7 / ADR-077 L12, the convection scheme and gradient method
           hard-coded outside the analysis definition and outside its
           fingerprint; and the inherited literal 0x08 byte in
           verify-harness.cmd, in 72 of the 96 committed copies across the
           lineage, which no gate has ever evaluated. Both are below, under
           Next.
           P19-STEADY-001 (PASS 2026-11-09, commit
           370b90da75de236e66d0ddfbda1fb9254e557133) came before it and held
           this section until now; its full record is ROADMAP.md's capability
           entry and docs/verification/P19-STEADY-001/. The sections below
           hold the milestones before that, from P19-CONT-001 backwards.

Before that:
           P19-CONT-001 - Continuity Equation. AUTHORIZED 2026-11-08 by the
           owner's "implement and fully verify" over the twelve-item
           checklist, which arrived pasted into the instruction as
           P19-BC-001's, P19-IC-001's, P19-DIFF-001's, P19-GRAD-001's,
           P19-CONV-001's and P19-MOM-001's did. PASS 2026-11-08, twelve of
           twelve, ON THE FIRST QUALIFICATION ATTEMPT: 6687/6687 x 3 presets,
           3355 x 5 executions x 2 presets, 17 of 17 stages exit 0, and the
           eight subtree identities after the last test run identical to the
           eight before the first build. Evidence
           docs/verification/P19-CONT-001/. P19-PVCOUPLE-001 IS NOW OPEN; see
           Current milestone.
           THE PASTED TEXT WAS DIFFED AGAINST
           THIS FILE'S OWN SECTION FIRST and is byte-for-byte identical across
           all 31 lines, with 12 unticked items and 0 ticked -- counted, not
           assumed -- so the scope is this file's checklist unaltered, and
           sha256 2c7e8a70c635f48b over both after CR normalization. This
           authorization was written here BEFORE any implementation, as the
           six before it were.
           Three scope notes recorded at authorization time, not discovered
           later:
           (1) "DEFINE MASS-FLUX CONVENTION" DOES NOT AUTHORIZE A SECOND MASS
           FLUX. P19-CONV-001 already defines `phi`, and ADR-072 fixes its
           sign and its scatter. This milestone BINDS to that definition and
           states it; a second definition would be the "second parameter
           system" ARCHITECTURE.md rejects. That three earlier milestones each
           invented `phi` privately, until P19-CONV-001 made one, is exactly
           why this is written down before the work starts.
           (2) THE TWELVE ITEMS CONTAIN NO MATRIX AND NO SOLVE. Every one is a
           residual, an aggregate, a balance, a tolerance or a determinism
           claim, so the deliverable is a continuity EVALUATOR and not an
           operator that assembles into CfdSparseMatrix. A pressure-Poisson
           matrix belongs to P19-PVCOUPLE-001 and P19-LINSOLVE-001, and
           NEITHER IS AUTHORIZED.
           (3) THE GLOBAL IMBALANCE IS REPORTED AND NEVER ASSERTED TO BE ZERO.
           P19-DIV-001 measured a bit-exact global sum at 40.34% and recorded
           the consequence as its L3. "Relative/absolute tolerance" is
           therefore an item about justifying a bound BY MEASUREMENT, and a
           tolerance chosen to make a test pass would fail this milestone
           rather than complete it.
           (4) A SCOPE COLLISION WITH P19-MASSBAL-001 WAS FOUND DURING THE
           UNDERSTAND PHASE AND IS RECORDED HERE RATHER THAN RESOLVED
           SILENTLY. That milestone ("Mass Conservation / Flow Balance", NOT
           authorized) carries fifteen items of which seven restate items of
           this checklist in the same or nearly the same words: Cell
           continuity residual and Deterministic aggregation are identical;
           Relative tolerance / Absolute tolerance and Multiple inlets /
           Multiple outlets are this checklist's items split in two; Net mass
           imbalance, Global mass residual, Inlet mass flow and Outlet mass
           flow cover Global mass imbalance, Boundary mass flow and the
           Inlet/outlet mass-balance check. MORE IMPORTANTLY, CfdData.hpp
           already DECLARES the answer: `CfdMassBalance` fixes the two
           measures ADR-058 chose -- `maxCellContinuity` and
           `globalImbalance`, each relative to the boundary throughput
           `0.5 * sum |phi|` -- and its comment assigns THE FLOOR AND THE
           ACCEPTANCE THRESHOLDS TO P19-MASSBAL-001 BY NAME.
           The boundary this milestone therefore works to, decided before any
           code and not after a convenient discovery: P19-CONT-001 COMPUTES
           the two declared measures and fills the declared type; it does NOT
           set the throughput floor, and it does NOT judge a solution against
           an acceptance threshold. Its own "Relative/absolute tolerance"
           item is the FLOATING-POINT bound on its own identities, the kind
           `kCfdConvectionRowSumTolerance` already is, and not a policy
           threshold -- ADR-053's rule and CfdData.hpp's "CONVERGENCE IS NOT
           ACCEPTANCE" both put policy elsewhere. Where the boundary
           throughput is zero there is nothing to normalise against, which is
           why "Closed-domain check" is a SEPARATE item and not a degenerate
           case of the relative one: a closed domain's boundary flux is
           bit-zero because ADR-067's table prescribes `phi = 0` at all three
           wall kinds, so it is checked exactly rather than to a tolerance.
           A consequence worth stating plainly: P19-MASSBAL-001 will later
           re-tick items this milestone ticks. That duplication is the
           checklists', not this implementation's, and the overlap is
           recorded here so the later milestone inherits a known boundary
           instead of rediscovering it.

And before that:
           P19-MOM-001 - Momentum Equation. AUTHORIZED 2026-11-08 by the
           owner's "implement and fully verify" over the thirteen-item
           checklist, which arrived pasted into the instruction as
           P19-BC-001's, P19-IC-001's, P19-DIFF-001's, P19-GRAD-001's and
           P19-CONV-001's did. PASS 2026-11-08, thirteen of thirteen, ON THE
           SECOND QUALIFICATION ATTEMPT; attempt 1 failed a release-only
           build error in a test helper 66m28s in and is recorded in full at
           docs/verification/P19-MOM-001/qualification-attempt-1/ rather than
           discarded. NO MILESTONE IS NOW OPEN. THE PASTED TEXT WAS DIFFED
           AGAINST THIS FILE'S OWN SECTION
           FIRST and is byte-for-byte identical across all 32 lines, with 13
           unticked items and 0 ticked -- counted, not assumed -- so the scope
           is this file's checklist unaltered, and sha256 386e3242048e9e85
           over both after CR normalization. This authorization was written
           here BEFORE any implementation, as the five before it were.
           Scope note recorded at authorization time, not discovered later:
           the checklist's `+ rho g` body force is a CONSTANT gravitational
           term at the single constant density P19 v1 carries. It is NOT
           buoyancy, which this phase refuses (ADR-058) because buoyancy needs
           density to vary with temperature. A constant `rho g` at constant
           density is also absorbable into a modified pressure, so it changes
           no velocity field in a closed domain with no fixed-pressure
           boundary; that is a property of the equation and not a defect, and
           the implementation must not pretend otherwise.

Milestone before that:
           P19-CONV-001 — Convective Flux. AUTHORIZED 2026-11-07 by the
           owner's "Implement nd fully verify" over the fifteen-item
           checklist, which arrived pasted into the instruction as
           P19-BC-001's, P19-IC-001's, P19-DIFF-001's and P19-GRAD-001's did.
           PASS 2026-11-08. All fifteen items are recorded: 17 of 17 stages
           exit 0, 6525/6525 in each of the three presets, 3258 x 5 in two
           repeats with 16,290 counted executions each at a ratio of exactly
           5.000, 0 warnings under -Werror with
           BETTERCAD_WARNINGS_AS_ERRORS=ON read from each BUILT tree's own
           cache, 4h07m07s, 52,155 test executions in all, and the eight
           qualified subtrees identical across four readings, the commit
           object of 10ad63a among them, so the qualified tree is the committed
           tree. 48 unit cases / 34,681
           assertions and 28 compile-fail cases, with the assertion count
           IDENTICAL in Debug, Release and Debug-shared. The cfd subsystem
           reads 702,187 assertions in 447 cases, which is 667,506 + 34,681
           and 399 + 48 EXACTLY, so no pre-existing cfd assertion moved; 76
           tests added and 0 removed by set difference against P19-DIV-001's
           own log. This authorization was written here BEFORE any
           implementation, as the four before it were. THE PASTED TEXT WAS
           DIFFED AGAINST THIS FILE'S OWN SECTION FIRST and is byte-for-byte
           identical across all 32 lines, 15 unticked items and 0 ticked --
           counted, not assumed -- so the scope was this file's checklist
           unaltered.
           TWO INDEPENDENT REVIEWS WERE RUN BEFORE THE FREEZE, which is the
           one defect P19-DIV-001 recorded against itself and the reason there
           is no discarded attempt here. 25 findings: six code defects, three
           false published claims in ADR-072, eight test defects. The most
           serious was a MISSING OWNER/NEIGHBOUR RANGE GUARD with the
           unvalidated SIZE_MAX index used for WRITES — the guard
           P19-DIV-001's own review had already forced into that operator,
           REGRESSED one milestone later. The interpolation weight was left
           unpinned in the same place P19-GRAD-001 and P19-DIV-001 had each
           left it unpinned. The author's own pass found none of the six code
           defects and wrote three of the false claims. Nothing after this
           milestone was started.

Before those:
           P19-DIV-001 — Divergence / Flux Operator. AUTHORIZED 2026-11-07 by
           the owner's "Authorize DIV-001", in answer to a question that named
           this milestone and no other. PASS 2026-11-07. All twelve items are
           recorded: 17 of 17 stages exit 0, 6449/6449 in each of the three
           presets, 3210 x 5 in two repeats with 16,050 counted executions
           each, 0 warnings under -Werror with
           BETTERCAD_WARNINGS_AS_ERRORS=ON read from each BUILT tree's own
           cache, 3h00m04s, and the eight qualified subtrees identical across
           SIX readings — at the freeze, by the harness before the first
           build, by the harness after the last test run, recomputed from the
           working tree afterwards by the harness's own method, out of the
           staged tree before the commit, and back out of the commit object of
           5732e8d — so the qualified tree is the committed tree. 40 unit cases / 36,930 assertions and
           28 compile-fail cases, and the assertion count is IDENTICAL in
           Debug, Release and Debug-shared, so no assertion here is
           configuration-dependent. The cfd subsystem reads 667,506 assertions
           in 399 cases, which is 630,576 + 36,930 and 359 + 40 EXACTLY, so no
           pre-existing cfd assertion moved. This authorization was written
           here BEFORE any implementation, as the three before it were.
           THE SCOPE WAS THE TWELVE ITEM CHECKLIST ALREADY IN THIS FILE, which
           the owner did not quote and did not alter — unlike the four
           milestones before it, where the checklist arrived pasted in the
           instruction. The owner also did not say "implement and fully verify"
           in those words; authorizing the milestone was read as permission to
           implement it under this file's own definition of done, and nothing
           wider. P19-CONV-001 was not started then; it was authorized
           separately afterwards and has since PASSED.
           THE MILESTONE REACHED A FREEZE, A LAUNCH AND A PASS WITH NO
           INDEPENDENT REVIEW, and that is recorded as its first defect
           because it is the one that let the other twelve reach a passing
           qualification. The three milestones before it each carry a review,
           and CLAUDE.md's lifecycle puts ADVERSARIAL REVIEW before
           QUALIFICATION. The author's own pass and a sixteen-probe mutation
           sweep had been treated as sufficient. They were not.
           TWO INDEPENDENT REVIEWS THEN CONVERGED ON THE SAME TWO SHARPEST
           FINDINGS from readings that could not see each other: the
           interpolation weight was COMPLETELY UNPINNED — every internal face
           the velocity path interpolated across under an assertion has
           w = 2/3 exactly, so `return 2.0 / 3.0;` survived the whole suite —
           and the oracle that should have caught it was a
           character-for-character transcription of production under a header
           claiming independence, which is P19-GRAD-001's r5 defect recurring
           one milestone later. The second: the exact-cancellation claim was
           pinned only on the MASS path, where the assertion holds for any
           implementation, while ADR-071 Decision 2 is a claim about the
           VELOCITY path. Both were confirmed in the source before anything
           was changed.
           THE OTHER CODE DEFECTS: range guards that ran AFTER the span reads
           they protect; a silently swallowed failure returning 0.5 on a
           degenerate face, which would have produced a SUCCESSFUL field
           interpolated at a midpoint nobody asked for; and a phase-5 branch
           reporting a phase-2 enumerator. ADR-071 Decision 6's claim that the
           weight "has exactly one definition, and both operators read it" was
           FALSE WHEN WRITTEN — there were three copies — and the ADR now
           carries a dated amendment saying so rather than being reworded.
           22 MUTATIONS KILLED IN TOTAL: 16 in the original four-pass sweep,
           whose first pass killed only 8 of 16, and 6 more written for the
           claims the reviews pinned. The sharpest of the six had to be
           structural, because exact cancellation cannot be broken by any
           one-line change — `-flux`, `(-1.0) * flux` and `0.0 - flux` are all
           the same double — so the probe is a gather that re-derives the face
           value from the neighbour's side, which is the implementation
           ADR-071 Decision 2 rejects.
           THE DESIGN WAS MEASURED BEFORE IT WAS WRITTEN, and the measurement
           contradicted it. ADR-071's first draft predicted a bit-exact global
           sum; a 20,000-mesh experiment run before any C++ existed measured
           bit-exactness on 40.34%, with a worst relative imbalance of
           1.125e-16, because each cell accumulates its own faces before the
           two meet and (a + x) + (b - x) is not a + b. A test asserting a
           bit-zero global imbalance would therefore have FAILED on a correct
           operator. The global imbalance is REPORTED, never asserted to be
           zero (L3), and the script is committed with its output.
           Nothing consumes this divergence yet (L1), as nothing consumes
           P19-GRAD-001's gradient: it is one term of an equation
           P19-MOM-001 and P19-PRES-001 have not assembled, and wiring them
           together is a coupling step no authorized checklist contains.
           (docs/verification/P19-DIV-001/README.md.)

Earlier still:
           P19-GRAD-001 — Gradient Reconstruction. AUTHORIZED 2026-11-07 by the
           owner ("implement and fully verify", with the fourteen item
           checklist quoted in full). PASS 2026-11-07. All fourteen items are
           recorded: 17 of 17 stages exit 0, qualified from clean on the FIRST
           attempt, 6381/6381 in each of the three presets, 3170 x 5 in two
           with 15850 counted executions each, 0 warnings under -Werror,
           5h34m00s, and the eight qualified subtrees identical across SIX
           readings — at the freeze, by the harness before the first build, by
           the harness after the last test run, recomputed from the working
           tree afterwards by the harness's own method, out of the staged tree
           before the commit, and back out of the commit object of 6c0ea6a — so
           the qualified tree is the committed tree. 46 unit cases / 82,762 assertions and 24
           compile-fail cases, and the assertion count is IDENTICAL in Debug
           and Release, so no assertion here is configuration-dependent. The
           cfd subsystem reads 630576 assertions in 359 cases, which is
           547814 + 82762 and 313 + 46 EXACTLY, so no pre-existing cfd
           assertion moved. This authorization was written here BEFORE any
           implementation, as P19-DIFF-001's and P19-IC-001's were.
           THE METHOD CHOICE WAS SETTLED BY MEASUREMENT BEFORE ANY C++ EXISTED.
           ADR-070 keeps BOTH methods, with weighted least squares as the
           default and Green-Gauss measured against it rather than dropped,
           because the divergence identity sum_f C_f (x) S_f = V I holds to
           2.900e-16 over 20,000 random tetrahedra — so Green-Gauss with the
           TRUE face value is linear-exact on any mesh and ALL of its
           inexactness is the interpolation's. The two scripts behind that
           number are committed under docs/verification/P19-GRAD-001/
           experiments/ with their output, because an adversarial review found
           the founding measurement had no artifact behind it, and a
           quantitative justification with no log is a failed gate.
           FIVE CODE DEFECTS WERE FOUND BY REVIEW, more than either P19-IC-001
           or P19-DIFF-001, and every one was verified in the source before it
           was fixed: a NON-FINITE GRADIENT RETURNED AS A SUCCESS (a 1e308 cell
           value overflows the surface sum to inf, cancels to NaN, and passed
           every gate because none of the three isfinite calls looked at the
           OUTPUT); a self-refuting diagnostic that printed "an extreme
           eigenvalue ratio of 3, against the 1e+12 a solve can carry" because
           two causes were collapsed into one enumerator; an OUT-OF-BOUNDS
           READ reachable only through a table in another module; a
           constant-mode flag hard-coded true whose stated reason was FALSE;
           and a FALSE REFUSAL of a solvable cell. Nine problem enumerators,
           up from seven, and each new one has a test.
           THE FIFTH CAME FROM REPLACING A TRANSCRIBED ORACLE WITH AN
           INDEPENDENT ONE, and it is the clearest evidence in the milestone
           for why that matters. The suite's conditioning oracle had been a
           line-for-line copy of the function it checked; a cyclic Jacobi
           diagonalisation disagreed with it, and the textbook trigonometric
           form for the SMALLEST eigenvalue proved wrong by a factor of 3.2 at
           a conditioning of 1e12 and reported a positive-definite matrix as
           having a non-positive eigenvalue — refusing a cell it could solve.
           The smallest eigenvalue comes from the invariants now, and the
           flat-cell sweep's split moved from 4-refused/1-solved to
           3-refused/2-solved as a result. Both figures are recorded.
           EIGHTEEN OF TWENTY MUTATIONS KILLED, 2 RETIRED WITH REASONS, AND IT
           TOOK FIVE PASSES. The first killed only 10 — P19-DIFF-001's killed
           15 — and the seven survivors were not a formality: FOUR were real
           defects in the suite. A constant-field test at 0.0 cannot see an
           unstable interpolation, and neither can one at 17.25, because 69/4
           is dyadic and both products are exact; a search over 200,000
           constants found 16% that discriminate at the fixture's weight of
           2/3, and the suite uses 12345.6789. Closing all four grew the suite
           from 33 cases to 39, and the reviews took it to 46. The five-pass
           count is recorded because a one-pass 20-of-20 would have been a
           weaker result honestly reported.
           ONE PREDICTION WAS CONTRADICTED, TWICE. ADR-070 Decision 6 claimed
           the inverse-distance weights cannot affect linear exactness, and the
           sweep was given that as a prediction; replacing the weight with 1.0
           was KILLED both times. The mathematics holds in EXACT ARITHMETIC,
           but the mutation changes the normal matrix's scaling, so
           conditioning and rounding error rise past a bound tight enough to
           see it. The Decision now separates the two claims rather than being
           quietly reworded, and ADR-070 was corrected twice in all — the same
           class of defect that caught ADR-069.
           ONE TEST WAS DELETED AND HALF OF ANOTHER CUT, because a test that
           reads as evidence and is not is worth less than no test:
           TheInterpolationWeightMatchesAnIndependentNormalProjection never
           called the operator at all, and one of its assertions reduced to
           value + weight * 0.0 == value.
           Ten limitations are recorded, and three belong in any honest reading
           of the result: NOTHING CONSUMES THIS OUTPUT YET (L1) — it produces a
           CELL gradient and assembleCfdDiffusion wants a FACE gradient, so the
           interpolation that would close P19-DIFF-001's L1 is a coupling step
           this checklist does not contain, and wiring it in was not
           authorized; the predictors cannot disagree with the reconstructions
           because they share one decision path, so their tests pin that a
           successful input predicts no problem, not that two implementations
           agree (L9, identical to P19-DIFF-001's L8); and there is NO
           PERFORMANCE RESULT (L10), because nothing in cfd has a baseline and
           this milestone does not create one.
           TWO COUNTING TRAPS beyond the nine in the freeze: the sixth from
           P19-DIFF-001 RECURRED in a new place — grep -c Passed returns 6382
           and 15855, one and five more than the executions, because a test
           NAME carries the word — and a TENTH is new: counting by test name
           undercounts the suite by 2, because two display names are each
           registered under two ctest ids. Every count recorded here is taken
           by id. Counting trap (i) recurred verbatim as well, inside the
           milestone's own tooling, and is recorded as r9.
           (docs/verification/P19-GRAD-001/README.md.)

Earlier than those:
           P19-DIFF-001 — Diffusion Operator. AUTHORIZED 2026-11-06 by the
           owner ("implement and fully verify", with the fourteen item
           checklist quoted in full). PASS 2026-11-06. All fourteen items are
           recorded: 17 of 17 stages exit 0, qualified from clean on the FIRST
           attempt, 6311/6311 in each of the three presets, 3124 x 5 in two
           with 15620 counted Passed markers each, 0 warnings under -Werror,
           3h13m48s, and the eight qualified subtrees identical across SIX
           readings — at the freeze, by the harness before the first build, by
           the harness after the last test run, recomputed from the working
           tree afterwards by the harness's own method, out of the staged tree
           before the commit, and back out of the commit object of dc5e5a1 —
           so the qualified tree is the committed tree. 49 unit cases / 31,688
           assertions and 22 compile-fail cases, and the assertion count is
           IDENTICAL in Debug and Release, so no assertion here is
           configuration-dependent — a count that agreed only in Debug would
           mean a branch compiled out. The cfd subsystem reads 547814
           assertions in 313 cases, which is 516126 + 31688 and 264 + 49
           EXACTLY, so no pre-existing cfd assertion moved.
           ADR-069 records the operator as a PER-COMPONENT SCALAR matrix, which
           follows from ADR-066: the segregated momentum predictor assembles no
           row coupling u to v, so component X's block IS the cell-centred
           scalar array. The surface decomposition is OVER-RELAXED, chosen
           because |T_f|/|E_f| is what governs a deferred-correction iteration
           and over-relaxed is the only one of the three that keeps it at or
           below 1 at every angle; minimum correction reaches 81.8 at the
           89.3 degrees ADR-063 measured. Both wrong turns taken on the way to
           that are recorded in Decision 2 rather than tidied away.
           THE NON-ORTHOGONAL CORRECTION IS CALLER-SUPPLIED, NOT COMPUTED. The
           operator needs a gradient it has no right to build here, so it takes
           one, measures the omission per face, and reports the worst fraction
           in its summary. ADR-063 F1 calls the correction "mandatory in the
           discretisation, not an option" and also reserves any accept-or-refuse
           threshold to P19-VALID-001 and the owner, so this operator REPORTS
           rather than refuses, and L1 says so as a limitation rather than as a
           completion. Without a supplied gradient it is a correct Laplacian
           only on an orthogonal mesh, and no P16 reference mesh is one.
           TWO REVIEW FINDINGS WERE SUBSTANTIVE AND BOTH WERE DEFECTS IN WHAT
           THIS AUTHOR HAD WRITTEN DOWN, not in the code's behaviour. ADR-069
           Decision 7 described an interpolation weight that did not exist —
           no weight, no call site, no test — in the Decision that is itself
           the evidence for it; it was implemented rather than reworded. And
           the symmetry claim was too strong: gamma is computed once and
           scattered four times, so largestSymmetryError() == 0.0 is a property
           of the scatter and not an independent confirmation of the
           coefficient. It is recorded as STRUCTURAL, and the author's stronger
           claim to the owner was corrected explicitly rather than left
           standing. A third finding was a test defect of the same family: the
           skew case took its expected value from a test-side oracle on both
           sides, so an operator dividing by |d| would have passed the very
           test whose comment ruled that out. It now reads the assembled
           matrix.
           TWENTY MUTATIONS, ALL KILLED, across three sweeps — and the sweep
           earned its place rather than decorating the evidence. The
           adversarial review PREDICTED that eight specific source-vector
           mutations would survive; all eight are killed, the eighth only after
           the test written to catch it was itself repaired. THAT REPAIR IS THE
           SWEEP'S OWN FINDING: AFaceVaryingGradientIsReadAtItsOwnFace could
           not detect a hard-coded face index, because isInternal(face) is
           face.value() < internalFaces and the fixture's only contributing
           face was therefore index 0. Three probes were VOID because they did
           not compile — P18-ELEM-001's finding F3, reported as void rather
           than as survivors, and reformulated so the mutated code still uses
           every name — one was MIS-PAIRED by the author against a test that
           cannot see a diagonal, and one was a NO-OP inside an idempotent
           lambda. Each is recorded for what it was.
           THE AUTHOR'S OWN NEW TESTS CAUGHT TWO OF THE AUTHOR'S OWN DEFECTS
           BEFORE ANY REVIEW: the first interpolation weight was not a convex
           weight on a skewed mesh and measured 3.6095 on the Cylinder, which
           is ADR-065's recorded skewness showing up as a number; and the
           viscosity lerp was not exact for a constant field, because
           w*mu + (1-w)*mu is not mu. Both were replaced.
           Nine limitations are recorded, L3 among them: there is NO SOLVE and
           no momentum equation, so nothing here is shown to produce a flow
           field — this is one operator of an equation that does not exist yet,
           and the matrix has never been handed to a solver. L4 records the
           input gate's boundary-currency arm as UNTESTED rather than claimed,
           because bindCfdBoundary deliberately returns a default source and no
           test can reach that arm. No performance result is measured, and L9
           says why: nothing in cfd assembled a matrix before this milestone,
           so there is no baseline.
           A SIXTH COUNTING TRAP was met while reading the run and is recorded
           beside the five from the freeze: a grep for Skipped / Not Run /
           Disabled returns 8 lines per full log and 40 per repeat, and every
           one is a TEST NAME containing the word. The figure that answers the
           question is the Passed-marker total.
           (docs/verification/P19-DIFF-001/README.md.)

And earlier:
           P19-IC-001 — CFD Initial Conditions. AUTHORIZED 2026-11-05 by the
           owner ("implement and fully verify", with the twelve item checklist
           quoted in full). PASS 2026-11-06. All twelve items are recorded:
           17 of 17 stages exit 0, 6240/6240 in each of the three presets,
           3075 x 5 in two with 15375 counted Passed markers each, 0 warnings
           under -Werror, 5h17m46s, and the eight qualified subtrees identical
           across SIX readings — at the freeze, by the harness before the
           first build, by the harness after the last test run, recomputed
           from the working tree afterwards by the harness's own method, out
           of the staged tree before the commit, and back out of the commit
           object of c7ce82a — so the qualified tree is the committed tree.
           36 unit cases / 6,589 assertions and 20 compile-fail cases, and the
           assertion count is IDENTICAL in Debug and Release, so no assertion
           here is configuration-dependent. The cfd subsystem reads 516126
           assertions in 264 cases, which is 509537 + 6589 and 228 + 36
           EXACTLY, so no pre-existing cfd assertion moved.
           ADR-068 answers the question ADR-066 Decision 1 deferred, and the
           answer is that NO FIELD STORAGE IS ADDED: the expansion writes into
           a std::span<double> the caller owns and sizes. That discharges the
           deferral rather than pre-empting it — the clause that decides is
           "with a use in hand", and the only consumer of an initial field is
           a solve, which is later and unauthorized — and it makes "expanded
           cell arrays remain derived" STRUCTURAL rather than a promise, since
           the cfd module owns no field memory for a later edit to persist by
           accident. A uniform initial field stays uniform and gravity does
           not enter it, where ADR-067 made a prescribed BOUNDARY pressure
           hydrostatic; the asymmetry is deliberate and its cost is stated.
           NINETEEN review findings were acted on across three passes — the
           author's own, written down before either independent review
           reported, and two independent agents given the code but not those
           conclusions — all fixed, none deferred. One was a substantive code
           defect: CfdAnalysisDefinition::mesh was omitted from the source
           binding with no safety argument available, so re-pointing an
           analysis at another mesh control left the field reporting itself
           CURRENT for an analysis whose array must now be a different length.
           It is refused by name as DefinitionNamesAnotherMesh, first in phase
           order. THREE DOCUMENTATION CLAIMS WERE WITHDRAWN AS MEASURABLY
           FALSE: the silent-no-op rationale for checking the unknown variable
           before the length, "the fingerprint is LOSSLESS" (it is a 64-bit
           FNV-1a digest; canonicalText is the lossless one), and ADR-068
           Decision 4's "every written slot is checked" — a review reversed
           the cell order and every test still passed, because a uniform
           field's array is byte-identical under any permutation of cells.
           Recorded as less than a reader might assume. Five of the twelve
           items were LARGELY SATISFIED BEFORE this milestone opened, by
           P19-DATA-001 and ADR-061, and the evidence says so rather than
           claiming the work; what IC-001 adds to them is one measured gap
           closed (the NaN/Inf test covered one velocity component, not
           three) and the structural guarantees that keep them true. There is
           no solve, so nothing here produces a flow field — only a legal
           starting state. No performance result is measured. "Exact undo /
           persistence semantics" is met as value semantics plus the
           fingerprint, per ADR-068 Decision 9, NOT as a round trip: there is
           no CFD serializer in src/io/json at all and no CfdCommands.cpp,
           and P19-PERSIST-001 and P19-CMD-001 are later and unauthorized.
           Seven limitations are recorded, L2 and L6 among them: the
           cell-to-offset mapping is UNOBSERVABLE for a uniform field and the
           first non-uniform field will be the first able to pin it, and the
           per-mutation pairing table did not survive the client restart that
           wiped the scratchpad — recorded as a gap rather than filled from
           memory, because re-running the sweep would modify the frozen
           surface and void the qualification.
           QUALIFIED ON THE THIRD ATTEMPT, and both earlier attempts stay
           recorded. Attempt 1 was REFUSED BY THE PREFLIGHT and the harness
           was right: the harness had been copied through a grep filter that
           silently dropped preflight-space.ps1, and the report that "all five
           harness files are byte-identical" was true and incomplete. Nothing
           was built and no number was produced. Attempt 2 was KILLED BY A
           SESSION TEARDOWN at test 5589 of 6240 — 0x40010004 is
           DBG_TERMINATE_PROCESS and 0xC000026B is
           STATUS_DLL_INIT_FAILED_LOGOFF, so the cause is in the exit codes
           and not in the code under test — and it is VOID, but its debug-ext
           had already passed 6240 of 6240, independently confirming the
           predicted count before the reported attempt ran. Its repeat stages,
           running against a build tree that no longer existed, selected 0
           tests and were COUNTED AS FAILED: the P19-DOF-001 attempt-1 lesson
           holding under a condition nobody arranged.
           (docs/verification/P19-IC-001/README.md.)

Earlier than all of those:
           P19-BC-001 — CFD Boundary Conditions. AUTHORIZED 2026-11-05 by the
           owner ("implement and fully verify", with the twenty item checklist
           and the canonical / derived split quoted). PASS 2026-11-05. All
           twenty items are recorded: 17 of 17 stages exit 0, qualified from
           clean on the FIRST attempt, 6184/6184 in each of the three presets,
           3039 x 5 in two with 15195 counted Passed markers each, 0 warnings
           under -Werror, 3h28m32s, and the eight qualified subtrees identical
           at the freeze, before the first build, after the last test run and
           when recomputed from the working tree afterwards, out of the
           staged tree before the commit and back out of the commit
           object of 1e0858a — six readings, all identical, so the
           qualified tree is the committed tree.
           48 unit cases / 2,136 assertions and 16 compile-fail cases, and the
           assertion count is IDENTICAL in all three builds, so no assertion
           here is configuration-dependent.
           ADR-067 freezes the per-field treatment as a TABLE — six condition
           kinds against p, U and phi — with four policy values rather than the
           checklist's two, because a slip wall's velocity is a symmetry
           condition and the mass flux at a pressure boundary is an outcome.
           Two values would have left every operator to re-derive the treatment
           from the kind, and the first that forgot would admit through-flow at
           a symmetry plane: a wrong answer that still converges. Resolution
           goes through P16 alone and there is NO nearest-face fallback. Every
           boundary facet carries exactly one condition, coverage is proved in
           both directions, and the reverse attribution is counted through
           P16's own sourceFaceOf rather than inferred from the forward pass.
           THIRTEEN behavioural defects were found before the freeze — four by
           the author's own adversarial pass, which two independent reviews then
           confirmed, and nine more by those reviews — all fixed, none
           deferred, six verified by MUTATION. Among them: a cardinality check
           that was a tautology; a tangency tolerance that was unsound on a
           curved target; an over-determined pressure datum that was accepted;
           and a crash rather than a diagnostic where a claim array was indexed
           on a precondition established twenty lines earlier.
           Recorded as less than a reader might assume. There is no matrix, no
           source vector and no solve, so nothing here is shown to produce a
           correct flow field — only a correct and fully attributed statement of
           the boundary intent. No performance result is measured. One
           enumerator, DegenerateFacet, is UNREACHABLE and is recorded as such
           rather than claimed tested. L2 is a NARROWING this milestone's own
           review introduced: a moving wall is planar-only, so a sliding
           cylindrical wall is refused by name, and that is the owner's to
           revisit. The authorization was NOT recorded in TODO.md before the
           implementation, unlike P19-DOF-001's, and the evidence says so in
           REVISION r1 rather than tidying it away.
           (docs/verification/P19-BC-001/README.md.)

           Earlier P19 milestones, each at its own recorded verdict:
           P19-DOF-001 PASSED 2026-11-05 on attempt 2 (6120/6120 x 3 presets,
           2991 x 5 in two, 17 items, 27 unit cases / 58,197 assertions and
           35 compile-fail cases, ADR-066; attempt 1 failed 2 of 17 stages
           while its launcher exited 0 — which is why the harness's own
           verdict and NOT an exit code is what is read — and the dead build
           cost the repeat stage its Catch2 discovery, which selected 352
           tests instead of 2991 and passed every one, the founding defect
           the zero-match guard exists to catch; attempt 1 is preserved under
           qualification/attempt-1/ rather than erased, and two independent
           reviews found the same four evidence defects, among them a
           canonical-state proof that was VACUOUS and a staged-gate line that
           was a NO-OP on every tested path);
           P19-GEOM-001 PASSED 2026-10-31 (6058/6058 x 3 presets,
           2964 x 5 in two, 21 items, ADR-065; the per-cell surface-vector
           sum closes to 2.03e-16 of the cell's own area scale on all eight
           reference meshes, measured rather than asserted away, and four
           builder guards called unreachable were found reachable and each
           now has a test);
           P19-TOPO-001 PASSED 2026-10-31 (5998/5998 x 3 presets,
           2h11m16s, 36 unit cases / 108,373 assertions; two independent
           reviews each found a real defect, one of them an unconditional
           outward-winding promise the builder did not enforce, now
           ENFORCED by two added refusals, ADR-064);
           P19-MESH-001 PASSED 2026-10-31 on attempt 2 (5939/5939 x 3
           presets, 2h46m08s; attempt 1 failed 5 of 17 stages because the
           previous machine's %TEMP% volume was full, and that machine was
           permanently replaced and re-qualified in its own right —
           ENVIRONMENT-MIGRATION.md, docs/verification/P19-MESH-001/FREEZE.md);
           P19-ARCH-001 PASSED 2026-10-27 (5826/5826 x 3 presets, 745
           objects, ADR-058/059/060); P19-DATA-001 PASSED 2026-10-27
           (5893/5893 x 3, 750 objects, ADR-061); P19-FLUID-001 PASSED
           2026-10-28 (5919/5919 x 3, 753 objects, ADR-062)
           (docs/verification/P19-*/).

Qualified:
           P0–P10 — BetterCAD v0.1.0
           P11 — Advanced Part Modelling
           P12 — Production Part Modelling
           P13 — Assemblies
           P14 — Technical Drawings
           P15 — Materials / Engineering Data
           P16 — Meshing
           P17 — Structural FEA
           P18 — Thermal Analysis

Next:
           NOTHING IS AUTHORIZED. P19-TRANSIENT-001 is PASS and no milestone is
           open, so the next one is a scope decision for the owner. A
           predecessor's PASS has never been permission to start the one after
           it, and the one it would be easiest to drift into is named so that
           the drift stays visible:
           P19-VALID-001 owns whether the numbers are good enough. CfdResult.hpp
           says "CONVERGENCE IS RECORDED, NOT RE-JUDGED HERE", so the marches
           this milestone publishes are converged runs and not validated ones,
           and nothing in docs/verification/P19-TRANSIENT-001/ claims otherwise.
           ADR-078 L3 says what is missing by name: backward Euler's ORDER is
           asserted by halving dt, and the absolute temporal error against an
           exact solution is not measured at all, because the fixture has no
           closed-form transient. An analytic transient reference is
           P19-VALID-001's to bring.
           THREE FINDINGS ARE OPEN AND UNAUTHORIZED, all reported rather than
           fixed, and all needing the owner's scope decision:
           (1) ADR-078 L7, inheriting ADR-077 L12 unchanged - the convection
           scheme and the gradient method are hard-coded in both solveCfdSteady
           and solveCfdTransient, outside the analysis definition and outside
           the definition fingerprint, which contradicts ADR-073 Decision 5.
           ADR-078 L14 reads the same finding from the test side: the
           steady-limit cross-check cannot discriminate them, because both
           orchestrators pin the same two choices, so changing ONE would be
           caught and changing BOTH would not. Moving them into the definition
           means changing CfdAnalysisDefinition and canonicalText, both
           qualified under P19-DATA-001.
           (2) ADR-078 L11 - the per-step convergence criterion becomes vacuous
           at a small enough dt, measured rather than argued: step 1 collapses
           from 60 outer iterations to 1 between dt = 1e-1 and dt = 1e-3, and
           the effect saturates. The ANSWER is not wrong; the EVIDENCE says more
           than it means, because outerIterations = 1 beside a residual under
           tolerance reads as convergence for a step that coupled nothing. The
           normalisation lives in advanceCfdCoupling, qualified under
           P19-PVCOUPLE-001, and changing it would change what every steady run
           is accepted on as well.
           (3) docs/verification/*/qualification/verify-harness.cmd carries a
           literal 0x08 byte where c-build was meant, in 72 of 96 committed
           copies, entered 2026-09-30 with the P15 materials work. It fires only
           when BETTERCAD_BUILD_ROOT is undefined, which run-qualification.cmd
           always defines, so no gate in any milestone ever evaluated it.
           Repairing it across the lineage needs the owner's scope decision.
           TWO LESSONS ARE WORTH CARRYING INTO THE NEXT MILESTONE, and both cost
           this one a re-run:
           - A QUALIFICATION IS ONLY AS GOOD AS THE SHELL IT IS LAUNCHED FROM.
           The desktop app's interactive terminal cannot see the dependency
           prefix, so CMake silently found a different Open CASCADE and the
           gate refused it in five seconds. Configure all three presets as a
           pre-flight before committing hours to a run.
           - A STAGE THAT RUNS WITHOUT A BUILD BEHIND IT CAN REPORT GREEN. Two
           repeat stages passed 100% against binaries left on disk from an
           earlier tree. Empty the build trees before a gate, so an absent
           build is an absent stage rather than a quiet pass.
P19 objective, as planned:
           authoritative CAD / fluid / mesh state
           → CFD analysis definition
           → finite-volume topology and geometric metrics
           → boundary and initial conditions
           → momentum / continuity assembly
           → pressure–velocity coupling
           → steady and transient incompressible solves
           → pressure / velocity fields
           → forces / fluxes / flow balance
           → CFD validation
           → inspectable, persistent, headless result

P19 v1 does NOT implement, and refuses rather than approximates (ADR-058):
           turbulence (k-epsilon, k-omega SST, LES / DES / DNS), thermal
           coupling, buoyancy and conjugate heat transfer, compressible flow,
           multiphase, free surface, cavitation, species, combustion,
           radiation, non-Newtonian flow, temperature-dependent properties,
           porous media, moving mesh, rotating machinery

P16, P17 and P18 milestones:
           recorded in ROADMAP.md with their evidence links. A completed
           phase does not accumulate in this file.
```

---

# Carried

```text
1. FileIo atomic replace

   A document save can still lose to a file synchroniser when Windows
   temporarily refuses the final rename.

   Keep carried.
   Do not silently fold this into P17.


2. Configuration regeneration

   A configuration parameter override can change the effective parameter
   without rebuilding the geometry that reads it.

   This matters directly to P17:
   a structural result must NEVER be computed from stale configuration
   geometry, and a mesh must never be generated from it either.

   Until fixed:
   configuration-sensitive meshing and analysis must explicitly refuse
   stale geometry, following the same safety principle used by P15 mass
   properties. P16 does; P17 must do the same.


3. Hole POSITION dimensions unsupported.

4. GD&T symbols not fully embedded in PDF/DXF.

5. Cross-preset drawing export byte identity not guaranteed.
```

---

# Completed Phases

```text
P0–P10   BetterCAD v0.1.0
P11      Advanced Part Modelling              QUALIFIED
P12      Production Part Modelling            QUALIFIED
P13      Assemblies                           QUALIFIED
P14      Technical Drawings                   QUALIFIED
P15      Materials / Engineering Data         QUALIFIED
P16      Meshing                              QUALIFIED
P17      Structural FEA                       QUALIFIED
```

Detailed completed history belongs in:

```text
ROADMAP.md
docs/verification/
docs/architecture/decisions/
```

Do not expand completed-phase implementation diaries in `TODO.md`.

---

# P17 — Structural FEA — QUALIFIED

`P17-QUAL-001` PASS, 2026-10-15. The seventeen milestones -- `P17-ARCH-001`
through `P17-REFMOD-001` and this gate -- are recorded in `ROADMAP.md`
("Structural FEA — Qualified") with their evidence links, as this file's own
rule requires. The phase diary that stood here, every milestone's checklist
and PASS record, is in git history at `a99f0b6` and in each milestone's
`docs/verification/<milestone>/` directory; it is not repeated.

```text
P17 objective, delivered:
    authoritative CAD / material / mesh state
    → structural analysis definition
    → linear-elastic FE assembly
    → boundary conditions / loads
    → linear solve
    → displacements / reactions
    → strain / stress recovery
    → structural validation
    → inspectable, persistent, headless result

P17 did NOT implement, by decision (ADR-034), and refuses rather than
approximates: nonlinear material behaviour, plasticity, large deformation,
contact, buckling, fatigue, fracture, composites, dynamics or modal
analysis, transient structural analysis, topology optimisation, adaptive
remeshing, shell or beam elements, multiple-solid contact assemblies.
```

---
# P17-QUAL-001 — PASS

## Full P17 Qualification

* [x] Freeze the final P17 tree
* [x] Audit every P17 milestone
* [x] Verify all TODO items complete
* [x] Verify all P17 ADRs
* [x] Audit CAD / material / mesh / solver authority
* [x] Audit solver identity and currentness
* [x] Audit the Tet4 formulation
* [x] Audit the DOF model
* [x] Audit loads
* [x] Audit restraints
* [x] Audit assembly
* [x] Audit the solver
* [x] Audit displacement recovery
* [x] Audit strain / stress recovery
* [x] Audit reactions
* [x] Audit the validation policy
* [x] Audit stale-input protection
* [x] Audit visualisation
* [x] Audit commands
* [x] Audit persistence
* [x] Audit the CLI
* [x] Re-run all reference models
* [x] Independent analytical validation
* [x] Force equilibrium PASS
* [x] Moment equilibrium PASS where relevant
* [x] Patch test PASS
* [x] Axial test PASS
* [x] Cantilever convergence PASS
* [x] Under-constrained failure PASS
* [x] Stale-mesh failure PASS
* [x] Clean Debug qualification
* [x] Clean Release qualification
* [x] Clean Debug-shared qualification
* [x] Repeated determinism qualification
* [x] Cross-preset equivalence
* [x] Final adversarial review
* [x] 0 unexpected warnings
* [x] Qualified tree == committed tree
* [x] Evidence recorded
* [x] Mark P17 qualified

```text
PASS 2026-10-15. Qualified against a99f0b6 plus the working tree, tests
fingerprint 90f93c52, the other seven paths byte-identical to the
P17-REFMOD-001 freeze: 4009/4009 x 3 presets, 1138 x 5 in two, 0 warnings
over 658 objects, 17 stages exit 0, 3h49m57s, fingerprints before == after.
17 of 17 milestones PASS from their own harness verdicts; 206 ADR clauses,
195 implemented, 11 text drifts amended, 0 invariant violations; three
final cross-milestone gates (tests/qualification/P17StructuralQualificationTests.cpp);
7 mutation probes on the production tree, 7 killed; 44 adversarial
questions, 0 production defects, 0 gate-blocking. Evidence:
docs/verification/P17-QUAL-001/.

The "Cantilever convergence PASS" box is ticked as the brief defines it --
a convergence study with strictly increasing meshes, a monotone trend and a
falling error -- and NOT as agreement with beam theory, which linear Tet4
does not reach on these meshes (F2, F3). "Moment equilibrium PASS where
relevant" is ticked for every solved reference model under the production
default, with F1 recorded beside it.
```

### P17 Final Gate

```text
all P17 milestones PASS

+ CAD geometry remains authority
+ P15 material remains canonical
+ P16 mesh remains derived
+ P17 results remain derived

+ stale geometry rejection PASS
+ stale mesh rejection PASS

+ DOF model PASS
+ Tet4 element formulation PASS
+ stiffness symmetry PASS
+ rigid-body-mode behaviour PASS

+ loads PASS
+ restraints PASS
+ geometry-reference mapping PASS

+ global assembly PASS
+ linear solver PASS
+ residual validation PASS

+ displacement PASS
+ strain PASS
+ stress PASS
+ von Mises PASS

+ reaction-force recovery PASS
+ static equilibrium PASS

+ mesh-quality acceptance policy explicit

+ visual inspection PASS

+ undo/redo PASS
+ generated solver arrays excluded from history

+ persistence PASS
+ derived solver arrays excluded as authority

+ CLI PASS
+ CLI/core equivalence PASS

+ reference models PASS
+ independent analytical validation PASS
+ patch test PASS

+ Debug PASS
+ Release PASS
+ Debug-shared PASS

+ determinism PASS
+ cross-preset equivalence PASS
+ adversarial review PASS
+ 0 unexpected warnings

+ qualified tree == committed tree
```

The thirty adversarial questions the gate named, and fourteen more, are
answered one by one with the attack, the test and the result in
`docs/verification/P17-QUAL-001/ADVERSARIAL_REVIEW.md`.

---

# P18 — Thermal Analysis

Authorized 2026-10-16 by an explicit scope decision ("start P18"), with the
phase plan below supplied by the owner. Milestones are worked top to bottom
and each one is authorized by its predecessor's PASS being recorded here and
the owner saying so -- P17's rule, unchanged.

Objective: qualified 3D solid thermal finite-element analysis using the
existing BetterCAD authority chain:

```text
CAD geometry
→ canonical material
→ derived P16 volume mesh
→ canonical P18 thermal-analysis intent
→ thermal FE assembly
→ solve
→ temperature / gradient / heat-flux recovery
→ heat-balance validation
→ visualisation / CLI / persistence
```

Authority rule:

```text
CAD geometry        = canonical
P15 material        = canonical
P16 mesh            = derived
P18 analysis intent = canonical

thermal DOF maps, K, C, Q, temperature arrays, temperature gradients,
heat-flux arrays, heat-balance reports, render buffers   = derived
```

Initial thermal formulation:

```text
3D solid conduction          linear Tet4            isotropic conductivity
steady-state conduction      transient conduction   prescribed temperature
surface heat flux            volumetric heat generation
convection / Robin boundary condition
```

Initially excluded unless explicitly added later:

```text
radiation                phase change             thermal contact
anisotropic conductivity nonlinear material properties
fluid/CFD coupling       thermo-mechanical coupling
moving heat sources      Joule heating            chemical heat generation
adaptive remeshing
```

---
# P18-ARCH-001

## Thermal Architecture / Scope

* [x] Define initial thermal physics scope
* [x] Define steady-state scope
* [x] Define transient scope
* [x] Define supported thermal loads
* [x] Define supported thermal boundary conditions
* [x] Define deferred thermal physics explicitly
* [x] Define canonical vs derived state
* [x] Define CAD / material / mesh / thermal-result authority
* [x] Define module boundaries
* [x] Define solver orchestration
* [x] Define currentness / stale-state rules
* [x] Define unit conventions
* [x] Define deterministic ordering rules
* [x] Define diagnostics model
* [x] ADRs recorded
* [x] Architecture tests PASS
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-16. Qualified against a3b721b plus the working tree:
4024/4024 x 3 presets, 1177 x 5 in two, 0 warnings over 660 objects,
17 stages exit 0, 3h29m47s, fingerprints before == after. A thermal
module at layer 45 (ADR-050, below structural so the roadmap's coupling
can flow structural -> thermal), the scope ADR-049, and the input
boundary requireThermalModel / ThermalModel (ADR-051) with 11 boundary
cases and 4 checker fixtures; 497 files, 0 layering violations.
Adversarial review: 40 questions, 0 architecture defects, 3 findings
closed or recorded before the freeze. Evidence:
docs/verification/P18-ARCH-001/.
```

Core authority:

```text
CAD → P15 material → P16 mesh → P18 thermal intent → derived thermal
system/results
```

No P18 solver-local identity becomes permanent CAD/document authority.

---
# P18-DATA-001

## Thermal Analysis Data Model

* [x] Define `ThermalAnalysisId`
* [x] Define `ThermalLoadId`
* [x] Define `ThermalBoundaryConditionId`
* [x] Define analysis state
* [x] Define steady solver settings
* [x] Define transient solver settings
* [x] Define initial-condition representation
* [x] Define temperature-result representation
* [x] Define gradient-result representation
* [x] Define heat-flux-result representation
* [x] Define heat-balance-result representation
* [x] Define provenance/source fingerprints
* [x] Define result-currentness semantics
* [x] Define stale-result semantics
* [x] Define failure-result semantics
* [x] Units explicit
* [x] Deterministic ordering
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-17. Qualified against e6fded3 plus the working tree:
4084/4084 x 3 presets, 1213 x 5 in two, 0 warnings over 665 objects,
17 stages exit 0, 3h40m44s, fingerprints at the freeze == before ==
after. ThermalAnalysisId (a document object), ThermalLoadId and
ThermalBoundaryConditionId in core/Id.hpp; TemperatureGradient, HeatFlux,
HeatTransferCoefficient and VolumetricHeatSource in core/units; the
canonical ThermalAnalysis document object with strict per-mode
validation; the immutable ThermalResult bound to its mesh stamp with a
revision-tuple source (body, control, geometry, mesh, material + revision,
analysis + revision, mode); currentness by source identity, never arrays
or sizes -- a remesh with identical NodeIds is stale; six derived states
and ThermalResultSlot, where a failure is never current and never
resurrects a stale result. 36 data cases, 24 compile-fail cases, 25
mutation probes (23 killed, 1 equivalent, 1 recorded), 42 adversarial
questions, 0 production defects. Evidence: docs/verification/P18-DATA-001/.
```

```text
CANONICAL   thermal analysis definition, loads, boundary conditions, initial
            condition, solver settings
DERIVED     thermal DOF map, K, C, Q, T, grad(T), q, balance results,
            visualisation buffers
```

---
# P18-MAT-001

## Thermal Material Resolution

Reuse P15 canonical materials. Required properties: `k` [W/(m K)], `rho`
[kg/m3], `cp` [J/(kg K)].

* [x] Resolve thermal conductivity from P15
* [x] Resolve density
* [x] Resolve specific heat
* [x] Define which properties steady analysis requires
* [x] Define which properties transient analysis requires
* [x] Reject missing conductivity
* [x] Reject missing density for transient analysis
* [x] Reject missing specific heat for transient analysis
* [x] Reject non-finite values
* [x] Require `k > 0`
* [x] Require `rho > 0` where required
* [x] Require `cp > 0` where required
* [x] Preserve material provenance
* [x] Material edit stales thermal result
* [x] Material edit does NOT stale P16 mesh
* [x] No P18 material database
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-17. Qualified against 843469a plus the working tree:
4115/4115 x 3 presets, 1244 x 5 in two, 0 warnings over 667 objects,
17 stages exit 0, 3h51m07s, fingerprints at the freeze == before ==
after. NO P15 SCHEMA CHANGE: k, rho and cp are P15's, validated at entry
by P15, and resolved through P15's own require* functions.
resolveThermalMaterial(document, body, mode) returns a derived,
source-bound ThermalMaterial -- MaterialId, revision, mode, k always, a
rho/cp ThermalCapacity exactly when the mode is transient -- with the
requirement READ from P15's table: steady needs k only, transient needs
k, rho and cp. The result source now resolves the material for the
analysis's mode, so a transient analysis on a k-only material is
InputsUnavailable where a steady one is Ready. A k, rho, cp or E edit
leaves the P16 mesh current (same stamp, same NodeIds) and stales the
result for reason material alone, under whole-material revision as P17.
No default, clamp, abs, epsilon, cache or database. 31 cases, 14
mutation probes (13 killed, 1 refused by the compiler, 0 survived), 38
adversarial questions, 0 production defects. Evidence:
docs/verification/P18-MAT-001/.
```

---
# P18-DOF-001

## Temperature DOF Model

Initial thermal model: 1 scalar temperature DOF per node.

* [x] Define thermal `DofIndex`
* [x] Define deterministic node → thermal DOF mapping
* [x] Define constrained/free thermal equations
* [x] Define temperature constraint map
* [x] Bind DOF map to exact P16 mesh source
* [x] Remesh invalidates old thermal DOF map
* [x] NodeId is not used as raw equation number
* [x] No thermal DOF IDs persisted as canonical intent
* [x] Sparse NodeId tests
* [x] Determinism PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-17. Qualified against d40001c plus the working tree:
4163/4163 x 3 presets, 1277 x 5 in two, 0 warnings over 670 objects,
17 stages exit 0, 3h44m12s, fingerprints at the freeze == before ==
after. ThermalDofIndex is the node's ordinal in the P16 mesh's own
enumeration -- zero-based, no default, no invalid value -- and indexes
P18-DATA-001's nodal temperature array directly; it is a distinct type
from NodeId, ElementId, ThermalFreeEquationIndex and P17's DofIndex (15
compile-fail cases). Sparse handles 7 / 104 / 9001 / 500000 number to
0 / 1 / 2 / 3 with four handles of storage; a handle near 2^32 gives a
two-entry map. ThermalDofMap is bound to the mesh's stamp AND node count:
a remesh with identical counts and NodeIds is refused by stamp, two
snapshots of one builder by count; material, load, solver and BC edits
leave it current and equal to a rebuild. The constraint map stores the
prescribed temperature in kelvin -- 300 K and 350 K survive exactly, a
free unknown has no prescribed value rather than 0 K -- collapses exact
duplicates, refuses conflicts naming both, and refuses NaN, infinity and
sub-zero by the absolute-temperature rule; nothing and everything
prescribed are representable. The free map numbers rows compactly in
ascending unknown order, a constrained unknown has no row, agreesWith
re-derives the complement, and reconstruction rebuilds [310 300 320 330
350]. Element-local unknowns keep the element's node order. No thermal
DOF index is persisted or constructible from the definition. 33 cases
(21 480 assertions), 17 mutation probes (17 killed, 0 survived), 37
adversarial attacks, 2 test-method findings, 0 production defects.
Evidence: docs/verification/P18-DOF-001/.
```

---
# P18-ELEM-001

## Linear Tet4 Thermal Element

```text
steady conduction      Ke = integral (grad N)^T k (grad N) dV
                          = V G^T k G   for constant isotropic k on a Tet4
transient capacity     Ce = integral rho cp N^T N dV
units                  Ke -> W/K      Ce -> J/K
```

* [x] Define local node order
* [x] Define Jacobian convention
* [x] Define signed volume convention
* [x] Reject inverted Tet
* [x] Reject degenerate Tet
* [x] Define shape-function gradients
* [x] Implement conduction matrix
* [x] Implement transient capacity matrix
* [x] Define consistent vs lumped capacity policy
* [x] Check symmetry
* [x] Check positive-semidefinite conduction behaviour
* [x] Check capacity matrix positivity
* [x] Translation invariance
* [x] Rotation invariance for isotropic `k`
* [x] Geometry scaling validation
* [x] Conductivity scaling validation
* [x] Independent analytical oracle
* [x] Determinism PASS
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-18. Qualified against d4800f7 plus the working tree:
4240/4240 x 3 presets, 1337 x 5 in two, 0 warnings over 673 objects,
17 stages exit 0, 4h10m35s, fingerprints at the freeze == before ==
after; second attempt on the one frozen candidate, the first (one
unrelated Windows file-replace flake in a drawing CLI test, 30/30 in
isolation) preserved under qualification/run1-failed/.
Local order [n1,n2,n3,n4] as P16 stores it, never sorted. J has the edge
vectors from corner 1 as columns; det J = e1.(e2 x e3) grouped as
meshing::signedVolume, and V = det J / 6 agrees with it bit for bit; no
absolute value repairs a sign. G is 3x4 (row = axis, column = corner),
grad T = G Te, mapped by J^-T. Ke = V k G^T G [W/K]: symmetric
(max |Kij - Kji| = 0), row sums 2.7e-16, constant null mode, positive
semidefinite with rank 3 / nullity 1, energy identity Te^T Ke Te = k V |G Te|^2
against an independent right-hand side. Capacity policy = CONSISTENT: Ce =
rho cp V/20 [2 1 1 1; 1 2 1 1; 1 1 2 1; 1 1 1 2] [J/K], symmetric positive
definite, rank 4, spectrum rho cp V/4 (once) and rho cp V/20 (three
times), total heat capacity rho cp V, and NOT the one-point centroid
matrix (rank 1, same row sums). Translation, proper rotation, even
permutation covariance and geometry scaling (V s^3, G 1/s, Ke s, Ce s^3)
hold; the six odd permutations and the reflected tetrahedron are refused.
The independent oracle is the [1 x y z] inverse plus hand matrices; it
shares no code with production. Steady Ke needs only k; Ce needs only rho
and cp. DEGENERACY: P16 and P17's exact-zero rule accepted 885 of 2000
exactly-degenerate tetrahedra (rounding noise in the determinant of
non-integer coordinates); the kernel uses |det J| <= 16 eps |e1||e2||e3|,
derived from the rounding bound (analytic 4 eps, measured 1.01 eps). The
same gap in P17's Tet4Element.cpp is recorded, not changed. Results are
bitwise identical across the three presets. 60 cases, 17 compile-fail
cases, 33 mutation probes (33 killed), 44 adversarial attacks, 6 findings.
Evidence: docs/verification/P18-ELEM-001/.
```

---
# P18-LOAD-001

## Thermal Loads

Initial supported loads: surface heat flux `q''` [W/m2] with one frozen
positive-direction convention; volumetric heat generation `q'''` [W/m3], with
`Q_element = q''' V` for constant generation. Convection's source term is
handled with the BC milestone.

* [x] Define heat-flux load
* [x] Define volumetric heat-generation load
* [x] Define `GeometryReference` authority
* [x] Resolve current P16 boundary mapping
* [x] Convert face heat flux to equivalent nodal heat input
* [x] Convert volumetric generation to equivalent nodal heat input
* [x] Preserve total heat rate
* [x] Define sign conventions
* [x] Explicit SI units
* [x] Multiple loads superpose deterministically
* [x] Remesh re-resolves canonical geometry references
* [x] No facet/NodeId persistence
* [x] Unsupported target fails explicitly
* [x] Stale prepared loads rejected
* [x] Analytical resultant validation
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-18. Qualified against f10f5f0 plus the working tree:
4325/4325 x 3 presets, 1403 x 5 in two, 0 warnings over 677 objects, 17
stages exit 0, 4h21m28s, fingerprints at the freeze == before == after; one
run, every stage exit 0. The canonical load was already frozen by P18-DATA-001
(SurfaceHeatFluxLoad {FaceName, HeatFlux}, VolumetricHeatSourceLoad
{VolumetricHeatSource}); positive is heat INTO the body, a negative value
extracts, and nothing takes an absolute value. A load holds no facet, node,
element or unknown: sizeof mirrors, static_asserts and five compile-fail cases.
The GeometryReference is the FaceName; P16's boundaryFacetsOf is the only
resolver, and an unresolved face is a refusal, never the nearest face (a hole
bottom, an unknown object and a malformed selector each refused, with both end
caps present). Surface: Q_i = q'' A / 3 per facet corner with A the unsigned P16
area in m2; volume: Q_i = q''' V / 4 per tetrahedron corner with V the signed
volume of a validated element in m3. A 3 m2 triangle at 12 W/m2 gives 12 W per
corner, 36 W; a unit tetrahedron at 24 W/m3 gives 1 W per corner, 4 W; the
block's end cap carries 0.6 W at 500 W/m2 and its volume 60 W at 2.5e6 W/m3,
the sums equal to q'' sum(A) and q''' sum(V) to a few ulps. Geometry scaling
gives s^2 and s^3, so no millimetre factor exists. Loads superpose by ordinal in
the order given; signed cancellation is exact for surfaces and 0.83 eps for a
volume. The vector is indexed by ThermalDofMap, never by NodeId (ids 7, 104,
9001, 500000 and 4 294 967 294 tested). The prepared loads carry the body, the
mesh stamp and node count, the geometry revision and the loads; a remesh with the
same counts and the same ids is stale, a material, solver or boundary-condition
edit is not. Coarse and fine cylinders genuinely differ (156 against 900 nodes,
140 against 572 boundary facets). One bad load refuses the whole set. Results are
bitwise identical across the three presets. 66 cases, 19 compile-fail cases, 46
mutation probes (45 killed, 1 dead code removed), 44 adversarial attacks, 9
findings. Limits recorded: a hole's cylindrical wall cannot be named (P16 has no
role for it); two failure values are reachable only through the integration
seams. No convection, no prescribed temperature, no assembly, no solve.
Evidence: docs/verification/P18-LOAD-001/.
```

---
# P18-BC-001

## Thermal Boundary Conditions

Initial supported BCs: prescribed temperature; convection / Robin,
`-k grad(T) . n = h (T - T_inf)`, assembled as the qualified equivalent
boundary matrix and source terms.

* [x] Define prescribed-temperature BC
* [x] Define convection BC
* [x] Define ambient temperature
* [x] Define convection coefficient
* [x] Define sign convention
* [x] Map BCs through canonical `GeometryReference`
* [x] Prescribed temperature resolves to current thermal DOFs
* [x] Duplicate constraints handled deterministically
* [x] Conflicting prescribed temperatures rejected
* [x] Convection contributes correct matrix/source terms
* [x] Unresolved reference fails explicitly
* [x] No nearest-face rebinding
* [x] Remesh invalidates derived mappings
* [x] Analytical validation
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-19. Qualified against b4714a4 plus the working tree:
4448/4448 x 3 presets, 1495 x 5 in two, 0 warnings over 682 objects, 17
stages exit 0, 4h48m16s, fingerprints at the freeze == before == after;
run 2 of 2 on the corrected tree. Run 1 passed debug-ext and release-ext
and failed the debug-shared-ext BUILD: PreparedConvectionBoundary, a plain
struct with member functions defined in the library, lacked
BETTERCAD_THERMAL_EXPORT, so a DLL build could not find area(); static
builds ignore the macro, which is why nothing else saw it. Fixed, logs
preserved under qualification/run1-failed/, whole qualification rerun.
The canonical conditions already existed (P18-DATA-001) and are unchanged:
PrescribedTemperature {FaceName, Temperature K} and ConvectionBoundary
{FaceName, h W/(m2 K), T_inf K}; no facet, node, element or unknown is
canonical (sizeof mirrors, seven compile-fail cases). The GeometryReference
is the FaceName; P16's boundaryFacetsOf, through ThermalTarget, is the only
resolver, and an unresolved face is refused for both kinds, never bound to
the nearest. Prescribed temperature: GeometryReference -> facets -> unique
NodeIds -> ThermalDofIndex -> ThermalConstraintMap, ONE constraint per
unknown (the end cap's six corners are four constraints), nonzero values
preserved (350 K stays 350 K, a free node has no temperature), equal
prescriptions deduplicated with both conditions recorded, a contradiction
refused by exact number equality with a per-pair report (end cap 350 K and a
side wall 300 K share two unknowns); no penalty, no epsilon, no-Dirichlet
sets valid. Convection: -k grad(T).n = h (T - T_inf), n outward; weak form
(K + Kconv) T = Q + Qconv derived in WEAK_FORM.md; per linear triangle
Kconv = (hA/12) [2 1 1; 1 2 1; 1 1 2] W/K and Qconv = (h T_inf A/3) [1 1 1]
W, exact, with no conductivity. Hand triangle A = 3 m2, h = 4, T_inf = 300
K: Kconv = [2 1 1; 1 2 1; 1 1 2], Qconv = [1200 1200 1200], error 0. The
centroid matrix hA/9 and the lumped matrix hA/3 I have the correct row sums
and are rejected by the whole-matrix oracle. T = T_inf gives a zero
residual; 350 K gives +600 W outward, 250 K gives -600 W. Convections add
(identical ones double), a prescribed temperature and a convection on one
face are both kept. Scaling s^2, h and T_inf hold. A canonical h = 0 stays
refused (P18-DATA-001's rule); the kernel and seam give zeros for h = 0.
Prepared boundary bound to body, MeshStamp, node count, geometry revision
and the conditions by value: stale after a remesh with the same counts and
the same ids, not stale after a material, load, solver or transient edit,
and the mesh is not regenerated. 92 cases, 31 compile-fail cases, 58
mutation probes (all killed; a share-order survivor closed by two
assertions), 40 + 9 adversarial questions, 7 findings, 1 production defect.
Results are bitwise identical across the three presets. Limits recorded: a
hole's cylindrical wall cannot be named; TargetWithoutFacets is reachable
only through the seams; nothing yet forces the currentness call. No
assembly, no solve, no penalty, no radiation, no time-varying BC, no
contact. Evidence: docs/verification/P18-BC-001/.
```

---
# P18-ASSEMBLY-001

## Global Thermal Assembly

```text
steady      K T = Q
transient   C dT/dt + K T = Q
```

* [x] Assemble global thermal conductivity matrix
* [x] Assemble global thermal capacity matrix
* [x] Assemble global heat-load vector
* [x] Deterministic sparse assembly
* [x] Duplicate sparse contributions SUM
* [x] Validate dimensions
* [x] Validate finite entries
* [x] Validate symmetry
* [x] Single-Tet analytical assembly
* [x] Two-Tet shared-node assembly
* [x] Independent dense oracle for small systems
* [x] Heat-load resultant preservation
* [x] Source compatibility checks
* [x] No silent regularisation
* [x] No production dense global matrix
* [x] Large-mesh sparse smoke
* [x] Determinism PASS
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-20. Qualified against a5b8cc2 plus the working tree:
4546/4546 x 3 presets, 1557 x 5 in two, 0 warnings over 687 objects, 17
stages exit 0, 5h08m45s, fingerprints at the freeze == before == after;
first run on the frozen tree (an earlier launch died in under a second for
want of PATH, built nothing and is not counted). Global system, unconstrained:
K = sum Ke + sum Kconv (W/K), C = sum Ce (J/K, transient only), Q = prepared
heat input + sum Qconv (W), over Ndof = Nnode unknowns from ThermalDofMap;
NodeId is never an index and the dimension is never the largest handle (a
mesh with max id 4 294 967 294 and four nodes gives 4 x 4). BetterCAD CSR,
64-bit indices, strictly ascending columns, a validating constructor, typed
so K is not C; a symbolic pass then a numeric += pass in a frozen serial
order, so duplicates SUM by construction. Single unit tet: K = [3 -1 -1 -1;
-1 1 0 0; -1 0 1 0; -1 0 0 1], C = [2 1 1 1; ...], Q = 1 W per corner, error
0. Two tets: K(1,1) = 1.0 + 1.5 = 2.5, C(1,1) = 2 + 4 = 6, Q(1) = 1 + 2 = 3 W;
independent dense oracle error 0. Real block (9 unknowns, 12 tets): Q = 0.6 +
60 + 8.7945 = 69.3945 W, ||K 1|| = 2.2e-16, 1^T C 1 = 86.664 J/K = rho cp V,
T^T K T and v^T C v equal the sums of the local energies. Steady k-only
assembles with no C; transient without rho/cp is refused (material_unavailable),
never a zero C. Stale numbering, loads, boundary or material refused before the
first element, in a fixed order; a remesh with the same counts and the same
ids is stale by stamp; a prescribed temperature edit does not stale the system
because it never enters K, C or Q (bit-identical with and without). No
Dirichlet, penalty, epsilon, pin, symmetrisation or pruning; no dense global
matrix (22^3 grid: 10 648 unknowns, nnz/Ndof^2 = 1.3e-3). 62 cases, 36
compile-fail cases, 75 mutation probes (74 killed; A61, the symmetry gate on a
correct scatter, is an explained equivalent; five test gaps found and closed),
35 + 6 adversarial questions, 5 findings, 0 production defects. Results are
bitwise identical across the three presets (fingerprints 6398b41313705d28,
c6a042bf47d865e0, 07c4bb5e30c65a38). Limits recorded: the seam takes
convection facets as data (exterior-ness is P18-BC's), thermalAssemblyFailure
costs a full assembly, inconsistent_system is reachable only through a
defective scatter. No solve, no elimination, no C/dt, no residual, no
reaction, no result field. Evidence: docs/verification/P18-ASSEMBLY-001/.
```

---
# P18-SOLVE-001

## Steady Thermal Solver

Solve `K T = Q` after prescribed-temperature constraints. Validate the reduced
free system, `r = Kff Tf - Qf`; never qualify from solver-success status
alone. ADR-050 fixes the reuse constraint: thermal (45) cannot include
structural (50), so P17's solver is reused by EXTRACTION below 45 or by an
independent private link, never by a hand copy.

* [x] Audit available linear algebra
* [x] Reuse qualified compatible solver infrastructure
* [x] Define solver tolerances
* [x] Apply temperature constraints correctly
* [x] Detect singular thermal system
* [x] Detect missing temperature datum where applicable
* [x] Detect disconnected thermal region
* [x] Reject non-finite solution
* [x] Structured solver diagnostics
* [x] Deterministic solve behaviour measured
* [x] Independent residual validation
* [x] Independent small-system validation
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-20. Qualified against 241c13a plus the working tree: 4638/4638 x 3
presets, 1623 x 5 in two, 0 warnings over 693 objects, 17 stages exit 0,
5h12m08s, fingerprints at the freeze == before == after; first run on the
frozen tree. AUDIT: the generic sparse solve belonged to structural (50);
thermal (45) cannot include it (ADR-050). EXTRACTION (ADR-052): P17's
runKernel moved mechanically to a new module linalg (layer 5; core PUBLIC,
Eigen PRIVATE); structural::solveSymmetricSparse is now a wrapper that puts
P17's units on the numbers and rebuilds P17's three unit- or structure-bearing
messages from the kernel's structured failure, byte for byte; 415 structural
cases / 271 414 assertions pass unchanged in all three presets (P17-SOLVE,
reaction, validation, post and the reference models requalified by the
unfiltered runs). NO hand copy: src/thermal holds no factorisation; no
structural include (architecture.layering, 525 files) and no structural link
(architecture.targets.link-graph, a fixture holds the forbidden private link
and is refused). DIRICHLET: Kff Tf = Qf - Kfc Tc over ThermalFreeEquationMap,
free and prescribed unknowns interleaved; one-free-node oracle K=[2 -2;-2 2],
Q=[0 100], T0=300: Kff=[2], Kfc=[-2], beff=700, T1=350 K exactly (50 K if the
term is dropped); 3-node chains 350 K and 366.667 K; Robin-only K=[4],
Q=[1240] gives 310 K with no Dirichlet; fully prescribed Nfree=0 gives T=Tc
with the kernel not invoked. WELL POSEDNESS: pure Neumann (compatible or not)
-> missing_temperature_datum; a region cut off from every datum while another
is anchored -> unanchored_thermal_region; anchored disconnected regions
solve; stored exact zeros join no regions; the pivot gate independently
refuses indefinite, exactly singular and tiny-pivot systems. RESIDUAL:
||r||_2 / (||Kff|| ||Tf|| + ||beff||), independent of the solver and scale
free (K, Q scaled 1e-6..1e6 give the same T to 5.7e-14 K and a normalized
residual in [1.7e-17, 6.3e-17]); a finite answer one kelvin off is refused
while solver status is Success; NaN/Inf, an overflowing residual and a
prescribed value one ulp off are refused. FE: 6^3 grid linear profile to
1.25e-12 K, affine patch to 2.84e-13 K, the real block 400->300 K to 5.7e-14 K,
Dirichlet in series with convection to 1.7e-13 K, 60 W generation leaves
through the film, 5832-unknown grid and the 900-unknown cylinder solve
sparsely (nnz(Kff) 70 766 vs Nfree^2 26.9 million). Stale system, boundary,
constraints and numbering refused before any arithmetic, including after a
remesh with the same counts and ids; a T_bar edit stales the solution but
not the assembled system; a failed re-solve leaves the old result stale and
creates no current one. No epsilon, pin, penalty, pseudo-inverse or hidden
datum; no dense reduced matrix; no gradient, flux, reaction, balance or
time stepping. Results are bitwise identical across the three presets.
51 + 10 cases, 26 compile-fail, 5 architecture tests, 43 mutation probes
(all killed; two test gaps closed), 30 + 7 adversarial questions, 5
findings, 0 production defects. Limits recorded: the solve accepts any
finite field including one below 0 K (POST/VALID's policy), the datum
classification rests on a measured 1e-10 tolerance. Evidence:
docs/verification/P18-SOLVE-001/ and ADR-052.
```
---
# P18-TRANSIENT-001

## Transient Thermal Solver

Initial implicit formulation, backward Euler:
`(C/dt + K) T[n+1] = Q[n+1] + (C/dt) T[n]`.

* [x] Define initial temperature
* [x] Define timestep
* [x] Define end time
* [x] Define output times
* [x] Implement implicit integration
* [x] Define transient constraint treatment
* [x] Define time-varying load policy
* [x] Define time-varying BC policy
* [x] Validate `dt > 0`
* [x] Validate time ordering
* [x] Reject non-finite temperatures
* [x] Define transient residual
* [x] Define energy-balance check
* [x] Deterministic timestepping
* [x] Constant-temperature solution test
* [x] Lumped thermal benchmark
* [x] Time-step refinement study
* [x] Steady-limit comparison
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-21. Qualified against 147d355 plus the working tree: 4745/4745 x 3
presets, 1695 x 5 in two, 0 warnings over 697 objects, 17 stages exit 0,
5h03m11s, fingerprints at the freeze == before == after; first run on the
frozen tree. NOTHING QUALIFIED CHANGED: the steady solver, the generic kernel
and the structural wrapper are byte-identical to P18-SOLVE-001's tree (one
comment in ThermalData.hpp now says how the end time is reached).
FORMULATION: (C/dt + K) T[n+1] = Q + (C/dt) T[n], history from T[n], every step
through the same reduceThermalSystem (Aff Tf = bf - Afc Tc, A = C/dt + K, so
Afc = Cfc/dt + Kfc) and the same linalg::solveSymmetricSparse as the steady
solve. ORACLES: one unknown C=10 G=2 Tinf=300 T0=400 dt=1 gives 4600/12 =
383.3333; the non-uniform pair C=[2 1;1 2] K=[1 -1;-1 1] T0=[300 330] dt=1 gives
[310 320] (a lumped C gives [306 324]); the prescribed value dominates T0 at
t=0 (1000/3, not 300). SINGULARITY: a pure-Neumann model and disconnected
positive-capacity regions SUCCEED where the steady solve refuses them; only a
region with no capacity at all is refused (singular_system at its step); the
steady datum rule is not applied (analyseAnchoring is not called; the faithful
leak, mutation TA21, is killed by 10 cases). TIME GRID: t_k = k dt (a
product), t_N = t_end exactly, the last step shortened and using its own dt,
0.3/0.1 is three steps, dt=0.3 t_end=1 is 0.3 0.3 0.3 0.1, one tolerance
(1e-9), at most 1 000 000 steps, integratedSpan() says what was integrated.
ACCEPTANCE: two independent residuals from the original K, C, Q (the solve of
the system that was built; the heat equation on the free rows); a step built
wrongly (C*dt, a flipped history sign) and solved perfectly is refused;
explicit Euler, Crank-Nicolson, a stagnant and a steady candidate are refused;
the boundary heat at prescribed rows is not a failure. CHECKS: first-order
refinement (orders 0.951 0.976 0.988), the lumped body and a lumped steel
block (tau 666.646 s, mean within 0.057 K of the discrete exponential, energy
identity 2.9e-13), stored energy +dt sum(Q) per step on insulated bodies, the
steady limit to 5.7e-14 K of the qualified steady solver, a constant
temperature in equilibrium stays constant, 216 unknowns against a dense
oracle, a 900-unknown cylinder. PROVENANCE: the history is bound to the
system, the prescribed conditions, the initial temperature, the time settings
and the solver settings; each edit stales exactly its own reason (a specific
heat edit stales it although K is unchanged); a remesh with the same counts
and ids is caught; a failed run publishes nothing and leaves an older result
stale. Typed K and C; no gradient, flux, reaction, balance, interpolation,
restart, adaptivity, retry, regularisation, lumping or time-dependent
coefficient anywhere. Bitwise identical across the three presets. 72 + 35
cases, 85 mutation probes (all killed; three test gaps closed), 30
adversarial questions, 6 findings, 0 production defects. Limits recorded: a
step whose temperature change is below the rounding of the temperature (dt/tau
~ 1e-12) is refused by the physical residual and says so; every step
refactorises; the solve accepts any finite field including one below 0 K.
Evidence: docs/verification/P18-TRANSIENT-001/.
```
---
# P18-POST-001

## Temperature / Gradient / Heat Flux Recovery

For each Tet4: `grad(T) = G T_e`; for isotropic conductivity `q = -k grad(T)`.
Units: `T` K, `grad(T)` K/m, `q` W/m2.

* [x] Recover nodal temperature
* [x] Recover temperature magnitude/value
* [x] Recover element temperature gradient
* [x] Recover heat-flux vector
* [x] Recover heat-flux magnitude
* [x] Define component order
* [x] Define sign convention
* [x] Units explicit
* [x] All results finite
* [x] Constant-gradient patch validation
* [x] Independent Fourier-law validation
* [x] Deterministic result ordering
* [x] No GUI recomputation
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-21. Qualified against 7a9ec06 plus the working tree: 4820/4820 x 3
presets, 1739 x 5 in two, 0 warnings over 701 objects, 17 stages exit 0,
5h38m59s, fingerprints at the freeze == before == after; first run on the
frozen tree. NOTHING QUALIFIED CHANGED: thirteen files of the solvers, the
element, the data and the DOF model are byte-identical to HEAD.
FORMULATION: grad(T) = G Te per Tet4 (Tet4Geometry::gradientFrom, P18-ELEM-001's
path; Te gathered through elementDofs in the tetrahedron's own node order),
q = -k grad(T) written 0 - k g, |q| = hypot(qx, qy, qz). Units K, K/m, W/m2,
W/m2; components [x, y, z]; sign: q . grad(T) = -k |grad(T)|^2 <= 0, heat flows
toward lower temperature. q is the conductive flux inside the solid: it holds
no convection (film exchange 2671.25 W/m2 against a conductive 3.27e-10 W/m2),
no generation (a 60 W body heats 69.2 K with max |q| 9.58e-10 W/m2) and no
storage (uniform heating: dT/dt != 0, max |q| 1.79e-09 W/m2). ORACLES: the
affine patch T = 307.3 + 7.2x - 3.5y + 11.4z, k = 5 gives (7.2, -3.5, 11.4) and
(-36, 17.5, -57) on 162 different tetrahedra to 2.73e-13 K/m and 1.36e-12
W/m2; the three axis fields; the test's own dense 4x4 solve on a scattered field
to 2.45e-13 K/m; a constant 325.7 K field has zero gradient (3.22e-13);
(3, 4, 12) has |q| = 13 (L1 would be 19). SLABS: 400 K -> 300 K, k = 45 on a
jittered 1 m grid (qz = +4500, reversed -4500 W/m2, 4.64e-11), the 20 mm block
(+225 000 W/m2, 3.27e-10), the cylinder on two meshes of 156 and 900 nodes
(1.68e-06), and the transient block at t_end. TRANSFORMS: translation, a proper
rotation (gradient and flux rotate, 2.90e-13 / 1.45e-12), geometry scale,
conductivity scale (gradient bit-identical, flux x4), temperature offset and
difference. KELVIN: 350 K and 280 K come back as 350 K and 280 K, bit for bit,
checked on their own because an offset cancels in every gradient. KEYING:
sparse handles 7, 42, 104, 9001 and ElementIds 2 and 3 give arrays of the node
and tetrahedron counts; the lookup is a binary search, never the raw id.
ATOMIC: one invalid tetrahedron, three overflows (gradient, flux, magnitude),
a non-finite or below-0 K temperature, in any state, fail the whole recovery;
a bad late transient state is named and nothing earlier is published.
CURRENTNESS: the solution's own comparison; a remesh with the same counts and
ids, a load, condition, settings, conductivity or time-step edit, a model older
than a material edit and a steady/transient confusion are refused; a failed
recovery leaves an older result stale. OWNERSHIP: CheckRecoveryOwnership.cmake
(497 files scanned, 0 violations; fixtures that fail it). Results are bitwise
identical across the three presets. 40 + 31 + 4 cases, 43 mutation probes (42
killed, 1 void; three test gaps closed), 34 adversarial questions, 6 findings,
0 production defects. Limits recorded: element-constant values are
discontinuous across faces (smoothing is P18-VIZ-001's display only); a field
below 0 K cannot be recovered; each state copies the nodal temperatures.
Evidence: docs/verification/P18-POST-001/.
```
---
# P18-BALANCE-001

## Heat / Energy Balance

```text
steady      heat in + generated heat - heat out ≈ 0   (one frozen signed form)
transient   heat input + generation - heat output ≈ rate of stored thermal
            energy
```

* [x] Define heat-flow sign convention
* [x] Recover prescribed-temperature reaction heat flow
* [x] Sum applied heat flux
* [x] Sum volumetric heat generation
* [x] Sum convection heat transfer
* [x] Validate steady heat balance
* [x] Validate transient stored-energy balance
* [x] Define tolerances from measurement
* [x] Multiple thermal boundaries
* [x] Deterministic aggregation
* [x] Analytical reference cases
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-22. Qualified against fa15d36 plus the working tree: 4927/4927 x 3
presets, 1813 x 5 in two, 0 warnings over 705 objects, 17 stages exit 0,
5h24m15s, fingerprints at the freeze == before == after; first run on the
frozen tree. NOTHING QUALIFIED CHANGED: twenty-five files of the solvers, the
recovery, the element, the data, the DOF model, the loads, the boundaries and
the assembly are byte-identical to HEAD.
FORMULATION: heat is positive INTO the body. The reaction at a prescribed
unknown is R = +r from the full residual (steady r = K T - Q; transient
r = C (T1 - T0)/dt + K T1 - Q), one per unique unknown, never the remainder
that closes the books. Convection per condition is -(Kconv T - Qconv), not the
ambient source (a 1200 W source for a face that exchanges 0 W at equilibrium).
Surface heat and generation come from P18-LOAD-001's contributions, the global
Q is never added on top, and sum Q = sum loads + sum Qconv is checked before
any residual is formed (an omitted or doubled category is refused). Stored
energy is 1^T C dT over every row with the consistent C and the ACTUAL step
(1 J over a 0.1 s last step is 10 W, not 3.33 W). A FinalOnly history of
several steps is refused; nothing is re-integrated. ORACLES: a g = 2 network at
400/300 K gives +200 W and -200 W, each asserted alone; the block's Dirichlet
reaction 270 W once for duplicate temperature conditions (not 540); the
first-step reaction on the block 334.3 W (it feeds the warm-up); the capacity in
the transient reaction (-30 W against the steady -20 W; lumped -22.5 W); 18 J
with the full C against 12 J with diag(C). TOLERANCE: |B| <= 1e-12 W +
1e-13 (gross + arithmetic), gross being the sum of individual magnitudes so a
+1e6 / -1e6 W cancellation cannot shrink it. MEASURED, twelve cases in three
presets: worst |B|/(gross + arithmetic) 1.7e-16, the policy 583x above it,
every allowed residual at least 584x the measured one, identical bits in the
three presets; an earlier 1e-10 was found 10^6 too loose and tightened. A
balance PASS is not a solve PASS: K = I, Q = 0, T = [+5, -5] balances with a
5 W local error; the accepted-solution type is the gate. CURRENTNESS: the
solution's own comparison; a remesh with the same counts and ids, a load,
condition, settings, conductivity or time-step edit and a steady/transient
confusion are refused; a failed recovery leaves an older result stale. DERIVED
only: not canonical, not persisted, no balance setting in its source; no GUI
recomputation (CheckBalanceOwnership.cmake: 497 files scanned, 0 violations;
fixtures that fail it). 69 + 33 + 5 cases, 48 mutation probes (45 killed, 1
void, 2 removed as redundant, 6 first-pass survivors closed), 36 adversarial
questions, 12 findings (three defects of the new code, all fixed before the
freeze), 0 qualified-module defects. Limits recorded: the books closing does
not prove the physics (P18-VALID-001's); prescribed temperatures are constant
in time in this version; no reaction per boundary condition; a FinalOnly
multi-step history cannot be balanced.
Evidence: docs/verification/P18-BALANCE-001/.
```
---
# P18-VALID-001

## Thermal Validation / Acceptance

This milestone owns P18 thermal-analysis acceptance. Every solve validates:
geometry current, mesh current, mesh structurally valid, thermal mesh quality
acceptable, material current and thermally complete, loads resolved, boundary
conditions resolved, thermal system well posed.

* [x] Define thermal mesh-acceptance policy
* [x] Reuse P16 quality metrics
* [x] Define invalid mesh rejection
* [x] Define warning policy
* [x] Define thermal material validation
* [x] Define thermal-load validation
* [x] Define thermal-BC validation
* [x] Define stale-input checks
* [x] Define singular thermal-system detection
* [x] Define steady solver residual acceptance
* [x] Define transient residual acceptance
* [x] Define heat-balance acceptance
* [x] Define result-finiteness checks
* [x] Structured validation report
* [x] Determinism PASS
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-23. Qualified against bc62b0c plus the working tree: 5106/5106 x 3
presets, 1959 x 5 in two, 0 warnings over 711 objects, 17 stages exit 0,
5h12m47s, fingerprints at the freeze == before == after; first run on the
frozen tree. NOTHING QUALIFIED CHANGED: 63 files (every other thermal source and
header, all of P16 meshing, the linalg and structural solve files) are
byte-identical to HEAD.
DECISION: validateThermalAnalysis(document, regenerator, mesher, analysis,
derived state) is the only place a thermal analysis is accepted. 15 ordered
rules with stable ids; a report whose status, counts and acceptance are derived
every time; severity is a function of the code (no field to change it); an
input failure skips the system, solve, result and balance rules and a skipped
rule is never a pass. accepted() (needs the recovered fields and the balance),
solveAccepted() and inputsAccepted() are three different answers.
MESH: P16 measures, P18-VALID decides. A temperature of 307 K carries the
rounding of a 307 K number, so a flattened or stretched element amplifies it:
relative gradient error <= C / sqrt(3r/R) with C <= 8.62e-13 and <= C * aspect
with C <= 4.28e-13 at 14 K/m, three orders above the isolating field the
structural kernel measured. Inverted at 1e-9 with a factor of three: failure
3r/R < 7.0e-6 or aspect > 700; warning 3.0e-4, 81, 6.0e-3 rad, 3.125 rad (the
qualified envelope, rounded away); the dihedral angles carry no failure bound;
a value on a bound is on the good side; a NaN is never on it. Qualified worst
case 3.226e-4 (46x the floor) and aspect 80.0 (8.7x inside the ceiling).
RULES: steady needs k only, a transient k, rho and cp; no load and no condition
are information; pure-Neumann steady FAILS (no datum), pure-Neumann transient
PASSES (the capacity), steady Robin-only and fully prescribed PASS; a remesh with
the same counts and the same ids is stale, found by identity. The residual is
re-evaluated independently for every step against the canonical solver
tolerance and the balance through P18-BALANCE-001's policy, consumed and never
copied; a balance that nearly cancels (|B| 6.2e-14 W) does not rescue a free
residual of 1.5e-4, and the reverse. A FinalOnly history of several steps can be
solve-accepted and cannot be fully accepted. No GUI decision: the gate scans
497 files and has 0 violations, with seven fixtures that fail it. 138 + 33 + 8
cases, 19 zero-match filters, 78 mutation probes all killed (3 first-pass
survivors were test gaps, closed), 35 adversarial questions, 4 findings (one
model gap, now BalanceAttributionMismatch, and three test gaps), 0 qualified-
module defects. Limits recorded: single-solid meshes only; prescribed
temperatures are constant in time; the mesh bounds guarantee gradient recovery
at a stated condition, not discretisation or conditioning; the dihedrals have no
failure bound; the books closing does not prove the physics.
Evidence: docs/verification/P18-VALID-001/.
```
---
# P18-VIZ-001

## Thermal Visualisation / Inspection

Critical rule: thermal visualisation = a view of derived P18 data, NOT a new
thermal-analysis authority.

* [x] Undeformed thermal mesh display
* [x] Temperature contour
* [x] Temperature-gradient magnitude contour
* [x] Heat-flux magnitude contour
* [x] Heat-flux vector display
* [x] Element inspection
* [x] Node temperature inspection
* [x] Heat-flux boundary arrows
* [x] Prescribed-temperature symbols
* [x] Convection BC symbols
* [x] Boundary-set highlighting
* [x] Highest / lowest temperature navigation
* [x] Highest heat-flux navigation
* [x] Transient playback
* [x] Clear stale-result visual state
* [x] Rendering does not mutate results
* [x] GUI does not duplicate canonical thermal data
* [x] SI metre → OCCT millimetre boundary verified
* [x] Classifier cardinality verified
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-23. Qualified against c56aef0 plus the working tree: 5269/5269 x 3
presets, 2071 x 5 in two, 0 warnings over 721 objects, 17 stages exit 0,
6h05m33s, fingerprints at the freeze == before == after; second attempt on the
frozen tree (the first, stopped by the author after cli.assembly.batch failed
once after 34 s and nine dependants did not run, is in qualification/attempt1/;
the test passes alone in 0.22 s and in attempt 2). NOTHING QUALIFIED CHANGED: 265
files (the whole thermal, meshing, structural, linalg and core modules, the P17
renderer files and the OCCT mesh presentation) are byte-identical to HEAD; the
Viewer gained one additive overload.
DECISION: a read-only view. Four headless files (ThermalResultView, ThermalContour,
ThermalInspection, ThermalOverlay) read the stored nodal temperature, element
gradient vector, heat-flux vector and magnitude, the prepared loads and conditions,
the validation report and the core's currentness; they compute nothing but a display
norm and arrow geometry. The unit and location of every channel are the core's
descriptors; the gradient magnitude is the display norm of the stored vector (a NaN
component stays NaN - std::hypot(0, NaN, 0) is 0 here, found by a test and fixed);
element values are unsmoothed; the legend tiles its range exactly and a constant range
is one band; a manual range clamps a colour and never a value; the cache key holds the
whole source and the view. q is drawn from the stored vector, never flipped or
normalised; an applied q'' points along -n_out for q'' > 0 and is a different overlay;
the outward normal is verified against the owning tetrahedron, also on a curved wall.
CURRENTNESS: the display state is recomputed from the core on every call; a remesh with
the same counts or the same ids, a material, load or condition edit, and a failed
recompute never leave a result drawn as current; the validation badge is the core's
verdict, belongs to its analysis and its result, and goes Stale with the model. CARDINALITY:
expected / classified / rendered on every field and glyph; zero-classified, partial and
unresolved are never complete, nothing is rebound to the nearest face. UNITS: K, K/m and
W/m^2 never rescaled; the one metre -> millimetre conversion is the OCCT adapter's,
measured against a 1 x 2 x 3 mm and a 40 mm CAD solid. PLAYBACK: the stored times
(0, 0.3, 0.6, 0.8999999999999999, 1 s - not 1.2), one state per frame, no interpolation.
The Qt application has no thermal panel yet (as for P17-VIZ-001); the GUI smoke is the
headless layer plus offscreen renders of the production viewer. One gate scans 43 files
with 0 violations and has eleven fixtures that fail it. 100 + 51 + 12 cases, 15 zero-match
filters, 75 mutation probes (71 killed, 3 of them by an abort; 4 unreachable by construction;
3 first-pass survivors were test gaps, closed), 50 adversarial questions, 13 findings (an
independent review found four holes - cache key without the view, glyphs not bound to the
prepared loads, a badge not bound to its analysis, an overflowing arrow length - all fixed
with tests; a test with literal \n sequences had never run its last paragraph, repaired),
0 qualified-module defects. Limits recorded: no Qt panels; inspect* default resultState to
Current (the caller states it); a glyph and an extreme carry no mesh stamp; four guards
are unreachable through any public construction; the core's HeatFlux3::magnitude() has the
hypot NaN property (untouched, POST validates first).
Committed 7ba06a7 (qualified == committed: all eight subtree hashes read back
from the commit equal the frozen ones).
Evidence: docs/verification/P18-VIZ-001/.
```

---
# P18-CMD-001

## Commands / Undo / Redo

Canonical history contains thermal analysis definitions, thermal loads,
thermal boundary conditions, initial conditions, solver settings and time
settings. It does NOT contain K, C, Q, temperature arrays, temperature-gradient
arrays, heat-flux arrays or heat-balance arrays.

* [x] Create-analysis command
* [x] Add-load command
* [x] Edit-load command
* [x] Remove-load command
* [x] Add-BC command
* [x] Edit-BC command
* [x] Remove-BC command
* [x] Initial-temperature command
* [x] Solver-settings command
* [x] Transient-settings command
* [x] Undo exact
* [x] Redo exact
* [x] Failed command atomic
* [x] Redo invalidation correct
* [x] `GeometryReference` preserved
* [x] Thermal result invalidated correctly
* [x] No thermal solver arrays in canonical history
* [x] Compile-fail mesh-identity misuse where practical
* [x] Determinism PASS
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-24. Qualified against 6b675ec plus the working tree: 5432/5432 x 3
presets, 2182 x 2 in two, 0 warnings over 725 objects, 17 stages exit 0, 6h35m56s,
fingerprints at the freeze == before == after; third attempt (attempt 1, stopped by
the author after 11 gui/deploy tests could not start because Windows Smart App Control
blocked the unsigned bettercad.exe - the owner then turned it off; attempt 2, which
passed debug-ext and release-ext and then could not link bettercad_tests on
debug-shared-ext because a test read a static member the thermal DLL does not export,
fixed and re-frozen; both are in qualification/attempt1 and attempt2). NOTHING
QUALIFIED CHANGED: 319 of the 322 thermal, meshing, structural, linalg, core and
renderer files and apps are byte-identical to HEAD, 2 are new and 1 differs by one line
(src/thermal/CMakeLists.txt).
DECISION: no new framework, no new invalidation mechanism, no new identity type. Ten
commands on Command / CommandHistory / AddObjectCommand / Document::modifyObject /
ThermalAnalysis::setDefinition. A command is keyed on a ThermalAnalysisId and addresses a
record by its stable id, never by position; an add appends, an edit replaces the whole
record in place, a removal's undo returns the record to its position. Undo and redo
ASSIGN a stored definition (no inverse, no text round trip, 50 cycles do not drift) and
CHECK that the analysis is as the command left it, refusing instead of discarding an
out-of-history edit. A failed command moves no field, revision or stack; a no-op
succeeds, moves nothing and is recorded; a steady analysis refuses an initial
temperature and transient settings (mode before value). A FaceName is stored as given
(eight selector forms round-trip, copies in order, an unresolved one too), never
resolved; the command layer refuses the invalid feature handle, which the data model
lets through. The payload is two definitions: independent of mesh and result size.
CURRENTNESS, measured on real solves: a load edit stales loads, system, solution, post,
balance and report; a held temperature the boundary, solution, post, balance and report
(not the system); a convection also the system; settings, initial temperature and
transient settings only solution, post, balance and report; the mesh never. TWO REGIMES:
derived objects compare their recorded intent by value (an undo makes an old result
current again, and a fresh solve is bit-identical), the validation report compares the
monotonic revision (it stays stale) - recorded, not changed. The Qt application has no
thermal panel. 95 + 52 + 16 cases (52 compile-fail with a control; a gate with 15
fixtures), 14 zero-match filters, 44 + 14 mutation probes all killed (3 first-run
survivors were a missing test and a missing fixture, closed), 21 adversarial questions,
7 review defects and 2 mutation findings, 0 qualified-module defects. Limits recorded:
a create accepts a dangling mesh-control handle; validate(TransientSettings) has no
step-count bound; no mode-switch, delete or whole-definition command (not on the list).
Committed fe16f9d (qualified == committed: all eight subtree hashes read back
from the commit equal the frozen ones).
Evidence: docs/verification/P18-CMD-001/.
```
---
# P18-PERSIST-001

## Thermal Analysis Persistence

Persist thermal analysis intent, loads, boundary conditions,
`GeometryReference`s, initial temperature, steady settings, transient settings.
Do NOT persist as authority: K, C, Q, T, grad(T), heat flux, heat-balance
arrays. Mandatory byte-identity test: the same canonical document saved before
a thermal solve == saved after it, byte for byte.

* [x] Define P18 schema
* [x] Persist analysis definition
* [x] Persist thermal loads
* [x] Persist thermal BCs
* [x] Persist `GeometryReference`s
* [x] Persist initial condition
* [x] Persist steady solver settings
* [x] Persist transient settings
* [x] Preserve units
* [x] Preserve unresolved references explicitly
* [x] Exclude thermal solver/result arrays
* [x] Malformed files rejected
* [x] Invalid values rejected
* [x] Duplicate IDs rejected
* [x] Backward compatibility
* [x] Deterministic serialization
* [x] Full round trip
* [x] Save / load / re-solve
* [x] Compare re-solved semantics
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-25. Qualified against 975efa6 plus the working tree: 5504/5504 x 3
presets, 2254 x 2 in two, 0 warnings over 727 objects, 17 stages exit 0, 6h44m47s,
fingerprints at the freeze == before == after; first attempt. NOTHING QUALIFIED
CHANGED: 511 of 515 src/include/apps files are byte-identical to HEAD, 3 differ and 1
is new (the io module's shared private header, its dispatch, its CMake list and the
new reader/writer); one P18-DATA test that pinned the refusal this milestone removes
was renamed and rewritten.
DECISION: no new file, no version bump, no migration, no new validation framework, no
new reference encoding. ONE OBJECT TYPE in the existing format, exactly as P17. The
writer's only argument is the analysis object (seven members, none derived); the
reader gets no document, mesher or regenerator, so an unresolved reference has nothing
to be rebound to. Fixed SI named by the key (flux W/m^2, generation W/m^3, temperature
and ambient K, coefficient W/(m^2 K), time_step and end_time s); variants and
enumerations by stable strings; a kind carries only its own keys; solver settings
always written and all required; unknown keys rejected; order and ids preserved; the
core's validators called, not copied; the writer refuses an identifier above the file
cap, the file refuses the invalid feature handle (as the commands do).
BYTES: the same document saved before and after assembly, solve, POST, BALANCE,
VALID, VIZ, a stale result and a failed solve is identical (4230 B); a 201-state x 84
node transient (3,926,736 B of numbers) leaves the 5251 B file unchanged; the thermal
section is equal on a coarse and a fine mesh; save-load-save is a fixed point; a
hand-written file reproduces itself character for character; two FNV constants pin the
golden sections (1559 B and 1728 B) in the source, identical in all three presets.
ROUND TRIP: destroy, load, remesh, re-solve: nodal temperatures and the element
gradient and heat-flux fields bit-identical (1, 20 and 5 states; a shortened last
step lands on the end time), balance terms within 1e-12, verdicts equal. A result of
one document is stale for a copy even with equal analysis revisions (mesh and loads).
48 cases (4363 assertions), a 24-test source-text gate with 22 fixtures, 11
zero-match filters, 41 + 16 mutation probes all killed (2 first-run survivors closed
by a sharper assertion and a new fixture), an independent review (4 medium, 6 low, 8
gate holes - all with dispositions, 0 high), 0 qualified-module defects. Limits
recorded: a repeated JSON key is last-wins format-wide (pinned, an owner decision);
a dangling mesh control loads and is refused later; validate(definition) is O(n^2);
no NamedBoundarySet, output schedule or material reference exists in the data model;
analysis names are validated identifiers.
Committed 3f00ca7 (qualified == committed: all eight subtree hashes read back
from the commit equal the frozen ones).
Evidence: docs/verification/P18-PERSIST-001/.
```
---
# P18-CLI-001

## Headless Thermal Analysis

Follow existing BetterCAD CLI naming.

* [x] Inspect thermal analysis
* [x] Add / remove thermal loads
* [x] Add / remove thermal BCs
* [x] Steady solve
* [x] Transient solve
* [x] Temperature summary
* [x] Gradient summary
* [x] Heat-flux summary
* [x] Heat-balance report
* [x] Validation report
* [x] Structured diagnostics
* [x] Correct process exit codes
* [x] Stale-geometry failure
* [x] Stale-mesh failure
* [x] Missing-material failure
* [x] Singular thermal-model failure
* [x] Invalid load / BC failure
* [x] CLI / core equivalence
* [x] Multi-process save / load / re-solve
* [x] No CLI-only thermal implementation
* [x] Fresh-binary proof
* [x] Raw structured STDOUT clean
* [x] Zero-match protection
* [x] End-to-end scripted workflow PASS
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-26. Qualified against 2ffba66 plus the working tree: 5725/5725 x 3
presets (5504 before + 221 new), 2483 x 5 repeats in two, 0 warnings over 732 objects
per preset, 17 stages exit 0, 6h38m21s, fingerprints at the freeze == before == after;
first attempt. NOTHING QUALIFIED CHANGED: 509 of 520 src/include/apps files are
byte-identical to HEAD, 6 differ (the registration of the verbs and the thermal source
list) and 5 are new; no existing thermal source or header was touched.
DECISION: the CLI is an adapter. The thermal core had no production orchestrator (only
the viewer's test-support derive), so ONE additive entry holds the order of the chain:
thermal::runThermalAnalysis (ADR-057) - it calls each link once, never meshes, never
writes, does no arithmetic. Twelve verbs: thermal-settings, -validate, -solve,
-temperature, -gradient, -heat-flux, -balance, and thermal-analysis-add, -load-add,
-load-remove, -boundary-add, -boundary-remove (registered in the edit registry, so
batch scripts them). Ids are the caller's, a bare number is kelvin, there is no
Celsius, no default solver setting, no CLI-chosen tolerance, no mesh-local target.
EXIT: 0 success, 1 failure (a refusal, or a rejected validation report / heat balance,
which is printed AND exits 1), 2 an unreadable command line; one command line gets one
code whatever the file holds.
EVIDENCE OF EQUIVALENCE: every printed temperature, gradient, flux, reaction and
residual equals the core's bit for bit; the five edits give the same canonical bytes as
the P18-CMD command objects; the 74-process workflow re-solves in fresh processes to
byte-identical payloads and a solve leaves the document byte-identical; the three
presets agree on every exit code, key and text field and (measured) on every number.
STDOUT: --json is one strict RFC 8259 object and a newline, parsed twice (CMake and the
strict reader) over the raw bytes of every process; a refusal prints nothing to stdout;
a non-finite number is null and says so; the backend's own chatter cannot reach it.
85 + 20 cases (6013 assertions), 12 process tests, a 104-test source-text ownership gate
with 102 violating fixtures, 45 mutation probes all killed (4 first-pass non-kills: 2
real survivors - ids derived from a position, closed by two gate rules - and 2 probe
defects of mine, an inert probe and one that did not compile, rewritten), an independent review (0 critical, 14 findings: 2 production defects
fixed, 9 gate escapes closed), 1 core addition (additive). Limits recorded: a
load/save failure inside the shared edit driver carries no code for any edit family;
tolerance 1e300 is accepted by the data model; a final_only transient of more than one
step can never be accepted; a 23750-step transient is refused at state 812; stale
geometry and stale mesh cannot be presented to a one-shot process (core tests plus the
regeneration_failed / mesh_generation_failed analogues).
Committed ebca776 (qualified == committed: all eight subtree hashes read back
from the commit equal the frozen ones).
Evidence: docs/verification/P18-CLI-001/.
```
---
# P18-REFMOD-001

## Thermal Reference Models

```text
RM-THERM-01  Single Tet linear-temperature patch test
RM-THERM-02  1D slab conduction
RM-THERM-03  Composite wall / layered conduction
RM-THERM-04  Uniform volumetric heat generation
RM-THERM-05  Convection / Robin boundary benchmark
RM-THERM-06  Rigidly transformed thermal model
RM-THERM-07  Local refinement / thermal-gradient field
RM-THERM-08  Transient thermal benchmark
RM-THERM-09  Singular thermal model / missing datum
RM-THERM-10  Stale mesh / unresolved thermal boundary failure
```

* [x] Define reference suite
* [x] Single-Tet thermal patch test
* [x] Constant temperature-gradient validation
* [x] Fourier-law validation
* [x] 1D slab conduction
* [x] Composite-wall conduction (RECORDED AS NOT REPRESENTABLE in P18 v1 - one material per document, one solid per mesh; closed form verified by hand, both refusals tested, no stand-in built)
* [x] Volumetric heat generation
* [x] Convection benchmark
* [x] Heat-balance validation
* [x] Temperature agreement
* [x] Heat-flux agreement
* [x] Rigid-transform invariance
* [x] Mesh-refinement behaviour
* [x] Local-refinement behaviour
* [x] Transient analytical benchmark
* [x] Singular-system failure
* [x] Stale-mesh failure
* [x] Unresolved-reference failure
* [x] Save / load / re-solve
* [x] CLI validation
* [x] Independent analytical validation
* [x] Determinism PASS
* [x] Three-preset regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-26. Qualified against b274df2 plus the working tree: 5789/5789 x 3
presets (5725 before + 64 new), 2547 x 5 repeats in two, 0 warnings over 742 objects
per preset, 17 stages exit 0, 6h15m48s, fingerprints at the freeze == before == after;
first attempt. NOTHING QUALIFIED CHANGED: src, include and apps (520 files), cmake and
the root build files are byte-identical to HEAD; only tests and examples differ.
DECISION: reference values come from hand arithmetic, closed forms and an independent
dense FE solver and backward-Euler integrator in a std-only header, never from the code
under test; a whitelist gate fails if that header ever includes production code.
Tolerances are classed A-E with no global percentage; the guards fixed from a
measurement are labelled and four redesigned after one are listed.
SUITE: 10 models, 20 documents (15 solve, 5 refused); 47 in-process cases (63168
assertions), 17 fresh-process tests, a 58-process two-binary workflow per preset, 6
hidden cases comparing every CLI number with the closed forms, 19 zero-match groups.
RESULTS: exact fields exact (slab 8e-13 K on 9/32/449 nodes, reactions +-200 W); production
== independent FE on real meshes (<= 3.8e-12 K) and == independent backward Euler over 11
states of a non-uniform held-cap transient (2.3e-13 K); convection network 350 K / +-200 W
with correct signs for loss and gain; surface heat flux 1.25 W with correct signs; rigid
motion rotates gradient and flux (3.8e-12 K/m); lumped body follows backward Euler to
0.0084 K, first order in dt (0.94, 0.96), shortened last step lands on 1.0 s; pure-Neumann
(also net-zero) refused as no datum at the solve stage; a stale mesh with the SAME counts
and ids, edited or not, still stales the old result; unresolved load / temperature /
convection refused before assembly, no rebinding. Save -> load -> re-solve bit-identical
(6 documents); results bit-identical across runs, processes and the three presets (57/57
process lines, 32 payloads, 20 documents, 72 evidence lines).
29 mutation probes all killed; an independent review (0 critical, 17 findings, 4 HIGH:
5 production-defect classes the first suite would have missed - surface-load share,
lumped capacity, lumped Robin matrix, held node and two convections in a transient,
persistence of loads - closed by new documents and cases; a false doc claim corrected).
OWNER FINDINGS (none a P18 defect): F-R1 P16 cannot refine a smooth field on a box, rod
or tube (tube hottest node 351.6-358.2 K vs 348.411 K, not converging, while production
== an independent solve to 4e-12 K) - convergence is shown on structured meshes (order
1.87, 1.96); F-R2 local sizing does not improve gradient accuracy near a held boundary
(+4.7 % / +4.1 % on the refined sides); F-R3 P18 v1 cannot represent a composite wall.
Limits recorded: only every_step is run in the reference documents; no unbalanced result
is built to test the balance classifier negatively; the CLI transient comparison (0.02 K)
is a guard; the exploratory mesh sweeps are not reproducible from committed code.
Committed 04751f6 (qualified == committed: all eight subtree hashes read
back from the commit equal the frozen ones).
Evidence: docs/verification/P18-REFMOD-001/.
```
### RM-THERM-01 — linear temperature patch test

Affine `T(x,y,z) = a + b x + c y + d z`; a linear Tet4 must reproduce it
exactly: `grad(T) = [b, c, d]^T`, `q = -k [b, c, d]^T`. Validate exact nodal
interpolation, constant gradient, constant heat flux, element-matrix
consistency, energy / weak-form consistency. The strongest single P18
element test.

### RM-THERM-02 — 1D slab conduction

`Qdot = k A (T_hot - T_cold) / L`; `T(x)` linear for constant `k` and no
generation. Validate the profile, the flux, the total heat rate, the boundary
heat reaction, the heat balance.

### RM-THERM-03 — composite wall

`R_total = sum L_i / (k_i A)`, `Qdot = dT / R_total`. Validate interface
temperatures, total heat flow, piecewise gradient, flux continuity. NOTE:
P16 refuses a multi-solid body (ADR-049 constraint), so the layered wall is
built on one solid with the layers expressed through what one material and
one solid can represent, or the case records what it cannot.

### RM-THERM-04 — volumetric heat generation

`q'''` [W/m3], `Qdot_gen = q''' V`, compared with the analytical temperature
behaviour where a closed form exists.

### RM-THERM-05 — convection / Robin boundary

`q'' = h (T_surface - T_inf)` against an analytical resistance-network / slab
case: surface temperature, heat rate, conduction/convection balance.

### RM-THERM-06 — rigid transform

Invariant: temperature, heat-flux magnitude, thermal energy, heat balance.
Transformed: `grad(T)' = R grad(T)`, `q' = R q`. Judge P16 boundary and
interior mesh populations separately.

### RM-THERM-07 — local refinement

Baseline, target-refined and mirror-refined documents with a 3D volume band;
never inferred from target planar surface triangles alone. Validate band
Tet size/count, mapping preserved, thermal solve valid, temperature/gradient
trend sensible.

### RM-THERM-08 — transient benchmark

A lumped-capacitance body with its assumptions deliberately satisfied, or a
1D transient slab with analytical series data. Validate `T(t)`, time-step
refinement, energy balance, the steady-state limit.

### RM-THERM-09 — singular thermal model

A conduction problem with no temperature datum. Required: the solve FAILS
explicitly. Forbidden: arbitrary finite temperatures returned as success.

### RM-THERM-10 — stale / unresolved inputs

A stale P16 mesh, an unresolved thermal load target, an unresolved
temperature/convection target. Expected: solve refused before assembly where
applicable. No nearest-face rebinding.

---
# P18-QUAL-001

## Full P18 Qualification

* [x] Freeze final P18 tree
* [x] Audit every P18 milestone
* [x] Verify all TODO items complete
* [x] Verify all P18 ADRs
* [x] Audit CAD / material / mesh / thermal authority
* [x] Audit thermal result currentness
* [x] Audit thermal DOF model
* [x] Audit Tet4 thermal formulation
* [x] Audit thermal loads
* [x] Audit thermal BCs
* [x] Audit global assembly
* [x] Audit steady solver
* [x] Audit transient solver
* [x] Audit temperature recovery
* [x] Audit gradient recovery
* [x] Audit heat-flux recovery
* [x] Audit heat balance
* [x] Audit validation policy
* [x] Audit stale-input protection
* [x] Audit visualisation
* [x] Audit commands
* [x] Audit persistence
* [x] Audit CLI
* [x] Re-run all thermal reference models
* [x] Independent analytical validation
* [x] Steady residual PASS
* [x] Heat balance PASS
* [x] Thermal patch test PASS
* [x] Slab conduction PASS
* [x] Convection benchmark PASS
* [x] Transient benchmark PASS
* [x] Singular thermal-model failure PASS
* [x] Stale-mesh failure PASS
* [x] Clean Debug qualification
* [x] Clean Release qualification
* [x] Clean Debug-shared qualification
* [x] Repeated determinism qualification
* [x] Cross-preset equivalence
* [x] Final adversarial review
* [x] Zero-match protection PASS
* [x] 0 unexpected warnings
* [x] Qualified tree == committed tree
* [x] Evidence recorded
* [x] Mark P18 qualified

```text
PASS 2026-10-27. Qualified against ece7691 plus the working tree, scratch tree
5386f4fb, apps b4e2268d include f40f760d src 7fae8afb tests ce05767c examples
4da234eb cmake 5a382115 CMakeLists.txt 3ec3c10f CMakePresets.json 3229f0f8:
5804/5804 x 3 presets (5789 before + 15 new), 2561 x 5 repeats in two, 0 warnings
over 743 objects per preset, 17 stages exit 0, 6h50m37s, fingerprints at the freeze
== before == after; first attempt.
AUDIT: 18 of 18 predecessor blocks PASS from their own harness verdicts (345 boxes,
0 open); 127 ADR clauses (122 implemented, 5 text drifts, 0 violated); 330
requirement rows read against the code and the tests.
THREE DEFECTS FIXED, each reproduced against the old code first: the persistence
writer emitted a file the reader refuses (invalid feature handle, ids above 2^53-1);
the mesh classifier skipped a metric no element was judged for; the thermal
inspection defaulted a result's currentness to Current. Plus 15 tests closing audit
gaps (final-gate cases in tests/qualification/P18ThermalQualificationTests.cpp).
28 production mutants, 27 killed, 1 survivor = an unreachable defensive branch.
Independent analytical verification of fresh CLI payloads on three presets (28
checks each, negative control fails); 20 reference documents, 59 payloads and 72
evidence lines identical across presets to the bit.
ADVERSARIAL REVIEW: a second, independent reviewer found NO wrong accepted result.
OPEN, reported and NOT changed (owner decisions): F1 the balance gate refuses correct
transient runs once the time step is small (needs a rounding term, which loosens a
tolerance); F2 no bound on a temperature; F3 no bound on the user's residual
tolerance. docs/verification/P18-QUAL-001/ADVERSARIAL_REVIEW.md, LIMITATIONS.md.
Evidence: docs/verification/P18-QUAL-001/.
Committed b5dce07, and all eight subtree hashes of that commit equal the frozen
fingerprint: qualified tree == committed tree.
```

### P18 Final Gate

```text
all P18 milestones PASS

+ CAD geometry remains authority
+ P15 material remains canonical
+ P16 mesh remains derived
+ P18 results remain derived

+ stale geometry rejection PASS
+ stale mesh rejection PASS

+ thermal DOF model PASS
+ Tet4 conduction formulation PASS
+ Tet4 capacity formulation PASS

+ conductivity matrix symmetry PASS
+ thermal-null-mode behaviour PASS

+ thermal loads PASS
+ thermal BCs PASS
+ GeometryReference mapping PASS

+ global thermal assembly PASS

+ steady solver PASS
+ residual validation PASS

+ transient solver PASS
+ time-step validation PASS
+ energy storage behaviour PASS

+ temperature PASS
+ gradient PASS
+ heat flux PASS

+ steady heat balance PASS
+ transient energy balance PASS

+ thermal mesh-quality acceptance policy explicit

+ visual inspection PASS

+ undo/redo PASS
+ generated thermal arrays excluded from history

+ persistence PASS
+ generated thermal arrays excluded as authority

+ CLI PASS
+ CLI/core equivalence PASS

+ reference models PASS
+ independent analytical validation PASS
+ thermal patch test PASS

+ Debug PASS
+ Release PASS
+ Debug-shared PASS

+ determinism PASS
+ cross-preset equivalence PASS
+ adversarial review PASS
+ zero-match protection PASS
+ 0 unexpected warnings

+ qualified tree == committed tree
```

### P18 Final Adversarial Questions

At minimum attack:

```text
Can P18 solve a stale mesh?
Can P18 solve geometry that failed regeneration?
Can a conductivity edit leave an old result current?
Can density/cp edits leave an old transient result current?
Can a thermal-load edit leave an old result current?
Can a thermal-BC edit leave an old result current?
Can NodeId become permanent thermal-load identity?
Can ElementId become permanent thermal intent?
Can a thermal boundary silently move to another CAD face?
Can an unresolved thermal BC bind to a nearby face?
Can an inverted Tet enter thermal assembly?
Can a zero-volume Tet generate finite-looking conductivity/capacity matrices?
Can thermal conductivity units be confused?
Can K lose symmetry?
Can duplicate sparse thermal contributions overwrite instead of accumulate?
Can a steady thermal model with a free temperature null mode appear solved?
Can the solver return success with a large thermal residual?
Can heat balance fail while the solve is accepted?
Can surface heat flux fail to preserve total heat rate?
Can volumetric generation fail to preserve q'''V?
Can convection use an incorrect sign?
Can a rigid transform change temperature or heat-flux magnitude?
Can GUI visualization recalculate heat flux differently from core?
Can visualization units mutate model coordinates?
Can undo restore thermal matrices/results instead of analysis intent?
Can save/load trust old temperature or heat-flux arrays?
Can the CLI implement separate Tet4 thermal logic?
Can the CLI return zero on a singular thermal solve?
Can mesh-quality thresholds differ between GUI and CLI?
Can transient time-step changes fail to stale the old result?
Can save-before-solve and save-after-solve differ?
Can stale binaries produce qualification evidence?
Can a zero-test filter pass qualification?
Can source change after final qualification?
```

For every credible defect: reproduce → regression test → root cause →
general fix → requalify the affected milestone → rerun P18-QUAL.

---
# P19 — CFD Integration

**AUTHORIZED 2026-10-27** by the owner's "start P19-ARCH-001", the open P18 findings
F1-F3 weighed as inputs to it. Milestones are worked top to bottom, and each is authorized
by its predecessor's PASS being recorded here and the owner saying so -- P17's and P18's
rule, unchanged. P19-ARCH-001, P19-DATA-001, P19-FLUID-001, P19-MESH-001, P19-TOPO-001,
P19-GEOM-001, P19-DOF-001 and P19-BC-001 have PASSED and P19-IC-001 is authorized
and in progress; **nothing after it is authorized**, and a predecessor's pass is
not permission to start the next.

Goal: add qualified incompressible CFD capability without duplicating CAD, material, mesh,
solver, result, persistence, or CLI authority.

Core authority model:

```text
CAD geometry
→ canonical geometry authority

fluid/material definition
→ canonical fluid-property authority

CFD analysis definition / loads / BCs / solver settings
→ canonical CFD intent

volume/surface CFD mesh
→ derived

cell / face / DOF maps
→ derived

assembled algebraic systems
→ derived

pressure / velocity / turbulence fields
→ derived

forces / fluxes / pressure loss / flow split
→ derived

validation / visualisation buffers
→ derived
```

Hard rules:

```text
+ no GUI-only CFD physics
+ no CLI-only CFD physics
+ no mesh-local IDs as persistent CAD/CFD intent
+ stale geometry / stale mesh / stale BC mappings must fail explicitly
+ persistence stores canonical CFD intent, not generated solver fields
+ command history stores canonical CFD intent, not generated fields
+ numerical results are never authority
+ no silent nearest-face rebinding
+ deterministic source/provenance tracking
+ no PASS with zero tests
+ 0 unexpected warnings
+ qualified source/test tree == committed source/test tree
```

Recommendation: start with laminar incompressible finite-volume CFD only, qualify that
completely, and add turbulence afterward. That keeps pressure–velocity coupling,
conservation, mesh geometry, BC mapping, persistence, and CLI correctness separable before
k-ε / SST adds another major layer of numerical complexity.

---
# P19-ARCH-001

## CFD Architecture / Authority Contract

Define the complete P19 ownership model before adding CFD physics.

* [x] Define P19 supported CFD scope
* [x] Define canonical CFD intent
* [x] Define derived CFD state
* [x] Define CAD authority
* [x] Define fluid/material authority
* [x] Define mesh authority
* [x] Define CFD result authority
* [x] Define source/currentness model
* [x] Define geometry-reference policy
* [x] Define field-location policy
* [x] Define unit system
* [x] Define steady/transient scope
* [x] Define incompressible/compressible scope
* [x] Define laminar/turbulence scope
* [x] Define thermal-coupling scope
* [x] Define unsupported-physics policy
* [x] Define dependency layers / ADRs
* [x] Define failure policy
* [x] Define determinism policy
* [x] Define evidence structure
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-27. Qualified against e1a8eb2 plus the working tree:
5826/5826 x 3 presets, 2583 x 5 in two, 0 warnings over 745 objects,
17 stages exit 0, 6h37m11s (attempt 2; attempt 1 stopped when the linker
ran out of memory, cause outside the tree), fingerprints before == after.
A cfd module at layer 42 (ADR-059, below thermal and structural so CFD
results can flow up; cfd links neither), the v1 scope ADR-058 (laminar
incompressible collocated finite volume on the P16 tets), and the input
boundary requireCfdModel / CfdModel (ADR-060, the fluid is the document's
P15 material) with 14 boundary cases and 8 architecture fixtures; 554
files, 0 layering violations. Adversarial review: independent reviewer
0 blockers, 13 findings disposed; 10 of 10 mutants killed. Committed as
42a3551; all eight subtree hashes of that commit equal the frozen
fingerprint: qualified tree == committed tree. Evidence:
docs/verification/P19-ARCH-001/.

P19-DATA-001 was NOT AUTHORIZED when this record was written; being next in the
plan was not permission. The owner authorized it on 2026-10-27.
```

Initial recommended P19 v1 scope:

```text
3D single-phase incompressible flow
Newtonian fluid
constant rho
constant mu
steady laminar
transient laminar
pressure inlet/outlet or velocity inlet
no-slip wall
symmetry/slip where supported
volumetric/source terms only if explicitly required
```

Defer initially:

```text
compressible flow
multiphase
species
combustion
radiation
conjugate heat transfer
moving mesh
rotating machinery
non-Newtonian flow
LES
DES
DNS
porous media
free surface
cavitation
```

---
# P19-DATA-001

## CFD Analysis Data Model

Define canonical and derived CFD data types.

* [x] Define `CfdAnalysisId`
* [x] Define `CfdBoundaryConditionId`
* [x] Define `CfdInitialConditionId` if needed
* [x] Define analysis mode
* [x] Define fluid model
* [x] Define reference pressure policy
* [x] Define gravity policy
* [x] Define steady settings
* [x] Define transient settings
* [x] Define nonlinear-iteration settings
* [x] Define convergence settings
* [x] Define output settings
* [x] Define result metadata
* [x] Define source/provenance metadata
* [x] Define stale/current semantics
* [x] Define current accepted result
* [x] Define latest failed attempt
* [x] Prevent derived fields becoming canonical
* [x] Deterministic semantic fingerprint
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-27. Qualified against 24a1496 plus the working tree:
5893/5893 x 3 presets, 2622 x 5 in two, 0 warnings over 750 objects,
17 stages exit 0, 6h43m23s (first attempt), fingerprints before == after.
CfdAnalysisId (document object) and CfdBoundaryConditionId; no
CfdInitialConditionId (one initial condition per analysis). The definition
(ADR-061) holds intent only and NOT the fluid (the document's P15
assignment); the reference pressure is a value at a point and the reference
height belongs to gravity; a missing datum is storable but not Ready, an
overdetermined one is refused; a semantic fingerprint records the content,
the revision decides currency. Result = provenance + metadata (fields with
P19-DOF-001); the latest failure is a structured record. 39 data cases, 28
compile-fail cases; golden fingerprint identical in all three presets.
Adversarial review: independent reviewer 0 blockers, 15 findings disposed
and re-verified; 20 of 20 mutants killed. Committed as f08019a; all eight
subtree hashes of that commit equal the frozen fingerprint: qualified tree
== committed tree. Evidence: docs/verification/P19-DATA-001/.

P19-FLUID-001 was NOT AUTHORIZED when this record was written; being next in the
plan was not permission. The owner authorized it on 2026-10-27.
```

Canonical intent may include:

```text
analysis definition
fluid reference
boundary-condition definitions
initial conditions
gravity setting
steady/transient settings
solver settings
output settings
GeometryReferences
```

Must NOT become canonical:

```text
cells
faces
cell centres
face normals
DOF maps
pressure arrays
velocity arrays
mass fluxes
gradients
residual vectors
matrices
factorisations
forces
pressure-drop results
flow-split results
visualisation buffers
```

---
# P19-FLUID-001

## Fluid Property Resolution

Resolve canonical fluid properties for CFD.

* [x] Define P19 fluid-property requirements
* [x] Define density `rho`
* [x] Define dynamic viscosity `mu`
* [x] Define kinematic viscosity derivation if used
* [x] Validate `rho > 0`
* [x] Validate `mu > 0`
* [x] Reject NaN / Inf
* [x] Reject missing fluid
* [x] No default air unless explicitly canonical
* [x] Preserve fluid material ID
* [x] Preserve revision / generation / fingerprint
* [x] Fluid edit stales CFD result
* [x] Fluid edit does not stale geometry unnecessarily
* [x] No duplicate CFD-only material database
* [x] SI units explicit
* [x] Determinism PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-28. Authorized 2026-10-27 ("start P19-FLUID-001"). Qualified
against c8b53f6 plus the working tree: 5919/5919 x 3 presets, 2783 x 5 in
two, 0 warnings over 753 objects, 17 stages exit 0, 6h47m34s (first
attempt), fingerprints before == after. P15 learned a dynamic viscosity
(ThermalPropertyKind::DynamicViscosity, in Pa s, validated finite and > 0 at
entry, optional file key "dynamic_viscosity", CLI code) and an
IncompressibleFlow consumer requiring { density } and { dynamic viscosity }
and nothing thermal (ADR-062). CFD resolves a CfdFluid (id, revision, rho,
mu; nu derived, never stored) through P15 only, with no default fluid; a
missing viscosity is FluidViscosityUnusable; a fluid edit stales a CFD
result for `fluid` alone and neither the geometry nor the mesh. P19-DATA-001's
CFD-side validate(CfdFluidProperties) retired as a second copy of P15's rule.
Adversarial review: independent reviewer 0 blockers, 0 major, 8 minor + 3
nits disposed; 17 of 18 mutants killed, 1 equivalent recorded. Committed
as 4e57b02; all eight subtree hashes of that commit equal the frozen
fingerprint: qualified tree == committed tree. Evidence:
docs/verification/P19-FLUID-001/.

P19-MESH-001 was NOT AUTHORIZED when this record was written; being next in the
plan was not permission. The owner authorized it on 2026-10-28; it is OPEN and
BLOCKED, and nothing it claims is qualified.
```
Recommended v1 units:

```text
rho    kg/m³
mu     Pa·s = kg/(m·s)
nu     m²/s derived only
```

---
# P19-MESH-001

## CFD Volume / Surface Mesh Contract

Define the CFD mesh representation required by P19.

* [x] Audit P16 suitability for CFD
* [x] Define cell topology supported
* [x] Define face topology
* [x] Define boundary-face representation
* [x] Define owner / neighbour relation
* [x] Define cell centres
* [x] Define face centres
* [x] Define face area vectors
* [x] Define cell volumes
* [x] Define outward-normal convention
* [x] Define internal-face orientation
* [x] Define boundary patch mapping
* [x] Define named boundary mapping
* [x] Define geometry-source binding
* [x] Define mesh-source binding
* [x] Reject zero / negative cell volume
* [x] Reject invalid face ownership
* [x] Reject non-manifold volume topology
* [x] Reject unclosed fluid domain
* [x] Define mesh-quality metrics required by CFD
* [x] No nearest-face fallback
* [x] Determinism PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-31. Authorized 2026-10-28 ("start P19-MESH-001"). Qualified by
attempt 2 against 6a0ed5d plus the working tree -- the same twelve identities as
the 2026-10-28 freeze, since every commit since then touched only docs/:
5939/5939 x 3 presets, 2803 x 5 in two with 14015 counted per-test Passed
markers each, 0 warnings over 755 objects, 17 stages exit 0, 2h46m08s,
identities before == after == after post-run verification. P16's Tet4 mesh
serves directly as the finite-volume mesh, with no adapter -- cell = Tet4,
boundary face = P16 boundary triangle, patch = a CAD face of GeometryMeshMap,
and interior faces, owner and neighbour derived from P16's connectivity and
never persisted -- and what P16 does not itself guarantee is proved by
requireCfdMesh(CfdModel) -> CfdMeshContract with nine named refusals, bound to
the mesh stamp (ADR-063). Adversarial review: the author's 6 questions each
resolved by a fix and a test; independent reviewer 0 blockers, 1 major (a shared
face traversed the same way by both cells passed; InconsistentFaceOrientation
added), 4 minor, 8 nits disposed; 31 of 31 mutation probes killed, 0 survived;
a third read re-audited all twenty-one earlier marks and found 0 false marks.
Finding F1 stands and is measured, not judged: the P16 reference meshes are poor
finite-volume meshes, which makes the non-orthogonal correction mandatory in the
solver and is judged by P19-VALID-001.

Attempt 1 (2026-10-28) FAILED 5 of 17 stages for an environmental reason and
stays recorded, not erased. The machine that produced it was then permanently
replaced, so the replacement environment was qualified in its own right before
the gate was run (docs/verification/P19-MESH-001/ENVIRONMENT-MIGRATION.md). One
post-run check also failed and was fixed in the checker, not the product:
postrun-mesh.ps1 compared stdout across presets without pinning a seed, on a
binary whose case order defaults to rand, so the comparison was a coin flip;
--rng-seed 1, the convention the P12 evidence scripts already use, makes it
deterministic and strictly stricter. The owner accepted that fix before any mark
moved. Committed as 189b87f, production code and evidence together, with all
eight subtree hashes equal to the frozen fingerprint before and after the commit:
qualified tree == committed tree. Evidence: docs/verification/P19-MESH-001/.
```

Critical question:

```text
Can P16's current Tet4 mesh directly serve as the P19 finite-volume mesh?
```

If yes, qualify it properly.
If no, define a derived CFD mesh adapter from P16 rather than creating a second geometry
authority.

---
# P19-TOPO-001

## Finite-Volume Topology

Build deterministic cell / face connectivity.

* [x] Define compact `CellIndex`
* [x] Define compact `FaceIndex`
* [x] Define boundary-face index
* [x] `ElementId != CellIndex`
* [x] Define owner cell
* [x] Define neighbour cell
* [x] Define boundary face
* [x] Define internal face
* [x] Define boundary patch membership
* [x] Define face orientation
* [x] Define cell-to-face adjacency
* [x] Define face-to-cell adjacency
* [x] Reject duplicate internal faces
* [x] Reject missing neighbours
* [x] Reject boundary faces with two owners
* [x] Reject internal faces with one owner
* [x] Sparse mesh IDs supported
* [x] No max-ID-sized arrays
* [x] Exact mesh-source binding
* [x] Determinism PASS
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-10-31. Authorized 2026-10-31 by the owner's "implement this and fully
verify", with the twenty-two item checklist quoted. Qualified from clean on the
first attempt against dacbf561 plus this milestone's candidate files:
5998/5998 x 3 presets, 2839 x 5 in two with 14195 counted per-test Passed
markers each, 0 warnings and 0 errors under -Werror, 17 stages exit 0,
2h11m16s, and the eight qualified subtrees identical before the first build,
after the last test run and in the committed tree. 36 unit cases / 108,373
assertions and 23 compile-fail cases; the 28 cfdids cases and 205 architecture
tests unchanged and passing. The harness is byte-identical to P19-MESH-001's
(qualify.cmd d313a640), so the only difference between the two qualifications
is the tree under test.

The topology is a DERIVED value -- built from a mesh and a mapping, never
persisted, carrying the mesh generation it was built for, so it cannot survive
a remesh. Four index spaces become four distinct types (CellIndex, FaceIndex,
BoundaryFaceIndex, PatchIndex, all dense and 0-based) against the mesh's sparse
1-based ElementId, so zero means "the first cell" in one space and "invalid" in
the other and the separation rests on the type system, not on naming; no
container is sized by a handle's value (ADR-064). Eight named refusals are a
subset of what ADR-063's gate already refuses, so possession of a topology is
the proof.

Adversarial review found one credible defect: faceNodes promised outward
winding unconditionally while the builder checked neither cell volume nor
shared-face orientation, and P19-GEOM-001 is told to build its area vectors on
that guarantee. It is now ENFORCED by two added refusals,
NonPositiveCellVolume and InconsistentFaceOrientation, each with a fixture
built to fail it -- not documented away. A test-gap review found eight claims
with no evidence behind them, each corrected; the largest was that the
structural assertions ran on one model each, leaving the transformed body
untested, so checkStructure now runs every structural claim on all eight
reference meshes.

Three checklist items are recorded as less than their wording suggests, stated
rather than glossed: "Reject duplicate internal faces" -- a duplicate internal
face is not representable, since the key is a sorted node triple, so what is
rejected is the two reachable attempts; "Reject internal faces with one owner"
-- the same condition and the same test as "Reject missing neighbours", so it
collapses onto it rather than being counted twice; "Sparse mesh IDs supported"
-- MeshBuilder allocates from one counter, so the reachable sparsity is
interleaving, tested alongside a far-outside lookup. "No max-ID-sized arrays"
is proved by review and negative lookups, NOT by measuring allocation.
Committed as a075abe, production code and evidence together; the eight subtree
hashes were read back from the commit object and each equals its frozen value:
qualified tree == committed tree. Evidence: docs/verification/P19-TOPO-001/.
```

---
# P19-GEOM-001

## Finite-Volume Geometric Metrics

Compute qualified geometric quantities for each CFD cell / face.

* [x] Cell volume
* [x] Cell centroid
* [x] Face area
* [x] Face centroid
* [x] Face unit normal
* [x] Face area vector `Sf`
* [x] Owner-to-neighbour vector
* [x] Cell-to-face vector
* [x] Orthogonal distance
* [x] Non-orthogonality angle
* [x] Skewness metric
* [x] Boundary outward normal
* [x] Conservation identities
* [x] Closed-cell surface-vector sum
* [x] Positive cell volume
* [x] SI metre units
* [x] Rigid-transform covariance
* [x] Determinism PASS
* [x] Independent analytical validation
* [x] Regression PASS
* [x] Evidence recorded

Key identity:

```text
Σ_faces Sf ≈ 0
```

for a closed polyhedral control volume.

```text
PASS 2026-10-31. Authorized 2026-10-31 by the owner's "start", with the
twenty-one item checklist quoted. Qualified from clean on the first attempt
against 16cd7458 plus this milestone's nine candidate files: 6058/6058 x 3
presets, 2964 x 5 in two with 14820 counted Passed markers each, 0 warnings and
0 errors under -Werror, 17 stages exit 0, 3h27m58s, and the eight qualified
subtrees identical before the first build, after the last test run, and when
recomputed independently afterwards by the harness's own method. 31 unit cases
/ 337,203 assertions and 29 compile-fail cases; the 36 CfdTopology, 20 CfdMesh,
23 cfdtopo, 28 cfdids and 205 architecture tests unchanged and passing, which is
the blast-radius evidence for a change to a layer-0 header. The harness differs
from P19-TOPO-001's only in the repeat filter (qualify.cmd d313a640), so the
only difference between the two qualifications is the tree under test.

The metrics are a DERIVED value whose area vector is primary (ADR-065): every
other face quantity is computed from Sf, so a face's normal and the closure
identity cannot disagree by construction. Units are carried in the type system
-- AreaVector3D is three Areas, not three doubles -- so a face normal cannot be
added to a position, and the dimensionless quality metrics cancel to double at
the boundary. Three passes in index order; every container ordered; nothing
keyed by a hash or an address.

The key identity is measured, not asserted away: the per-cell surface-vector
sum closes to 2.03e-16 of the cell's own area scale on all eight reference
meshes, and the divergence theorem closes to 1.21e-14 of each cell's volume.
The quality metrics rest on a closed form this milestone had to derive -- an
analytic face pair fixing cos = 3/5 and skewness = sqrt(10)/15 -- because the
cross-milestone agreement with P19-MESH-001's recorded figures checks the
plumbing and not the definition: both transcribe one formula.

Two reviews ran before the freeze and each found a real defect. The first was a
hard gate failure: the compile-fail control target did not build under
-Werror=unused-function, while ctest -R compile_fail.cfdgeom. reported 29 of
29 passing over a project that did not build -- a control is a build target
ctest never builds -- and it would have failed all 17 stages. The second found
that the first round's fixes had reached the ADR but not the public header,
which kept verbatim a claim written without the work behind it: four builder
guards called unreachable are REACHABLE, each now has a test, and the quoted
mechanism for the cosine's conditioning (eps/cos) was disproved by measurement
and replaced with eps*L/|d|.

Recorded as less than a reader might assume, stated rather than glossed: the
-1.0 half of the cosine clamp is unreachable dead code; orthogonalDistance is
the worst-conditioned number here and is deliberately measured, never judged,
so a consumer can be handed a wholly wrong number with no diagnostic, and a
bound on it is P19-VALID-001's; cross-preset bit-identity is NOT claimed,
because magnitude and the quality metrics go through libm. Nine known
limitations are listed in the evidence, among them that nothing in the
repository tests that a compile-fail control builds -- which is how the gate
failure above stayed invisible -- and that the centroid formula was considered
and deliberately left unchanged. Committed as 9d2b1ed, production code and
evidence together; the eight subtree hashes were read back from the commit
object and each equals its frozen value: qualified tree == committed tree.
Evidence: docs/verification/P19-GEOM-001/README.md.
```

---
# P19-DOF-001

## CFD Field / DOF Model

Define field locations and compact indexing.

**AUTHORIZED 2026-11-04** by the owner's "implement and fully verify", with the
seventeen item checklist below and the recommended layout quoted. PASS
2026-11-05; the record is below the checklist. Nothing after this milestone is
authorized.

* [x] Pressure location
* [x] Velocity location
* [x] Mass-flux location
* [x] Cell-centred scalar indexing
* [x] Cell-centred vector indexing
* [x] Face-field indexing
* [x] Boundary-field indexing
* [x] `CellIndex != ElementId`
* [x] No raw mesh-ID indexing
* [x] Deterministic field ordering
* [x] Exact mesh-source binding
* [x] Remesh invalidates maps
* [x] Fluid-property edits do not invalidate topology maps
* [x] Boundary edits invalidate boundary-derived state only
* [x] No field arrays in canonical state
* [x] Regression PASS
* [x] Evidence recorded

Recommended initial layout:

```text
pressure p     cell-centred
velocity U     cell-centred
mass flux phi  face-centred
```

```text
PASS 2026-11-05. Authorized 2026-11-04 by the owner's "implement and fully
verify", with the seventeen item checklist and the recommended p / U / phi
layout quoted. Qualified against a9cd4f67 plus this milestone's eleven candidate
files: 6120/6120 x 3 presets, 2991 x 5 in two repeat stages with 14955 counted
Passed markers each, 0 warnings and 0 errors under -Werror, 17 of 17 stages exit
0, 3h03m28s, and the eight qualified subtrees identical across FOUR readings --
at the freeze, by the harness before the first build, by the harness after the
last test run, and recomputed from the working tree afterwards by the harness's
own method. 27 unit cases / 58,197 assertions and 35 compile-fail cases.

QUALIFIED ON THE SECOND ATTEMPT, and the first is preserved rather than
overwritten, under docs/verification/P19-DOF-001/qualification/attempt-1/.
Attempt 1 failed 2 of 17 stages while its LAUNCHER exited 0, which is why the
harness's own verdict and not an exit code is what gets read. The cause was
measured, not guessed: the machine ran out of Windows COMMIT -- 71.3 GB used of
an 80.8 GB limit before the build began, against about 29 GB wanted at 16
concurrent compiles -- so src/features/SolidSupport.cpp exited 1 with no
diagnostic at all and left an orphaned 0-byte %TEMP%\cc*.s behind. The dead
build took the ctest stage with it and then the repeat stage, which selected 352
tests instead of 2991 because Catch2 discovery runs at BUILD time and passed
every one of the 352; the number was reconciled exactly against the test list
(205 architecture + 93 refmod + 15 cli.refmod + 12 cli.thermal + 9
cli.structural + 8 cli.mesh + 10 gui). Not a tree defect: the same frozen tree
built clean and passed full ctest under release-ext and debug-shared-ext in that
very run. Exactly ONE line differs between the attempts, in this milestone's own
driver and outside the frozen surface -- CMAKE_BUILD_PARALLEL_LEVEL 16 -> 4 --
so qualify.cmd stays byte-identical at d313a640 for its 56th milestone, all four
gate-defining set lines are unchanged, and no source or test file was touched.
Build parallelism is a resource parameter, not a gate: ninja compiles the same
sources with the same flags and -Werror at any -j. The cost is recorded rather
than waved away -- the debug-ext build went 8.9 -> 18.3 min while ninja's edge
count rose only 786 -> 789.

The layout is a DERIVED index space (ADR-066): the Document holds no field array
and no layout, canonicalText holds neither, and a layout is rebuilt from the
topology. A cell vector is COMPONENT-BLOCKED -- component * cells + cell,
reversing P17's interleaved 3k+c -- because ADR-058 fixes SIMPLE-family
segregated coupling; the test discriminates the two on a six-cell block where
both halves of the subscript differ, after review found the earlier two-cell
fixture could not tell them apart. Identity is typed: CellIndex,
CellVectorIndex, FaceIndex and BoundaryFaceIndex are mutually
non-interconvertible, no mesh handle can subscript anything, and 35
compile-fail gates hold that at BUILD time instead of asserting it at run time.
Invalidation is three-valued rather than boolean: a remesh invalidates every
location, a patch re-attribution invalidates the boundary layout and leaves the
cells current, and a fluid-property edit invalidates nothing.

Validated against instruments the implementation does not share: the SI
dimensions against exponent sets written out BY HAND as well as against the
catalog, so the kg/s mass flux is discriminated from kg/(m^2 s); the array
lengths counted off the P16 mesh on all eight reference meshes, faces by Euler
bookkeeping; the patch partition counted off the P16 mapping, which is its only
upstream source; and the blocked subscript proved a BIJECTION onto [0, 3 cells)
by marking every slot, because a round trip alone would pass for a layout that
mapped two cells to one slot. The cross-check against CfdTopology is labelled in
the test as a comparison between two implementations, NOT an independent oracle.

Two reviews ran before the freeze, independently, and both found the same four
evidence defects -- which is the useful signal. The canonical-state proof was
VACUOUS: it interrogated a local struct the document never held, so it proved
only that canonicalText is a pure function; the analysis is now created in the
document and read back through findCfdAnalysis. The staged gate's one
distinguishing line was a NO-OP on every tested path, and its fix is verified by
MUTATION -- deleting the line now fails three assertions, where before it passed
the whole suite. A header cited a measurement nobody had made, and a test
comment described a fixture it did not build. Four coverage gaps each became a
test, two of them mutation-verified, and three assertions that merely restated
the implementation were DELETED rather than reworded, because they could not
fail.

Recorded as less than a reader might assume. No performance claim is made or
measured, and ADR-066's remark about cache lines is labelled an expectation that
no later milestone may cite as measured. No field VALUES exist yet: the result
arrays CfdResult.hpp had promised this milestone are deferred by ADR-066
Decision 1, and the two comments that promised otherwise were corrected rather
than quietly dropped. There is no cross-preset golden digest, because a layout
is derived and never persisted, so the three-preset evidence is that the same
assertions hold in each build. Six known limitations are listed, among them that
a patch re-attribution preserving every patch's size AND CAD face is not
detected -- unreachable from a CfdModel, needing hand-built MappedFace spans,
and held by a test that will FAIL if the binding is ever strengthened, which is
the intended notice -- and that an empty patch's range carries an invalid first
slot, so span arithmetic on it is undefined behaviour rather than an empty
subspan.

One PRE-EXISTING defect, found while checking this run's counts, is deliberately
NOT fixed here, being outside this milestone: two pairs of distinct Catch2 cases
share a ctest name -- MeshIssueKind_EveryKindHasAName in
tests/meshing/MeshDataTests.cpp:904 and tests/meshing/TetValidationTests.cpp:215,
and "Dependency cycles are reported and block their dependents" in
tests/features/RegeneratorTests.cpp:245 and tests/features/ValidationTests.cpp:154.
P19-GEOM-001's log carries the same two pairs, so this milestone did not
introduce them. Consequence: ctest -R on either name selects two tests, and a
failure report does not say which source file it came from. All four pass; it is
naming hygiene, not correctness, and it is the owner's to schedule.
Committed as cdd42a3, production code and evidence together; the eight
subtree hashes were read back from the commit object and each equals its
frozen value: qualified tree == committed tree.
Evidence: docs/verification/P19-DOF-001/README.md.
```

---
# P19-BC-001

## CFD Boundary Conditions

Implement canonical CFD BC definitions using `GeometryReference`.

**AUTHORIZED 2026-11-05** by the owner's "implement and fully verify", with the
twenty item checklist below and the canonical / derived split quoted. PASS
2026-11-05; the record is below the checklist. Nothing after this milestone is
authorized.

* [x] Velocity inlet
* [x] Pressure inlet if supported
* [x] Pressure outlet
* [x] No-slip wall
* [x] Slip / symmetry boundary
* [x] Zero-gradient policies
* [x] Fixed-value policies
* [x] Reference-pressure treatment
* [x] Boundary orientation verified
* [x] Boundary cardinality verified
* [x] Shared / overlapping target policy
* [x] Empty target rejected
* [x] Unresolved target rejected
* [x] No nearest-face fallback
* [x] Exact source binding
* [x] Multiple boundary patches
* [x] Deterministic BC ordering
* [x] Invalid values rejected
* [x] Regression PASS
* [x] Evidence recorded

No mesh face IDs in canonical BC intent.

Canonical BC:

```text
CfdBoundaryConditionId
type
GeometryReference
typed physical values
```

Derived:

```text
resolved boundary faces
face indices
matrix/source contributions
```

```text
PASS 2026-11-05. Authorized the same day by the owner's "implement and fully
verify", with the twenty item checklist and the canonical / derived split
quoted. Qualified against 9e76814b plus this milestone's eleven candidate files,
seven new and four registration points amended:
6184/6184 x 3 presets, 3039 x 5 in two repeat stages with 15195 counted Passed
markers each, 0 warnings and 0 errors under -Werror, 17 of 17 stages exit 0,
3h28m32s, and the eight qualified subtrees identical across FOUR readings -- at
the freeze, by the harness before the first build, by the harness after the last
test run, and recomputed from the working tree afterwards by the harness's own
method. 48 unit cases / 2,136 assertions and 16 compile-fail cases. QUALIFIED
FROM CLEAN ON THE FIRST ATTEMPT, on the harness P19-DOF-001 settled -- qualify.cmd
byte-identical at d313a640 for its 57th milestone, all four gate-defining set
lines unchanged, CMAKE_BUILD_PARALLEL_LEVEL still 4.

The assertion count is IDENTICAL in all three builds -- 2136 in 48 cases on
debug-ext, release-ext and debug-shared-ext alike -- which is a stronger
statement than three passes: a count that agreed only in Debug would mean a
branch compiled out. The whole CFD subsystem reads 509537 assertions in 228
cases on the qualified debug-ext binary, the same figure the pre-freeze
measurement gave, which independently says the tree did not move.

ADR-067 freezes the per-field treatment as a TABLE, six condition kinds against
p, U and phi, and it is the only place that table exists. FOUR policy values,
not the checklist's two: a slip wall's velocity is neither fixed nor
zero-gradient (Symmetry) and the mass flux at a pressure boundary is an outcome
(Computed). Two would have left every operator to re-derive the treatment from
the kind, and the first that forgot would admit through-flow at a symmetry
plane -- a wrong answer that still converges. A prescribed value is held PER
BOUNDARY FACE and not per condition, because a prescribed pressure varies
hydrostatically along a face that is not level and an inlet's -v n_hat varies
over a curved one.

Resolution goes through P16 ALONE and there is NO nearest-face fallback.
cfd::TargetProblem is a third copy of the three-check resolver thermal and
structural each hold -- SelectorInvalid, Unresolved, WithoutFacets -- written
out rather than shared, because cfd is layer 42 and may include neither of them
(ADR-059); fullyResolved() inspects state only, so two of those three checks
would otherwise be missing. Coverage is proved in BOTH directions: the forward
count through boundaryFacetsOf, the assigned count off the binding, and the
reverse attribution counted through P16's own sourceFaceOf. Only the third can
disagree, and the record carries all three rather than asserting the forward
count twice.

Twenty refusals, declared in phase order, nineteen of them reached by a test and
asserted BY TAG rather than by message -- bindCfdBoundaryFailure exists for
exactly that, after review found eight refusals whose tag could have been
swapped for another with every message assertion still passing. Atomic: one
refusal refuses the whole set and a failed preparation publishes nothing.

Validated against instruments the implementation does not share: an independent
barycentric containment oracle for the point locator, written out again rather
than called; centroids recomputed from the mesh nodes for the hydrostatic term;
the datum rule checked against P19-DATA-001's own pressureDatumProblem; facets
chosen by a node plane read off the mesh, never by list order, so a test cannot
pass by picking the face the implementation happened to produce. Six mutations
each fail loudly.

THIRTEEN behavioural defects were found before the freeze and all thirteen are
fixed, none deferred, six verified by MUTATION. Four came from the author's own
adversarial pass and two independent reviews then confirmed the same four; nine
more came from those reviews. The ones worth naming: the cardinality check was a
TAUTOLOGY, comparing the forward count with itself; the tangency tolerance was
unsound on a curved target, which is why a moving wall is now planar-only; an
OVER-DETERMINED pressure datum was accepted; the datum records were not
validated at all; a repeated facet was misreported as a different problem; the
seam discarded the tag it was built to expose; and indexing a claim array on a
precondition established twenty lines earlier CRASHED instead of diagnosing,
which a mutation exposed rather than a review. Two of the author's own test
assertions were found VACUOUS and replaced with an oracle, and one measurement
intended as a bound was found to be measuring the wrong sub-volume.

THREE ASSUMPTIONS OF MINE WERE WRONG AND WERE CORRECTED BY MEASUREMENT, not by
argument, and each is recorded in the evidence rather than quietly fixed: that a
geometry edit stales the Geometry reason (GeometryRevision is a mix of logical
dependency revisions, never a hash of BRep bytes, so two documents built by the
same call sequence share it whatever their dimensions); that the channel's
boundary centroids span more than half its length (measured 13.33 mm of 40 mm,
so the bound is stated at a quarter with the reason); and, in ADR-067 Decision 8
before any code followed it, that density is not a dependency of the source
binding (the hydrostatic prescribed pressure contains rho, so it is carried
exactly when gravity and a pressure-fixing condition are both present).

Recorded as less than a reader might assume. There is NO matrix, no source
vector, no operator and no solve -- those are P19-LINSOLVE-001's and later, and
src/cfd/CMakeLists.txt still records that this module links no linear algebra --
so nothing here has been shown to produce a correct flow field, only a correct
and fully attributed statement of the boundary intent. No flux-balance check and
no backflow sign claim: a pressure outlet does not constrain the sign of phi,
which is the same statement as Computed. No turbulence, temperature or species
boundary condition, there being no field for them. No performance result is
measured or claimed. Eight known limitations are listed, among them that
DegenerateFacet is UNREACHABLE and is recorded as such rather than claimed
tested, and that one name covering several patches -- permitted by ADR-063
Decision 3 -- is not demonstrated, because all eight reference bodies come out
with one name per patch; the refusal direction IS demonstrated.

L2 IS A NARROWING THIS MILESTONE'S OWN REVIEW INTRODUCED, and it is the owner's
to revisit rather than mine to settle: fixing the unsound tangency tolerance made
a moving wall PLANAR-ONLY, so a curved target is refused by name. ADR-058's
authorized uses -- a driven cavity's lid, a Couette plate -- are planar, so
nothing in v1's scope is lost, but a sliding cylindrical wall is no longer
expressible and would need a per-facet velocity field this milestone does not
have.

THE AUTHORIZATION WAS NOT RECORDED HERE BEFORE THE IMPLEMENTATION, unlike
P19-DOF-001's, which was written into TODO.md in six places before any code was
written. This file still read "P19-BC-001 NOT AUTHORIZED" at its status table
while the work was done, and the authorization and the PASS are recorded in the
same edit. The authorization itself is not in doubt -- the owner pasted this
whole section and said "implement and fully verify" -- but the repository did
not say so at the time, and a reader of the tree alone could not have told. The
evidence records it in REVISION r1 rather than tidying it away.

Two counting errors of mine were caught while reading this run's logs and are
recorded in the evidence rather than corrected silently: compile_fail.cfdbc. was
first counted at 4 of 16 by a regex that stops at a hyphen, and the three -ext
presets were first read as not inheriting BETTERCAD_WARNINGS_AS_ERRORS by a
script that followed only the first parent of an inherits list. Both were my
measurements, not the run; the second was then read where it actually binds, the
qualified build tree's own CMakeCache.txt.

The two PRE-EXISTING duplicate ctest names P19-DOF-001 reported are still there,
deliberately unfixed as outside this milestone, and carried forward as L8. All
four of those tests pass; it is naming hygiene, not correctness.
Committed as 1e0858a, production code, tests and evidence together; the eight
subtree hashes were read out of the staged tree before the commit and back out
of the commit object afterwards, and each equals its frozen value: qualified
tree == committed tree. Only .claude/ and AGENTS.md remain untracked, both
being agent tooling and so not this milestone's to commit.
Evidence: docs/verification/P19-BC-001/README.md.
```

---
# P19-IC-001

## CFD Initial Conditions

Define initial fields for transient / iterative solves.

**AUTHORIZED 2026-11-05** by the owner's "implement and fully verify", with
the twelve item checklist below quoted in full. PASS 2026-11-06; the record is
below the checklist. Nothing after this milestone is authorized.

* [x] Initial velocity
* [x] Initial pressure
* [x] Uniform-field policy
* [x] Future field-based initialization policy
* [x] SI units
* [x] Reject NaN / Inf
* [x] Source binding
* [x] Initial condition remains canonical intent
* [x] Expanded cell arrays remain derived
* [x] Exact undo / persistence semantics
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-11-06. Authorized 2026-11-05 by the owner's "implement and fully
verify", with the twelve item checklist quoted in full, and that authorization
was RECORDED HERE BEFORE ANY CODE WAS WRITTEN, which P19-BC-001's was not.
Qualified against cf6e37cc plus this milestone's nine candidate files, five new
and four registration points amended:
6240/6240 x 3 presets, 3075 x 5 in two repeat stages with 15375 counted Passed
markers each, 0 warnings and 0 errors under -Werror, 17 of 17 stages exit 0,
5h17m46s, and the eight qualified subtrees identical across SIX readings -- at
the freeze, by the harness before the first build, by the harness after the last
test run, recomputed from the working tree afterwards by the harness's own
method (scratch GIT_INDEX_FILE, read-tree HEAD, add -A, write-tree --prefix),
out of the staged tree before the commit, and back out of the commit object of
c7ce82a. The qualified tree is the committed tree.
36 unit cases / 6,589 assertions and 20 compile-fail cases. Exactly three of the
eight subtrees moved -- include, src and tests -- and they are exactly the three
this milestone edited. The harness is byte-identical to P19-BC-001's, its 58th
milestone, and it was SELF-CHECKED FIRST: verify-harness.cmd confirmed that a
failed stage gives exit 2 and that a zero-match repeat filter is counted as a
failed stage, because an unchanged harness is not the same as a working one.

The assertion count is IDENTICAL in Debug and Release -- 6589 in 36 cases on
both -- which is a stronger statement than two passes: a count that agreed only
in Debug would mean a branch compiled out. The whole CFD subsystem reads 516126
assertions in 264 cases, which is 509537 + 6589 and 228 + 36 EXACTLY, so no
pre-existing cfd assertion moved. The full-set arithmetic reconciles the same
way: 6240 = 6184 + 36 + 20, and the run's own logs give rest = 6184 in each of
the three presets, 6184 being P19-BC-001's own figure.

ADR-068 answers what ADR-066 Decision 1 deferred. P19-IC-001 is the first
milestone to produce a LAYOUT-INDEXED FIELD ARRAY -- length from
size(CfdVariable), subscript from the layout -- and the answer is that NO FIELD
STORAGE IS ADDED: expandCfdInitialField writes into a std::span<double> the
caller owns and sizes. That discharges the deferral rather than pre-empting it,
since the clause that decides is "with a use in hand" and the only consumer of
an initial field is a solve, which is later and unauthorized. It also makes
"expanded cell arrays remain derived" STRUCTURAL rather than a promise: the cfd
module owns no field memory, so there is no field for a later edit to persist by
accident, and compile-fail gates hold it. The cost is accepted and stated: every
consumer sizes its own array, and a wrongly-sized array becomes a case refused by
name rather than one that cannot be expressed.

A uniform initial field STAYS UNIFORM and gravity does not enter it, where
ADR-067 made a prescribed BOUNDARY pressure hydrostatic. The asymmetry is
deliberate: a boundary pressure is a constraint the solve must satisfy along a
face that is not level, an initial field is a starting guess whose only
requirement is to be a legal state, the record is authorized uniform by ADR-061,
and a hydrostatic expansion would make density and gravity dependencies of a
value they provably cannot change. The accepted consequence is recorded -- a
gravity-driven case starts further from its solution than it needs to.

Six refusals, declared in phase order and each asserted BY TAG rather than by
message, the lesson P19-BC-001's review recorded. Atomic: the variable and the
length are both decided before the first write, asserted on a PRE-POISONED array
in both the too-short and the too-long direction, so a refused expansion leaves
the caller's array untouched rather than part-initialised.

NINETEEN review findings were acted on, all fixed, none deferred, across three
passes -- the author's own, written down before either independent review
reported so that what they confirmed stays distinguishable from what they added,
and two independent agents given the code and the ADR but not those conclusions.
Both independent reviews then found the same two things the author's pass had
found and added the rest.

ONE SUBSTANTIVE CODE DEFECT: CfdAnalysisDefinition::mesh was omitted from the
source binding and, unlike the other six omissions, no provable safety statement
was available for it -- re-point an analysis at another mesh control and the
field reported itself CURRENT for an analysis whose array must now be a
different LENGTH. Refused by name as DefinitionNamesAnotherMesh, first in phase
order because it invalidates the length. The sibling CfdBoundarySource omits it
too; that is a pattern-level gap outside this milestone's scope and is recorded
for the owner as L3 rather than reached into.

THREE DOCUMENTATION CLAIMS WERE WITHDRAWN AS MEASURABLY FALSE, each by a review
that measured rather than argued. The silent-no-op rationale for deciding the
unknown variable before the length is false twice over: with the checks swapped
(unknown, 0) still reports the variable, and with the check removed entirely the
expansion's switch has no default, so an unrecognised enumerator falls out to a
named refusal rather than to success -- the order buys the honest TAG and nothing
more, and both comments now say that. "The fingerprint is LOSSLESS" is wrong
because it is a 64-bit FNV-1a digest; canonicalText is the lossless one, and the
strong claim moved there, where two ADJACENT SUBNORMALS are asserted to produce
different texts. And ADR-068 Decision 4's "every written slot is checked against
vectorIndex" did not hold: a review replaced CellIndex::fromValue(c) with
fromValue(cells-1-c) and ALL TESTS PASSED, because a uniform field's array is
byte-identical under any permutation of cells. The component-to-block mapping IS
pinned -- the interleaving mutation fails -- but the cell arm is not, and the ADR
now says so and names what carries it instead.

Among the rest: a CHECK guarded by its own predicate in the very block the test
file called "THE DISCRIMINATING PART", which could not fail; a premise printed
with INFO instead of asserted, where a review then MEASURED that the 3 mm remesh
keeps the cell count, so the stamp really is what stales the field; twelve NaN
and Inf rejection cases that all still passed with isFinite weakened to a
magnitude bound, because nothing asserted that a finite EXTREME is ACCEPTED;
CfdInitialFieldSource::operator== documented as the guarantee that "a dependency
added here without being compared is not possible" while being called by no test
and no production code, so that return true broke nothing; and componentSi,
a second copy of CartesianVector<Q>::component written out and defended in a
comment, deleted in favour of the accessor that already existed.

The two reviews DISAGREED once, and the disagreement was settled by measurement
rather than by seniority: the test-gap review asserted the 3 mm remesh "almost
certainly" changes the cell count, and the adversarial review measured that it
does not. The measurement won and the test asserts the measured premise.

Recorded as less than a reader might assume. FIVE of the twelve items were
LARGELY SATISFIED BEFORE this milestone opened -- initial velocity, initial
pressure, the uniform-field policy, SI units and NaN/Inf rejection all rest on
P19-DATA-001's record and ADR-061 -- and the evidence says so rather than
claiming the work. What IC-001 adds to them is one measured gap closed
(P19-DATA-001's own validate test exercised NaN in velocity.x and +Inf in the
pressure and nothing else, so it would have passed unchanged checking only x;
the new case covers four slots by three non-finite values) and the structural
guarantees that keep them true. There is NO SOLVE, so nothing here is shown to
produce a flow field -- only a legal starting state, correctly placed and
correctly bound. No performance result is measured, and there is no baseline to
measure against because nothing expanded a field before this milestone. "Exact
undo / persistence semantics" is met as the value semantics and the fingerprint,
per ADR-068 Decision 9, NOT as a round trip: there is no CFD serializer in
src/io/json at all -- thermal and structural have one, cfd does not -- and no
CfdCommands.cpp, so P19-PERSIST-001 and P19-CMD-001 remain later and
unauthorized.

SEVEN LIMITATIONS are recorded. L1: a single-cell mesh, the one case where
blocked and interleaved numbering coincide, is unreachable by construction. L2:
the cell-to-offset mapping is UNOBSERVABLE for a uniform field, and the first
milestone to write a non-uniform field will be the first able to pin it. L6: the
per-mutation pairing table did not survive the client restart that wiped the
scratchpad, and is recorded as a gap rather than filled from memory -- re-running
the sweep would modify the frozen surface and void the qualification. The
contemporaneous sweep result and the pre-fix survival measurements are in the
evidence; the pairing is not reconstructed.

TWO OF THE AUTHOR'S OWN IMPROVEMENTS WERE DECLINED AFTER THE FREEZE, with the
reason recorded rather than quietly dropped: asserting the source's stamp against
the layout's rather than the topology's, and round-tripping every written slot
through CfdFieldMap::cellOf / componentOf. Both duplicate coverage P19-DOF-001
already qualified on those same functions, and applying either would have voided
a five-hour qualification for no new claim. They are the first candidates for
whichever milestone next touches this file.

QUALIFIED ON THE THIRD ATTEMPT, and both earlier attempts stay recorded rather
than erased. ATTEMPT 1 WAS REFUSED BY THE PREFLIGHT AND THE HARNESS WAS RIGHT:
the harness had been copied by listing the previous milestone's directory through
a grep -iE 'cmd|txt|out|err' filter, which silently dropped preflight-space.ps1,
so the report that "all five harness files are byte-identical" was true and
incomplete. Nothing was built, nothing was tested and no number was produced, so
there was nothing to discard. Both preflight hashes were then checked against the
ones run-qualification.cmd records for itself. ATTEMPT 2 WAS KILLED BY A SESSION
TEARDOWN at test 5589 of 6240 in the second preset: 0x40010004 is
DBG_TERMINATE_PROCESS, which means killed and not failed, and 0xC000026B is
STATUS_DLL_INIT_FAILED_LOGOFF, which a process gets when it starts while the
window station is shutting down. The cause is in the exit codes, not in the code
under test, and no test failed and no stage reported a defect. It is VOID, and
two things from it are worth keeping, clearly labelled as from a voided run: its
debug-ext passed 6240 OF 6240, independently confirming the predicted count
before the reported attempt ran, and its repeat stages, running against a build
tree that no longer existed, SELECTED 0 TESTS AND WERE COUNTED AS FAILED -- the
P19-DOF-001 attempt-1 lesson holding under a condition nobody arranged, where
ctest's "No tests were found!!!" would otherwise have passed as success. Attempt
3 was launched DETACHED so a client restart could not take it down again.

A FOURTH INSTANCE of this project's counting-trap family occurred while writing
the evidence, and is recorded there: a check of whether the 20 compile-fail gates
ran used [A-Za-z0-9_]+, which stops at a hyphen, and the compile-fail test names
are hyphenated -- so it reported 0 of 20 on a run where all 20 had passed. The
figure was wrong; the run was not. Alongside ctest -R matching nothing and
exiting 0, the hyphen-stopping regex that counted 4 of 16, the inherits[0] walk
that read -Werror as off, and this milestone's own shutil.copy2 mtime artefact
that made a stale binary report a defect that was not there.
```

Evidence: docs/verification/P19-IC-001/README.md, with FREEZE.txt written
before the run and the qualification logs beside it. ADR-068 records the
decision and what it rejected.


---
# P19-DIFF-001

## Diffusion Operator

Implement finite-volume diffusion operator independently and verify it.

**AUTHORIZED 2026-11-06** by the owner's "implement and fully verify", with the
fourteen item checklist below quoted in full. PASS 2026-11-06; the record is
below the checklist. Nothing after this milestone is authorized.

For constant viscosity:

```text
∇·(mu ∇U)
```

* [x] Define orthogonal diffusion
* [x] Define face interpolation
* [x] Define non-orthogonal correction policy
* [x] Boundary diffusion treatment
* [x] Matrix coefficient sign convention
* [x] Conservation across internal faces
* [x] Equal/opposite internal contribution
* [x] Constant-field null behaviour
* [x] Linear-field patch test
* [x] Independent 1D diffusion oracle
* [x] Symmetry where mathematically expected
* [x] Sparse assembly
* [x] Regression PASS
* [x] Evidence recorded

```text
PASS 2026-11-06. Authorized 2026-11-06 by the owner's "implement and fully
verify", with the fourteen item checklist quoted in full, and that authorization
was RECORDED HERE BEFORE ANY CODE WAS WRITTEN, as P19-IC-001's was. Two stale
rows this author left in the status table below -- IC-001 still "IN PROGRESS"
after it had passed and been pushed, and DIFF-001 still "NOT AUTHORIZED" -- were
corrected in the same edit; that table is live, since the "kept as history"
marker sits below it.
Qualified against d46a2d1 plus this milestone's ten candidate files inside the
frozen surface, SIX NEW and four registration points amended -- counted from
git rather than from memory:
6311/6311 x 3 presets, 3124 x 5 in two repeat stages with 15620 counted Passed
markers each, 0 warnings and 0 errors under -Werror, 17 of 17 stages exit 0,
3h13m48s, QUALIFIED FROM CLEAN ON THE FIRST ATTEMPT, and the eight qualified
subtrees identical across SIX readings -- at the freeze, by the harness before
the first build, by the harness after the last test run, recomputed from the
working tree afterwards by the harness's own method (scratch GIT_INDEX_FILE,
read-tree HEAD, add -A, write-tree --prefix), out of the staged tree before the
commit, and back out of the commit object of dc5e5a1. The qualified tree is
the committed tree.
49 unit cases / 31,688 assertions and 22 compile-fail cases. Exactly three of
the eight subtrees moved -- include, src and tests -- and they are exactly the
three this milestone edited; the other five are byte-identical to P19-IC-001's
freeze. The harness is byte-identical to P19-IC-001's, its 59th milestone, and
it was SELF-CHECKED FIRST: verify-harness.cmd confirmed that a failed stage
gives exit 2 and that a zero-match repeat filter is counted as a failed stage,
because an unchanged harness is not the same as a working one.

The assertion count is IDENTICAL in Debug and Release -- 31688 in 49 cases on
both -- which is a stronger statement than two passes: a count that agreed only
in Debug would mean a branch compiled out. The whole CFD subsystem reads 547814
assertions in 313 cases, which is 516126 + 31688 and 264 + 49 EXACTLY, so no
pre-existing cfd assertion moved. The full-set arithmetic reconciles the same
way: 6311 = 6240 + 49 + 22, and 6240 is P19-IC-001's own figure, so no
pre-existing test moved either. Compile-fail is excluded from the repeat, as in
every milestone, which is why the repeat grows by 49 and not 71 -- measured
rather than assumed, and no filter was widened for this milestone.

ADR-069 records ten decisions. The operator is a PER-COMPONENT SCALAR matrix,
which follows from ADR-066 rather than being chosen here: a segregated momentum
predictor assembles no row coupling u to v, so component X's block IS the
cell-centred scalar array, laid out comp * cells + cell. The surface vector is
split S_f = E_f + T_f by the OVER-RELAXED decomposition, E_f = (A/(d.n)) d, with
T_f . S_f zero identically. The reason is the ratio that governs a
deferred-correction iteration, |T_f|/|E_f|: sin(theta) for over-relaxed, so at
or below 1 at every angle, against tan(theta) for minimum correction, which
reaches 81.8 at the 89.3 degrees ADR-063 F1 measured on a reference mesh. The
algebra was verified numerically over twenty thousand random configurations
before any C++ existed, and both wrong turns on the way to it are recorded in
Decision 2 rather than tidied away.

THE NON-ORTHOGONAL CORRECTION IS CALLER-SUPPLIED, NOT COMPUTED. The deferred arm
needs a gradient that belongs to P19-GRAD-001, so assembleCfdDiffusion accepts
one per face, measures each face's omitted fraction, and reports the worst in its
summary. With no gradient supplied the operator is a correct Laplacian only on an
ORTHOGONAL mesh, and no P16 reference mesh is one -- the best maximum is the
Block's 64.09 degrees. ADR-063 F1 calls the correction "mandatory in the
discretisation, not an option" and in the same milestone reserves any
accept-or-refuse threshold to P19-VALID-001 and the owner, so this operator
REPORTS rather than refuses, and L1 of the evidence says so as a limitation.

Nine refusals, declared in phase order and each asserted BY TAG rather than by
message, the lesson P19-BC-001's review recorded. Two of them exist because a
review found that neither caller-supplied array was checked for finiteness.
Atomic: the whole input gate runs before any geometry is read, the assembly
writes only locals, and every failure returns before CfdDiffusionSystem is
constructed, so there is no out-parameter a refused assembly could leave part
written. CfdSparseMatrix cannot be conjured -- fromCsr is the only door and the
default constructor is private -- so an unvalidated matrix does not exist, and a
system holding one by value inherits the guarantee. Compile-fail gates hold it.

TWO OF THE FOURTEEN ITEMS ARE RECORDED NARROWER THAN THEIR WORDING, and the
evidence states both rather than glossing them. "Define non-orthogonal
correction policy" is a policy and a measurement, not a correction. "Symmetry
where mathematically expected" is STRUCTURAL: gamma is computed once and
scattered four times, so largestSymmetryError() == 0.0 is a property of the
scatter, and the mutation sweep confirms it cannot see a wrong coefficient or a
wrong diagonal. What carries the numerical claim instead is the row sum and an
analytic fixture whose arithmetic is exact -- |d| = 5/4, d.n = 3/4,
cos(theta) = 3/5 -- which is also where the choice of decomposition is pinned,
because the deferred fraction must read 4/5 there and not 4/3 or sqrt(0.8).

TWENTY MUTATIONS, ALL KILLED, across three sweeps. The adversarial review
PREDICTED that eight specific source-vector mutations would survive the suite as
it then stood; all eight are killed, which is a prediction and a measurement
rather than an assertion. THE SWEEP FOUND ONE REAL GAP that neither review
could: AFaceVaryingGradientIsReadAtItsOwnFace could not detect a hard-coded face
index, because isInternal(face) is face.value() < internalFaces and the fixture's
only contributing face was therefore index 0. Three probes are reported VOID
rather than as survivors because they did not compile, which is P18-ELEM-001's
finding F3 and its recorded remedy; one was MIS-PAIRED by this author against a
test that cannot see a diagonal; one was a NO-OP inside an idempotent lambda.
Each is recorded for what it was, because counting a void probe either way
claims something that did not happen.

TWO REVIEW FINDINGS WERE DEFECTS IN WHAT THIS AUTHOR HAD WRITTEN DOWN. ADR-069
Decision 7 described an interpolation weight that did not exist -- no weight, no
call site, no test -- in the Decision that is itself the evidence for it; it was
implemented rather than reworded. And the symmetry claim was too strong; it is
now recorded as structural, and the stronger claim made to the owner was
corrected explicitly rather than left standing. A third was a test defect of the
same family: the skew case took its expected value from a test-side oracle on
both sides, so an operator dividing by |d| instead of d.n would have passed the
very test whose comment ruled that out.

AND THE AUTHOR'S OWN NEW TESTS CAUGHT TWO OF THE AUTHOR'S OWN DEFECTS BEFORE ANY
REVIEW REPORTED. The first interpolation weight, |C_N - C_f| / |C_N - C_P|, is
not a convex weight on a skewed mesh and measured 3.60954574408530382 on the
Cylinder, which is ADR-065's recorded skewness of 6.5 arriving as a number; it
was replaced by the normal-projection form. And cfdFaceViscosity was not exact
for a constant field, because w*mu + (1-w)*mu is not mu; it was replaced by the
stable lerp mu_N + w(mu_P - mu_N).

Nine limitations are recorded. L3: THERE IS NO SOLVE AND NO MOMENTUM EQUATION,
so nothing here is shown to produce a flow field -- this is one operator of an
equation that does not exist yet, and the matrix has never been handed to a
solver. L4: the input gate's boundary-currency arm is UNTESTED, because
bindCfdBoundary deliberately returns a default source and no test can reach that
arm; recorded rather than claimed, after a review found no test does. L2: no
skewness correction exists, and it needs the same missing gradient. L9: no
performance result, because nothing in cfd assembled a matrix before this
milestone and there is no baseline. The one measurement worth recording is
structural rather than temporal: nonZeros == cells + 2 * internalFaces on all
eight reference bodies, so the pattern is the connectivity and nothing is stored
twice.

A SIXTH COUNTING TRAP was met while reading the run, and is recorded beside the
five in FREEZE.txt. A grep for Skipped / Not Run / Disabled returns 8 lines per
full log and 40 per repeat, which reads like a skip and is not: every one is a
TEST NAME containing the word, four such tests, each printed twice per pass. The
figure that answers the question is the Passed-marker total -- 6311, 6311, 6311,
15620, 15620 -- because that says every selected test RAN, where "none failed"
does not.
```

---
# P19-GRAD-001

## Gradient Reconstruction

Implement qualified scalar / vector gradient reconstruction.

**AUTHORIZED 2026-11-07** by the owner's "implement and fully verify", with the
fourteen item checklist below quoted in full. **PASS 2026-11-07.** All fourteen
items are implemented, tested, independently validated, adversarially reviewed,
regression clean and recorded: 6381/6381 in each of the three presets, 3170 x 5
in two repeats with 15850 executions each, 17 of 17 stages exit 0, qualified
from clean on the first attempt, and the eight qualified subtrees identical
before the first build and after the last test run, so the qualified tree is
the committed tree. Least squares is the default and Green-Gauss is kept and
measured against it (ADR-070), the choice settled by a measurement taken before
any code existed and committed with its scripts. Nothing after this milestone
is authorized.
(docs/verification/P19-GRAD-001/README.md.)

* [x] Pressure gradient
* [x] Velocity gradient
* [x] Gauss gradient
* [x] Least-squares gradient if needed
* [x] Boundary contribution
* [x] Constant-field test
* [x] Linear-field exactness / convergence
* [x] Skew mesh behaviour
* [x] Rigid-transform covariance
* [x] SI units
* [x] Deterministic ordering
* [x] Independent analytical validation
* [x] Regression PASS
* [x] Evidence recorded

---
# P19-DIV-001

## Divergence / Flux Operator

Implement finite-volume divergence.

```text
∫_V ∇·F dV = Σ_f F_f · Sf
```

**AUTHORIZED 2026-11-07** by the owner's "Authorize DIV-001", in answer to a
question naming this milestone and no other. **PASS 2026-11-07.** The twelve
items below were the scope; they stood exactly as they did before the
authorization, because the owner neither quoted nor altered them. Nothing after
this milestone is authorized.

QUALIFIED ON THE SECOND ATTEMPT, and the reason is a process defect of this
milestone's own: attempt 1 passed before either independent review had been
run. The reviews then found five defects in the code and eight in the suite,
and fixing them changed `include`, `src` and `tests` — so that qualification
was VOID by the freeze's own rule and was run again from clean. Attempt 1's
logs are preserved under `qualification/attempt-1/` rather than erased.
(docs/verification/P19-DIV-001/README.md.)

* [x] Scalar flux divergence
* [x] Velocity divergence
* [x] Face-flux summation
* [x] Internal-face cancellation
* [x] Boundary-face contribution
* [x] Constant-field validation
* [x] Linear-field validation
* [x] Closed-volume conservation
* [x] Independent analytical validation
* [x] Determinism PASS
* [x] Regression PASS
* [x] Evidence recorded

---
# P19-CONV-001

## Convective Flux

Implement convection discretisation.

**AUTHORIZED 2026-11-07** by the owner's "Implement nd fully verify" over the
fifteen-item checklist below, pasted into the instruction. THE PASTED CHECKLIST
WAS DIFFED AGAINST THIS SECTION BEFORE ANY WORK BEGAN and is byte-for-byte
identical to the fifteen items as they already stood -- so the owner neither
quoted a different scope nor altered this one, and the items stand unchanged.
The recommended first qualified scheme, first-order upwind before higher-order
schemes, is part of that scope and was read as binding.

**PASS 2026-11-08.** All fifteen items are recorded. 17 of 17 stages exit 0;
6525/6525 tests in each of debug-ext, release-ext and debug-shared-ext; 3258
selected tests x 5 executions in each of two repeat presets, 16,290 executions
per log counted from the `Start` markers at a ratio of exactly 5.000; 0
warnings with BETTERCAD_WARNINGS_AS_ERRORS read as ON from each BUILT tree's
own cache; 4h07m07s. 52,155 test executions in all.

48 unit cases / 34,681 assertions and 28 compile-fail cases, and the assertion
count is IDENTICAL in Debug, Release and Debug-shared, so no assertion here is
configuration-dependent. The cfd subsystem reads 702,187 assertions in 447
cases, which is 667,506 + 34,681 and 399 + 48 EXACTLY, so no pre-existing cfd
assertion moved. 76 tests added and 0 removed, established by a set difference
against P19-DIV-001's own qualified log rather than by arithmetic on the
totals.

The eight qualified subtrees are identical across FOUR readings — at the
freeze, by the harness before the first build and after the last test run,
recomputed from the working tree by the harness's own method immediately
before the commit, and back out of the commit object of 10ad63a itself — so the
qualified tree is the committed tree.

**TWO INDEPENDENT REVIEWS WERE RUN BEFORE THE FREEZE**, which is the one thing
this milestone did differently from P19-DIV-001, and the reason there is no
discarded attempt here. 25 findings: six code defects, three false published
claims in ADR-072, eight test defects. The most serious was a MISSING
OWNER/NEIGHBOUR RANGE GUARD with the unvalidated SIZE_MAX index used for
WRITES — a guard P19-DIV-001 carries and whose comment records that a review
forced it there, so it was that correction REGRESSED one milestone later. The
interpolation weight was also left completely unpinned, in the same place
P19-GRAD-001 and P19-DIV-001 had each left it unpinned before. The author's own
pass found none of the six code defects and wrote three of the false claims.

Evidence: [docs/verification/P19-CONV-001/](docs/verification/P19-CONV-001/)

* [x] Define mass flux `phi`
* [x] Define interpolation scheme
* [x] First-order upwind baseline
* [x] Central scheme if in scope
* [x] Sign convention for `phi`
* [x] Internal-face conservation
* [x] Boundary treatment
* [x] Boundedness policy
* [x] Constant-field transport test
* [x] 1D advection test
* [x] Reverse-flow behavior
* [x] Sparse assembly
* [x] Determinism PASS
* [x] Regression PASS
* [x] Evidence recorded

Recommended first qualified scheme:

```text
first-order upwind
```

before higher-order schemes.

---
# P19-MOM-001

## Momentum Equation

Assemble incompressible momentum equations:

```text
rho (∂U/∂t + ∇·(U U))
=
-∇p
+ mu ∇²U
+ rho g
```

within v1 scope.

PASS 2026-11-08, on the SECOND qualification attempt. Attempt 1 failed a
release-only build error in a test helper 66m28s in, and is recorded in full
rather than discarded.

Evidence: [docs/verification/P19-MOM-001/](docs/verification/P19-MOM-001/)

* [x] Steady momentum form
* [x] Transient momentum term
* [x] Convection contribution
* [x] Diffusion contribution
* [x] Pressure-gradient contribution
* [x] Gravity/body-force contribution
* [x] Boundary contributions
* [x] Sparse global matrix
* [x] Deterministic assembly
* [x] No hidden stabilization
* [x] Independent small-system oracle
* [x] Regression PASS
* [x] Evidence recorded

---
# P19-CONT-001

## Continuity Equation

For incompressible flow:

```text
∇·U = 0
```

or finite-volume mass conservation:

```text
Σ_f rho Uf · Sf = 0
```

* [x] Define mass-flux convention
* [x] Cell continuity residual
* [x] Global mass imbalance
* [x] Boundary mass flow
* [x] Internal-face cancellation
* [x] Closed-domain check
* [x] Inlet/outlet mass-balance check
* [x] Relative/absolute tolerance
* [x] Multiple inlets / outlets
* [x] Deterministic aggregation
* [x] Regression PASS
* [x] Evidence recorded

---
# P19-PVCOUPLE-001

## Pressure–Velocity Coupling

Implement the first qualified coupling algorithm.

Recommended v1:

```text
SIMPLE
```

for steady incompressible flow.

* [x] Define predictor
* [x] Define pressure correction
* [x] Define velocity correction
* [x] Define face-flux correction
* [x] Define pressure reference
* [x] Define under-relaxation policy
* [x] Define residual evaluation
* [x] Define continuity closure
* [x] Define stopping criteria
* [x] Define maximum iterations
* [x] Reject divergence
* [x] Reject NaN / Inf
* [x] Structured solver diagnostics
* [x] Deterministic iteration behaviour
* [x] Independent simple benchmark
* [x] Regression PASS
* [x] Evidence recorded

Do not silently copy OpenFOAM implementation.

---
# P19-LINSOLVE-001

## CFD Linear Solver Infrastructure

Audit and qualify reusable sparse linear algebra.

* [x] Audit existing generic solver
* [x] Respect dependency-layer ADRs
* [x] Reuse generic lower-layer solver
* [x] Pressure-system solver
* [x] Momentum-system solver
* [x] Define tolerances
* [x] Define singular detection
* [x] Define non-finite rejection
* [x] Independent residual validation
* [x] No solver-success-only qualification
* [x] Determinism PASS
* [x] Large sparse smoke PASS
* [x] Regression PASS
* [x] Evidence recorded

---
# P19-STEADY-001

## Steady Incompressible Solver

Implement full steady incompressible CFD orchestration.

* [x] Validate inputs
* [x] Prepare mesh topology
* [x] Prepare BCs
* [x] Initialize fields
* [x] Momentum predictor
* [x] Pressure correction
* [x] Velocity correction
* [x] Flux correction
* [x] Residual computation
* [x] Mass-balance computation
* [x] Convergence loop
* [x] Maximum-iteration failure
* [x] Divergence detection
* [x] NaN / Inf rejection
* [x] Deterministic solve behavior
* [x] Atomic result publication
* [x] Structured diagnostics
* [x] Regression PASS
* [x] Evidence recorded

---
# P19-TRANSIENT-001

## Transient Incompressible Solver

Add transient Navier–Stokes integration.

Recommended initial scheme:

```text
backward Euler
```

* [x] Define initial fields
* [x] Define `dt`
* [x] Define end time
* [x] Define output times
* [x] Transient momentum term
* [x] Pressure-velocity coupling per step
* [x] Actual final partial step
* [x] Time-dependent BC policy
* [x] Time-dependent load policy
* [x] Per-step residual
* [x] Per-step mass balance
* [x] Reject invalid `dt`
* [x] Reject time ordering errors
* [x] Reject NaN / Inf
* [x] Deterministic timestepping
* [x] Time-step refinement
* [x] Steady-limit comparison
* [x] Regression PASS
* [x] Evidence recorded

---
# P19-TURB-ARCH-001

## Turbulence Architecture

Do not jump directly into a turbulence model without defining architecture.

* [ ] Define laminar/turbulent mode
* [ ] Define turbulence model interface
* [ ] Define model field ownership
* [ ] Define wall-treatment interface
* [ ] Define turbulence BC interface
* [ ] Define turbulence result fields
* [ ] Define persistence policy
* [ ] Define command policy
* [ ] Define CLI policy
* [ ] Define validation policy
* [ ] No turbulence model implemented here unless explicitly included
* [ ] Regression PASS
* [ ] Evidence recorded

---
# P19-KEPS-001

## Realizable / Standard k-epsilon

Only after turbulence architecture is qualified.

* [ ] Define model variant
* [ ] Define `k`
* [ ] Define `epsilon`
* [ ] Define turbulent viscosity
* [ ] Define production term
* [ ] Define source terms
* [ ] Define wall treatment
* [ ] Define inlet turbulence specification
* [ ] Define outlet policy
* [ ] Positivity enforcement policy
* [ ] Reject non-physical states
* [ ] Reference benchmarks
* [ ] Regression PASS
* [ ] Evidence recorded

If P19 v1 is strictly laminar, defer this whole milestone.

---
# P19-SST-001

## k-omega SST

Only if explicitly within P19 scope.

* [ ] Define `k`
* [ ] Define `omega`
* [ ] Blending functions
* [ ] Turbulent viscosity
* [ ] Near-wall treatment
* [ ] Freestream treatment
* [ ] Cross diffusion
* [ ] Positivity handling
* [ ] Reference benchmarks
* [ ] Regression PASS
* [ ] Evidence recorded

Otherwise defer.

---
# P19-POST-001

## CFD Result Recovery / Derived Fields

Recover qualified engineering outputs.

* [ ] Cell pressure
* [ ] Cell velocity vector
* [ ] Velocity magnitude
* [ ] Face mass flux
* [ ] Pressure gradient
* [ ] Velocity gradient
* [ ] Vorticity if in scope
* [ ] Wall pressure
* [ ] Wall shear stress if in scope
* [ ] Units explicit
* [ ] All results finite
* [ ] Deterministic ordering
* [ ] No GUI recomputation
* [ ] Source/currentness binding
* [ ] Regression PASS
* [ ] Evidence recorded

---
# P19-INTEGRALS-001

## Forces / Fluxes / Engineering Integrals

Implement engineering-level integrated results.

* [ ] Boundary mass flow
* [ ] Volumetric flow rate
* [ ] Pressure average
* [ ] Total-pressure average if in scope
* [ ] Pressure drop
* [ ] Surface force
* [ ] Pressure force
* [ ] Viscous force
* [ ] Moment about reference point
* [ ] Flow split
* [ ] Multiple outlets
* [ ] Deterministic aggregation
* [ ] Sign conventions frozen
* [ ] Independent analytical checks
* [ ] Regression PASS
* [ ] Evidence recorded

This milestone is critical for the manifold use case.

---
# P19-MASSBAL-001

## Mass Conservation / Flow Balance

Validate mass continuity independently from solver residuals.

* [ ] Inlet mass flow
* [ ] Outlet mass flow
* [ ] Net mass imbalance
* [ ] Cell continuity residual
* [ ] Global mass residual
* [ ] Relative tolerance
* [ ] Absolute tolerance
* [ ] Multiple inlets
* [ ] Multiple outlets
* [ ] Reverse flow
* [ ] Transient accumulation policy
* [ ] Deterministic aggregation
* [ ] Independent validation
* [ ] Regression PASS
* [ ] Evidence recorded

Hard rule:

```text
linear-solver residual PASS
does NOT imply
mass conservation PASS
```

Both gates matter.

---
# P19-VALID-001

## CFD Validation / Acceptance

Own final P19 CFD-analysis acceptance.

Every solve validates:

```text
geometry current
mesh current
mesh topology valid
CFD mesh quality acceptable
fluid current
BCs resolved
system well posed
solver converged
residual acceptable
mass balance acceptable
result finite
```

* [ ] Define CFD mesh-acceptance policy
* [ ] Reuse P16 metrics where applicable
* [ ] Define FV-specific mesh metrics
* [ ] Define invalid mesh rejection
* [ ] Define warning policy
* [ ] Define fluid validation
* [ ] Define BC validation
* [ ] Define stale-input checks
* [ ] Define solver-convergence acceptance
* [ ] Define continuity acceptance
* [ ] Define mass-balance acceptance
* [ ] Define result-finiteness checks
* [ ] Structured validation report
* [ ] Determinism PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

---
# P19-VIZ-001

## CFD Visualisation / Inspection

Critical rule:

```text
CFD visualization
=
read-only view of qualified derived P19 data
```

* [ ] CFD mesh display
* [ ] Pressure contour
* [ ] Velocity-magnitude contour
* [ ] Velocity-vector display
* [ ] Streamlines if in scope
* [ ] Pressure-gradient display
* [ ] Mass-flux display
* [ ] Boundary BC symbols
* [ ] Boundary-set highlighting
* [ ] Cell inspection
* [ ] Face inspection
* [ ] Highest / lowest pressure navigation
* [ ] Highest velocity navigation
* [ ] Transient playback
* [ ] Clear stale-result state
* [ ] Rendering does not mutate results
* [ ] GUI does not duplicate CFD physics
* [ ] SI metre → OCCT millimetre boundary verified
* [ ] Classifier cardinality verified
* [ ] Regression PASS
* [ ] Evidence recorded

---
# P19-CMD-001

## CFD Commands / Undo / Redo

Canonical history contains CFD intent only.

* [ ] Create-analysis command
* [ ] Add-BC command
* [ ] Edit-BC command
* [ ] Remove-BC command
* [ ] Fluid-settings command
* [ ] Gravity-settings command
* [ ] Solver-settings command
* [ ] Steady-settings command
* [ ] Transient-settings command
* [ ] Undo exact
* [ ] Redo exact
* [ ] Failed command atomic
* [ ] Redo invalidation correct
* [ ] `GeometryReference` preserved
* [ ] CFD result invalidated correctly
* [ ] No pressure / velocity arrays in history
* [ ] No matrices / flux arrays in history
* [ ] No cell / face IDs as canonical BC intent
* [ ] Compile-fail identity misuse where practical
* [ ] Determinism PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

---
# P19-PERSIST-001

## CFD Persistence

Persist canonical CFD intent only.

Persist:

```text
analysis definition
fluid reference
BCs
GeometryReferences
initial conditions
gravity
solver settings
steady/transient settings
```

Do NOT persist as authority:

```text
CFD mesh-local mappings
cell/face indices
matrices
pressure
velocity
mass flux
gradients
forces
mass-balance arrays
solver residuals
```

* [ ] Define P19 schema
* [ ] Persist analysis definition
* [ ] Persist fluid reference
* [ ] Persist CFD BCs
* [ ] Persist `GeometryReference`s
* [ ] Persist initial conditions
* [ ] Persist gravity
* [ ] Persist solver settings
* [ ] Persist transient settings
* [ ] Preserve units
* [ ] Preserve unresolved references
* [ ] Exclude generated CFD arrays
* [ ] Malformed files rejected
* [ ] Invalid values rejected
* [ ] Duplicate IDs rejected
* [ ] Backward compatibility
* [ ] Deterministic serialization
* [ ] Full round trip
* [ ] Save / load / re-solve
* [ ] Compare re-solved semantics
* [ ] Pre/post-solve byte identity
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

Hard gate:

```text
same canonical document saved before CFD solve
==
same canonical document saved after CFD solve

byte for byte
```

---
# P19-CLI-001

## Headless CFD Analysis

Follow existing BetterCAD CLI naming.

* [ ] Inspect CFD analysis
* [ ] Add / remove CFD BCs
* [ ] Steady solve
* [ ] Transient solve
* [ ] Pressure summary
* [ ] Velocity summary
* [ ] Mass-flow summary
* [ ] Pressure-drop report
* [ ] Flow-split report
* [ ] Mass-balance report
* [ ] Validation report
* [ ] Structured diagnostics
* [ ] Correct process exit codes
* [ ] Stale-geometry failure
* [ ] Stale-mesh failure
* [ ] Missing-fluid failure
* [ ] Invalid-BC failure
* [ ] Divergence failure
* [ ] CLI / core equivalence
* [ ] Multi-process save / load / re-solve
* [ ] No CLI-only CFD implementation
* [ ] Fresh-binary proof
* [ ] Raw structured STDOUT clean
* [ ] Zero-match protection
* [ ] End-to-end scripted workflow PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

---
# P19-REFMOD-001

## CFD Reference Models

```text
RM-CFD-01   Constant-field preservation
RM-CFD-02   Linear-gradient finite-volume patch
RM-CFD-03   Plane Poiseuille flow
RM-CFD-04   Hagen–Poiseuille pipe flow
RM-CFD-05   Lid-driven cavity
RM-CFD-06   Backward-facing step or developing channel
RM-CFD-07   Pressure-driven manifold / flow split
RM-CFD-08   Rigid-transform invariance
RM-CFD-09   Mesh-refinement study
RM-CFD-10   Transient analytical benchmark
RM-CFD-11   Mass-conservation failure
RM-CFD-12   Stale / unresolved CFD inputs
```

* [ ] Define reference suite
* [ ] Constant-field preservation
* [ ] Gradient/operator patch test
* [ ] Plane Poiseuille benchmark
* [ ] Pipe Poiseuille benchmark
* [ ] Lid-driven cavity benchmark
* [ ] Developing-flow benchmark
* [ ] Pressure-drop validation
* [ ] Flow-rate validation
* [ ] Flow-split validation
* [ ] Mass-balance validation
* [ ] Rigid-transform invariance
* [ ] Global mesh-refinement behavior
* [ ] Local-refinement behavior
* [ ] Transient analytical benchmark
* [ ] Divergence/failure benchmark
* [ ] Stale-mesh failure
* [ ] Unresolved-reference failure
* [ ] Save / load / re-solve
* [ ] CLI validation
* [ ] Independent analytical validation
* [ ] Determinism PASS
* [ ] Three-preset regression PASS
* [ ] Evidence recorded

### RM-CFD-01 — Constant Field Preservation

Uniform:

```text
U = constant
p = constant
```

must preserve:

```text
grad(p) = 0
div(U) = 0
diffusion(U) = 0
```

where appropriate.

Validate:

```text
internal-face cancellation
boundary treatment
mass conservation
```

This is the basic finite-volume null test.

### RM-CFD-02 — Linear Gradient Patch

Use:

```text
p(x,y,z) = a + bx + cy + dz
```

Validate reconstructed:

```text
grad(p) = [b,c,d]
```

on suitable mesh.

Also validate rigid rotation:

```text
grad(p)' = R grad(p)
```

### RM-CFD-03 — Plane Poiseuille Flow

Between parallel plates:

```text
u(y) =
(-dp/dx)/(2 mu)
*
(H² - y²)
```

or equivalent coordinate form.

Validate:

```text
velocity profile
centreline velocity
wall velocity = 0
volumetric flow
pressure gradient
wall shear
mass balance
```

Excellent laminar Navier–Stokes reference.

### RM-CFD-04 — Hagen–Poiseuille Pipe Flow

For circular pipe:

```text
DeltaP =
128 mu L Q / (pi D^4)
```

or:

```text
Q =
pi D^4 DeltaP / (128 mu L)
```

Validate:

```text
velocity profile
centreline velocity
bulk velocity
pressure drop
flow rate
wall shear
mass conservation
```

### RM-CFD-05 — Lid-Driven Cavity

Use published benchmark data.

Validate:

```text
centreline velocity profiles
recirculation location
mass conservation
mesh convergence
```

Freeze benchmark source/data.
Do not compare screenshots.

### RM-CFD-06 — Developing Channel / Backward-Facing Step

Use a geometry suited to testing:

```text
convection
recirculation
pressure recovery
mesh sensitivity
```

Keep Reynolds number within qualified laminar scope unless turbulence is already
qualified.

### RM-CFD-07 — Manifold / Flow Split

Use a four-port manifold-style reference model.

Validate:

```text
total inlet mass flow
outlet mass flows
flow split
pressure drop
global mass imbalance
```

This can later connect directly to the existing manifold project.

Do not use a previous OpenFOAM result as the sole oracle. Use:

```text
symmetry case
analytical restriction case
or independently verified reference solution
```

for qualification.

### RM-CFD-08 — Rigid Transform

Rigidly rotate/translate model.

Invariant:

```text
pressure scalar
velocity magnitude
mass flow
pressure drop
forces magnitude where applicable
mass balance
```

Transform:

```text
U' = R U
grad(p)' = R grad(p)
force' = R force
```

### RM-CFD-09 — Mesh Refinement

Use:

```text
coarse
medium
fine
```

for Poiseuille or another analytical case.

Validate convergence of:

```text
velocity
pressure drop
mass flow
```

Separate:

```text
field error
integral error
mass-conservation error
```

### RM-CFD-10 — Transient Benchmark

Choose an analytically tractable case, for example:

```text
start-up plane Poiseuille flow
decaying velocity field
or manufactured transient solution
```

Validate:

```text
U(t)
pressure behavior
time-step refinement
mass balance
steady limit
final partial step
```

### RM-CFD-11 — Failure / Mass-Conservation Case

Create intentionally invalid / nonconverged configuration.

Required:

```text
FAIL
```

if:

```text
continuity unacceptable
solver diverges
nonfinite fields
```

No false PASS from momentum residual alone.

### RM-CFD-12 — Stale / Unresolved Inputs

Test:

```text
stale geometry
stale CFD mesh
same-count new mesh
same-ID new source
unresolved inlet
unresolved outlet
unresolved wall
```

Expected:

```text
solve refused
no nearest-face rebinding
no current result published
```

---
# P19-QUAL-001

## Full P19 Qualification

* [ ] Freeze final P19 tree
* [ ] Audit every P19 milestone
* [ ] Verify all TODO items complete
* [ ] Verify all P19 ADRs
* [ ] Audit CAD / fluid / mesh / CFD authority
* [ ] Audit CFD result currentness
* [ ] Audit cell / face indexing
* [ ] Audit FV geometry
* [ ] Audit gradient operator
* [ ] Audit divergence operator
* [ ] Audit diffusion operator
* [ ] Audit convective flux
* [ ] Audit CFD BCs
* [ ] Audit momentum assembly
* [ ] Audit continuity equation
* [ ] Audit pressure–velocity coupling
* [ ] Audit linear solvers
* [ ] Audit steady solver
* [ ] Audit transient solver
* [ ] Audit pressure recovery
* [ ] Audit velocity recovery
* [ ] Audit mass flux
* [ ] Audit forces / pressure drop / flow split
* [ ] Audit mass balance
* [ ] Audit validation policy
* [ ] Audit stale-input protection
* [ ] Audit visualisation
* [ ] Audit commands
* [ ] Audit persistence
* [ ] Audit CLI
* [ ] Re-run all CFD reference models
* [ ] Independent analytical validation
* [ ] Momentum residual PASS
* [ ] Continuity residual PASS
* [ ] Mass balance PASS
* [ ] Poiseuille benchmark PASS
* [ ] Pressure-drop benchmark PASS
* [ ] Transient benchmark PASS
* [ ] Divergence failure PASS
* [ ] Stale-mesh failure PASS
* [ ] Clean Debug qualification
* [ ] Clean Release qualification
* [ ] Clean Debug-shared qualification
* [ ] Repeated determinism qualification
* [ ] Cross-preset equivalence
* [ ] Final adversarial review
* [ ] Zero-match protection PASS
* [ ] 0 unexpected warnings
* [ ] Qualified tree == committed tree
* [ ] Evidence recorded
* [ ] Mark P19 qualified

### P19 Final Gate

```text
all P19 milestones PASS

+ CAD geometry remains authority
+ canonical fluid/material model remains authority
+ CFD mesh remains derived
+ CFD fields remain derived

+ stale geometry rejection PASS
+ stale mesh rejection PASS
+ same-count stale source rejection PASS
+ same-ID stale source rejection PASS

+ compact cell/face indexing PASS
+ no raw mesh-ID indexing PASS

+ cell volume / centroid PASS
+ face area / normal PASS
+ owner/neighbour topology PASS
+ closed-cell geometric conservation PASS

+ gradient operator PASS
+ divergence operator PASS
+ diffusion operator PASS
+ convection operator PASS

+ CFD BCs PASS
+ GeometryReference mapping PASS
+ no nearest-face rebinding PASS

+ momentum assembly PASS
+ continuity assembly PASS

+ pressure–velocity coupling PASS

+ linear solver residual validation PASS

+ steady solver PASS
+ nonlinear convergence PASS

+ transient solver PASS
+ timestep validation PASS
+ final partial timestep PASS

+ pressure field PASS
+ velocity field PASS
+ mass flux PASS

+ pressure drop PASS
+ mass flow PASS
+ flow split PASS
+ force integration PASS where in scope

+ momentum residual PASS
+ continuity residual PASS
+ global mass balance PASS

+ CFD mesh-quality acceptance policy explicit

+ visual inspection PASS
+ visualisation remains read-only

+ undo/redo PASS
+ generated CFD arrays excluded from history

+ persistence PASS
+ generated CFD arrays excluded as authority
+ pre/post-solve byte identity PASS

+ CLI PASS
+ CLI/core equivalence PASS
+ raw structured STDOUT clean PASS
+ fresh-binary proof PASS

+ reference models PASS
+ independent analytical validation PASS
+ Poiseuille benchmark PASS
+ rigid-transform PASS
+ mesh refinement PASS
+ transient benchmark PASS

+ expected divergence/failure model PASS
+ stale/unresolved model PASS

+ Debug PASS
+ Release PASS
+ Debug-shared PASS

+ determinism PASS
+ cross-preset equivalence PASS
+ adversarial review PASS
+ zero-match protection PASS
+ 0 unexpected warnings

+ qualified source/test tree == committed source/test tree
```

---
# P18 Carried Constraints

Inherited on entering P18, and not P18's to lift without an explicit scope
decision:

```text
1. P16's VolumeMesh currentness is re-checked at the solver entry point
   (requireThermalModel, ADR-051). Never trust a cached mesh because a
   caller handed it over.

2. Configuration-sensitive geometry REFUSES a stale configuration state.
   P18 inherits the refusal through P16's geometry boundary (carried
   defect 2).

3. P16 quality thresholds are report-only. P18-VALID-001 owns the thermal
   mesh-acceptance policy, as P17-VALID-001 owned the structural one.

4. P16 REFUSES a body with more than one solid. P18 is single-solid, which
   bears on RM-THERM-03 (ADR-049).

5. A drilled-hole feature provides no stable hole-wall reference. Do not
   fake load or boundary-condition targeting for unsupported topology.

6. This MinGW toolchain has no ASan/UBSan. Record the limitation.

7. A clean checkout cannot re-fetch dependencies here; clean-tree checks
   configure from the same SHA256-verified local sources.

8. The project has no licence. A solver dependency's licence is an owner
   decision (P18-SOLVE-001, as P17-SOLVE-001).

9. thermal (45) may not include structural (50) (ADR-050). P17's solver is
   reused by extraction below 45 or by an independent private link; a hand
   copy is refused.

10. P17's findings F1, F2 and F3 stand unchanged. A thermal flux
    concentration on Tet4 inherits F2's mechanism and F3's cause.
```

---

# P17 Carried Constraints

Inherited on entering P17, and not P17's to lift without an explicit scope
decision:

```text
1. P16's VolumeMesh currentness must be re-checked at the solver entry point.
   Never trust a cached mesh because a caller handed it over.

2. Configuration-sensitive geometry currently REFUSES a stale configuration
   state. P17 respects the same refusal until configuration regeneration is
   fixed. (Carried defect 2.)

3. P16 quality thresholds are report-only. P17-VALID-001 owns the structural
   mesh-acceptance policy.

4. P16 REFUSES a body with more than one solid. Initial P17 therefore stays
   single-solid unless a prerequisite architecture change is authorized.

5. A drilled-hole feature provides no stable hole-wall reference. Do not fake
   load or restraint targeting for unsupported topology.

6. This MinGW toolchain has no ASan/UBSan. Record the limitation; do not
   pretend sanitizer qualification exists.

7. A clean checkout cannot re-fetch dependencies here -- CMake's bundled curl
   carries no CA trust anchors -- so clean-tree checks configure from the same
   SHA256-verified local sources. The source tree is verified; the download
   path is not.

8. The project has no licence. A solver dependency's licence is an owner
   decision, not an implementation detail. (P17-SOLVE-001.)
```

---

# Future Phases

```text
P18  Thermal Analysis
P19  CFD Integration
P20  Design Optimization
P21  Semantic Topology
P22  Versioning / Collaboration
P23  Python / Automation
P24  AI Engineering Agent
P25  Manufacturing / CAM
P26  Performance / GPU / Scale
P27  Production Hardening
P28  BetterCAD 1.0
```

---

# Workflow

```text
UNDERSTAND
→ ARCHITECT
→ BLAST RADIUS
→ IMPLEMENT
→ TARGETED TESTS
→ INDEPENDENT VALIDATION
→ FAILURE PATHS
→ PERSISTENCE
→ DETERMINISM
→ ADVERSARIAL REVIEW
→ FULL REGRESSION
→ EVIDENCE
→ [x]
→ COMMIT
→ PUSH
```

If a gate fails:

```text
STOP
→ reproduce
→ regression test
→ root cause
→ fix
→ revalidate
```

Never mark work complete because it merely compiles.

---

# Project Authority

```text
TODO.md
→ current authorized work

ROADMAP.md
→ long-term direction + completed phases

ARCHITECTURE.md
→ architecture / invariants

CLAUDE.md
→ engineering process / Definition of Done

docs/architecture/decisions/
→ durable ADRs

docs/verification/<milestone>/
→ qualification evidence

docs/engineering/
→ reusable engineering templates
```

---

# CURRENT NEXT STEP

```text
P19-TOPO-001 — Finite-Volume Topology has PASSED, 2026-10-31. Authorized
2026-10-31 by the owner's "implement this and fully verify" with the twenty-two
item checklist quoted, and qualified from clean on the first attempt.

    all 22 checklist items are recorded, two of them narrower than their wording
      and one collapsing onto another, each stated in the evidence: a duplicate
      internal face is not representable (the key is a sorted node triple), so
      what is rejected is the two reachable attempts; "sparse mesh IDs" is
      interleaving through the real builder plus a far-outside lookup, because
      MeshBuilder allocates from one counter; "no max-ID-sized arrays" is proved
      by review and negative lookups, NOT by measuring allocation
    four index spaces, four types: CellIndex, FaceIndex, BoundaryFaceIndex and
      PatchIndex are dense and 0-based, an ElementId is sparse and 1-based, and
      zero means "the first cell" in one space and "invalid" in the other, so the
      separation rests on the type system and not on naming (ADR-064)
    possession is the proof: the constructor is private with two friends, and the
      eight refusals are a subset of what ADR-063's gate already refuses, so none
      can fire on a mesh holding a CfdMeshContract
    two refusals were ADDED during adversarial review, not documented away:
      faceNodes promised outward winding unconditionally while the builder checked
      neither cell volume nor shared-face orientation, and P19-GEOM-001 is told to
      build its area vectors on that guarantee. NonPositiveCellVolume and
      InconsistentFaceOrientation now enforce it, each with a fixture built to
      fail it
    a test-gap review found eight claims with no evidence behind them, each
      corrected: the structural assertions ran on one model each, leaving the
      transformed body untested, so checkStructure now runs every structural
      claim on all eight reference meshes; cellByElement_'s sortedness was
      assumed by lower_bound and never asserted; patchFace was only checked where
      the expected value coincided with the index passed in; the ADR's motivating
      sparse mesh cannot be built by MeshBuilder; the "independent" census is an
      independent transcription, not an independent method; Decision 7's subset
      mapping was wrong in detail; patchFace returned a bare SIZE_MAX; and
      equality was never shown to discriminate
    independent validation: the Tet4 identity 4C == 2I + B on all eight reference
      meshes, every internal-face count equal to the F1 column P19-MESH-001
      recorded from two other code paths, and the census's 946 + 140 = 1086 faces
      for the Cylinder matched by the winding test's own independent count
    qualification, Sat 31/10/2026 04:02:35 -> 06:13:51 (2h11m16s): 17 of 17
      stages exit 0, qualify.cmd exit 0. Three presets configured, cleaned, built
      and run unfiltered at 5998/5998 each; the two 5x repeat stages at a counted
      selection of 2839 with 14195 per-test Passed markers each; 0 warnings and
      0 errors under -Werror; 36 unit cases / 108,373 assertions and 23
      compile-fail cases, with the 28 cfdids cases and 205 architecture tests
      unchanged and passing; the eight qualified subtrees identical before the
      first build, after the last test run and in the committed tree
    the harness is byte-identical to P19-MESH-001's (qualify.cmd d313a640, as
      since P12), so the only difference between the two qualifications is the
      tree under test

P19-MESH-001 — CFD Volume / Surface Mesh Contract has PASSED, 2026-10-31.
Authorized 2026-10-28; qualified by attempt 2, after attempt 1 failed
environmentally on 2026-10-28 and the qualification machine was then replaced.

    all 24 checklist items are recorded: Determinism PASS, Regression PASS and
      Evidence recorded closed on attempt 2
    the milestone's critical question is answered — P16's Tet4 mesh IS the
      finite-volume mesh, with no adapter and no second geometry authority — and
      what P16 does not guarantee is proved by a staged gate,
      requireCfdMesh(CfdModel) -> CfdMeshContract, with nine named refusals,
      possession as the proof, and the contract bound to the mesh stamp (ADR-063)
    review closed before the gate: 6 author questions each resolved by a fix and a
      test; an independent reviewer at 0 blockers, 1 major (a shared face traversed
      the same way by both cells passed, so InconsistentFaceOrientation was added),
      4 minor, 8 nits; re-verification at 0 new major or minor; a second read on
      another machine at 0 defects; 31 of 31 mutation probes killed, 0 survived
    attempt 2, Fri 30/10/2026 23:04:51 -> Sat 31/10/2026 01:50:59 (2h46m08s):
      17 of 17 stages exit 0, qualify.cmd exit 0. Three presets configured,
      cleaned, built at 780/780 edges and run unfiltered at 5939/5939 each; the two
      5x repeat stages at a counted selection of 2803 with 14015 per-test Passed
      markers each; 0 warnings and 0 errors over 755 objects; the eleven
      GUI-dependent tests present in every preset, which is what makes 5939 rather
      than 5928 the proof that the environment is whole; and the CfdMesh census and
      quality text byte-identical across all three presets
    the twelve frozen identities — eight subtree hashes and four milestone
      fingerprints — unchanged before the first build, after the last test and
      after all post-run verification; qualify.cmd still d313a640
    the environment changed and was qualified first: the previous machine was
      permanently replaced, and the replacement reproduces the pinned toolchain
      from the same WinGet WinLibs package (GCC 16.1.0, with CMake 4.4.1 and Ninja
      1.13.2 from that same bundle), with Qt 6.11.2, OCCT 8.0.1, Netgen 6.2.2604
      and zlib 1.3.1 built by the repository's own hash-verified superbuild
      (ENVIRONMENT-MIGRATION.md)
    one post-run check failed and was fixed in the CHECKER, not the product:
      postrun-mesh.ps1 compared stdout across presets without pinning a seed, on a
      binary whose case order defaults to rand, so the comparison could pass or
      fail by luck. --rng-seed 1 — the convention the P12 evidence scripts
      already use — makes three runs on three presets byte-identical and the
      check strictly stricter. The owner accepted the fix before any mark moved
    OPEN, owner finding F1: the eight P16 reference meshes are poor finite-volume
      meshes (non-orthogonality to 89.28 deg, skewness to 6.51). The topology
      passes everywhere, so the non-orthogonal correction is MANDATORY in the
      solver, P19-VALID-001 may refuse some of these meshes, and a benchmark will
      need finer P16 sizing or an owner-decided FV-quality option in P16 — never a
      CFD-side remesh. This milestone sets no threshold.

P19-GEOM-001 PASSED 2026-10-31, authorized the same day by the owner's "start"
with the twenty-one item checklist quoted: 6058/6058 x 3 presets, 2964 x 5 in
two, 17 of 17 stages exit 0, and all twenty-one items recorded. P19-DOF-001
was authorized on 2026-11-04 and PASSED 2026-11-05, and
P19-BC-001 was authorized and PASSED on 2026-11-05, and P19-IC-001 was
authorized on 2026-11-05 and is in progress; nothing after it is authorized. P19-MESH-001's owner finding F1 stands unchanged above, and
this milestone neither resolves it nor adds to it: it measured the same eight
meshes -- non-orthogonality and skewness among them -- and set no threshold,
because its summary measures and judges nothing. P18's F1-F3 and P17's F1-F3
stand as owner decisions.
```

Evidence: [docs/verification/P19-GEOM-001/](docs/verification/P19-GEOM-001/README.md),
[docs/verification/P19-TOPO-001/](docs/verification/P19-TOPO-001/README.md) and
[docs/verification/P19-MESH-001/](docs/verification/P19-MESH-001/README.md).

## What is authorized, and what is not

```text
P18              QUALIFIED 2026-10-27 by P18-QUAL-001: eighteen milestones
                 PASS (345 boxes), 5804/5804 x 3, 2561 x 5 in two, 0 warnings
                 over 743 objects, 17 stages exit 0, qualified tree ==
                 committed tree. Committed as b5dce07, recorded as 4334bf8.
                 The chain, the figures and the evidence links are in
                 ROADMAP.md and docs/verification/P18-QUAL-001/.

P19              AUTHORIZED 2026-10-27 by the owner's "start P19-ARCH-001",
                 with the open P18 findings F1-F3 as inputs. The phase plan
                 is recorded in this file; authorization is per MILESTONE,
                 not per phase.

P19-ARCH-001     PASS 2026-10-27. 5826/5826 x 3, 2583 x 5 in two, 0 warnings
                 over 745 objects, 17 stages exit 0. Committed as 42a3551,
                 recorded as 24a1496. ADR-058, ADR-059, ADR-060. Evidence
                 recorded.

P19-DATA-001     PASS 2026-10-27. 5893/5893 x 3, 2622 x 5 in two, 0 warnings
                 over 750 objects, 17 stages exit 0. Committed as f08019a,
                 recorded as c8b53f6. ADR-061. Evidence recorded.

P19-FLUID-001    PASS 2026-10-28. 5919/5919 x 3, 2783 x 5 in two, 0 warnings
                 over 753 objects, 17 stages exit 0. Committed as 4e57b02,
                 recorded as b1f151c. ADR-062. Evidence recorded.

P19-MESH-001     PASS 2026-10-31. 5939/5939 x 3, 2803 x 5 in two with 14015
                 counted per-test Passed markers each, 0 warnings over 755
                 objects, 17 stages exit 0, 2h46m08s. Qualified by attempt 2
                 on a replacement machine, itself qualified in its own right
                 first (ENVIRONMENT-MIGRATION.md); attempt 1 of 2026-10-28
                 FAILED 5 of 17 stages environmentally and stays recorded.
                 The twelve frozen identities unchanged before and after.
                 Committed as 189b87f; all eight subtree hashes of that commit
                 equal the frozen fingerprint, checked before and after the
                 commit: qualified tree == committed tree. ADR-063. Evidence
                 recorded.

P19-TOPO-001     PASS 2026-10-31. 5998/5998 x 3, 2839 x 5 in two with 14195
                 counted per-test Passed markers each, 0 warnings and 0 errors
                 under -Werror, 17 stages exit 0, 2h11m16s. Qualified from clean
                 on the first attempt, on the harness byte-identical to
                 P19-MESH-001's. 36 unit cases / 108,373 assertions, 23
                 compile-fail cases; the 28 cfdids cases and 205 architecture
                 tests unchanged. The eight qualified subtrees identical before
                 the first build, after the last test run and in the committed
                 tree: qualified tree == committed tree. Committed as a075abe,
                 production code and evidence together, the eight subtree hashes
                 read back from the commit object. Two independent reviews each
                 found a real defect and both are fixed; two checklist items are
                 recorded narrower than their wording and one collapses onto
                 another. ADR-064. Evidence recorded.

P19-GEOM-001     PASS 2026-10-31. Authorized the same day by the owner
                 ("start", with the twenty-one item checklist quoted). All
                 twenty-one items recorded: 6058/6058 x 3 presets, 2964 x 5 in
                 two with 14820 Passed markers each, 17 of 17 stages exit 0, 0
                 warnings under -Werror, 31 unit cases / 337,203 assertions and
                 29 compile-fail cases, and the four unchanged CFD groups and
                 205 architecture tests passing. The eight qualified subtrees
                 identical before the first build, after the last test run and
                 recomputed independently afterwards. Two reviews each found a
                 real defect -- a compile-fail control that did not build while
                 ctest showed its group green, and a claim written without the
                 work behind it -- both fixed before the freeze. Committed as
                 9d2b1ed, production code and evidence together; the eight
                 subtree hashes read back from the commit object each equal
                 their frozen value: qualified tree == committed tree.
                 ADR-065. Evidence recorded.

P19-DOF-001      PASS 2026-11-05. Authorized 2026-11-04 by the owner
                 ("implement and fully verify", with the seventeen item
                 checklist and the recommended p / U / phi layout quoted). All
                 seventeen items recorded: 6120/6120 x 3 presets, 2991 x 5 in
                 two with 14955 Passed markers each, 17 of 17 stages exit 0, 0
                 warnings under -Werror, 27 unit cases / 58,197 assertions and
                 35 compile-fail cases. The eight qualified subtrees identical
                 across four readings -- at the freeze, before the first build,
                 after the last test run, and recomputed from the working tree
                 afterwards. Qualified on the SECOND attempt: attempt 1 failed
                 2 of 17 stages on measured commit exhaustion while its
                 launcher exited 0, and is preserved rather than erased; one
                 line of this milestone's own driver differs between the
                 attempts and no frozen file does. Two independent reviews
                 found the same four evidence defects -- a vacuous
                 canonical-state proof and a no-op staged-gate line among them
                 -- all fixed before the freeze, two verified by mutation.
                 Committed as cdd42a3, production code and evidence together; the
                 eight subtree hashes read back from the commit object each
                 equal their frozen value: qualified tree == committed tree.
                 ADR-066. Evidence recorded.

P19-BC-001       PASS 2026-11-05. Authorized the same day by the owner
                 ("implement and fully verify", with the twenty item checklist
                 and the canonical / derived split quoted). All twenty items
                 recorded: 6184/6184 x 3 presets, 3039 x 5 in two with 15195
                 Passed markers each, 17 of 17 stages exit 0 from clean on the
                 FIRST attempt, 0 warnings under -Werror, 48 unit cases / 2,136
                 assertions and 16 compile-fail cases -- the same 2,136 in all
                 three builds, so no assertion is configuration-dependent. The
                 eight qualified subtrees identical across four readings -- at
                 the freeze, before the first build, after the last test run,
                 recomputed from the working tree afterwards, and read out of
                 the staged tree and the commit object of 1e0858a — six in
                 all. Resolution
                 goes through P16 alone with no nearest-face fallback, coverage
                 is proved in both directions with the reverse attribution
                 counted through P16's own sourceFaceOf, and the per-field
                 treatment is a frozen table of six kinds against p, U and phi.
                 Thirteen behavioural defects found before the freeze -- four by
                 the author's own pass, which two independent reviews confirmed,
                 and nine more by those reviews -- all fixed, six verified by
                 mutation; a tautological cardinality check and a crash where a
                 claim array was indexed on a stale precondition among them.
                 Three assumptions of the author's were corrected by
                 measurement, not argument, and are recorded. No matrix, no
                 solve and no performance claim; DegenerateFacet is unreachable
                 and recorded as such; L2 is a narrowing this milestone's own
                 review introduced, a moving wall being planar-only, and it is
                 the owner's to revisit. The authorization was not recorded in
                 this file before the implementation, and the evidence says so.
                 ADR-067. Evidence recorded.

P19-IC-001       AUTHORIZED 2026-11-05 by the owner's "implement and fully
                 verify", with the twelve item checklist quoted in full. PASS
                 2026-11-06, all twelve items recorded (code and evidence
                 c7ce82a). This authorization was written here BEFORE any
                 implementation, which P19-BC-001's was not. 6240/6240 x 3
                 presets, 3075 x 5 in two repeats, 17 of 17 stages exit 0, and
                 the eight qualified subtrees identical across six readings.
                 ADR-068: no field storage is added, the expansion writes into
                 the caller's array. Nineteen review findings acted on; three
                 documentation claims withdrawn as measurably false. Qualified
                 on the third attempt, the two earlier ones void for a
                 preflight refusal and a session teardown, both recorded.
                 Evidence recorded.

P19-DIFF-001     AUTHORIZED 2026-11-06 by the owner's "implement and fully
                 verify", with the fourteen item checklist quoted in full. PASS
                 2026-11-06, all fourteen items recorded. This authorization was
                 written here BEFORE any implementation, as P19-IC-001's was.
                 6311/6311 x 3 presets, 3124 x 5 in two repeats, 17 of 17
                 stages exit 0, qualified from clean on the first attempt, and
                 the eight qualified subtrees identical across six readings
                 (code and evidence dc5e5a1).
                 49 unit cases / 31,688 assertions, the count IDENTICAL in
                 Debug and Release; 22 compile-fail gates. ADR-069: a
                 per-component scalar matrix, an over-relaxed decomposition,
                 and the non-orthogonal correction CALLER-SUPPLIED and its
                 omission measured rather than computed. Twenty mutations
                 killed, one of which found a real test gap. Two review
                 findings were defects in this author's own written claims --
                 a Decision describing code that did not exist, and a symmetry
                 claim too strong -- and both are corrected rather than
                 reworded. Evidence recorded. Everything after it is still the
                 owner's decision alone.

P19-GRAD-001     AUTHORIZED 2026-11-07 by the owner's "implement and fully
                 verify", with the fourteen item checklist quoted in full. PASS
                 2026-11-07, all fourteen items recorded. This authorization was
                 written here BEFORE any implementation, as P19-DIFF-001's was.
                 6381/6381 x 3 presets, 3170 x 5 in two repeats with 15850
                 executions each, 17 of 17 stages exit 0, qualified from clean
                 on the first attempt, and the eight qualified subtrees
                 identical across six readings (code and evidence 6c0ea6a).
                 46 unit cases / 82,762 assertions, the count IDENTICAL in
                 Debug and Release; 24 compile-fail gates. The cfd subsystem
                 reads 630576 assertions in 359 cases, exactly 547814 + 82762
                 and 313 + 46. ADR-070: BOTH methods kept, weighted least
                 squares the default and Green-Gauss measured against it, the
                 choice settled by the divergence identity measured to 2.900e-16
                 over 20,000 tetrahedra BEFORE any code existed -- and the
                 scripts behind that number are committed, because a review
                 found the measurement had no artifact. Five code defects found
                 by review, among them a non-finite gradient returned as a
                 success and a FALSE REFUSAL of a solvable cell that only an
                 independent eigenvalue oracle could see. 18 of 20 mutations
                 killed over five passes and 2 retired with reasons; the first
                 pass killed 10, and four of its seven survivors were real
                 defects in the suite. One ADR prediction was contradicted
                 twice and the Decision was corrected rather than reworded.
                 Nothing consumes this output yet (L1): the face interpolation
                 that would close P19-DIFF-001's L1 is not in this checklist.
                 Evidence recorded.

P19-DIV-001      PASS 2026-11-07. Twelve of twelve items. AUTHORIZED
                 2026-11-07 by the owner's "Authorize DIV-001", in answer to a
                 question naming this milestone and no other; the authorization
                 was written here BEFORE any implementation, as P19-GRAD-001's
                 was. The scope was the twelve item checklist already in this
                 file, which the owner did not quote and did not alter.
                 17 of 17 stages exit 0, 6449/6449 x 3 presets, 3210 x 5 in
                 two repeats, 0 warnings under -Werror, 3h00m04s, 40 unit
                 cases / 36,930 assertions IDENTICAL in Debug, Release and
                 Debug-shared, 28 compile-fail cases, ADR-071.
                 QUALIFIED ON THE SECOND ATTEMPT, and the first attempt's PASS
                 is VOID rather than quietly reused: it was reached before
                 either independent review had run, which is this milestone's
                 own first defect. The reviews found five code defects and
                 eight suite defects and converged independently on the two
                 sharpest — an interpolation weight that was COMPLETELY
                 UNPINNED, because every internal face under an assertion has
                 w = 2/3 exactly, together with an oracle that was a
                 character-for-character copy of the production expression it
                 was supposed to check; and an exact-cancellation claim pinned
                 only where it is a tautology. Fixing them changed source and
                 tests, so the qualification was re-run from clean.
                 ADR-071 Decision 6 asserted the weight had "exactly one
                 definition" and there were three; the ADR carries a dated
                 amendment rather than a rewording, and the gradient's
                 surviving private copy is recorded as L11. 22 mutations
                 killed in all. The design was measured before it was written
                 and the measurement contradicted it: a bit-exact global sum
                 was predicted and measured at 40.34%, so the imbalance is
                 REPORTED and never asserted to be zero (L3).
                 Nothing consumes this divergence yet (L1). Evidence
                 recorded.
P19-CONV-001     PASS 2026-11-08. Fifteen of fifteen. A face mass-flux
                 producer and an upwind / central convection operator
                 assembling into CfdSparseMatrix, with a twelve-phase refusal
                 gate and a tagged error channel. 17 of 17 stages exit 0,
                 6525/6525 x 3 presets, 3258 x 5 x 2 repeats (16,290
                 executions per log, ratio exactly 5.000), 0 warnings,
                 4h07m07s, 52,155 test executions. 48 unit cases / 34,681
                 assertions, identical in all three configurations, and 28
                 compile-fail cases; the cfd subsystem's 702,187 assertions in
                 447 cases are 667,506 + 34,681 and 399 + 48 EXACTLY, so no
                 pre-existing cfd assertion moved. Qualified tree == committed
                 tree across four readings, 10ad63a among them.
                 TWO INDEPENDENT REVIEWS RAN BEFORE THE FREEZE -- the defect
                 P19-DIV-001 recorded against itself -- and found 25 issues:
                 six code defects, three false claims in ADR-072, eight test
                 defects. A missing owner/neighbour range guard let an
                 unvalidated SIZE_MAX index reach WRITES: P19-DIV-001's own
                 review had forced that guard, and it had regressed. The
                 interpolation weight was unpinned in the same place
                 P19-GRAD-001 and P19-DIV-001 had each left it unpinned. The
                 author's own pass found none of the six and wrote three of
                 the false claims.
                 Boundedness is CONDITIONAL and the condition is not enforced
                 (L3); Central is unbounded and nothing corrects it (L4).
                 Nothing consumes this operator yet (L1) and there is no
                 solve (L2). Evidence recorded.
P19-MOM-001      PASS 2026-11-08, ON THE SECOND ATTEMPT. Thirteen of
                 thirteen. The momentum assembly is the SUM OF TWO ALREADY
                 QUALIFIED OPERATORS plus a backward-Euler transient term, a
                 caller-supplied pressure gradient and a constant body force,
                 behind a fourteen-phase refusal gate with a tagged error
                 channel. It is the first P19 milestone to CONSUME its
                 predecessors, which answers the limitation P19-GRAD-001,
                 P19-DIV-001 and P19-CONV-001 each recorded against
                 themselves. The two sub-matrices' sparse patterns are
                 VERIFIED IDENTICAL rather than assumed before they are
                 summed. 17 of 17 stages exit 0, 6601/6601 x 3 presets,
                 3306 x 5 x 2 repeats (16,530 executions per log, ratio
                 exactly 5.000), 0 warnings, 3h43m06s, 52,863 test
                 executions. 48 unit cases / 14,230 assertions, identical in
                 all three configurations, and 28 compile-fail cases; the cfd
                 subsystem's 716,417 assertions in 495 cases are 702,187 +
                 14,230 and 447 + 48 EXACTLY, so no pre-existing cfd
                 assertion moved. Qualified tree == committed tree across four
                 readings, b9842f2 among them, and equal to the eight its
                 freeze recorded in advance.
                 ATTEMPT 1 FAILED AND THE FAILURE WAS A PROCESS FAILURE, NOT
                 A DEFECT THE GATES MISSED. A -Werror=array-bounds rejection
                 at -O3 on a pop_back() the optimizer cannot prove safe was
                 found and fixed in one test helper BEFORE that freeze, and
                 recorded as having caught the class; a second helper thirty
                 lines below carried the same defect, was written after the
                 only release-ext build, and broke the Release build 66m28s
                 into the gate. The published note had argued those sites
                 safe because "no access follows them" -- but pop_back() IS
                 the access, so the argument was false and the sites it
                 excused were never compiled. The wrong paragraph is kept
                 with a correction under it. The launcher reported exit code
                 0 while the harness reported two failed stages, so the
                 reading rule earned itself again. TWO INDEPENDENT REVIEWS
                 RAN BEFORE THE FIRST FREEZE, 42 findings, all resolved: the
                 sharpest two were that seven static_asserts constrained
                 nothing because the assembly computed in raw doubles, and
                 that the milestone's central claim -- convection and
                 diffusion meeting with a PLUS -- had no independent check,
                 every assertion having written the production expression.
                 24 mutation probes applied to the real tree, 22 killed (two
                 BY THE COMPILER, both only possible because of the typed
                 arithmetic fix), two honest survivors and one no-op control;
                 the one real survivor led to a new test, and two of its
                 fixtures failed on their own premises first. Five
                 measurement-tool defects of my own were found and published
                 rather than quietly fixed. No field is produced and nothing
                 is solved (L1, L2). Evidence recorded.
```

The text below is the standing record as of P18-QUAL-001 (2026-10-27) and is kept as history.

```text
P18 — Thermal Analysis is QUALIFIED. P18-QUAL-001 PASS, 2026-10-27.

    eighteen milestones audited from their own recorded verdicts, 18 PASS (345 boxes)
    127 ADR clauses checked against the code: 122 implemented, 5 text drifts,
      0 violated; 330 requirement rows read against code and tests
    three defects found at boundaries and fixed, each reproduced first: a writer
      that emitted a file the reader refuses; a mesh metric that nobody judged;
      a stale result that could be labelled Current by omission
    seven final cross-milestone gates (tests/qualification/P18ThermalQualificationTests.cpp)
    28 production mutants: 27 killed, 1 unreachable defensive branch
    independent verification of fresh CLI payloads against closed forms, on three
      presets, with a negative control; a second independent adversarial reviewer
    qualification: 5804/5804 x 3 presets, 2561 x 5 in two, 0 warnings over 743
      objects, 17 stages exit 0, 6h50m37s, fingerprints before == after,
      20 documents, 59 payloads and 72 evidence lines identical across presets
    OPEN, owner decisions: F1 the balance gate refuses correct transient runs once
      the time step is small (a missing rounding term; fixing it loosens a
      tolerance); F2 no bound on a temperature; F3 no bound on the residual
      tolerance a user may set; see docs/verification/P18-QUAL-001/LIMITATIONS.md

There is no open milestone and nothing is authorized. P19 — CFD Integration is
next in the sequence and is NOT AUTHORIZED; P18 being qualified is not
permission to start it.
```

Evidence: [docs/verification/P18-QUAL-001/](docs/verification/P18-QUAL-001/README.md).

The text below is the standing record as of P17-QUAL-001 (2026-10-15) and is kept as history.

```text
P17 — Structural FEA is QUALIFIED. P17-QUAL-001 PASS, 2026-10-15.

    seventeen milestones audited from their own recorded verdicts, 17 PASS
    206 ADR clauses checked against the code: 195 implemented, 11 sentences
      overtaken by later milestones and amended, 0 invariant violations
    three final cross-milestone gates: solving leaves every reference
      document byte-identical; every dependency edit stales the result in
      one sequence with undo in the middle; a FAILED regeneration never
      yields a current result
    seven mutation probes on the production tree -- duplicate-entry
      overwrite, pivot gate removed, residual gate removed, inverted
      element admitted, material staleness dropped, CLI exit 0 on a
      singular solve, reaction sign flipped -- seven killed
    44 adversarial questions, 0 production defects, 0 gate-blocking
    qualification: 4009/4009 x 3 presets, 1138 x 5 in two, 0 warnings over
      658 objects, 17 stages exit 0, 3h49m57s, fingerprints before == after,
      suite fingerprints and CLI solve payloads bit-identical across presets
    NO file under src/, include/, apps/ or examples/ changed

There is no open milestone and nothing is authorized. P18 — Thermal
Analysis is next in the sequence and is NOT AUTHORIZED; P17 being qualified
is not permission to start it. F1, F2 and F3 (P17-REFMOD-001) are owner
decisions it should weigh first.
```

What P17 hands over: a result keyed to the mesh stamp, the material revision
and the analysis revision it was computed from; a material resolved per
analysis mode through P15's consumer model; one validation entry whose gates
a later phase inherits rather than re-asks; and ten reference models with
closed-form oracles that cannot call BetterCAD.
Evidence: [docs/verification/P17-QUAL-001/](docs/verification/P17-QUAL-001/README.md).

What it consumes is P16: a validated Tet4 volume mesh from authoritative CAD
geometry, with sizing controls, quality metrics, geometry/mesh correspondence,
boundary regions, undo/redo, a headless CLI and eight independently validated
reference models.
[docs/verification/P16-QUAL-001/](docs/verification/P16-QUAL-001/README.md).

## What was authorized, and what was not — the record as of P17-QUAL-001

```text
P17-ARCH-001 .. P17-REFMOD-001
                 PASS, each at its own recorded candidate and harness
                 verdict; the chain and the figures are in
                 docs/verification/P17-QUAL-001/MILESTONE_AUDIT.md and the
                 evidence links in ROADMAP.md.

P17-QUAL-001     PASS 2026-10-15. Qualified against a99f0b6 plus the
                 working tree: 4009/4009 x 3, 1138 x 5 in two, 0 warnings
                 over 658 objects, 17 stages exit 0. Committed as ae8c2f7,
                 and all eight subtree hashes of that commit equal the
                 frozen fingerprint (apps a95876bc, include c9696d26, src
                 f3c1245d, tests 90f93c52, examples a955cf65, cmake
                 5a382115, CMakeLists.txt 3ec3c10f, CMakePresets.json
                 3229f0f8): qualified tree == committed tree. Evidence
                 recorded.

P18              NOT AUTHORIZED. Being next in the sequence is not
                 permission, and P17 being qualified is not either.
                 Authorizing it is a scope decision, and F1-F3 are inputs
                 to it.

everything after it
                 likewise.
```


## Still owed, from earlier reviews

```text
F6 (P16-ARCH)   DISCHARGED by P16-QUAL-001: a MeshControl whose body is
                deleted reports ineligibility object_not_found, refuses
                generation, and keeps the mesh it already held marked stale.
                One test proves the sequence.
F6 (P16-GEOM)   partMassProperties has no currency check, so mass properties can
                be computed from stale geometry after an unregenerated edit. A
                P15 behaviour change, so a scope decision
cross-preset    DISCHARGED by P16-REFMOD-001: the CLI reference fixtures run
determinism     in a fresh process in every preset and assert exact element
                counts and SI volume doubles, so a mesh IS now compared across
                Debug, Release and Debug-shared.
no sanitizers   this MinGW ships no libasan/libubsan, so neither Netgen nor
                BetterCAD has ASan/UBSan coverage. Worth a different toolchain
                before a volume mesh is trusted numerically. (INFRA-NETGEN-001)
deps/ unfingerprinted
                the qualification fingerprints 8 paths, and deps/ is not one of
                them, so a changed Netgen patch would not void a qualification.
                Pre-existing in kind -- a differently-rebuilt dependency prefix
                was never detectable either. (INFRA-NETGEN-001)
clean-checkout  a clean checkout cannot re-fetch Catch2 or nlohmann_json,
fetch           because CMake's bundled curl carries no CA trust anchors in
                this environment. Verified instead with
                FETCHCONTENT_SOURCE_DIR_* pointing at the same SHA256-verified
                archives. (P16-REFMOD-001, P16-QUAL-001)
stale cache     DISCHARGED for P17 by P17-ARCH-001 and re-proved by
                P17-QUAL-001: the solver entry takes the document, the
                regenerator and the mesher and re-checks mesh currency
                through requireStructuralModel on every call; a failed
                regeneration and a stale mesh are both refused in sequence.
                Still true in general: nothing FORCES a holder of a
                VolumeMesh to call isStale, so a future consumer that CACHES
                a mesh owns the same check. (P16-VOL-001)
concurrency     calls into the volume backend are serialised by a mutex because
                nglib keeps global state; no test runs two threads through it.
                (P16-VOL-001)
multiple solids a body with more than one solid is REFUSED, explicitly and
                tested. ADR-032's one-region-per-solid remains the eventual
                design. (P16-VOL-001)
sphere          no fixture, so a degenerate pole edge is the one geometry where
                exact-coordinate node unification is untested
boundary edges  protected by exactly one test, because closed solids cannot
                detect the counter's removal
quality         DISCHARGED for P17 by P17-VALID-001 (ADR-042): the
thresholds      structural acceptance policy rejects on measured accuracy
                laws, one bound per degeneration family, and warns at the
                qualified envelope. P16's own policy stays report-only, so
                the next consumer of a mesh (P18) owns its own thresholds
                the same way. (P16-QUALITY-001, P16-QUAL-001)
drilled hole    a hole cut by a feature has no stable face reference, so a
wall            boundary set cannot name it. A tube's bore can. (P16-MAP-001)
RM-MESH-09      the configuration reference case is DEFERRED, not failed: a
                configuration override is a refusal by design, so the case
                would assert a refusal rather than a mesh. It belongs with
                carried defect 2. (P16-REFMOD-001)
```

## Carried defects, neither P16's

```text
the FileIo replace defect     a document save can still lose to a file
                              synchroniser. Arguably ahead of any new milestone.

configuration regeneration    a configuration override does not rebuild the
                              geometry it changes. Mass properties and meshing
                              both REFUSE under one, with the same guard.
```

Also carried from earlier phases: hole POSITION dimensions are unsupported, GD&T symbols are not
fully embedded in PDF/DXF, and cross-preset export byte identity is not guaranteed for drawings.
