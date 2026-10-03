@echo off
rem INFRA-VIEWER-001 qualification: the exact invocation, recorded so the run
rem is reproducible rather than described.
rem
rem qualify.cmd beside this file is carried UNCHANGED from P15-QUAL-001, byte
rem for byte (git hash-object d313a64070718c44fae290ac042fe259d1a03c8b);
rem verify-harness.cmd is its regression and is meant to be run whenever it is
rem edited, which it was not. This wrapper only supplies the three variables
rem the harness reads.
rem
rem THE REPEAT FILTER IS THE BLAST RADIUS, and this milestone's has an unusual
rem shape, because its largest change is to a dependency rather than to the
rem source tree:
rem
rem   deps/CMakeLists.txt         OCCT rebuilt from TKernel upward with
rem                               BUILD_MODULE_Visualization=ON and
rem                               USE_OPENGL=ON. EVERY OCCT-linked test is now
rem                               running against a new build of the kernel.
rem   src/CMakeLists.txt          the renderer module registered, which
rem                               architecture.layering re-derives
rem   apps/bettercad/             the placeholder QLabel replaced by a viewport
rem   tests/compile_fail/         six new cases
rem
rem The OCCT rebuild is answered by the PRESET STAGES, which run the entire
rem suite -- every mesh, boolean, drawing, STEP and mass-property test -- in
rem Debug, Release and Debug-shared against the rebuilt kernel. That is the
rem gate for a dependency change, and no filter could substitute for it.
rem
rem The REPEAT stage answers a different question: is the new subject
rem deterministic when run five times over. So it selects this milestone's own
rem tests, the GUI smoke test that now reports whether a view was created, the
rem compile-failure groups (which include the viewer's public header and must
rem keep needing neither Qt nor OCCT to do it), and the architecture checks
rem that would notice OCCT or Qt escaping its directory.
rem
rem unit\.Viewer_ and not unit\.View: unit.ViewProjection_* and
rem unit.Annotation_ACentrelineWhoseAxisPointsAtTheViewerIsRefused are drawing
rem tests and belong to the preset stages, not to this milestone's subject.
rem
rem unit\.RenderedImage_ is named SEPARATELY and on purpose. The coverage
rem measure every rendering assertion rests on is tested on its own, in a
rem test case that does NOT begin with Viewer_, so the obvious filter missed
rem it -- and that test's fixture is one of this milestone's own changes
rem (Finding 4). A determinism gate that silently omits a changed test is
rem exactly the gap this filter exists to close.
rem
rem A GL IMPLEMENTATION IS REQUIRED. The renderer tests create a real
rem WNT_Window -- never mapped, so nothing appears on screen -- and a real
rem OpenGl_GraphicDriver. On a machine with no usable OpenGL they FAIL rather
rem than skip, deliberately: a skipped test that looks like a pass is worse.
rem Recorded in ADVERSARIAL_REVIEW.md as a carried limitation.
rem
rem CMAKE_BUILD_PARALLEL_LEVEL=6, AND WHY IT IS NOT A RELAXED GATE.
rem
rem The first attempt at this qualification failed five stages, and every one
rem of them came from a single line in the debug-ext build log:
rem
rem     cc1plus.exe: out of memory allocating 4198399 bytes
rem
rem This machine has 16 logical cores, so `cmake --build` lets Ninja run 18
rem compiles at once, and a CLEAN build is 592 objects of template-heavy C++23
rem against OCCT, Eigen, nlohmann_json and Netgen headers. An incremental build
rem never showed the problem, because it compiles a handful of objects -- which
rem is exactly why a qualification builds from clean.
rem
rem WHAT IS OBSERVED, AND WHAT IS NOT. Observed: that error, on
rem src/io/json/ShellJson.cpp, at object 286 of 592, with 18-way parallelism in
rem force and about 9 GB of 32 GB physically free. NOT measured: the peak
rem memory of the heaviest translation unit, or which of them coincided.
rem Sampled during the capped run, six concurrent compilers held under 1 GB
rem between them and the largest was 363 MB -- so the typical TU is small, and
rem the exhaustion must have come from a coincidence of heavy ones on top of
rem whatever else the machine held. The cap is therefore chosen for headroom,
rem not calculated from a measured peak, and that is the honest account.
rem
rem The damage was not confined to that stage: release-ext's build failed the
rem same way, debug-shared-ext's CONFIGURE failed (its try_compile checks had
rem no memory either, leaving a one-line log that says only 'Configuring
rem incomplete'), and both repeat stages then failed for want of binaries. One
rem resource failure, five failed stages. Re-running the shared configure
rem afterwards gives exit 0, which is how the cause was confirmed rather than
rem guessed at.
rem
rem CAPPING PARALLELISM WEAKENS NOTHING. The same sources are compiled with
rem the same flags, -Werror included; the no-op rebuild check still proves the
rem binaries are the ones just built; the full suite still runs in all three
rem presets; the determinism stages still repeat five times. Only the number
rem of compilers running at once changes. ctest stays at -j 8, which the
rem pre-freeze full-suite run completed without trouble.
rem
rem It is set HERE, in this milestone's wrapper, and not in qualify.cmd, which
rem is carried byte for byte from P15-QUAL-001. cmake --build honours this
rem variable when no explicit -j is passed, so the carried harness needs no
rem edit and its regression stays valid.
rem
rem It is expanded as !REPEAT! inside qualify.cmd, never %REPEAT%: a %VAR%
rem holding | ( or ) is substituted when the for-block is PARSED and closes the
rem block early, killing the run after every preset has already passed.
rem ---------------------------------------------------------------------
rem DO NOT EDIT THIS FILE WHILE IT IS RUNNING. NOT EVEN A COMMENT.
rem
rem cmd.exe reads a batch file INCREMENTALLY, BY BYTE OFFSET, as it executes
rem it. Changing the file's length mid-run moves everything after the cursor,
rem and execution resumes at an offset that no longer means what it meant.
rem
rem That cost this milestone a four-hour qualification that had PASSED. Two
rem minutes into the run a comment above was reworded; four hours later, when
rem qualify.cmd returned, cmd.exe resumed at a stale offset and called it a
rem SECOND time, which truncated qualification-times.txt and overwrote three
rem of debug-ext's logs before it was noticed. The run had already printed
rem "Qualification passed: every stage exited 0" -- and the evidence for it
rem was gone. See attempt-2-clobbered-by-an-edit/WHY_THIS_WAS_DISCARDED.md.
rem
rem The tree being frozen is not the only thing that must hold still during a
rem qualification. So must the script driving it.
rem ---------------------------------------------------------------------
setlocal
set "BETTERCAD_BUILD_ROOT=C:/Users/uqhas/AppData/Local/bc-build"
set "CMAKE_BUILD_PARALLEL_LEVEL=6"
set "QUALIFY_PRESETS=debug-ext release-ext debug-shared-ext"
set "QUALIFY_REPEAT_PRESETS=release-ext debug-ext"
set "QUALIFY_REPEAT=unit\.Viewer_|unit\.RenderedImage_|gui\.|compile_fail|architecture"
call "%~dp0qualify.cmd" "%~dp0."
set "RESULT=%errorlevel%"
echo qualify.cmd exit %RESULT%
endlocal & exit /b %RESULT%
