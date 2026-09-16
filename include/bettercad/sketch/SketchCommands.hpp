#pragma once

#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/math/Frame.hpp>
#include <bettercad/sketch/Export.hpp>

#include <functional>
#include <memory>
#include <string>

namespace bettercad::sketch {

class Sketch;

/// Creates an empty sketch on a plane; redo recreates it with the same ID.
class BETTERCAD_SKETCH_EXPORT CreateSketchCommand final : public Command {
public:
    explicit CreateSketchCommand(std::string name, const Frame3D& placement = Frame3D::xy());

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

    /// ID of the created sketch; invalid before execute().
    [[nodiscard]] SketchId sketchId() const noexcept {
        return SketchId::fromValue(add_.objectId().value());
    }

private:
    std::string name_;
    AddObjectCommand add_;
};

/// Applies an edit to a sketch (adding entities or constraints, changing
/// values, ...) as one undoable step.
///
/// execute() runs @p edit on a copy of the sketch; if the edit fails, the
/// document is unchanged. Otherwise the sketch's content before and after is
/// kept, so undo() and redo() restore it exactly, IDs included, without
/// running the edit again.
class BETTERCAD_SKETCH_EXPORT ModifySketchCommand final : public Command {
public:
    using Edit = std::function<Result<void>(Sketch&)>;

    ModifySketchCommand(SketchId sketch, std::string description, Edit edit);
    ~ModifySketchCommand() override;

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    Result<void> restore(Document& document, const Sketch& state);

    SketchId sketch_;
    std::string description_;
    Edit edit_;
    std::unique_ptr<Sketch> before_;
    std::unique_ptr<Sketch> after_;
};

} // namespace bettercad::sketch
