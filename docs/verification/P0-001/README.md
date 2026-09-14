# P0-001 — Build system and repository skeleton: verification

Date: 2026-09-14. The raw logs in this directory were captured from a clean
run (all `build/` trees deleted first):

```powershell
foreach ($p in 'debug','release','debug-shared') {
    cmake --preset $p        *> configure-$p.log
    cmake --build --preset $p *> build-$p.log
    ctest --preset $p         *> ctest-$p.log
}
```

## Environment

| Item | Value |
|------|-------|
| OS | Windows 11 Pro 10.0.26200, AMD Ryzen 7 5800H |
| Compiler | GCC 16.1.0, WinLibs MinGW-w64 14.0.0 (UCRT, POSIX threads) |
| CMake / Ninja | 4.4.2 / 1.13.2 |
| Qt | 6.11.2 qtbase, built from source by `deps/` with the same toolchain |
| Catch2 | 3.16.0 (FetchContent, SHA-256 pinned) |

## Acceptance criteria

| Criterion | Evidence | Result |
|-----------|----------|--------|
| CMake configure succeeds | `configure-*.log`: exit 0 for debug, release, debug-shared | pass |
| Project builds cleanly | `build-*.log`: exit 0 with `-Werror`, no `warning` or `error` lines in any log | pass |
| Test executable runs | `ctest-*.log`: 26/26 tests passed in each configuration | pass |
| CLI launches | `cli.launch.*` process tests (5 per configuration) | pass |
| Desktop executable placeholder | `gui.launch.smoke` (`bettercad --smoke-test -platform offscreen`) | pass |
| Debug and Release builds | `debug` and `release` presets | pass |
| Warning flags | `cmake/BetterCADCompilerOptions.cmake`; applied to BetterCAD targets, errors in presets | pass |
| Compiler/version reporting | configure summary in `configure-*.log`; `bettercad-cli version` output below | pass |

## Test inventory (26 per configuration)

- 14 Catch2 unit tests (`unit.*`): build information and in-process CLI behaviour.
- 5 CLI process tests (`cli.launch.*`): launch `bettercad-cli.exe` and check
  its exit code and output for `--version`, `version`, `--help`, no arguments,
  and an unknown command.
- 1 GUI process test (`gui.launch.smoke`): headless launch of `bettercad.exe`.
- 6 architecture tests: the layering check on the real tree (9 files,
  0 violations), plus 5 checker self-tests against fixture trees (one valid
  tree and four trees with one planted violation each).

## Negative controls (run by hand)

These confirm the checks can fail:

| Check | Result |
|-------|--------|
| `RunAndCheck` expecting exit 0 from `bettercad-cli frobnicate` | fails: `exit code 2 != expected 0` |
| `RunAndCheck` with wrong version regex `^bettercad-cli 9\.9\.9$` | fails (exit 1) |
| `bettercad --smoke-test -platform doesnotexist` | non-zero exit (`-1073740286`) |
| `bettercad --bogus` | exit 2, `bettercad: Unknown option 'bogus'.` |
| Architecture checker on each violation fixture | exit 1 with the matching rule message (automated tests) |

## CLI output (release preset)

```text
> bettercad-cli --version
bettercad-cli 0.1.0

> bettercad-cli version
BetterCAD 0.1.0
  revision   : unknown
  build type : Release
  compiler   : GCC 16.1.0
  language   : C++23 (__cplusplus=202302)
  platform   : Windows x86_64
  libraries  : static
```

The `debug-shared` build reports `libraries  : shared`. The revision is
`unknown` because the repository has no commits yet; it is regenerated on every
build once commits exist.

## Notes

- The official Qt 6.10.3 MinGW binaries link `msvcrt.dll` (GCC 13.1), which
  is incompatible with this UCRT toolchain. Qt is therefore built from source
  by `deps/CMakeLists.txt`. The rebuilt `Qt6Core.dll` imports
  `api-ms-win-crt-*` and GCC 16's `libstdc++-6.dll`.
- `windeployqt` output is wrapped by `cmake/DeployQtRuntime.cmake`. Its
  advisory messages (missing optional translations catalog) are shown only if
  deployment fails.
