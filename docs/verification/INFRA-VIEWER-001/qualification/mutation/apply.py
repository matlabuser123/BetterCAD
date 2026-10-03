"""Applies one mutation from mutations.json, or restores the pristine sources.

    python apply.py snapshot      take the pristine copies
    python apply.py <index>       apply that mutation to the pristine text
    python apply.py restore       put every pristine text back
    python apply.py count         how many mutations there are
    python apply.py label <i>     that mutation's label

Each mutation names the FILE it applies to, because the provenance chain this
milestone builds runs through four of them: the kernel adapter, the surface
mesh, the volume mesh and the mapping. A mutation is applied to the PRISTINE
text, never to an already-mutated file, so mutations cannot compound. A pattern
that is absent, or that occurs more than once, is refused rather than guessed
at: a mutation landing somewhere unintended would make its verdict meaningless.
"""
import json
import pathlib
import sys

HERE = pathlib.Path(__file__).parent
ROOT = HERE.parents[4]
PRISTINE = HERE / "pristine"
MUTATIONS = json.loads((HERE / "mutations.json").read_text(encoding="utf-8"))
FILES = sorted({mutation["file"] for mutation in MUTATIONS})


def pristine_path(relative):
    return PRISTINE / relative.replace("/", "__")


arg = sys.argv[1]
if arg == "snapshot":
    PRISTINE.mkdir(exist_ok=True)
    for relative in FILES:
        pristine_path(relative).write_text((ROOT / relative).read_text(encoding="utf-8"),
                                           encoding="utf-8")
    print("snapshot taken: %d file(s)" % len(FILES))
elif arg == "restore":
    for relative in FILES:
        (ROOT / relative).write_text(pristine_path(relative).read_text(encoding="utf-8"),
                                     encoding="utf-8")
    print("restored")
elif arg == "count":
    print(len(MUTATIONS))
elif arg == "label":
    mutation = MUTATIONS[int(sys.argv[2])]
    print("%s [%s]" % (mutation["label"], mutation["file"].rsplit("/", 1)[-1]))
else:
    mutation = MUTATIONS[int(arg)]
    # Every file goes back to pristine first, so the only difference from the
    # committed tree is this one mutation.
    for relative in FILES:
        (ROOT / relative).write_text(pristine_path(relative).read_text(encoding="utf-8"),
                                     encoding="utf-8")
    target = ROOT / mutation["file"]
    text = target.read_text(encoding="utf-8")
    occurrences = text.count(mutation["old"])
    if occurrences == 0:
        print("PATTERN-ABSENT")
        sys.exit(2)
    if occurrences != 1:
        print("PATTERN-AMBIGUOUS x%d" % occurrences)
        sys.exit(3)
    target.write_text(text.replace(mutation["old"], mutation["new"], 1), encoding="utf-8")
    print("applied")
