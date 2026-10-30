# Qualification environment migration: HASIB -> WORK, 2026-10-30

```text
RESULT:    COMPLETE, and the replacement environment is QUALIFIED IN ITS OWN
           RIGHT. This document records the migration of the BetterCAD
           development and qualification environment to a replacement laptop.
           The migration itself closes no gate: P19-MESH-001 was qualified by
           the harness run of 2026-10-31 that followed it, recorded in
           FREEZE.md's Fifteenth issue and in README.md.
REASON:    the previous qualification laptop was permanently replaced. The
           machine that produced attempt 1 is gone, so the gate cannot be run
           on it and the environment has to be reproduced here instead.
```

The engineering requirement is unchanged: the gate still demands 5939 tests per
preset, 2803 in each repeat selection, and the twelve frozen identities. Nothing
below weakens any of those. What changed is the machine underneath them.

## The previous environment, as recorded rather than recalled

Attempt 1's own `configure-debug-ext.log` is the authority for what the old
machine was:

```text
CMake                 4.4.1 (Ninja)
C++ compiler          GNU 16.1.0
Compiler path         .../WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT.../mingw64/bin/c++.exe
C++ standard          C++23
Warnings as errors    ON
CLI                   ON
Desktop app           ON (Qt 6.11.2)
Volume meshing        ON (Netgen 6.2.2604)
Tests                 ON (Catch2 3.16.0 (fetched))
Dependency prefix     C:/Users/hasib/bcdev/bettercad-deps/gnu-16-mingw-amd64
Build root            C:/Users/hasib/bcdev/bc-build
Source tree           C:/Users/hasib/OneDrive/Desktop/CAD  (inside OneDrive)
```

## The replacement environment

```text
                   PINNED / OLD                    WORK (new)                      VERDICT
machine            HASIB laptop                    WORK, 32 logical cores,         replaced
                                                   63.7 GB RAM
compiler           WinLibs GCC 16.1.0 POSIX UCRT   same, WinGet package
                                                   BrechtSanders.WinLibs.POSIX.
                                                   UCRT 16.1.0-14.0.0-r4           MATCH
                                                   x86_64-ucrt-posix-seh,
                                                   thread model posix
CMake              4.4.1                           4.4.1, bundled in that same
                                                   WinLibs package                 MATCH
Ninja              1.13.2                          1.13.2, same bundle             MATCH
Qt                 6.11.2, built from source       6.11.2, same pinned archive
                   by deps/CMakeLists.txt          and SHA256, same superbuild     BUILT, GUI ON
OCCT               8.0.1 (tag V8_0_1)              same pin, same superbuild       BUILT
Netgen             6.2.2604 (tag v6.2.2604)        same pin, same superbuild       BUILT, MESHING ON
zlib               1.3.1                           same pin, same superbuild       BUILT
Catch2             3.16.0, fetched                 unchanged, fetched by the
                                                   main project                    MATCH
source tree        inside OneDrive                 C:\Users\Hasib\Desktop\CAD,
                                                   NOT cloud-synced                DIFFERENT, better
build root         C:\Users\hasib\bcdev\bc-build   C:\bettercad\bc-build,
                                                   local, non-synced               DIFFERENT by design
deps prefix        C:/Users/hasib/bcdev/            the DEFAULT location,
                   bettercad-deps/                  %LOCALAPPDATA%\bettercad-deps\
                   gnu-16-mingw-amd64               gnu-16-mingw-amd64             DIFFERENT, simpler
parallelism        -j6                             -j16 for deps                   DIFFERENT
```

The compiler, CMake and Ninja are not merely the same versions: they come from
the same WinGet package the old machine used, and WinGet verified the installer
hash on download. That is the strongest form of reproduction available here.

**Qt, OCCT, Netgen and zlib are being built from the repository's own
hash-verified superbuild**, from the same pinned archives and the same SHA256
values, with the same compiler. They are not substituted, and no MSVC-built Qt
is involved anywhere. Until that build finishes and the resulting tree is
qualified, their rows above say only that the inputs match.

## Three things the migration had to solve

### 1. The dependency prefix key cannot distinguish two GCC 16 compilers

`cmake/BetterCADDepsPrefix.cmake` keys the prefix on the compiler's **major**
version:

```text
key = ${CMAKE_CXX_COMPILER_ID}-${major}-mingw-${CMAKE_SYSTEM_PROCESSOR}
    = gnu-16-mingw-amd64
```

WinLibs GCC 16.1.0 and MSYS2 UCRT64 GCC 16.2.0 both key to
`gnu-16-mingw-amd64`. This machine already held an MSYS2-built prefix at that
name from the 2026-10-30 second-machine run, so a WinLibs superbuild would have
installed **into the same directory**, silently mixing two toolchains' C++
binaries in one process -- the exact hazard the file's own comment warns about,
which its key cannot detect.

Handled by renaming, not deleting:

```text
%LOCALAPPDATA%\bettercad-deps\gnu-16-mingw-amd64
  -> %LOCALAPPDATA%\bettercad-deps\gnu-16-mingw-amd64.msys2-gcc-16.2.0
```

The MSYS2 prefix is preserved and the rename is reversible. The WinLibs
superbuild then installs to the now-free default name, which is also why no
`BETTERCAD_DEPS_PREFIX` override is needed here. The old machine did need one:
its prefix sat under `bcdev`, which the presets do not set, so it must have come
from a persisted CMake cache written by a one-time manual configure. Relying on
the default is less fragile, because `qualify.cmd` runs a bare `cmake --preset`
and cannot pass an override.

