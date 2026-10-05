#include "EditSupport.hpp"
#include "Selectors.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/units/UnitCatalog.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/MeshingCommands.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The meshing edits (P16-CLI-001).
//
// EACH ONE IS AN ADAPTER AND NOTHING ELSE. It parses, resolves a selector,
// builds the P16-CMD-001 command object and hands it to EditSupport's
// execute() -- whose own comment states why: "the CLI inherits that
// milestone's validation and its all-or-nothing execution instead of
// restating either". So there is no size check, no duplicate-face check and
// no conflict rule in this file. A zero, a negative, an infinity or a second
// control on one face is refused by P16-SIZE-001's validator, reached through
// the command, with the diagnostic the GUI would show.
//
// AND NO DEFAULTS. There is no CLI target size, no CLI tolerance and no CLI
// threshold anywhere here. An absent global size means BetterCAD's
// scale-relative default, which is the core's and is expressed by the absence
// rather than by a number this file knows.
//
// REGISTERING THESE IN EditRegistry MAKES THEM SCRIPTABLE. `batch` drives the
// same table, so each is a batch verb in the same one-transaction-or-nothing
// script as a mate verb, with nothing added to BatchCommand.cpp.
namespace bettercad::cli {

namespace {

/// The document's one meshing control, or a diagnosis of why there is not
/// exactly one.
///
/// A control is per body and this version of the CLI does not ask the user to
/// choose between several, which is the same limit the GUI records. Saying so
/// by name beats meshing whichever came first.
[[nodiscard]] std::expected<MeshControlId, EditFailure> theControl(const Document& document) {
    const std::vector<MeshControlId> controls = meshing::meshControls(document);
    if (controls.empty()) {
        return std::unexpected(
            rejected("no_mesh_control",
                     Error{ErrorCode::NotFound,
                           "this document has no meshing control; mesh-control-add creates one"}));
    }
    if (controls.size() > 1U) {
        return std::unexpected(rejected(
            "ambiguous_mesh_control",
            Error{ErrorCode::FailedPrecondition,
                  std::format("this document has {} meshing controls, and choosing between them "
                              "needs a selector this version does not have",
                              controls.size())}));
    }
    return controls.front();
}

/// The single result body, which is what a control meshes.
[[nodiscard]] std::expected<ObjectId, EditFailure> theBody(const Document& document) {
    const std::vector<ObjectId> results = features::resultFeatures(document);
    if (results.empty()) {
        return std::unexpected(rejected(
            "no_body",
            Error{ErrorCode::FailedPrecondition, "this document has no solid body to mesh"}));
    }
    if (results.size() > 1U) {
        return std::unexpected(rejected(
            "ambiguous_body",
            Error{ErrorCode::FailedPrecondition,
                  std::format("this document has {} bodies, and choosing between them needs a "
                              "selector this version does not have",
                              results.size())}));
    }
    return results.front();
}

[[nodiscard]] std::string describeSize(const std::optional<Length>& size) {
    return size ? std::format("{:.4g} mm", size->in(units::mm)) : std::string{"the default"};
}

EditResult applyMeshControlAdd(Document& document, Args args) {
    auto parsed = parseArguments(args, {{"--name", true}, {"--size", true}});
    if (!parsed) {
        return std::unexpected(fromParse(parsed.error()));
    }
    if (!parsed->positional().empty()) {
        return std::unexpected(
            malformed(std::format("unexpected argument '{}'", parsed->positional().front())));
    }
    if (!meshing::meshControls(document).empty()) {
        return std::unexpected(rejected(
            "mesh_control_exists",
            Error{ErrorCode::AlreadyExists, "this document already has a meshing control"}));
    }
    auto body = theBody(document);
    if (!body) {
        return std::unexpected(body.error());
    }

    meshing::MeshControlDefinition definition;
    definition.body = *body;
    if (const auto size = parsed->value("--size")) {
        // Through the CLI's own quantity parser, the one every other command
        // uses, so 10mm and 0.01m are the same Length here as everywhere.
        auto length = parseLength(*size, units::mm);
        if (!length) {
            return std::unexpected(fromParse("--size", length.error()));
        }
        definition.mesh.sizing.globalTargetSize = *length;
    }
    auto name = namedOr(*parsed, document, "Mesh");
    if (!name) {
        return std::unexpected(fromParse(name.error()));
    }

    meshing::CreateMeshControlCommand command{*name, definition};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Added meshing control '{}' for {}, global size {}", *name,
                       label(document, *body),
                       describeSize(definition.mesh.sizing.globalTargetSize));
}

EditResult applyMeshSetGlobalSize(Document& document, Args args) {
    auto parsed = parseArguments(args, {});
    if (!parsed) {
        return std::unexpected(fromParse(parsed.error()));
    }
    if (parsed->positional().size() != 1) {
        return std::unexpected(malformed("expected one size, or 'default'"));
    }
    auto control = theControl(document);
    if (!control) {
        return std::unexpected(control.error());
    }

    std::optional<Length> target;
    if (parsed->positional()[0] != "default") {
        auto length = parseLength(parsed->positional()[0], units::mm);
        if (!length) {
            return std::unexpected(fromParse(length.error()));
        }
        // NOT CHECKED HERE. Zero, negative and non-finite are
        // P16-SIZE-001's to refuse, through the command below.
        target = *length;
    }

    meshing::SetGlobalMeshSizeCommand command{*control, target};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Set the global mesh size to {}", describeSize(target));
}

EditResult applyMeshLocalAdd(Document& document, Args args) {
    auto parsed = parseArguments(args, {});
    if (!parsed) {
        return std::unexpected(fromParse(parsed.error()));
    }
    if (parsed->positional().size() != 2) {
        return std::unexpected(malformed("expected a face and a size"));
    }
    auto control = theControl(document);
    if (!control) {
        return std::unexpected(control.error());
    }
    // THE SHARED FACE GRAMMAR, the one drawings and mates use. A local
    // control's target is a CAD face and never a NodeId, an ElementId or a
    // facet: those belong to one generated mesh and would be wrong at the
    // next remesh.
    auto face = parseFaceReference(document, parsed->positional()[0]);
    if (!face) {
        return std::unexpected(coded("bad_face_reference", face.error()));
    }
    auto size = parseLength(parsed->positional()[1], units::mm);
    if (!size) {
        return std::unexpected(fromParse(size.error()));
    }

    meshing::AddLocalMeshSizingCommand command{*control, *face, *size};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Added local mesh sizing {:.4g} mm on {}", size->in(units::mm),
                       parsed->positional()[0]);
}

