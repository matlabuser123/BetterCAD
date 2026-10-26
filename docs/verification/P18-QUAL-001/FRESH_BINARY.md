# P18-QUAL-001 - fresh-binary proof (finding D-H)

Question 32 of the TODO: *can stale binaries produce qualification evidence?*

**Identity (carried).** The two workflow scripts (`tests/cli/ThermalWorkflow.cmake`, `ThermalReferenceWorkflow.cmake`) fail on a relative binary path, a binary outside the build tree, an unresolved
or foreign runtime library, a GUI dependency, and a shared/static mismatch, and record the absolute path, the size, the SHA-256 and the resolved runtime libraries in each run's ledger
(`qualification/<workflow>-<preset>/ledger.txt`).

**Freshness (added here, at the harness).** Identity cannot say that the binary was built from *this* source. Four facts together do:

1. `qualify.cmd` cleans each preset (`ninja -t clean`, retried while OneDrive or a scanner holds a file; the attempt is logged with its time), then builds with warnings as errors;
2. it builds **again** and the second build has nothing to do (`rebuild-<preset>.log` is one line);
3. the source fingerprint (eight paths, from a scratch index) is computed before the first build and after the last test and recorded in `qualification-times.txt`, and the frozen value was taken before the run
   (`FREEZE.md`);
4. `postrun-qual.ps1` reads each binary's last-write time and requires it to be later than that preset's clean and later than the newest last-write time of any file in the eight source paths, and records the
   size and SHA-256 of `bettercad-cli.exe`, `bettercad_tests.exe` and `bettercad_example_reference_models.exe`, and the CLI's own `version` text.

The result is in `qualification/postrun-summary.txt`, section "FRESH BINARY".

**Why not inside the CMake scripts.** A timestamp comparison between the binary and the sources inside a six-hour workflow would turn a synchronising folder touching a file (the tree is under OneDrive) into a
qualification failure that proves nothing about the binary. Done after the run, a violation is investigated instead of aborting the run, and the clean-then-rebuild-then-nothing-to-do record above is the
primary proof; the timestamps are a corroboration.