This is worth fixing properly at some point: a key that cannot tell two
toolchains apart is a correctness hazard, not a cosmetic one. That is a change
to shared infrastructure and is not in this milestone's scope.

### 2. The pinned CMake had no TLS trust anchors

The first superbuild attempt failed in seconds:

```text
SSL Trust Anchors:  no trust anchors configured
SSL certificate verification failed: certificate signer not trusted.
                                    (CAfile: none CRLfile: none)
```

The WinLibs-bundled CMake 4.4.1 ships no CA bundle, so it could not verify
`download.qt.io` and the download failed before any hashing happened.

Resolved by giving it a trust store, **not** by disabling verification:

```text
-DCMAKE_TLS_VERIFY=ON
-DCMAKE_TLS_CAINFO=C:/Program Files/Git/mingw64/etc/ssl/certs/ca-bundle.crt
                   (121 certificates; verified reachable with HTTP 200)
```

`TLS_VERIFY` stays ON and every archive's pinned SHA256 is still enforced, so
content verification is unchanged -- only transport trust was added. A second
attempt then failed on a CMake syntax error of my own making: the path was first
passed with backslashes and CMake wrote it verbatim into the generated download
script, where `\P` is an invalid escape. Forward slashes fixed it. Recorded
because a reader comparing attempt logs will otherwise see three starts.

### 3. Every machine-specific path in the harness pointed at the dead laptop

Adapted as a separate reviewed infrastructure change, six files:

```text
run-qualification.cmd      BETTERCAD_BUILD_ROOT -> C:/bettercad/bc-build
preflight-space.ps1        -SourceTree default
freeze-mesh.ps1            -RootDir and -Scratch defaults
commit-mesh.ps1            -Scratch and -RootDir defaults
postrun-mesh.ps1           $out, $root and $src were HARDCODED, not parameters.
                           They are now -Out, -BuildRoot and -SourceTree, which
                           also retires the dead-session-GUID $out recorded as
                           the fourteenth issue in FREEZE.md: it pointed into an
                           agent session scratchpad that no longer exists.
mutation/mutate-mesh.ps1   its scratch, root, build root and tree paths
```

`qualify.cmd` was **not** touched and is still `d313a640`: it contains no
absolute path, deriving the repository root relatively and using `%TEMP%`. It
stays byte-identical across all fifty-four milestones.

New durable scratch directory: `C:\bettercad\p19-mesh-scratch`, replacing
`C:\Users\hasib\bcdev\p19-mesh-scratch`.

All five PowerShell scripts were parsed with
`System.Management.Automation.Language.Parser` after editing and parse clean.
`run-qualification.cmd` still has every line beginning with one of
`rem / set / call / echo / setlocal / endlocal / @echo`, which `cmd.exe`
requires. Two of the scripts are pure LF in the working tree and four are CRLF;
each was rewritten in the style it already had, since `.gitattributes`
normalises both and changing the style would only add noise.

## What this document claims, and what it does not

```text
the replacement environment IS qualified, on measurement rather than on the
  matching version strings above: all three presets configured with Qt 6.11.2
  and Netgen 6.2.2604 found and the GUI ON, built 780 of 780 edges each with 0
  warnings, and selected and passed 5939 tests per preset and 2803 x 5 in two.
the frozen candidate is UNCHANGED: the twelve identities were re-confirmed
  before the migration edits, before the first build, after the last test and
  after all post-run verification. The edits touch only files under docs/, in
  neither the eight fingerprinted paths nor the four milestone fingerprints.
this document still closes no gate by itself. The gate was closed by the
  harness run that followed, and the marks moved only on that run's evidence.
```

Before any result from this machine could be treated as equivalent to the old
one, the replacement environment had to be qualified in its own right: all three
presets configuring with Qt 6.11.2 and Netgen 6.2.2604 found, the GUI ON, the
full suite selecting **5939** rather than 5928, and the repeat selection
**2803** rather than 2793. Those two counts are the test that the environment is
whole: they are exactly the eleven GUI-dependent tests that a Qt-less build
silently omits, which is how the earlier second-machine run was known to be
incomplete despite passing everything it selected.

Every one of those conditions was met, and measured:

```text
configure, all three presets   Qt 6.11.2 found, Desktop app ON,
                               Netgen 6.2.2604 found, Volume meshing ON,
                               GNU 16.1.0, CMake 4.4.1 (Ninja), -Werror ON
build, all three presets       780 / 780 edges, 0 warnings, 0 errors
full suite, all three presets  5939 / 5939
repeat selection, two presets  2803, x 5, 14015 Passed markers each
the eleven GUI tests           present in all three presets
cli.new.unicode-path           passes in all three (chcp 65001 in force)
```

## A hazard the brief did not anticipate

A fresh process on this machine resolves `gcc` and `cmake` to MSYS2 UCRT64 --
GCC 16.2.0 and CMake 4.4.3 -- and not to the pinned WinLibs bundle, whose WinGet
shim does not even satisfy `where gcc`. Nothing downstream would have caught it:
the test counts, the warning count and all twelve identities would have looked
exactly right while the wrong toolchain was being qualified. `run-qualification.cmd`
now puts the pinned bundle first on `PATH`, ahead of the preflight and `qualify.cmd`
calls, which pins `gcc`, `g++`, `cmake`, `ctest` and `ninja` together because the
bundle carries all five. Verified before the run: GCC 16.1.0, CMake 4.4.1,
Ninja 1.13.2, and the configure logs of all three presets record the WinLibs
compiler path.
