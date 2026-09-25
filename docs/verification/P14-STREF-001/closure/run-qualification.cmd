@echo off
rem P14-STREF-001 closure: three-preset regression of the chamfer identity fix.
rem
rem ON THE STANDARD PRESETS, and NOT on a build outside the source tree.
rem
rem Moving the build out of the OneDrive-synced checkout was attempted first --
rem that decision has been due since P14-DIM-001 and had to be taken before this
rem tree is frozen, because binaryDir lives in CMakePresets.json, inside the
rem fingerprint. It does not work yet, and the reason is a defect this attempt
rem uncovered rather than anything about the presets:
rem
rem   windeployqt resolves the Qt runtime RELATIVE TO THE EXECUTABLE it is
rem   deploying, as <exe dir>/../../<toolchain key>/bin. That only names the
rem   real Qt when the build tree sits inside the source tree. With the build
rem   at %LOCALAPPDATA%/BetterCAD-build it looked for
rem   "BetterCAD-build/gnu-16-mingw-amd64/bin/Qt6Core.dll" -- the right parent,
rem   the wrong name -- and the GUI target failed to link in all three presets.
rem   Putting Qt bin on PATH, running windeployqt from Qt bin, and pre-placing
rem   Qt6Core.dll beside the executable were each tried and none changed it.
rem
rem Fixing that is its own piece of work and is not what this milestone is
rem authorized to do, so the -local presets were REMOVED rather than committed
rem unvalidated, and the build-location decision stays open with that finding
rem recorded against it.
rem
rem The repeat filter is broad: the change reaches core (FaceSelector), features
rem (ChamferFeature, regeneration, patterns, mirrors), io (both serializers and
rem the format version) and every committed model, so repeating only the chamfer
rem tests would repeat the new work and none of what it could have disturbed.
setlocal
set "QUALIFY_PRESETS=debug release debug-shared"
set "QUALIFY_REPEAT_PRESETS=release debug"
set "QUALIFY_REPEAT=[Cc]hamfer|[Rr]eference|[Ss]tref|[Ff]ace|[Nn]ame|[Ss]elector|[Ee]dge|[Pp]ersist|[Ss]erial|[Ss]ave|[Ll]oad|[Ff]ile|json|[Rr]ound|[Mm]alformed|[Ll]egacy|[Vv]ersion|[Mm]igrat|[Rr]egener|[Dd]ependen|[Uu]ndo|[Rr]edo|[Cc]ommand|[Pp]attern|[Mm]irror|[Dd]rawing|[Ss]heet|[Vv]iew|[Dd]imension|[Aa]nnotation|[Bb]om|[Bb]alloon|[Ee]xport|[Ss]cene|pdf|svg|dxf|cli|[Aa]ssembl|[Cc]omponent|[Mm]ate|[Cc]onfiguration|[Ss]uppress|[Ss]olve|[Uu]nit|[Pp]arameter|architecture|compile_fail|[Dd]ocument|[Oo]bject|[Vv]alidat|[Ee]xample|[Mm]odel"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal & exit /b %errorlevel%
