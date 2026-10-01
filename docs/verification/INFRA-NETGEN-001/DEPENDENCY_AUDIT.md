# INFRA-NETGEN-001 — dependency closure and licence audit

```text
SUBJECT:  everything admitting Netgen brings into BetterCAD
RESULT:   two new source dependencies (Netgen LGPL-2.1, zlib Zlib).
          No new binary dependency. No new obligation class.
DATE:     2026-10-01
```

## What is admitted

```text
Netgen   v6.2.2604   LGPL-2.1   https://github.com/NGSolve/netgen
                     sha256 0a614193ee6106c0c276a1516639bef872f2f2371dec159e1b1f6ffcb425954c
                     3 308 713 bytes, tarball of tag v6.2.2604

zlib     1.3.1       Zlib       https://github.com/madler/zlib
                     sha256 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
```

Both are pinned by tag *and* by hash in `deps/CMakeLists.txt`, the same way Qt
and OCCT already are. A changed archive fails the build rather than being
built.

### Why zlib is here at all

zlib is not a BetterCAD dependency and nothing in BetterCAD includes it. It is
here because Netgen's `libsrc/general/gzstream.{h,cpp}` wraps zlib for
compressed mesh file I/O, and `gzstream.h` is included by `meshclass.cpp` —
Netgen's core mesh Save/Load — so it cannot be excised without invasive
patching of code BetterCAD does not call. The honest result is that admitting
Netgen admits zlib.

zlib's licence is the Zlib licence: permissive, no copyleft, no attribution
requirement beyond not misrepresenting origin. It adds no obligation class.

## The prebuilt binary that was removed

Netgen's superbuild does **not** build zlib on Windows. It downloads one:

```cmake
# netgen-6.2.2604/cmake/external_projects/zlib.cmake
ExternalProject_Add(project_win_zlib
  URL ${ZLIB_DOWNLOAD_URL_WIN}      # .../ngsolve_dependencies/releases/.../zlib_win64.zip
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_directory lib ...
```

No configure step, no build step: a zip of prebuilt binaries copied into the
install tree. Inspecting the `zlib.dll` it delivers:

```text
zlib.dll imports:  KERNEL32.dll
                   VCRUNTIME140.dll        <-- MSVC runtime
                   api-ms-win-crt-*.dll
```

It is **MSVC-built**. Loading it alongside a MinGW `libstdc++` puts two C++
runtimes in one process — the same class of fault as the `makerls` failure
documented in [MAKERLS_ROOT_CAUSE.md](MAKERLS_ROOT_CAUSE.md) — and it is an
opaque third-party binary of unverifiable provenance, which this project does
not ship.

It is gone. BetterCAD builds Netgen with `USE_SUPERBUILD=OFF`, which makes
Netgen fall back to `find_package(ZLIB REQUIRED)`, and supplies a zlib built
from pinned source by the same compiler. Confirmed on the built library:

```text
libnglib.dll imports libzlib.dll     (ours, MinGW, built from zlib-1.3.1 source)
                 NOT zlib.dll        (theirs, MSVC, downloaded)
```

`USE_SUPERBUILD=OFF` also removes the build-time download of prebuilt OCC and
Tcl/Tk binaries from the same third-party release page. **Nothing BetterCAD
links is downloaded as a binary.**

## Runtime closure

What `libnglib.dll` actually requires at load time, read from its import
table rather than assumed:

```text
libngcore.dll        Netgen core          built here
libzlib.dll          zlib 1.3.1           built here
libstdc++-6.dll      GCC C++ runtime      already deployed by BetterCAD
libgcc_s_seh-1.dll   GCC runtime          already deployed by BetterCAD
libwinpthread-1.dll  GCC threads          already deployed by BetterCAD
KERNEL32.dll + api-ms-win-crt-*.dll       Windows / UCRT
```

The three GCC runtime DLLs were already copied into the runtime output
directory by `cmake/BetterCADRuntime.cmake` before this milestone. The two new
ones reach the build tree through `$<TARGET_RUNTIME_DLLS>`, which resolves
them because `cmake/FindNetgen.cmake` declares `Netgen::nglib`,
`Netgen::ngcore` and `ZLIB::ZLIB` as imported targets with their DLL as
`IMPORTED_LOCATION`. No PATH setup, no manual copy step.

This closure was discovered the hard way and is recorded because getting it
wrong is silent until run time: an executable links successfully against
import libraries and only then fails to start. The first run of the standalone
probe failed with a Windows loader error naming
`api-ms-win-crt-string-l1-1-0.dll`, which is present on every Windows 10+
machine and was not the problem at all — the missing files were
`libngcore.dll` and zlib. **A Windows DLL-load error names an unreliable
culprit; read the import table instead.**

## What Netgen does *not* bring in

```text
pybind11     external_dependencies/pybind11 is a git submodule and is EMPTY in
             the release tarball. USE_PYTHON=OFF, so it is never referenced.

Python       USE_PYTHON=OFF. No interpreter, no runtime, no headers.

Tcl / Tk     USE_GUI=OFF.
Togl         ng/Togl2.1/ ships source under a permissive X11-style licence,
             but ng/CMakeLists.txt guards it with if(USE_GUI) and nothing from
             it is compiled, linked or installed.
OpenGL / X11 USE_GUI=OFF.

OCCT         USE_OCC=OFF. Netgen does NOT get its own copy of Open CASCADE,
             and there is exactly one path from CAD to mesh (ADR-033).

MPI          USE_MPI=OFF. Also a determinism requirement, not just a size one.

CGNS, FFmpeg, libjpeg, METIS, MKL, NUMA     all OFF, all default OFF.
```

Verified against the install tree, which contains exactly two libraries:

```text
bin/libngcore.dll   bin/libnglib.dll   lib/*.dll.a   cmake/*   include/*
share/netgen/*      example .geo/.stl/.step files and ng4.pdf
```

`share/netgen` is Netgen's own example geometry and manual. It is installed
into the dependency prefix because Netgen installs it unconditionally; it is
not used, not referenced and not redistributed by BetterCAD.

## Licence position

BetterCAD's `LICENSE` grants no distribution permission — the project licence
is still unchosen — so `P16-ARCH-001` set the admission rule that a dependency
must not *decide* that choice. LGPL-2.1 does not: it is the same obligation
class BetterCAD already accepts for Open CASCADE (LGPL-2.1 with an exception),
and it attaches to Netgen, not to BetterCAD.

```text
Netgen   LGPL-2.1   same obligation class as OCCT, which is already accepted
zlib     Zlib       permissive; adds no obligation
```

Two practical conditions follow from LGPL-2.1, and both are already satisfied
by how the dependency is built:

```text
dynamic linking      nglib and ngcore are SHARED libraries. BetterCAD links
                     the import library and loads the DLL; it does not absorb
                     Netgen into its own binary.

source availability  the exact source is a hash-pinned public tarball, and
                     the four modifications are in deps/patches/netgen/ as
                     readable diffs rather than a mutated private copy.
```

Nothing here decides BetterCAD's own licence, which remains the owner's to
choose.

## Reproducibility

```text
network at build time   two hash-verified source archives, from github.com
                        (netgen) and github.com (zlib). No binary downloads.
patches                 4 files, 47 changed lines, in-repo, applied by an
                        idempotent script that fails loudly on drift
toolchain               built by the same compiler as BetterCAD, into the
                        same toolchain-keyed prefix
determinism             USE_NATIVE_ARCH=OFF, so the result does not depend on
                        the build machine's instruction set
```
