"""Compares the reference models' fingerprints across build configurations.

Reads the fingerprints-<preset>.txt files written by
bettercad_example_reference_models in each preset and reports, per model, how
far the builds are apart: the structure (items, feature and body counts,
validity, topology) must match exactly, while volumes, areas, centroids and
bounds are compared as numbers, since different optimisation settings may
round differently.

    python cross-build.py fingerprints-debug.txt fingerprints-release.txt ...
"""
import sys


def read(path):
    """{model: {"items": [...], "bodies": [(header, {key: [numbers]})]}}"""
    models = {}
    model = None
    body = None
    for raw in open(path, encoding="utf-8"):
        line = raw.rstrip("\n")
        if line.startswith("== "):
            model = {"name": line[3:], "items": [], "bodies": []}
            models[model["name"]] = model
            body = None
        elif model is None or line.startswith("timing_ms"):
            continue
        elif line.startswith("item ") or line.startswith("features ") or line.startswith("bodies "):
            model["items"].append(line)
        elif line.startswith("body "):
            body = (line, {})
            model["bodies"].append(body)
        elif line.startswith("  ") and body is not None:
            parts = line.split()
            body[1][parts[0]] = [float(value) for value in parts[1:]]
    return models


def main(paths):
    builds = [(path, read(path)) for path in paths]
    reference, models = builds[0]
    print(f"Reference model fingerprints across builds, against {reference}.")
    print("Structure (items, IDs, names, feature and body counts, validity, topology) must be identical;")
    print("volumes, areas, centroids and bounds are compared as numbers.\n")
    worst_relative = 0.0
    worst_position = 0.0
    failures = 0
    for path, other in builds[1:]:
        print(f"== {path}")
        if set(other) != set(models):
            print(f"  different models: {sorted(set(other) ^ set(models))}")
            failures += 1
            continue
        for name in sorted(models):
            a, b = models[name], other[name]
            structure = "same" if a["items"] == b["items"] else "DIFFERENT"
            if structure != "same":
                failures += 1
            relative = 0.0
            position = 0.0
            for (headerA, valuesA), (headerB, valuesB) in zip(a["bodies"], b["bodies"]):
                if headerA != headerB:
                    print(f"  {name}: body line differs:\n    {headerA}\n    {headerB}")
                    failures += 1
                for key in valuesA:
                    for x, y in zip(valuesA[key], valuesB[key]):
                        scale = max(abs(x), abs(y))
                        if key in ("volume_mm3", "area_mm2"):
                            relative = max(relative, abs(x - y) / scale if scale else 0.0)
                        else:
                            position = max(position, abs(x - y))
            worst_relative = max(worst_relative, relative)
            worst_position = max(worst_position, position)
            print(f"  {name:16} structure {structure}, volume and area within {relative:.3g} relative, "
                  f"positions within {position:.3g} mm")
    print(f"\nWorst over all models and builds: {worst_relative:.3g} relative, {worst_position:.3g} mm.")
    print("FAILURES: " + str(failures) if failures else "No structural differences.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
