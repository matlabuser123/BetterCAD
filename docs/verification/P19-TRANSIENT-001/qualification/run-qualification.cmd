rem P19-TRANSIENT-001 -- Transient Incompressible Solver. The qualification launcher.
rem
rem WHAT THIS MILESTONE IS. A time grid, a backward-Euler march from t = 0 to the
rem analysis's end time with the last step shortened so the run lands on it
rem exactly, pressure-velocity coupling inside every step, and a history whose
rem last sample is published as the analysis's answer. ADR-078.
rem
rem IT ORCHESTRATES AND IT DOES NOT COMPUTE, AND THE TRANSIENT TERM IS THE PROOF.
rem rho V / dt on the momentum diagonal and rho V / dt * U^n in its source were
rem qualified under P19-MOM-001 two milestones ago, with ADR-073's unit
rem assertions and a dedicated test for the one way a caller can get them wrong.
rem CfdTransientSettings, CfdTimeSampling, CfdResultMetadata's time and step, and
rem CfdResult::create's "transient: a finite time > 0 s and a step >= 1" were all
rem written by P19-DATA-001 FOR this milestone. So the new numerics are none:
rem solveCfdTransient calls cfdMassFlux, reconstructCfdPressureGradient,
rem assembleCfdMomentum three times and advanceCfdCoupling, in that order, every
rem outer iteration of every step.
rem
rem NO EXISTING DECLARATION CHANGED. Four files are added and three CMakeLists
rem gain a registration; not one signature, field, enumerator or default moved,
rem and CfdSteady.hpp and CfdSteady.cpp are not touched at all. That is ADR-078
rem Decision 4: P18 split its steady and transient solvers the same way, a steady
rem analysis is not a one-step transient (the steady momentum form has no
rem rho V / dt at all), and extracting a shared loop body would be a refactor of a
rem file qualified one day earlier. The declared cost is that the ORDER of five
rem operator calls now appears in two files, and the mitigation is a MEASUREMENT:
rem one step at dt = 1e7 s must reproduce solveCfdSteady, and it does to 1.5e-07
rem relative on the velocity in the same 59 outer iterations.
rem
rem THE BLAST RADIUS WAS DRAWN THE OTHER WAY ROUND THIS TIME, and P19-STEADY-001
rem is why. Its gate failed because a private constructor gained a parameter and
rem an inherited compile-fail case that NAMED that constructor silently became an
rem arity test -- a deliberate non-caller, invisible to a search for the factory's
rem callers. Nothing is changed here, so the question asked instead was what is
rem sensitive to what is ADDED: no CMakeLists uses GLOB, so a missed registration
rem is a link error; nothing enumerates the cfd headers or the test tags; and the
rem new group's control target must COMPILE. [cfd][steady] 1263/19, [linalg]
rem 2560/29 and architecture 205 were measured IDENTICAL to baseline before the
rem freeze, which is what says the radius really was this small.
rem
rem THE TWO VELOCITY ARRAYS ARE THE WHOLE MILESTONE. CfdMomentumInput takes
rem currentVelocity (the latest outer iterate) and previousVelocity (U^n, frozen
rem for a step's inner loop) as spans of the same length in the same layout, so
rem the type system cannot tell them apart. ADR-073 Decision 4: passing the
rem current field as the previous one makes the transient term's two halves
rem cancel and THE RUN CONVERGES TO THE CORRECT STEADY ANSWER with every steady
rem assertion passing. Two checklist items are the tests that see it -- the
rem first-step change must scale with dt (measured ratios 1.9834 and 1.9916,
rem which the defect would make 1), and a short march must NOT be the steady
rem solution (measured 1.75% of the way there).
rem
rem AN ADVERSARIAL REVIEW FOUND A DEFECT AND THE SUITE HAD PASSED ON IT.
rem CfdTransientStepRecord::balance was never assigned, so every step record
rem published two zeros where the qualified evaluator's two mass-balance measures
rem belong -- "Per-step mass balance" is one of the nineteen items. The only
rem assertion on it was isfinite(), and zero is finite. The repair asserts that
rem each sampled step's record EQUALS its sample's own metadata, which has no
rem tolerance a default can satisfy, and it was verified to fail without the fix.
rem
rem THE TWO POLICY ITEMS ARE BUILD FAILURES, because a negative claim with no
rem test is prose. "Time-dependent BC policy" and "Time-dependent load policy"
rem claim that nothing in the canonical data varies with time, and thirteen of
rem the twenty-eight compile-fail gates are "no member named" gates over every
rem record a time profile would have to live in, plus gates that a prescribed
rem speed and a gravity vector do not accept a callable. P18-TRANSIENT-001
rem enforced its own policy the same way.
rem
rem MEASURED AGAINST THE FROZEN TREE, so the logs can be read against a number
rem rather than for an absence of the word "fail". The figures are in FREEZE.md
rem with their derivations, counted off the frozen tree by `ctest -N` rather than
rem predicted -- P19-GEOM-001's freeze recorded getting that wrong once.
rem
rem     full suite, per preset      6965 = 6903 + 10 unit.CfdTimeGrid_
rem                                             + 24 unit.CfdTransient_
rem                                             + 28 compile_fail.cfdtransient
rem     repeat set, per preset      3527 = 3493 + 34 matching the filter
rem     repeat EXECUTIONS per log  17635 = 3527 x 5
rem
rem     THE COUNTS ROSE AFTER THE REVIEWS. The first draft of this launcher
rem     carried 6960 / 3522 / 17610, counted off the tree as it stood before an
rem     adversarial review and a mutation analysis added five test cases: a
rem     closed-domain march, atomicity past step 1, gravity on the transient
rem     path, the small-dt acceptance measurement, and the relative-tolerance
rem     band of the grid. Counted again off the FROZEN tree by ctest -N.
rem
rem compile_fail stays EXCLUDED from the repeat, as in every milestone: each case
rem is a real compilation and the groups take minutes serialised on the build-tree
rem lock. The new group's 28 cases are in the full run of all three presets.
rem
rem THE REPEAT FILTER IS UNCHANGED, and it already selects the new tests: both
rem unit.CfdTimeGrid_ and unit.CfdTransient_ match its existing unit\.Cfd branch.
rem unit\.Structural and unit\.Thermal stay in it. They are not required here --
rem this milestone touches cfd only, and [structural] 271,414 in 415 and
rem [thermal] 595,622 in 1168 were measured IDENTICAL to their baselines before
rem the freeze -- but removing a preset's coverage to save time is how a
rem regression gets missed.
rem
rem THE VERDICT IS THE HARNESS'S OWN TWO STATEMENTS, never this launcher's exit
rem code: run-qualification.out's sentence and qualification-times.txt's failed-
rem stage count. P19-MOM-001's attempt 1 was reported as "completed (exit code 0)"
rem while the harness had written "QUALIFICATION FAILED: 2 stage(s) failed", and
rem P19-STEADY-001's first attempt did exactly the same thing.
rem
@echo off
setlocal
set "PATH=%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin;%PATH%"
set "CURL_CA_BUNDLE=C:\Program Files\Git\mingw64\etc\ssl\certs\ca-bundle.crt"
set "SSL_CERT_FILE=C:\Program Files\Git\mingw64\etc\ssl\certs\ca-bundle.crt"
set "BETTERCAD_BUILD_ROOT=C:/bettercad/bc-build"
set "CMAKE_BUILD_PARALLEL_LEVEL=4"
set "QUALIFY_PRESETS=debug-ext release-ext debug-shared-ext"
set "QUALIFY_REPEAT_PRESETS=release-ext debug-ext"
set "QUALIFY_MIN_FREE_GB=20"
set "QUALIFY_REPEAT=architecture|unit\.Linalg|unit\.Structural|unit\.Cfd|unit\.Fluid|unit\.Provenance|unit\.CustomMaterial|unit\.MaterialFile|unit\.MaterialCli|unit\.Thermal|unit\.P18Qualification|unit\.P17Qualification|unit\.Quality|unit\.MeshDofMap|unit\.ConstraintSet|unit\.FreeEquationMap|unit\.Tet4Element|unit\.Mesh|unit\.Volume|unit\.Tet|unit\.Siz|unit\.Id|unit\.ReferenceModel|unit\.Analytic|unit\.View|unit\.RigidTransform|unit\.Transform|unit\.Point|unit\.Direction|unit\.Frame|unit\.Unit|unit\.Dimension|unit\.MassProperties|unit\.SurfaceMesh|refmod\.|gui\.|cli\.thermal\.|cli\.structural\.|cli\.mesh\."
call "%~dp0preflight-space.cmd"
call "%~dp0qualify.cmd" "%~dp0."
set "RESULT=%errorlevel%"
echo qualify.cmd exit %RESULT%
endlocal & exit /b %RESULT%
