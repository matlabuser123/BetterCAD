#!/bin/bash
set -u
CAD=/c/Users/uqhas/OneDrive/Desktop/CAD
SP=/c/Users/uqhas/AppData/Local/Temp/claude/C--Users-uqhas-OneDrive-Desktop-CAD/3f122dc2-e3e9-4e92-bb25-f10e04d7c1e9/scratchpad/vizmut
export BETTERCAD_BUILD_ROOT=C:/Users/uqhas/AppData/Local/bc-build
export CMAKE_BUILD_PARALLEL_LEVEL=6
MP="$CAD/src/renderer/occt/OcctMeshPresentation.cpp"
cd "$CAD" || exit 1
echo "=== E9: the highlight group takes band colours ==="
perl -0i -pe 's/facets\(highlighted_, false\)/facets(highlighted_, true)/' "$MP"
grep -c "facets(highlighted_, true)" "$MP"
cmake --build --preset debug-ext --target bettercad_tests 2>&1 | grep -cE "error:" | sed 's/^/  errors: /'
cd /c/Users/uqhas/AppData/Local/bc-build/debug-ext
ctest -R "StructuralViz_" -j 8 2>&1 | grep -E "tests passed|tests failed" | tail -1
echo "=== restore ==="
cp "$SP/c2.mp.pristine" "$MP"
cd "$CAD" && cmake --build --preset debug-ext --target bettercad_tests 2>&1 | tail -1
sha1sum "$MP" | cut -c1-12
cd /c/Users/uqhas/AppData/Local/bc-build/debug-ext && ctest -R "StructuralViz_" -j 8 2>&1 | grep -E "tests passed" | tail -1
echo "DONE"
