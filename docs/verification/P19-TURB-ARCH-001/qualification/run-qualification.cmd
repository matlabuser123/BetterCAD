rem P19-TURB-ARCH-001 -- Turbulence Architecture. The qualification launcher.
rem
rem WHAT THIS MILESTONE IS. ADR-058 defers turbulence in full from P19 v1 and names
rem this milestone as the one that decides its architecture "before any model".
rem ADR-079 decides that architecture WITHOUT TYPES: no mode field, no model
rem enumerator, no turbulence variable, no wall-treatment record, no turbulence
rem result array, no flow-regime query. include/ and src/ are UNTOUCHED.
rem
rem SO THE DELIVERABLE IS A DECISION AND THE GATES THAT MAKE IT FALSIFIABLE.
rem ADR-061 Decision 4 forbids the obvious design -- "a selector would be a field
rem with one legal value" -- and CfdBoundary.hpp records that "an enumerator no
rem input can produce is worse than no enumerator", so the ten policy decisions are
rem enforced mechanically rather than declared:
rem
rem     architecture.turbulence-ownership  a source-text checker over apps/, src/,
rem                                        include/ and examples/, attributing each
rem                                        hit to the area it is in so canonical
rem                                        intent, persistence, a command, the CLI,
rem                                        the GUI and a laminar threshold each have
rem                                        a gate that NAMES the decision it breaks
rem     compile_fail.cfdturb               fourteen absence gates, which is the
rem                                        mechanism ADR-058 prescribed by name:
rem                                        "each unsupported item stays absent from
rem                                        the types, which the compile-fail groups
rem                                        of those milestones assert"
rem     unit.CfdTurbulenceArchitecture_    two structural pins and the canonical-text
rem                                        absence check
rem
rem THE GATES WERE MADE TO FAIL ON PURPOSE BEFORE THEY WERE BELIEVED. Seven fixture
rem trees drive the checker's seven branches, and a valid tree carries the measured
rem false positives -- mut in 29 files, epsilon in 9, LES in 8, nut in 6, omega in 4
rem -- none of which may be flagged. Four mutations of the production tree were
rem applied, built and reverted, and the logs are in experiments/.
rem
rem AND ONE OF THIS MILESTONE'S OWN CLAIMS WAS DISPROVED BY THAT RUN. It first said
rem kCfdVariables was "frozen by a comment". It is not: CfdVariable is switched on
rem exhaustively at src/cfd/CfdFields.cpp:64 and src/cfd/CfdInitialField.cpp:297 and
rem the build is -Werror, so a fourth enumerator fails there, before any assertion.
rem A third mutation established what the new pin IS worth -- with both switches
rem satisfied and both tables widened it is the only failure left -- and the claim is
rem corrected in ADR-079 and in the test header rather than left standing.
rem
rem MEASURED AGAINST THE FROZEN TREE, so the logs can be read against a number
rem rather than for an absence of the word "fail". The figures are in FREEZE.md with
rem their derivations, counted by `ctest -N` and off the built binary rather than
rem predicted -- P19-GEOM-001's freeze recorded getting that wrong once, and
rem P19-TRANSIENT-001's repeat stage caught a superseded binary by a count of five.
rem
rem     full suite, per preset      6995 = 6965 + 4 unit.CfdTurbulenceArchitecture_
rem                                             + 12 architecture.turbulence-ownership
rem                                             + 14 compile_fail.cfdturb
rem     repeat set, per preset      3543 = 3527 + 4 + 12, the compile-fail group
rem                                        excluded as in every milestone
rem     repeat EXECUTIONS per log  17715 = 3543 x 5
rem     architecture                 217 = 205 + 12
rem
rem     THE COUNTS ROSE AFTER THE REVIEWS, and this header's first draft carried
rem     6991 / 3539 / 214 -- counted off the tree before an adversarial review and
rem     a mutation analysis. Between them they added a fourth runtime case (the
rem     nested settings records, which were the way into canonical intent), two
rem     fixtures (a zero-scan tree and a virtual interface) and a third (a bare
rem     turbulence symbol inside cfd). Corrected in place, counted again by
rem     ctest -N off the FROZEN tree.
rem
rem THE REPEAT FILTER IS UNCHANGED and already selects both new groups:
rem unit.CfdTurbulenceArchitecture_ matches its existing unit\.Cfd branch and the
rem ownership tests match `architecture`. Nothing had to be added to it, which was
rem measured with `ctest -N -R` over the filter rather than assumed.
rem
rem THE VERDICT IS THE HARNESS'S OWN TWO STATEMENTS, never this launcher's exit
rem code: run-qualification.out's sentence and qualification-times.txt's failed-
rem stage count. P19-MOM-001, P19-STEADY-001 and P19-TRANSIENT-001 each had a run
rem where the launcher reported exit 0 and the harness reported failed stages.
rem
rem AND THE SHELL MATTERS. P19-TRANSIENT-001's second attempt failed in five seconds
rem because it was launched from a shell that cannot see the dependency prefix, so
rem CMake found msys64's Open CASCADE 7.9.3 and the project's own supported-range
rem gate refused it. All three presets are configured as a pre-flight before this
rem launcher is run.
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
