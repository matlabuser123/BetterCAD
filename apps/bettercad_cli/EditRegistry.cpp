#include "Edits.hpp"

#include <algorithm>
#include <vector>

// The one listing of edit verbs (P14-CLI-001).
//
// The verbs live in three files -- the assembly ones in AssemblyEdits.cpp, the
// drawing ones in DrawingEdits.cpp, the material ones in MaterialEdits.cpp --
// and are joined here, so that adding a sheet verb does not mean opening the
// file that owns mates. A script does not care which file a verb came from, so
// there is exactly one listing and exactly one lookup.
//
// Joining is also all a verb needs to become scriptable: `batch` drives this
// same table, so a material verb registered here is a batch verb, in the same
// one-transaction-or-nothing script as a mate verb, with nothing added to
// BatchCommand.cpp (P15-CLI-001).
namespace bettercad::cli {
namespace {

/// Built once, in a fixed order: the assembly verbs, then the drawing ones,
/// then the material ones, then the meshing ones. Order is what `help` prints and is therefore part of
/// the output, so it is stated here rather than left to a link order.
const std::vector<EditCommand>& allEdits() {
    static const std::vector<EditCommand> all = [] {
        std::vector<EditCommand> joined;
        const auto assembly = assemblyEditCommands();
        const auto drawing = drawingEditCommands();
        const auto material = materialEditCommands();
        const auto meshing = meshingEditCommands();
        joined.reserve(assembly.size() + drawing.size() + material.size() + meshing.size());
        joined.insert(joined.end(), assembly.begin(), assembly.end());
        joined.insert(joined.end(), drawing.begin(), drawing.end());
        joined.insert(joined.end(), material.begin(), material.end());
        joined.insert(joined.end(), meshing.begin(), meshing.end());
        return joined;
    }();
    return all;
}

} // namespace

std::span<const EditCommand> editCommands() noexcept { return allEdits(); }

const EditCommand* findEditCommand(std::string_view name) noexcept {
    const auto& edits = allEdits();
    const auto found = std::ranges::find(edits, name, &EditCommand::name);
    return found == edits.end() ? nullptr : &*found;
}

} // namespace bettercad::cli
