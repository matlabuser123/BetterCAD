#!/bin/bash
# P17-VIZ-001 mutation probes. ONE DRIVER, ALL BATCHES, nothing else touching
# the build root. See docs/verification/P17-VALID-001/MUTATION_PROTECTION.md
# for why: a background harness once outlived its own completion notification
# and spent twenty minutes overwriting source edits.
#
# MUTATIONS MUST COMPILE. -Werror=unused-variable turns "drop a term" into a
# COMPILER kill, which says nothing about the tests.
set -u
CAD=/c/Users/uqhas/OneDrive/Desktop/CAD
SP=/c/Users/uqhas/AppData/Local/Temp/claude/C--Users-uqhas-OneDrive-Desktop-CAD/3f122dc2-e3e9-4e92-bb25-f10e04d7c1e9/scratchpad/vizmut
export BETTERCAD_BUILD_ROOT=C:/Users/uqhas/AppData/Local/bc-build
export CMAKE_BUILD_PARALLEL_LEVEL=6
RV="$CAD/src/renderer/StructuralResultView.cpp"
SI="$CAD/src/renderer/StructuralInspection.cpp"
RVH="$CAD/include/bettercad/renderer/StructuralResultView.hpp"
SIH="$CAD/include/bettercad/renderer/StructuralInspection.hpp"
FILTER='StructuralViz_'
cd "$CAD" || exit 1

restore() {
  cp "$SP/rv.cpp.pristine" "$RV"
  cp "$SP/si.cpp.pristine" "$SI"
  cp "$SP/rv.hpp.pristine" "$RVH"
  cp "$SP/si.hpp.pristine" "$SIH"
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
    echo "$out" | grep -E "^[[:space:]]+[0-9]+ - " | head -4 | sed 's/^/        /'
  fi
  restore
}

echo "######## A: units and the deformed buffer ########"
date

probe "V1  the deformation scale ignored  " "$RV" \
  '        out[(vertex * 3U) + 0U] += scale * nodal->displacement.x.si();' \
  '        out[(vertex * 3U) + 0U] += nodal->displacement.x.si();'

probe "V2  deformed MULTIPLIES the position" "$RV" \
  '        out[(vertex * 3U) + 0U] += scale * nodal->displacement.x.si();
        out[(vertex * 3U) + 1U] += scale * nodal->displacement.y.si();
        out[(vertex * 3U) + 2U] += scale * nodal->displacement.z.si();' \
  '        out[(vertex * 3U) + 0U] *= (1.0 + scale * nodal->displacement.x.si());
        out[(vertex * 3U) + 1U] += scale * nodal->displacement.y.si();
        out[(vertex * 3U) + 2U] += scale * nodal->displacement.z.si();'

probe "V3  a negative scale allowed      " "$RV" \
  'return std::isfinite(scale) && scale >= 0.0;' \
  'return std::isfinite(scale);'

probe "V4  scale 0 no longer undeformed  " "$RV" \
  '        out[(vertex * 3U) + 2U] += scale * nodal->displacement.z.si();' \
  '        out[(vertex * 3U) + 2U] += (scale + 1.0) * nodal->displacement.z.si();'

probe "V5  a metres-to-mm factor sneaks in" "$RV" \
  '    const std::span<const double> undeformed = view.positions();
    std::vector<double> out(undeformed.begin(), undeformed.end());' \
  '    const std::span<const double> undeformed = view.positions();
    std::vector<double> out(undeformed.begin(), undeformed.end());
    for (double& each : out) {
        each *= 1000.0;
    }'

echo "######## B: channels ########"
date

probe "V6  tau_YZ and tau_ZX transposed  " "$RV" \
  '    case ResultChannel::StressYZ:
        return element.stress.yz.si();
    case ResultChannel::StressZX:
        return element.stress.zx.si();' \
  '    case ResultChannel::StressYZ:
        return element.stress.zx.si();
    case ResultChannel::StressZX:
        return element.stress.yz.si();'

probe "V7  |u| returns a component       " "$RV" \
  '        return nodal.magnitude.si();' \
  '        return nodal.displacement.x.si();'

probe "V8  a signed channel plots abs()  " "$RV" \
  '    case ResultChannel::DisplacementX:
        return nodal.displacement.x.si();' \
  '    case ResultChannel::DisplacementX:
        return std::abs(nodal.displacement.x.si());'

probe "V9  von Mises declared NODAL      " "$RV" \
  '        return {channel, ChannelDomain::Element, "von Mises stress", "Pa", false};' \
  '        return {channel, ChannelDomain::Node, "von Mises stress", "Pa", false};'

probe "V10 a norm declared signed        " "$RV" \
  '        return {channel, ChannelDomain::Node, "Displacement magnitude", "m", false};' \
  '        return {channel, ChannelDomain::Node, "Displacement magnitude", "m", true};'

echo "######## C: the classifier ########"
date

probe "V11 an EMPTY domain accepted      " "$RV" \
  '    if (out.expected == 0) {' \
  '    if (false && out.expected == 0) {'

probe "V12 an INCOMPLETE field accepted  " "$RV" \
  '    if (out.classified != out.expected) {' \
  '    if (false && out.classified != out.expected) {'

probe "V13 a non-finite value accepted   " "$RV" \
  '    if (out.nonFinite > 0) {' \
  '    if (false && out.nonFinite > 0) {'

probe "V14 complete() drops expected>0   " "$RVH" \
  '        return expected > 0 && classified == expected && skipped == 0 && nonFinite == 0;' \
  '        return classified == expected && skipped == 0 && nonFinite == 0;'