EditResult applyMeshLocalRemove(Document& document, Args args) {
    auto parsed = parseArguments(args, {});
    if (!parsed) {
        return std::unexpected(fromParse(parsed.error()));
    }
    if (parsed->positional().size() != 1) {
        return std::unexpected(malformed("expected one face"));
    }
    auto control = theControl(document);
    if (!control) {
        return std::unexpected(control.error());
    }
    // BY FACE, which IS the identity of a local control (P16-SIZE-001
    // refuses two on one face and declares the stored order meaningless).
    // Never by position in a list: an index would name a different control
    // after any removal.
    auto face = parseFaceReference(document, parsed->positional()[0]);
    if (!face) {
        return std::unexpected(coded("bad_face_reference", face.error()));
    }

    meshing::RemoveLocalMeshSizingCommand command{*control, *face};
    if (auto failed = execute(document, command)) {
        // NotFound for a face with no control, carried through with the
        // command's own message. Not a silent no-op: asking to remove
        // something that is not there is a mistake worth reporting.
        return std::unexpected(*failed);
    }
    return std::format("Removed the local mesh sizing on {}", parsed->positional()[0]);
}

constexpr std::array kEdits{
    EditCommand{"mesh-control-add",
                "mesh-control-add <file.bcad> [--name <name>] [--size <length>]",
                "Create the document's meshing control for its result body. --size is the global "
                "target element size; without it the control uses BetterCAD's scale-relative "
                "default.",
                &applyMeshControlAdd},
    EditCommand{"mesh-set-global-size", "mesh-set-global-size <file.bcad> <length>|default",
                "Set the global target element size, or 'default' to use BetterCAD's "
                "scale-relative default. A bare number is millimetres; write a unit for another.",
                &applyMeshSetGlobalSize},
    EditCommand{"mesh-local-add", "mesh-local-add <file.bcad> <face> <length>",
                "Refine one CAD face. The face is face:<feature>:<role>[:<entity>], the same way a "
                "dimension names one. One control per face.",
                &applyMeshLocalAdd},
    EditCommand{"mesh-local-remove", "mesh-local-remove <file.bcad> <face>",
                "Remove the local sizing on one CAD face. Named by the face, which is the "
                "control's identity -- never by position in a list.",
                &applyMeshLocalRemove},
};

} // namespace

std::span<const EditCommand> meshingEditCommands() noexcept { return kEdits; }

} // namespace bettercad::cli
