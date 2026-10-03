@echo off
rem P16-MAP-001 qualification: the exact invocation, recorded so the run is
rem reproducible rather than described.
rem
rem qualify.cmd itself is carried UNCHANGED from P15-QUAL-001, byte for byte;
rem verify-harness.cmd beside it is its regression and is meant to be run
rem whenever it is edited, which it was not. This wrapper only supplies the
rem three variables the harness reads.
rem
rem THE REPEAT FILTER IS THE BLAST RADIUS, and this milestone's is wider than
rem the last two, because it changes a file outside the meshing module:
rem
rem   src/core/geometry/occt/OcctMesh.cpp   triangulate() now verifies the
rem                                         pairing between the body's faces
rem                                         and the meshed copy's
rem   include/bettercad/core/Id.hpp         one additive alias, BoundarySetId
rem
rem `triangulate` has exactly two production callers -- src/io/ModelExport.cpp
rem and src/meshing/SurfaceMesh.cpp -- so the io export tests belong in the set
rem alongside the whole meshing module. The compile-failure groups compile
rem against meshing and core headers, and the architecture checks are what
rem would notice a containment or layering violation.
rem
rem It is expanded as !REPEAT! inside qualify.cmd, never %REPEAT%: a %VAR%
rem holding | ( or ) is substituted when the for-block is PARSED and closes the
rem block early, killing the run after every preset has already passed.
setlocal
set "BETTERCAD_BUILD_ROOT=C:/Users/uqhas/AppData/Local/bc-build"
set "QUALIFY_PRESETS=debug-ext release-ext debug-shared-ext"
set "QUALIFY_REPEAT_PRESETS=release-ext debug-ext"
set "QUALIFY_REPEAT=unit\.Map|unit\.Boundary|unit\.Mesh|unit\.Surf|unit\.Vol|unit\.Tet|unit\.Size|unit\.Quality|unit\.Geom|unit\.Netgen|unit\.Export|unit\.Step|unit\.Stl|compile_fail|architecture"
call "%~dp0qualify.cmd" "%~dp0."
set "RESULT=%errorlevel%"
echo qualify.cmd exit %RESULT%
endlocal & exit /b %RESULT%
