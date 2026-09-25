@echo off
rem P14-REFMOD-001: three-preset regression on the frozen tree.
rem
rem The subject is the production drawing reference suite, and a drawing
rem reference model reaches nearly everything: parameters and sketches, the
rem features it is built from, patterns and their copied face names,
rem assemblies with their solve, configurations and suppression, the drawing
rem objects, hidden-line removal, sections and hatch, dimensions of every
rem type, annotations including balloons and the bill of materials, the scene
rem and the three writers, persistence, and the CLI that drives all of it from
rem another process.
rem
rem Three production files changed and each widens the set:
rem
rem   src/drawing/Annotations.cpp        annotationText() over every
rem                                      model-driven kind -- so balloons,
rem                                      BOM tables and hole callouts
rem   src/drawing/SheetScene.cpp         a closed circular edge is recovered
rem                                      as a circle -- so every hole, every
rem                                      arc and every exported file
rem   apps/bettercad_cli/DrawingReports  the report's columns -- so the CLI
rem
rem The repeat filter is therefore broad rather than narrow. A filter that
rem matched only "refmod" would repeat the new tests and none of the qualified
rem ones those three files could have disturbed.
setlocal
call "%~dp0repeat-filter.cmd"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal & exit /b %errorlevel%
