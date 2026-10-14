# P17-CLI-001 — backend chatter and the stdout channel

```text
backend                 Netgen 6.2.2604, through nglib
known chatter path      "WARNING: RestrictLocalH called, creating mesh-size
                        tree", written to STDOUT
suppression layer       THE ADAPTER -- src/meshing/netgen/NetgenBackend.cpp
consumer filtering      NO
raw stdout parse        PASS
P16 change needed       NONE
```

## It was already fixed, in the right place, by the milestone that found it

The brief's §76 and §206 ask for this defect to be reproduced and fixed in the
Netgen adapter. **P16-CLI-001 already did**, and the comment on
`SilentBackendScope` is the brief's own argument written out before the brief
existed:

> NETGEN WRITES TO STDOUT. Meshing with a local size restriction prints
> "WARNING: RestrictLocalH called, creating mesh-size tree" — which is
> informational noise about an internal data structure, not an engineering
> diagnostic, and nothing a caller can act on. BetterCAD's own meshing path
> prints nothing at all.
>
> It has to be contained HERE, in the adapter that owns the backend's quirks,
> because stdout is a shared resource and no consumer can be asked to tolerate
> a library writing to it. **P16-CLI-001 found this the hard way**: a
> structured `mesh-info --json` was unparseable, because the first line a
> script read was Netgen's warning rather than the opening brace. **Fixing it
> in the CLI would have left the GUI, the tests and any future consumer with
> the same corrupted stream and the CLI compensating for it.**

So this milestone changes nothing about it, and **no P16 requalification
follows from backend suppression**. What P17 owes is a measurement that the
inheritance actually holds for a consumer that did not exist when the fix was
made.

## The mechanism, and its one honest limit

```cpp
class SilentBackendScope {
public:
    SilentBackendScope() : saved_(std::cout.rdbuf(&sink_)) {}
    ~SilentBackendScope() { std::cout.rdbuf(saved_); }
    ...
};
```

The **streambuf is swapped rather than the stream replaced**, so anything the
backend writes is discarded while BetterCAD's own use of `std::cout` outside
the scope is unaffected. The backend mutex is already held whenever it is
used, so the swap cannot race with another mesh — which answers the brief's
§205 about thread safety without any new mechanism.

**The limit, stated:** this catches C++ `std::cout`. A library writing to the C
`stdout` FILE* with `printf` would not be caught by it. Netgen's nglib path
does not, which is what the measurement below shows; a future backend that did
would need the adapter extended, not a consumer wrapped. No `freopen` hack was
added to the CLI — the brief's §205 forbids one and there was no need.

## The measurement

`StructuralCli_StructuredOutputIsTheWholeOfStdout` captures raw stdout from all
six structural reports and requires, for each:

```text
stdout.front() == '{'              nothing before the payload
stdout.back()  == '\n'             one trailing newline
stdout[n-2]    == '}'              nothing after the payload
braces balanced outside strings    no prefix, no suffix, no interleaving
```

**Four of those six run the mesher and the solver**, so Netgen's chatter path
is live during the capture: a sizing restriction is applied, a mesh-size tree
is created, and the warning that broke P16's `mesh-info --json` is emitted
inside the adapter and discarded there.

The multi-process workflow is the stronger half, because it reads the real
executable's stdout as a shell would: every payload goes through CMake's
`string(JSON)` — a genuine parser with no `ERROR_VARIABLE`, so **any**
contamination is a fatal error rather than a regex that still happened to
match. Nine separate processes, every payload parsed.

```text
consumer-side filtering used:   NO
  no `2>/dev/null`, which would not have helped anyway -- the chatter is on
    STDOUT, which is the whole lesson
  no `| tail -n 1`, no `sed` extracting the braces, no `grep '{'`
  checked by grep: the only match for those words in the workflow script is
    the comment saying there are none
```

## The other libraries

| Library | Writes to stdout? | Evidence |
| --- | --- | --- |
| Netgen (nglib) | **yes** | P16-CLI-001 found it; contained in the adapter |
| Eigen (the solver) | no | no output appeared in any of the nine workflow payloads, each of which factorises |
| OCCT (geometry, via the regenerator) | no | same — every payload is a clean parse, and all nine regenerate |
| nlohmann (the document reader) | no | same |

These are measurements over the runs that happened, not a survey of the
libraries' source. What they establish is that **the structured channel is
clean for every path P17 exercises**, which is the claim that matters; a
library that began printing later would be caught by the same parse gate on
the next run.

## There is no raw write in the CLI either

```text
std::cout / printf / puts in StructuralReports.cpp   0
std::cout / printf / puts in StructuralEdits.cpp     0
std::cout anywhere in apps/bettercad_cli/            1 -- main.cpp, passing it
                                                     to cli::run as the `out`
                                                     stream
```

Every report writes through its `std::ostream& out` parameter, which is what
lets the in-process tests capture it at all. A `--verbose` flag that wrote to
stdout would break the contract; there is none.

Probe **L4** adds `out << "debug: emitting\n"` immediately before the payload
and is **killed**, so the gate is demonstrated to have teeth rather than
assumed to.
