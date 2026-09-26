@echo off
rem P14-QUAL-001: the final qualification of P14 — Technical Drawings.
rem
rem ITS OWN RUN, on the final committed tree, and not a reuse of any earlier
rem milestone's. P14-STREF-001's closure qualified the same source fingerprint
rem hours earlier, and reusing it would have cost nothing — but a phase
rem qualification that points at another milestone's logs is the shortcut this
rem gate exists to refuse. If the final tree is qualified, it is qualified here.
rem
rem THE FILTER IS EVERYTHING. The three clean presets already run the whole
rem suite; these repeat stages are the determinism gate, and P14's claim is
rem that a drawing stays attached to its model across regeneration, save,
rem load, undo, configuration switching and export. That reaches essentially
rem the whole system, so the filter is deliberately near-total rather than
rem drawing-shaped: a filter that matched only "drawing" would repeat the
rem drawings and none of the model they are derived from.
setlocal
set "QUALIFY_PRESETS=debug release debug-shared"
set "QUALIFY_REPEAT_PRESETS=release debug"
set "QUALIFY_REPEAT=[A-Za-z]"
set "HERE=%~dp0"
call "%HERE%qualify.cmd" "%HERE:~0,-1%"
endlocal & exit /b %errorlevel%
