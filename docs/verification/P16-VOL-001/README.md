# P16-VOL-001 — 3D Tetrahedral Volume Mesh

```text
STATUS:      NOT IMPLEMENTED -- no longer blocked
TASK:        P16-VOL-001 -- 3D tetrahedral volume mesh
PHASE:       P16 -- Meshing
DATE:        2026-10-01 (first issue), corrected 2026-10-01 (second issue)
```

> **CORRECTION, second issue.** This document originally reported the milestone
> BLOCKED because Netgen "does not build on this toolchain", on the strength of
> a crash and a hang in Netgen's mesh-rule generator. **That diagnosis was
> wrong.** The generator has no defect this toolchain exposes; it was loading a
> C++ standard library it had not been compiled against, because of the
> investigating shell's `PATH`. Netgen v6.2.2604 builds, runs and meshes
> correctly with GCC 16.1.0 MinGW.
>
> `INFRA-NETGEN-001` established that, found the real cause, and made Netgen a
> qualified BetterCAD dependency. See
> [../INFRA-NETGEN-001/MAKERLS_ROOT_CAUSE.md](../INFRA-NETGEN-001/MAKERLS_ROOT_CAUSE.md).
>
> The sections below are kept as written, with the incorrect finding marked, so
> the error and its correction stay visible rather than being quietly edited
> away. **Nothing here is a claim that P16-VOL-001 is implemented** — it is
> not, it remains 0/19, and it still needs its own authorization in
> `TODO.md` before any of it is written.

**No production code was written.** The repository is unchanged apart from this evidence
directory and the TODO entry recording the block. No adapter exists, no tests were written
against a backend that does not run, and no milestone box is ticked.

## Baseline

```text
branch        main
HEAD          4005815f6f7177d1a071ce206d7fca336f1a517a
origin/main   4005815f6f7177d1a071ce206d7fca336f1a517a   (HEAD == origin/main)
tree          b32da8cc4dc952db3098caefb7a2bb12ed5ca69e
log           4005815 BetterCAD: add validated engineering surface mesh
working tree  clean
build root    C:/Users/uqhas/AppData/Local/bc-build
compiler      GNU 16.1.0 (MinGW, WinLibs POSIX UCRT), C++23
OCCT          8.0.1
```

## Prerequisites

All four verified before anything was attempted:

```text
P16-ARCH-001   25/25 ticked, PASS marker   evidence at 9964f88
P16-DATA-001   26/26 ticked, PASS marker   evidence at 20b04b9
P16-GEOM-001   20/20 ticked, PASS marker   evidence at 9c755e8
P16-SURF-001   21/21 ticked, PASS marker   evidence at 4005815
```

None of them is the blocker. The milestone is blocked on a dependency, not on its predecessors.

## The backend decision, and the approval that cleared it

`P16-ARCH-001` established that BetterCAD and OCCT 8.0.1 contain **no volume mesher**, so one
must come from outside, and that the choice is licence-gated because `LICENSE` grants no
permission to distribute BetterCAD at all — a GPL or AGPL mesher would decide the project's
unchosen licence.

Applying the admission rule derived from obligations BetterCAD already accepts left exactly one
candidate of six:

```text
Netgen        LGPL-2.1        ADMISSIBLE -- same obligation class as OCCT
MMG           LGPL-3+         admissible by licence, but it MODIFIES meshes
Gmsh          GPL-2+          not admissible
CGAL Mesh_3   GPL             not admissible, and re-discretises the boundary
TetGen        AGPL-3          not admissible
OCCT 8.0.1    --              has no volume mesher at all
```

**The project owner approved Netgen on 2026-10-01.** That cleared the entry condition
`P16-ARCH-001` placed on this milestone, and it is why the work was attempted rather than
refused outright.

## The blocker

**Netgen v6.2.2604 does not build on this toolchain.** Three distinct problems were found, in
increasing order of seriousness. The first two were fixed; the third is not a patch.

