# P18-CLI-001 — the fresh-binary proof

## The risk

A CLI test that runs a stale `bettercad-cli.exe` — one built before the change, found on `PATH`, copied from another build, or linking an installed DLL — passes while testing nothing. This project has met each of these.

## What is proved, and by what

| Link in the chain | Proof | Where |
|---|---|---|
| The tree was built **from clean** | `qualify.cmd` configures the preset, runs `cmake --build --target clean` (ninja's `-t clean`, up to five tries) and only then rebuilds with `-Werror`; it keeps the logs (`clean-<preset>.log`, `build-<preset>.log`; `postrun-cli.ps1` counts the `Building CXX object` lines and any `warning:` / `error:`) | `qualification/` |
| Nothing is left to rebuild | a **no-op rebuild** immediately afterwards must exit 0, and `postrun-cli.ps1` counts the build edges `rebuild-<preset>.log` shows | `qualification/` |
| The binary was built from the frozen source | the git tree ids of the eight paths (`apps include src tests examples cmake CMakeLists.txt CMakePresets.json`) are recorded by `qualify.cmd` from a scratch index **before the first build** and **after the last test run** (`qualification-times.txt`), and `freeze-cli.ps1` took the same fingerprint before the run; they must be equal to one another and to the commit's tree | `FREEZE.md` |
| The workflow runs **that** binary | the executable is passed as an **absolute path** (`$<TARGET_FILE:bettercad-cli>`); the script refuses a relative path, so `PATH` cannot choose, and refuses a path that is not inside the preset's build directory | `ThermalWorkflow.cmake` |
| Its identity is recorded | path, size, SHA-256 are the first lines of `ledger.txt`; `postrun-cli.ps1` re-measures each preset's `bettercad-cli.exe` and `bettercad_tests.exe` and prints the hashes beside the ledger's | `workflow-<preset>/ledger.txt`, `qualification/postrun` summary |
| Its libraries are **its own** | `file(GET_RUNTIME_DEPENDENCIES)` (objdump) resolves the binary's closure against its own directory; every `libbettercad_*` must resolve to that directory, none may be unresolved; a shared preset must resolve at least one project library (a vacuous proof fails) and a static preset must resolve none | `ThermalWorkflow.cmake` |
| It is headless | any `Qt*` library in the closure is a fatal error | `ThermalWorkflow.cmake` |
| It reports what it is | step 0 runs `version`, so the ledger holds the build's own account of version, compiler and configuration beside the hash | `ledger.txt` |
| The test executable is fresh too | `postrun-cli.ps1` runs `ThermalCli_*` and `ThermalRun_*` on **each preset's own** `bettercad_tests.exe` and records the Catch2 totals | summary |

## What this does not prove

That the source fingerprint is the source the compiler read is a property of the harness (clean + rebuild + no-op rebuild + before/after fingerprints), not of the script: a script run by hand against an old build would pass its own checks, which is why the ledger is only evidence when it is read together with `FREEZE.md` and the build logs. The document says so rather than leaving a reader to assume more.

## Zero-match protection that belongs here

`tests/cli/ZeroMatchGuard.cmake` discovers the tests behind each CLI filter and requires **at least** the number this milestone defined (per `ThermalCli_` group, `ThermalRun_` 20, the 12 `cli.thermal.*` process tests, the 104 gate tests). It is a floor, as the guard has been since P16 — not an exact count — so a renamed or deleted test makes a qualification run fail rather than select less and pass, while a later milestone adding tests does not break it. (The exact counts are in the qualification summary, from `ctest -N -R` on each preset.) Hidden tests (`[.]`) have no ctest entry; the one hidden test (`ThermalCliWorkflow_EveryPayloadIsStrictJson`) is run by name from the workflow script, which itself requires that at least 25 payloads were parsed.
