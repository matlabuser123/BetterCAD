#pragma once

#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/math/Frame.hpp>
#include <bettercad/sketch/Export.hpp>

#include <string>

namespace bettercad::sketch {

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

} // namespace bettercad::sketch
