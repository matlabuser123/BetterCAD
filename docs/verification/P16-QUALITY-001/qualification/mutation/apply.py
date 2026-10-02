"""Applies one mutation from mutations.json to src/meshing/MeshQuality.cpp.

    python apply.py snapshot      take the pristine copy
    python apply.py <index>       apply that mutation to the pristine text
    python apply.py restore       put the pristine text back
    python apply.py count         how many mutations there are
    python apply.py label <i>     that mutation's label

A mutation is applied to the PRISTINE text, never to an already-mutated file,
so the mutations cannot compound. A pattern that is absent, or that occurs more
than once, is refused rather than guessed at: a mutation landing somewhere
unintended would make its verdict meaningless.
"""
import json
import pathlib
import sys

HERE = pathlib.Path(__file__).parent
ROOT = HERE.parents[3]  # docs/verification/P16-QUALITY-001/qualification/mutation
SRC = ROOT / "src" / "meshing" / "MeshQuality.cpp"
PRISTINE = HERE / "MeshQuality.cpp.pristine"
MUTATIONS = json.loads((HERE / "mutations.json").read_text())

arg = sys.argv[1]
if arg == "snapshot":
    PRISTINE.write_text(SRC.read_text())
    print("snapshot taken")
elif arg == "restore":
    SRC.write_text(PRISTINE.read_text())
    print("restored")
elif arg == "count":
    print(len(MUTATIONS))
elif arg == "label":
    print(MUTATIONS[int(sys.argv[2])]["label"])
else:
    mutation = MUTATIONS[int(arg)]
    text = PRISTINE.read_text()
    occurrences = text.count(mutation["old"])
    if occurrences == 0:
        print("PATTERN-ABSENT")
        sys.exit(2)
    if occurrences != 1:
        print("PATTERN-AMBIGUOUS x%d" % occurrences)
        sys.exit(3)
    SRC.write_text(text.replace(mutation["old"], mutation["new"], 1))
    print("applied")