### 1. MSVC compiler flags passed to GCC — FIXED

`libsrc/core/CMakeLists.txt` applies MSVC-only options under `if(WIN32)` rather than
`if(MSVC)`:

```cmake
target_compile_options(ngcore PUBLIC /bigobj $<BUILD_INTERFACE:/MP;/W1;/wd4068>)
target_link_options(ngcore PUBLIC /ignore:4273 /ignore:4217 /ignore:4049)
```

GCC receives `/bigobj` as a filename: `error: /bigobj: linker input file not found`. Netgen's
build assumes Windows means MSVC — consistent with its CMakeLists containing no `MINGW`
handling at all.

**Patch:** move the options under `if(MSVC)`, keep the *definitions* (`WNT`, `NOMINMAX`,
`_WIN32_WINNT`) under `if(WIN32)` because any Windows compiler needs them, and give GCC
`-Wa,-mbig-obj`, which is `/bigobj`'s equivalent and which BetterCAD already uses itself. Small
and upstreamable. It took the build from 0 to 17 of 167 objects.

### 2. `FARPROC` to `void*` without a cast — FIXED

`libsrc/core/utils.cpp:206`:

```cpp
void* func = GetProcAddress((HMODULE)lib, func_name.c_str());
```

`GetProcAddress` returns `FARPROC`, a function pointer. MSVC permits the implicit conversion;
GCC rejects it. **Patch:** one `reinterpret_cast`, which POSIX `dlsym`-style code needs anyway.
Also upstreamable. `ngcore` then compiled.

### 3. `makerls` crashes, and hangs — **THIS FINDING IS WRONG**

> **Superseded.** The two symptoms below were observed exactly as described,
> but they are not Netgen's. `makerls.exe` was compiled by a UCRT toolchain and
> was loading an msvcrt-based `libstdc++-6.dll` from an MSYS2 directory that
> sat earlier on `PATH`, putting two C runtimes in one process and corrupting
> `std::basic_ios` state — hence a loop that never ended at `-O0` and a fault
> at `-O3`. Netgen is now built with `makerls` statically linked, which removes
> the failure by construction, and it generates all seven rule files. Full
> analysis, including the evidence that distinguishes the two explanations:
> [../INFRA-NETGEN-001/MAKERLS_ROOT_CAUSE.md](../INFRA-NETGEN-001/MAKERLS_ROOT_CAUSE.md).

`makerls` is Netgen's **own build-time code generator**: it converts the `.rls` mesh-rule files
into C++. Built by the superbuild at `-O3`, it fails on **every one of the seven rule files**:

```text
FAILED: [code=3221225477] rules/rule_tetrules.cpp
FAILED: [code=3221225477] rules/rule_triarules.cpp
FAILED: [code=3221225477] rules/rule_quadrules.cpp
FAILED: [code=3221225477] rules/rule_hexrules.cpp
FAILED: [code=3221225477] rules/rule_prismrules2.cpp
FAILED: [code=3221225477] rules/rule_pyramidrules.cpp
FAILED: [code=3221225477] rules/rule_pyramidrules2.cpp
```

`3221225477` is `0xC0000005` — an **access violation**.

Rebuilt from the same source at `-O0` to test whether the optimiser was exposing undefined
behaviour, it does not crash. It **hangs**: two processes were still running after more than two
minutes on a 17.9 KB input file, and had to be killed.

```text
makerls at -O3   access violation, immediately, on all 7 rule files
makerls at -O0   hangs indefinitely on a 17.9 KB input
```

So the generator is non-functional on this toolchain either way, and the behaviour is
characteristic of undefined behaviour that MSVC happened to tolerate.

### Why this blocks the milestone rather than being one more patch — **PREMISE WITHDRAWN**

> **Superseded.** The reasoning below is sound and the project should keep
> applying it. It was applied to a finding that turned out to be false, so its
> conclusion does not hold. There was no undefined behaviour in Netgen to
> disqualify it.

