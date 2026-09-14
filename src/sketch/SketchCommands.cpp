#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/sketch/SketchCommands.hpp>

#include <format>
#include <memory>
#include <utility>

namespace bettercad::sketch {

CreateSketchCommand::CreateSketchCommand(std::string name, const Frame3D& placement)
    : name_(std::move(name)), add_(std::make_unique<Sketch>(name_, placement)) {}

std::string CreateSketchCommand::description() const {
    return std::format("Create sketch '{}'", name_);
}

Result<void> CreateSketchCommand::execute(Document& document) {
    return add_.execute(document);
}

Result<void> CreateSketchCommand::undo(Document& document) {
    return add_.undo(document);
}

Result<void> CreateSketchCommand::redo(Document& document) {
    return add_.redo(document);
}

} // namespace bettercad::sketch
