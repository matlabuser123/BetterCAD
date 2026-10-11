#!/bin/bash
# P17-VIZ-001 contour/legend mutation probes. Same protocol as probe.sh:
# ONE DRIVER, restore from a hash-verified pristine copy, one verified literal
# substitution that dies on a missing or ambiguous anchor, rebuild, run the
# filter, restore -- and the run ENDS with a restore BUILD, because a restored
# source is not a restored binary.
set -u
CAD=/c/Users/uqhas/OneDrive/Desktop/CAD
SP=/c/Users/uqhas/AppData/Local/Temp/claude/C--Users-uqhas-OneDrive-Desktop-CAD/3f122dc2-e3e9-4e92-bb25-f10e04d7c1e9/scratchpad/vizmut
export BETTERCAD_BUILD_ROOT=C:/Users/uqhas/AppData/Local/bc-build
export CMAKE_BUILD_PARALLEL_LEVEL=6
CC="$CAD/src/renderer/StructuralContour.cpp"
CH="$CAD/include/bettercad/renderer/StructuralContour.hpp"
FILTER='StructuralViz_'
cd "$CAD" || exit 1

restore() {
  cp "$SP/contour.cpp.pristine" "$CC"
  cp "$SP/contour.hpp.pristine" "$CH"
}

probe() {
  local name="$1"; shift
  local target="$1"; shift
  local old="$1"; shift
  local new="$1"; shift
  restore
  OLD="$old" NEW="$new" perl -0i -pe '
    my $o = $ENV{OLD}; my $n = $ENV{NEW};
    my $i = index($_, $o);
    die "ANCHOR NOT FOUND\n" if $i < 0;
    die "ANCHOR AMBIGUOUS\n" if index($_, $o, $i + 1) >= 0;
    substr($_, $i, length($o)) = $n;
  ' "$target" || { echo "$name  ANCHOR FAILED"; restore; return; }

  local build
  if ! build=$(cmake --build --preset debug-ext --target bettercad_tests 2>&1); then
    if echo "$build" | grep -qE "ld returned|truncated|Permission denied"; then
      echo "$name  VOID -- collided or failed link"
    else
      echo "$name  KILLED-BY-COMPILER (weak)"
    fi
    echo "$build" | grep -E "error:" | head -2 | sed 's/^/        /'
    restore
    return
  fi
  local out
  out=$(cd /c/Users/uqhas/AppData/Local/bc-build/debug-ext && ctest -R "$FILTER" -j 8 2>&1 | tail -40)
  if echo "$out" | grep -q "100% tests passed"; then
    echo "$name  *** SURVIVED ***"
  else
    echo "$name  KILLED  ($(echo "$out" | grep -oE '[0-9]+ tests failed out of [0-9]+' | head -1))"
    echo "$out" | grep -E "^[[:space:]]+[0-9]+ - " | head -3 | sed 's/^/        /'
  fi
  restore
}

echo "######## the partition ########"
date

probe "C1  the last band rounds its top " "$CC" \
  '    boundary.front() = out.minimum;
    boundary.back() = out.maximum;' \
  '    boundary.front() = out.minimum;'

probe "C2  boundaries computed per band " "$CC" \
  '        band.lower = boundary[i];
        band.upper = boundary[i + 1];' \
  '        band.lower = out.minimum + (static_cast<double>(i) * width);
        band.upper = out.minimum + (static_cast<double>(i + 1) * width);'

probe "C3  EVERY band closed above     " "$CC" \
  '        band.closedAbove = (i + 1 == bands);' \
  '        band.closedAbove = true;'

probe "C4  NO band closed above        " "$CC" \
  '        band.closedAbove = (i + 1 == bands);' \
  '        band.closedAbove = false;'

probe "C5  partition check: gaps allowed" "$CC" \
  '        if (!last && bands[i].upper != bands[i + 1].lower) {
            return false;
        }' \
  '        if (false && !last && bands[i].upper != bands[i + 1].lower) {
            return false;
        }'

probe "C6  partition check: ends ignored" "$CC" \
  '    if (bands.front().lower != minimum || bands.back().upper != maximum) {
        return false;
    }' \
  '    if (false) {
        return false;
    }'

probe "C7  two closed bands allowed    " "$CC" \
  '        if (bands[i].closedAbove != last) {
            return false;
        }' \
  '        if (false && bands[i].closedAbove != last) {
            return false;
        }'

echo "######## the uniform field ########"
date

probe "C8  the one-band collapse removed " "$CC" \
  '    if (field.uniform() || out.minimum + trialWidth == out.minimum) {' \
  '    if (false) {'

probe "C9  uniform flag never set      " "$CC" \
  '        out.uniform = true;' \
  '        out.uniform = false;'