**The rule files are the mesher.** `tetrules.rls` is the rule set that drives tetrahedral
generation. Without `makerls` there are no rules, and without rules there is no volume mesher —
this is not a peripheral tool.

And there is a second reason, which matters more than whether the build can be coaxed into
finishing. **A dependency that exhibits undefined behaviour under this compiler cannot be
qualified as a numerical component.** Suppose `makerls` were made to run — at `-O0`, or with the
UB found and patched. The mesh rules it generated would then feed a mesher compiled by the same
toolchain that just produced an access violation in the same codebase. P16's premise is that a
mesh is *validated*, not merely produced; "the generator crashed at -O3 so we built it at -O0"
is not a foundation to qualify a solver boundary on.

> **What actually happened.** `makerls` was neither run at `-O0` nor patched.
> It is linked statically, so it cannot load a foreign C++ runtime at all, and
> it then generates all seven rule files at `-O3` — byte-for-byte identical to
> the files produced once the correct runtime was placed beside the dynamically
> linked build. The mesh rules are the same rules upstream ships; nothing was
> coaxed, and no tolerance or optimisation level was traded away.

## What was NOT done, deliberately

```text
no adapter written          against a backend that does not run, the tests would
                            pass against nothing
no substitute backend       Netgen is the only candidate that survives the licence
                            admission rule, and swapping it would quietly undo the
                            owner's decision. The brief forbids it explicitly
no vendored binary          a backend that exists only in a scratch directory is
                            not a qualified dependency: the regression's fresh
                            binary proof would be meaningless and no clean
                            checkout could reproduce it
no boxes ticked             P16-VOL-001 remains 0/19
```

## What would unblock it — **DONE**

> `INFRA-NETGEN-001` was carried out and Netgen is now a qualified BetterCAD
> dependency: pinned by tag and SHA-256 in `deps/CMakeLists.txt`, built from
> source by the same toolchain, with four upstreamable patches in
> `deps/patches/netgen/`. Every bullet below was addressed, including the
> open-ended one — the `makerls` fault was found, and it was not Netgen's.
> One bullet turned out to understate the problem: Netgen's superbuild does
> not merely *download* zlib, it downloads a **prebuilt MSVC binary**, which
> has been eliminated rather than admitted.
>
> Evidence: [../INFRA-NETGEN-001/](../INFRA-NETGEN-001/README.md).
>
> The two alternatives below were therefore **not** taken: the toolchain is
> unchanged and the licence decision stays untouched and the owner's.
>
> `P16-VOL-001` itself is still unimplemented and still needs authorization.

An infrastructure milestone, because this is infrastructure work and not what this milestone's
brief describes — its ninety-odd sections are about the adapter, Tet4 validation, occupancy,
void preservation, boundary extraction and determinism, none of it porting a mesher to a new
toolchain.

```text
INFRA-NETGEN-001   make Netgen a qualified BetterCAD dependency

  * pin v6.2.2604 and add it to deps/CMakeLists.txt as an ExternalProject,
    beside Qt and OCCT, so a clean checkout reproduces it
  * carry the two patches above, and upstream them -- both are small and
    general, and neither is BetterCAD-specific
  * FIND AND FIX the makerls defect, or establish that Netgen cannot be built
    with GCC 16 MinGW at all. That is the open-ended part and the reason this
    is its own milestone
  * record what Netgen pulls in: its superbuild downloads zlib, so admitting
    Netgen admits zlib too, and the evidence must say so
  * prove the built mesher is correct on this toolchain before anything
    qualifies it -- a mesher with latent UB that happens to return a mesh is
    worse than none
```

Two alternatives are worth weighing before committing to that, and both are the owner's call:

