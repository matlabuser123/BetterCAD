// Comments may name thermalConductivity, solveSteadyThermal, 1e-9, std::abs, temperatures[node.value()], degC and std::cout: not code.
#include "Commands.hpp"

#include <bettercad/thermal/ThermalAnalysisRun.hpp>
#include <bettercad/thermal/ThermalPost.hpp>

#include <string>

#include <bettercad/thermal/xThermalAnalysisRun.hpp>
namespace x {
const char* help = "conductivity 1e-9 degC std::cout std::abs(x) --node 12 solveSteadyThermal residual < 1e-9 it's // not a comment";
const char quote = '"';
void emit(std::ostream& out, bool json) {
    if (json) {
        out << a;
    } else {
        out << b;
    }
}
void f() {
    runThermalAnalysis(d, r, m, a);
    thermalAnalysisIds(d);
    loadDocument(p);
    meshing::Mesher mesher;
    return run.accepted() ? ExitCode::Success : ExitCode::Failure;
    return run.accepted() ? ExitCode::Success : ExitCode::Failure;
    run.accepted();
    run.accepted();
    run.accepted();
    checkFaceName(a1);
    checkFaceName(a2);
    checkFaceName(a3);
    checkFaceName(a4);
    validationJson(v1);
    validationJson(v2);
    validationJson(v3);
    fin.sawNonFinite();
    fin.sawNonFinite();
    fin.sawNonFinite();
    fin.sawNonFinite();
    fin.sawNonFinite();
    fin.sawNonFinite();
    map.nodeOf(i);
    ordinalOrThrow(post, e1);
    ordinalOrThrow(post, e2);
    ordinalOrThrow(post, e3);
    refusalOf(r1);
    refusalOf(r2);
    refusalOf(r3);
    g(static_cast<std::uint64_t>(node->value()));
    g(static_cast<std::uint64_t>(ids[i].value()));
    g(static_cast<std::uint64_t>(ids[j].value()));
    if (!ordinal.has_value()) {
        throw std::logic_error("the element is not in the result");
    }
    if (!node.has_value()) {
        throw std::logic_error("the node is not in the numbering");
    }
    return *ordinal;
}
} // namespace x
