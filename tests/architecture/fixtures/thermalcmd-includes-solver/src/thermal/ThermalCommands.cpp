#include <bettercad/thermal/ThermalSolve.hpp>
#include <bettercad/thermal/ThermalCommands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <algorithm>
namespace bettercad::thermal {
Result<bool> apply(Document& d) { return d.modifyObject<ThermalAnalysis>(id, [&](ThermalAnalysis& o) { return o.setDefinition(definition); }); }
void edit() { if (auto c = validate(load); !c) { return; } auto at = std::ranges::find_if(loads, [](auto& l) { return true; }); }
}