```text
a different toolchain for this dependency   Netgen is routinely built with MSVC
                                            and on Linux. Building it with MSVC
                                            while BetterCAD uses MinGW raises its
                                            own ABI questions and is not obviously
                                            cheaper.

reopening the licence decision              Gmsh (GPL-2+) builds readily and is
                                            widely used, but admitting it decides
                                            BetterCAD's unchosen licence. That is
                                            a project decision, not an engineering
                                            convenience.
```

## Reproduction

Everything above is reproducible from a clean tree. Nothing was done inside the repository.

```text
curl -L https://github.com/NGSolve/netgen/archive/refs/tags/v6.2.2604.tar.gz | tar xz
cmake -S netgen-6.2.2604 -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DUSE_GUI=OFF -DUSE_PYTHON=OFF -DUSE_OCC=OFF -DUSE_MPI=OFF \
      -DUSE_NATIVE_ARCH=OFF -DUSE_STLGEOM=OFF -DUSE_CSG=OFF \
      -DUSE_GEOM2D=OFF -DUSE_INTERFACE=OFF -DENABLE_UNIT_TESTS=OFF
cmake --build build
```

`USE_NATIVE_ARCH=OFF` is deliberate and should stay: `-march=native` would make meshing results
depend on the build machine's instruction set, which the determinism contract forbids.

## What the investigation did establish, and is worth keeping

The integration design is settled even though it could not be built, and it is better than the
alternative the architecture allowed for:

```text
nglib takes a surface mesh directly
    Ng_Init / Ng_NewMesh / Ng_AddPoint / Ng_AddSurfaceElement
    Ng_GenerateVolumeMesh / Ng_GetNP / Ng_GetNE / Ng_GetVolumeElement

so the input is P16-SURF-001's VALIDATED engineering surface -- watertight,
outward-oriented, conforming, with shared NodeIds -- rather than a second
unvalidated boundary route, which is what the brief's section 6 asks for.

Netgen's own OCC front end is switched OFF (USE_OCC=OFF), so there is exactly
one path from CAD to mesh and it runs through the boundaries P16-GEOM-001 and
P16-SURF-001 already qualified.
```

That design should be reused when the backend becomes available. It is recorded here so the
next attempt does not have to rediscover it.

## Result

```text
RESULT:   NOT IMPLEMENTED, no longer blocked
REASON:   the block was misdiagnosed. Netgen v6.2.2604 (LGPL-2.1) DOES build,
          run and mesh with GCC 16.1.0 MinGW. The crash and hang in its
          mesh-rule generator were caused by the investigating environment
          loading a mismatched C++ runtime, not by Netgen.
BACKEND:  qualified by INFRA-NETGEN-001 and pinned in deps/CMakeLists.txt.
NEXT:     P16-VOL-001 is implementable, but it is NOT authorized by this
          document. Authorization comes from TODO.md alone.
TODO:     P16-VOL-001 remains 0/19, unimplemented.
```

## Revision

```text
First issue    2026-10-01   reported BLOCKED on a Netgen build failure.
Second issue   2026-10-01   CORRECTED. The third and decisive finding of the
                            first issue was wrong: makerls has no defect that
                            GCC 16 MinGW exposes, and Netgen is buildable.
                            The block is withdrawn. Superseded passages are
                            marked in place rather than deleted.
```

**Why the first issue reached the wrong conclusion**, recorded because the
reasoning was confident and the failure mode is easy to repeat:

```text
two symptoms were treated as corroboration    a crash at -O3 and a hang at -O0
                                              looked like two independent signs
                                              of undefined behaviour. They were
                                              one cause with two faces.

the environment was not a suspect             every hypothesis tested was about
                                              Netgen's source. The question
                                              "which libstdc++ is this process
                                              actually loading" was not asked
                                              until much later, and answered it
                                              immediately.

a sound principle made a wrong finding feel   "a dependency showing UB cannot be
safe to act on                                qualified" is correct, and arguing
                                              it well made the premise feel
                                              checked when it had not been.
```