probe "V15 the extremum seeded at zero   " "$RV" \
  '        if (!seeded) {
            out.minimum = value;
            out.maximum = value;' \
  '        if (false && !seeded) {
            out.minimum = value;
            out.maximum = value;'

echo "######## D: extremes and source guards ########"
date

probe "V16 ties go to the LAST entity    " "$RV" \
  '        const bool better = kind == ExtremeKind::Maximum ? field.values[i] > field.values[best]' \
  '        const bool better = kind == ExtremeKind::Maximum ? field.values[i] >= field.values[best]'

probe "V17 MaximumMagnitude loses the sign" "$RV" \
  '    out.value = field.values[best];' \
  '    out.value = std::abs(field.values[best]);'

probe "V18 extremeOf drops completeness  " "$RV" \
  '    if (!field.complete()) {' \
  '    if (false && !field.complete()) {'

probe "V19 describes() ignores the source" "$RV" \
  '    return mesh.owns(mesh_) && fields.describes(mesh) && fields.mesh() == mesh_ &&
           fields.source() == source_;' \
  '    return mesh.owns(mesh_) && fields.describes(mesh) && fields.mesh() == mesh_;'

# (void)-CAST, because dropping `mesh` orphans the parameter and
# -Werror=unused-parameter would make this a COMPILER kill -- which says
# nothing about the tests. The first run reported exactly that.
probe "V20 describes() ignores the mesh  " "$RV" \
  '    return mesh.owns(mesh_) && fields.describes(mesh) && fields.mesh() == mesh_ &&
           fields.source() == source_;' \
  '    (void)mesh;
    return fields.source() == source_;'

echo "######## E: inspection, overlays and state ########"
date

probe "V21 restraint read from displacement" "$SI" \
  '        out.restrained = std::ranges::binary_search(held, node);' \
  '        (void)held;
        out.restrained = nodal->magnitude.si() == 0.0;'

probe "V22 the reaction sign reversed    " "$SI" \
  '    out.force = found->force;
    out.constrained = found->constrained;' \
  '    out.force = Force3D{} - found->force;
    out.constrained = found->constrained;'

probe "V23 the pressure glyph negated    " "$SI" \
  '        out.push_back(LoadGlyph{.load = contribution.load,
                                .kind = contribution.kind,
                                .resultant = contribution.resultant,' \
  '        out.push_back(LoadGlyph{.load = contribution.load,
                                .kind = contribution.kind,
                                .resultant = Force3D{} - contribution.resultant,'

probe "V24 every restraint drawn as FIXED" "$SI" \
  '        out.push_back(RestraintGlyph{.restraint = resolution.restraint,
                                     .components = resolution.components,' \
  '        out.push_back(RestraintGlyph{.restraint = resolution.restraint,
                                     .components =
                                         structural::RestraintComponents::fixed(),'

probe "V25 a zero vector given a direction" "$SI" \
  '    return force.x.si() != 0.0 || force.y.si() != 0.0 || force.z.si() != 0.0;' \
  '    (void)force;
    return true;'

probe "V26 the arrow scale ignored       " "$SI" \
  '    return scale * (std::abs(magnitude) / reference);' \
  '    (void)scale;
    return std::abs(magnitude) / reference;'

probe "V27 a STALE result drawn as current" "$SIH" \
  '        return analysis == structural::AnalysisState::SolvedCurrent;' \
  '        return analysis == structural::AnalysisState::SolvedCurrent ||
               analysis == structural::AnalysisState::SolvedStale;'

probe "V28 stale-on-current ignores mesh " "$SIH" \
  '        return meshDescribesModel() && analysis == structural::AnalysisState::SolvedStale;' \
  '        return analysis == structural::AnalysisState::SolvedStale;'

probe "V29 stale REASONS never populated " "$SI" \
  '            out.staleReasons = structural::staleReasons(lastResult->source(), *current);' \
  '            out.staleReasons.clear();'

probe "V30 a load target silently empty  " "$SI" \
  '    if (!target.has_value()) {' \
  '    if (false && !target.has_value()) {'

echo "######## F: the adversarial-review fix ########"
date

probe "V31 nodal absent becomes 0.0      " "$RV" \
  '    case ResultChannel::HydrostaticStress:
        break;
    }
    return std::nullopt;
}

std::optional<double> channelValue(ResultChannel channel,
                                   const structural::ElementFields& element) noexcept {' \
  '    case ResultChannel::HydrostaticStress:
        break;
    }
    return 0.0;
}

std::optional<double> channelValue(ResultChannel channel,
                                   const structural::ElementFields& element) noexcept {'

probe "V32 element absent becomes 0.0    " "$RV" \
  '    case ResultChannel::DisplacementZ:
        break;
    }
    return std::nullopt;
}' \
  '    case ResultChannel::DisplacementZ:
        break;
    }
    return 0.0;
}'

probe "V33 the skipped refusal removed   " "$RV" \
  '    if (out.skipped > 0) {' \
  '    if (false && out.skipped > 0) {'

probe "V34 complete() drops skipped==0   " "$RVH" \
  '        return expected > 0 && classified == expected && skipped == 0 && nonFinite == 0;' \
  '        return expected > 0 && classified == expected && nonFinite == 0;'

probe "V35 ChannelUnavailable not Internal" "$RV" \
  '    case FieldProblem::ChannelUnavailable:
        return ErrorCode::Internal;' \
  '    case FieldProblem::ChannelUnavailable:
        return ErrorCode::FailedPrecondition;'

echo "######## RESTORE ########"
restore
cmake --build --preset debug-ext --target bettercad_tests 2>&1 | tail -2
sha1sum "$RV" "$SI" "$RVH" "$SIH"
(cd /c/Users/uqhas/AppData/Local/bc-build/debug-ext && ctest -R "$FILTER" -j 8 2>&1 | tail -3)
echo "######## ALL DONE ########"
date
