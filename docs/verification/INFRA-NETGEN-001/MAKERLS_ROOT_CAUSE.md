# INFRA-NETGEN-001 — root cause of the `makerls` crash and hang

```text
SUBJECT:  why Netgen's mesh-rule generator failed, and why the
          P16-VOL-001 BLOCKED report was wrong
RESULT:   NOT a Netgen defect. A C++ runtime ABI mismatch in the
          investigating environment.
DATE:     2026-10-01
```

## What was reported, and what was actually true

`P16-VOL-001` was reported BLOCKED on the finding that `makerls` — Netgen's
build-time generator that turns the `.rls` mesh-rule files into C++ — took an
access violation at `-O3` and hung at `-O0`, on all seven rule files. The
conclusion drawn was that Netgen exhibits undefined behaviour under GCC 16
MinGW and therefore cannot be qualified.

**The observations were real. The conclusion was wrong.** `makerls` has no
defect that this toolchain exposes. It was loading a C++ standard library it
was not compiled against.

This document exists because a false BLOCKED is a more expensive error than a
missed one: it stops a milestone, invites a backend or toolchain change that
was never necessary, and the reasoning that produced it was confident.

## The mechanism

`makerls.exe` was compiled by WinLibs GCC 16.1.0, a **UCRT** toolchain. It
links `libstdc++-6.dll` dynamically. On Windows the loader resolves that name
by searching the executable's own directory first, then the system
directories, then `PATH`.

The investigating shell's inherited `PATH` contained an MSYS2 `mingw64/bin`
at positions 2 and 6, and the WinLibs toolchain at position 29. MSYS2's
`mingw64` environment is **msvcrt**-based, not UCRT. So:

```text
makerls.exe          compiled against  WinLibs libstdc++ (UCRT)
loaded at run time                     MSYS2  libstdc++ (msvcrt)
```

Both C runtimes were then resident in the one process — `ucrtbase.dll` and
`msvcrt.dll` together, each with its own `FILE*` tables, heap and locale
state.

The visible consequence was in `std::basic_ios`. Its stream state was being
written through one runtime's view of the object and read through the other's,
so the stream's own accessors contradicted each other: `operator bool` and
`fail()` disagreed about the same stream. `makerlsfile.cpp:36`

```cpp
while (inf.good())
```

therefore never terminated when the compiler had not cached the condition —
the `-O0` hang — and at `-O3`, where the optimiser relies on the object
actually obeying its invariants, it faulted instead. One cause, two symptoms
that looked like two different bugs.

## How it was established rather than guessed

```text
1. Both ucrtbase.dll and msvcrt.dll were present in the faulting process,
   which cannot happen in a correctly linked MinGW program.

2. The libstdc++-6.dll that was loaded resolved to MSYS2's mingw64/bin, not
   to the compiler's own directory.

3. Prepending the WinLibs directory to PATH did NOT fix it.

4. Copying the compiler's own libstdc++-6.dll, libgcc_s_seh-1.dll and
   libwinpthread-1.dll next to makerls.exe DID fix it, immediately and
   completely: all seven rule files generated, at -O3, first attempt.

Step 3 and step 4 together are the proof. If the fault had been in Netgen's
source, neither would have changed anything; and a PATH change not helping
while a file placed beside the executable does is precisely the signature of
Windows DLL search order, because the executable's directory is searched
before PATH.
```

## The hypothesis that was tested and rejected

The first explanation attempted was a buffer overflow in `makerlsfile.cpp`,
which reads each rule-file line into a fixed buffer:

```cpp
#define maxlen 1000
char line[maxlen];
...
while (ch != '\n' && inf.good() && i < maxlen)
  { ... line[i] = '\\'; line[i+1] = '\"'; i += 2; ... }
```

This **is** a latent defect — the guard tests `i < maxlen` and the escape
branches then write both `line[i]` and `line[i+1]`, so a line of exactly the
wrong length writes one byte past the end. It is not the cause. The longest
line in any of Netgen's rule files is 86 characters:

```text
hexrules.rls       86     prismrules2.rls    53     pyramidrules.rls   47
quadrules.rls      75     prisms.rls         53     pyramidrules2.rls  47
tetrules.rls       75     triarules.rls      75
```

86 against a 1000-byte buffer. The overflow is unreachable with the inputs
Netgen ships, and it is reported here only so the next reader does not
rediscover it and draw the same wrong conclusion.

## What was done about it

Not a workaround. `makerls` is now linked **statically**:

```cmake
-DCMAKE_EXE_LINKER_FLAGS:STRING=-static
```

It then has no `libstdc++-6.dll`, `libgcc_s_seh-1.dll` or
`libwinpthread-1.dll` import at all, so there is no name for the loader to
resolve and no PATH on any machine that can resolve it wrongly. Verified on
the built binary: its import table lists `KERNEL32.dll` and the UCRT apiset
DLLs and nothing else.

`CMAKE_EXE_LINKER_FLAGS` affects executables only. `nglib` and `ngcore` remain
shared libraries and are unchanged by it.

Copying runtime DLLs beside the executable would also have worked, and was
what proved the diagnosis, but it leaves the failure mode available to anyone
who builds the dependency in a differently-ordered environment. Static linkage
removes it by construction, which is the difference between a fix and a
mitigation.

## That the fix did not change the result

The mesh rules are the mesher, so a change to how the generator is linked must
be shown not to change what it generates. The seven generated files were
compared byte for byte between

```text
build A   makerls dynamically linked, correct runtime placed beside it
build B   makerls statically linked, deliberately restricted PATH
```

```text
rule_tetrules.cpp       28274 bytes      identical
rule_triarules.cpp       8399 bytes      identical
rule_quadrules.cpp      15961 bytes      identical
rule_hexrules.cpp        4171 bytes      identical
rule_prismrules2.cpp     7557 bytes      identical
rule_pyramidrules.cpp    4390 bytes      identical
rule_pyramidrules2.cpp   5599 bytes      identical

byte-identical: 7 / 7
```

## Consequence for the earlier report

`docs/verification/P16-VOL-001/README.md` claimed that Netgen could not be
qualified because a dependency showing undefined behaviour under this compiler
cannot be trusted as a numerical component. The *principle* stands. The
*finding* it was applied to does not: there was no undefined behaviour in
Netgen, and the environment that produced the symptom was the investigation's
own.

That file has been corrected rather than deleted, and the correction is
recorded in its Revision section. See also the RESULT section of
[README.md](README.md).
