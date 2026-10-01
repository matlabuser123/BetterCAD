# INFRA-NETGEN-001 — the Netgen patch set

```text
SUBJECT:  the four patches BetterCAD carries against Netgen v6.2.2604
LOCATION: deps/patches/netgen/
APPLIED:  deps/ApplyNetgenPatches.cmake, from ExternalProject's patch step
RESULT:   4 patches, 47 changed lines, all upstreamable, none BetterCAD-specific
```

Netgen v6.2.2604 does not build with GCC/MinGW as published. None of the four
fixes is a workaround or a BetterCAD-local hack: each is a defect that any
MinGW consumer hits, and each is small enough to offer upstream unchanged.

## The patches

### `0001-mingw-msvc-only-flags.patch` — MSVC flags given to GCC

`libsrc/core/CMakeLists.txt` applies MSVC-only options under `if(WIN32)`:

```cmake
target_compile_options(ngcore PUBLIC /bigobj $<BUILD_INTERFACE:/MP;/W1;/wd4068>)
target_link_options(ngcore PUBLIC /ignore:4273 /ignore:4217 /ignore:4049)
```

GCC reads `/bigobj` as a filename and fails with `linker input file not
found`. Netgen's CMake contains no `MINGW` handling anywhere, so "Windows"
means "MSVC" throughout.

The patch moves the *options* under `if(MSVC)` and gives GCC `-Wa,-mbig-obj`,
which is `/bigobj`'s equivalent and which BetterCAD already uses on its own
targets. The *definitions* (`WNT`, `NOMINMAX`, `_WIN32_WINNT`, …) stay under
`if(WIN32)`, because any Windows compiler needs them — moving those too would
be the easy over-correction and would break the build differently.

### `0002-mingw-getprocaddress-cast.patch` — `FARPROC` to `void*`

`libsrc/core/utils.cpp:206`:

```cpp
void* func = GetProcAddress((HMODULE)lib, func_name.c_str());
```

`GetProcAddress` returns `FARPROC`, a function pointer. MSVC permits the
implicit conversion to `void*`; GCC rejects it, correctly. One
`reinterpret_cast`, which the POSIX `dlsym` branch of the same function needs
anyway.

### `0003-mingw-dllimport-inline-defs.patch` — defining a `dllimport` function

`libsrc/core/bitarray.hpp` marks two functions `NGCORE_API` and then defines
them in the header:

```cpp
NGCORE_API auto * Data() const { return data; }
NGCORE_API TBitArray & Or (const TBitArray & ba2) { ... }
```

For a consumer, `NGCORE_API` expands to `__declspec(dllimport)`. Declaring
that a function is imported from a DLL and supplying its body in the same
breath is a contradiction: MSVC warns and carries on, GCC rejects it. The
patch removes `NGCORE_API` from the two definitions. Nothing is exported that
was not already — an inline function defined in a header never needed to be.

### `0004-version-without-git.patch` — a release that misnames itself

Two linked defects, and the only patch of the four that is about correctness
rather than portability.

`cmake/generate_version_file.cmake` derives the version with `git describe`.
It has a fallback for sources that are not a git checkout — but the fallback
is unreachable, because the line before it is `find_package(Git REQUIRED)`,
which aborts the build first. **Netgen v6.2.2604 cannot be built from its own
release tarball on a machine without git**, although the code to handle
exactly that case is sitting right there.

With git present but the source still not a checkout — the normal case for a
release archive — `git describe` fails, the fallback runs, finds no
`version.txt`, and settles on a hardcoded default. The result, observed on the
first build made here:

```c
#define NETGEN_VERSION "6.2.0"      /* actually 6.2.2604 */
```

A pinned dependency that misreports its own version defeats the point of
pinning it, and it is the kind of error that is discovered much later and
believed for a long time.

The patch makes `find_package(Git)` optional so the existing fallback is
reachable, and forwards `NETGEN_VERSION_GIT` — which the script already
honours as its first choice — from the cache into the custom target that
regenerates the header. BetterCAD then passes the version it pinned:

```cmake
-DNETGEN_VERSION_GIT:STRING=v6.2.2604-0-0
```

`tests/meshing/VolumeBackendTests.cpp` asserts the value the built library
reports, so this cannot silently regress.

## How they are applied

`deps/ApplyNetgenPatches.cmake`, from `ExternalProject`'s patch step. `git` is
used purely as a patch tool; it needs no repository.

Two properties matter and are tested:

```text
idempotent   a patch already present is detected (git apply --reverse --check)
             and skipped, so re-running the patch step does not corrupt the
             source tree

loud         a patch that neither applies nor is already present is a
             FATAL_ERROR naming the file. It never half-applies and continues:
             that would build a Netgen that is not the one that was qualified
```

The invocation pins `core.autocrlf=false` and `core.eol=lf` on the command
line. Without that, `git apply` consults the *user's* global configuration
even outside a repository, and a developer with `autocrlf=true` would get a
source tree whose patched files have CRLF endings while everyone else's have
LF. The compiled result is identical either way; the source tree of a
qualified dependency should not vary with a personal setting.

## Verification

```text
applied to a pristine extraction of the pinned tarball        4/4 applied
applied a second time to the same tree                        4/4 skipped, exit 0
resulting tree vs the tree that was built and qualified       byte-identical
build from that tree, with git absent from PATH entirely      exit 0
version reported by the built library                         6.2.2604
```

The third line is the one that matters: the patch set in the repository
reproduces, byte for byte, the source tree the qualification was run against.

## Upstreaming

All four are general MinGW/portability fixes with no BetterCAD content. They
should be offered to NGSolve/netgen. Doing so is not a precondition for this
milestone — carrying them is already reproducible and hash-pinned — and no
claim is made here that they have been submitted.