echo "######## the refusals ########"
date

probe "C10 an incomplete field banded   " "$CC" \
  '    if (!field.complete()) {
        return fail(ContourProblem::FieldIncomplete,
                    std::format("channel {} classified {} of {} {}s, so it cannot be banded",' \
  '    if (false && !field.complete()) {
        return fail(ContourProblem::FieldIncomplete,
                    std::format("channel {} classified {} of {} {}s, so it cannot be banded",'

probe "C11 the band-count range dropped " "$CC" \
  '    if (bands < kMinimumContourBands || bands > kMaximumContourBands) {' \
  '    if (false && (bands < kMinimumContourBands || bands > kMaximumContourBands)) {'

probe "C12 the ceiling only, no floor   " "$CC" \
  '    if (bands < kMinimumContourBands || bands > kMaximumContourBands) {' \
  '    if (bands > kMaximumContourBands) {'

probe "C13 the legend mismatch ignored  " "$CC" \
  '    if (legend.channel != field.channel || legend.minimum != field.minimum ||
        legend.maximum != field.maximum) {' \
  '    if (false && (legend.channel != field.channel || legend.minimum != field.minimum ||
        legend.maximum != field.maximum)) {'

probe "C14 the range ignored, name only " "$CC" \
  '    if (legend.channel != field.channel || legend.minimum != field.minimum ||
        legend.maximum != field.maximum) {' \
  '    if (legend.channel != field.channel) {'

probe "C15 partition not checked at use " "$CC" \
  '    if (!legend.partitionsItsRange()) {' \
  '    if (false && !legend.partitionsItsRange()) {'

probe "C16 an unbanded value clamped    " "$CC" \
  '        if (band >= legend.bands.size()) {' \
  '        if (false && band >= legend.bands.size()) {'

echo "######## bandOf and the ramp ########"
date

probe "C17 bandOf computed, not searched" "$CC" \
  '    for (std::size_t i = 0; i < bands.size(); ++i) {
        if (bands[i].contains(value)) {
            return i;
        }
    }
    return bands.size();' \
  '    if (bands.empty() || maximum == minimum) {
        return 0;
    }
    const double fraction = (value - minimum) / (maximum - minimum);
    const auto index = static_cast<std::size_t>(fraction * static_cast<double>(bands.size()));
    return index >= bands.size() ? bands.size() - 1 : index;'

probe "C18 contains() closes every band " "$CH" \
  '        return value >= lower && (closedAbove ? value <= upper : value < upper);' \
  '        return value >= lower && value <= upper;'

probe "C19 contains() opens every band  " "$CH" \
  '        return value >= lower && (closedAbove ? value <= upper : value < upper);' \
  '        return value >= lower && value < upper;'

probe "C20 the ramp is one colour       " "$CC" \
  '    band.red = kRamp[lower][0] + local * (kRamp[lower + 1][0] - kRamp[lower][0]);' \
  '    band.red = kRamp[0][0];'

probe "C21 the band fraction loses +0.5 " "$CC" \
  '        const double t = (static_cast<double>(i) + 0.5) / static_cast<double>(bands);' \
  '        const double t = static_cast<double>(i) / static_cast<double>(bands);'

probe "C22 the legend borrows no unit   " "$CC" \
  '    out.unit = descriptorOf(field.channel).unit;' \
  '    out.unit = {};'

probe "C23 the range taken from values  " "$CC" \
  '    out.minimum = field.minimum;
    out.maximum = field.maximum;' \
  '    out.minimum = field.values.empty() ? 0.0 : *std::ranges::min_element(field.values);
    out.maximum = field.values.empty() ? 0.0 : *std::ranges::max_element(field.values);'

probe "C24 the floor drops to one band  " "$CH" \
  'constexpr std::size_t kMinimumContourBands = 2;' \
  'constexpr std::size_t kMinimumContourBands = 1;'

probe "C25 the narrow-range collapse gone" "$CC" \
  '    if (field.uniform() || out.minimum + trialWidth == out.minimum) {' \
  '    if (field.uniform()) {'

probe "C26 the collapse fires too eagerly" "$CC" \
  '    if (field.uniform() || out.minimum + trialWidth == out.minimum) {' \
  '    if (field.uniform() || out.minimum + trialWidth <= out.minimum + span) {'

echo "######## RESTORE ########"
restore
cmake --build --preset debug-ext --target bettercad_tests 2>&1 | tail -2
sha1sum "$CC" "$CH"
(cd /c/Users/uqhas/AppData/Local/bc-build/debug-ext && ctest -R "$FILTER" -j 8 2>&1 | tail -3)
echo "######## ALL DONE ########"
date
