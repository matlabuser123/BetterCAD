#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Datums.hpp>

#include <format>
#include <optional>
#include <string>
#include <utility>

// Undoable creation and editing of datum planes, datum axes and coordinate
// systems (P12-DATUM-001). One implementation serves each kind D, which
// provides Definition, kTypeName, create(), definition() and setDefinition().
namespace bettercad::features {

/// Creates a datum object; redo recreates it with the same ID.
template <typename D>
class CreateDatumCommand final : public Command {
public:
    using Definition = typename D::Definition;

    CreateDatumCommand(std::string name, const Definition& definition)
        : name_(std::move(name)), definition_(definition) {}

    [[nodiscard]] std::string description() const override {
        return std::format("Create {} '{}'", D::kTypeName, name_);
    }

    [[nodiscard]] Result<void> execute(Document& document) override {
        auto datum = D::create(name_, definition_);
        if (!datum) {
            return std::unexpected(datum.error());
        }
        add_.emplace(std::move(*datum));
        auto executed = add_->execute(document);
        if (!executed) {
            add_.reset();
        }
        return executed;
    }

    [[nodiscard]] Result<void> undo(Document& document) override {
        if (!add_) {
            return makeError(ErrorCode::FailedPrecondition, "cannot undo: the command has not been executed");
        }
        return add_->undo(document);
    }

    [[nodiscard]] Result<void> redo(Document& document) override {
        if (!add_) {
            return makeError(ErrorCode::FailedPrecondition, "cannot redo: the command has not been executed");
        }
        return add_->redo(document);
    }

    /// ID of the created object; invalid before execute().
    [[nodiscard]] ObjectId objectId() const noexcept { return add_ ? add_->objectId() : ObjectId{}; }

private:
    std::string name_;
    Definition definition_;
    std::optional<AddObjectCommand> add_;
};

/// Replaces the definition of a datum object.
template <typename D>
class ModifyDatumCommand final : public Command {
public:
    using Definition = typename D::Definition;

    ModifyDatumCommand(ObjectId datum, const Definition& definition) : datum_(datum), after_(definition) {}

    [[nodiscard]] std::string description() const override { return std::format("Modify {}", datum_); }

    [[nodiscard]] Result<void> execute(Document& document) override {
        const auto* datum = document.findObjectAs<D>(datum_);
        if (datum == nullptr) {
            return makeError(ErrorCode::NotFound, std::format("{} is not a {}", datum_, D::kTypeName));
        }
        const Definition before = datum->definition();
        if (auto applied = apply(document, after_); !applied) {
            return applied;
        }
        before_ = before;
        return {};
    }

    [[nodiscard]] Result<void> undo(Document& document) override {
        if (!before_) {
            return makeError(ErrorCode::FailedPrecondition, "cannot undo: the command has not been executed");
        }
        return apply(document, *before_);
    }

    [[nodiscard]] Result<void> redo(Document& document) override { return apply(document, after_); }

private:
    Result<void> apply(Document& document, const Definition& definition) {
        auto changed = document.modifyObject<D>(datum_, [&](D& datum) { return datum.setDefinition(definition); });
        if (!changed) {
            return std::unexpected(changed.error());
        }
        return {};
    }

    ObjectId datum_;
    Definition after_;
    std::optional<Definition> before_;
};

using CreateDatumPlaneCommand = CreateDatumCommand<DatumPlane>;
using ModifyDatumPlaneCommand = ModifyDatumCommand<DatumPlane>;
using CreateDatumAxisCommand = CreateDatumCommand<DatumAxis>;
using ModifyDatumAxisCommand = ModifyDatumCommand<DatumAxis>;
using CreateCoordinateSystemCommand = CreateDatumCommand<CoordinateSystem>;
using ModifyCoordinateSystemCommand = ModifyDatumCommand<CoordinateSystem>;

} // namespace bettercad::features
