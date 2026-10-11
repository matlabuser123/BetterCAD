#!/bin/bash
# Refuses while anything is building or a probe driver is alive.
#
# TWO FALSE-NEGATIVE/POSITIVE TRAPS, BOTH HIT:
#  1. `ps aux | grep probe.sh` DOES NOT SEE THE DRIVER in this Git Bash -- it
#     returned 0 with three driver shells running. The check goes through
#     Win32_Process, which does see them.
#  2. A substring pattern like '*probe*.sh*' matches THE SHELL RUNNING THIS
#     GUARD, because the Bash tool's own command line can contain both
#     "probe.log" and "guard.sh". So the match is anchored to the END of the
#     command line: only a shell whose script IS probe.sh or
#     probe-contour.sh counts.
busy=0
if tasklist 2>/dev/null | grep -qiE "^(ninja|cc1plus|ctest|bettercad_tests|collect2|ld)\.exe"; then
  echo "BUSY: a build or test process is running"
  busy=1
fi
drivers=$(powershell -NoProfile -Command "(Get-CimInstance Win32_Process -Filter \"Name='bash.exe'\" | Where-Object { \$_.CommandLine -match 'probe(-contour)?\.sh.?\s*\$' }).ProcessId -join ','" 2>/dev/null | tr -d '\r')
if [ -n "$drivers" ]; then
  echo "BUSY: probe driver alive (PID $drivers)"
  busy=1
fi
[ "$busy" = "0" ] && echo "CLEAR"
exit "$busy"
