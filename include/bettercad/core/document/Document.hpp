#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Export.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/parameters/ParameterTable.hpp>
#include <bettercad/core/units/DimensionedValue.hpp>

#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bettercad {

/// Descriptive information about a document; not geometry.
struct DocumentMetadata {
    std::string description{};
    std::string author{};
    std::map<std::string, std::string> properties{};

    friend bool operator==(const DocumentMetadata&, const DocumentMetadata&) = default;
};

/// A CAD document: parameters, objects (sketches, features, bodies) and
/// metadata, plus identity and change tracking. There is no global document
/// state; everything lives in Document instances.
///
/// - IDs of parameters and objects come from one allocator and are never
///   reused.
/// - Names are unique across parameters and objects, so lookup by name is
///   unambiguous.
/// - revision() increments on every effective change. isDirty() reports
///   changes since the last markClean().
/// - All changes go through Document methods; contained items are exposed
///   only as const. Failed operations leave the document unchanged.
class BETTERCAD_CORE_EXPORT Document {
public:
    /// New document with a random DocumentId.
    explicit Document(std::string name = "Untitled");
    /// Document with a known identity (loading, tests).
    Document(DocumentId id, std::string name);

    Document(Document&&) noexcept;
    Document& operator=(Document&&) noexcept;
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    ~Document();

    /// Deep copy with identical identity and content.
    [[nodiscard]] Document clone() const;

    // --- Identity and metadata --------------------------------------------
    [[nodiscard]] DocumentId id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    /// Document names are free text but must be non-empty and have no control characters.
    Result<bool> setName(std::string name);
    [[nodiscard]] const DocumentMetadata& metadata() const noexcept { return metadata_; }
    Result<bool> setMetadata(DocumentMetadata metadata);

    // --- Parameters -------------------------------------------------------
    [[nodiscard]] const ParameterTable& parameters() const noexcept { return parameters_; }

    template <Dimension D>
    Result<ParameterId> createParameter(std::string name, const Quantity<D>& value,
                                        const Unit<D>& displayUnit) {
        return createParameter(std::move(name), value.si(), describe(displayUnit));
    }
    Result<ParameterId> createParameter(std::string name, double siValue,
                                        const UnitDescriptor& displayUnit);
    /// Inserts a parameter that already has an ID (undo/redo, loading). Its
    /// expression, if any, must parse; its names need not exist yet.
    Result<void> insertParameter(Parameter parameter);
    Result<Parameter> removeParameter(ParameterId id);

    /// Sets the value of a parameter. A driven parameter (one with an
    /// expression) is refused with FailedPrecondition: its value comes from
    /// its expression (see ParameterExpressions.hpp).
    template <Dimension D>
    Result<bool> setParameterValue(ParameterId id, const Quantity<D>& value) {
        return setParameterSiValue(id, D, value.si());
    }
    Result<bool> setParameterSiValue(ParameterId id, Dimension dimension, double siValue);
    Result<bool> setParameterValue(ParameterId id, double value, const UnitDescriptor& unit);
    Result<bool> setParameterDisplayUnit(ParameterId id, const UnitDescriptor& unit);
    /// Sets or clears (std::nullopt) the expression that drives a parameter.
    /// The text must parse (ParseError or InvalidArgument otherwise; see
    /// Expression::parse()). Its names are resolved, and its value computed,
    /// when expressions are evaluated: evaluateParameterExpressions(), which
    /// regeneration runs. Until then the parameter keeps its value.
    Result<bool> setParameterExpression(ParameterId id, std::optional<std::string> expression);
    /// Makes the parameter's name, value, unit and expression equal to @p state
    /// (same ID). Used by undo/redo; revisions keep increasing. The state's
    /// expression must parse.
    Result<bool> restoreParameter(const Parameter& state);

    // --- Objects ----------------------------------------------------------
    /// Adds a new object (whose ID must still be invalid) and assigns its ID.
    Result<ObjectId> addObject(std::unique_ptr<DocumentObject> object);
    /// Inserts an object that already has an ID (undo/redo).
    Result<void> insertObject(std::unique_ptr<DocumentObject> object);
    /// Adds a new object (ID still invalid) under the given ID, e.g. when
    /// loading a file. The ID must not be in use; it is reserved.
    Result<void> restoreObject(ObjectId id, std::unique_ptr<DocumentObject> object);
    Result<std::unique_ptr<DocumentObject>> removeObject(ObjectId id);

    [[nodiscard]] const DocumentObject* findObject(ObjectId id) const noexcept;
    [[nodiscard]] const DocumentObject* findObjectByName(std::string_view name) const noexcept;
    /// The object if it exists and is a T; nullptr otherwise.
    template <typename T>
    [[nodiscard]] const T* findObjectAs(ObjectId id) const noexcept {
        return dynamic_cast<const T*>(findObject(id));
    }
    [[nodiscard]] std::size_t objectCount() const noexcept { return objects_.size(); }
    /// All objects in ascending ID order.
    [[nodiscard]] auto objects() const {
        return objects_ | std::views::transform([](const auto& entry) -> const DocumentObject& {
                   return *entry.second;
               });
    }

    /// Applies @p mutation to the object, which must be a T. The mutation
    /// returns whether it changed anything; changes bump the object and
    /// document revisions.
    template <typename T, typename Mutation>
    Result<bool> modifyObject(ObjectId id, Mutation&& mutation) {
        DocumentObject* object = findMutableObject(id);
        if (object == nullptr) {
            return objectNotFound(id);
        }
        T* typed = dynamic_cast<T*>(object);
        if (typed == nullptr) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("{} is a '{}', which this operation cannot modify", id,
                                         object->typeName()));
        }
        Result<bool> changed = std::forward<Mutation>(mutation)(*typed);
        if (changed && *changed) {
            ++object->revision_;
            ++revision_;
        }
        return changed;
    }

    // --- Items of either kind ---------------------------------------------
    /// True if a parameter or object has this ID.
    [[nodiscard]] bool contains(ObjectId id) const noexcept;
    /// The ID as a ParameterId if it names a parameter (checked narrowing).
    [[nodiscard]] std::optional<ParameterId> asParameter(ObjectId id) const noexcept;
    /// Name of the parameter or object with this ID.
    [[nodiscard]] std::optional<std::string_view> nameOf(ObjectId id) const noexcept;
    /// ID of the parameter or object with this name.
    [[nodiscard]] std::optional<ObjectId> findByName(std::string_view name) const noexcept;
    /// Renames a parameter or object.
    Result<bool> rename(ObjectId id, std::string name);
    /// "<base>1", "<base>2", ...: the first such name not in use.
    [[nodiscard]] std::string uniqueName(std::string_view base) const;

    // --- Change tracking --------------------------------------------------
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] bool isDirty() const noexcept { return revision_ != cleanRevision_; }
    /// Declares the current state saved.
    void markClean() noexcept { cleanRevision_ = revision_; }
    /// Highest ID value allocated so far (persisted so IDs are never reused).
    [[nodiscard]] std::uint64_t lastAllocatedId() const noexcept { return ids_.lastValue(); }
    /// Makes later allocations return values above @p value (loading).
    void reserveIdsThrough(std::uint64_t value) noexcept { ids_.reserveThrough(value); }
    /// Revision of the parameter or object with this ID.
    [[nodiscard]] std::optional<std::uint64_t> revisionOf(ObjectId id) const noexcept;
    /// IDs of all parameters and objects, ascending.
    [[nodiscard]] std::vector<ObjectId> itemIds() const;

private:
    friend class ParameterExpressionWriter;

    /// Stores the value a driven parameter's expression evaluated to.
    Result<bool> storeExpressionValue(ParameterId id, const DimensionedValue& value);

    [[nodiscard]] DocumentObject* findMutableObject(ObjectId id) noexcept;
    [[nodiscard]] static std::unexpected<Error> objectNotFound(ObjectId id);
    [[nodiscard]] Result<void> requireNameAvailable(std::string_view name) const;
    [[nodiscard]] Result<void> requireNotDriven(ParameterId id) const;
    [[nodiscard]] static Result<void> checkExpressionSyntax(std::string_view parameter,
                                                            const std::optional<std::string>& expression);
    Result<bool> bump(Result<bool> changed) noexcept;

    DocumentId id_;
    std::string name_;
    DocumentMetadata metadata_;
    IdAllocator ids_;
    ParameterTable parameters_;
    std::map<ObjectId, std::unique_ptr<DocumentObject>> objects_;
    std::map<std::string, ObjectId, std::less<>> objectIdsByName_;
    std::uint64_t revision_ = 0;
    std::uint64_t cleanRevision_ = 0;
};

/// Same identity, name, metadata, parameters and objects. Revisions, dirty
/// state and the ID allocator position are change-tracking details and are
/// ignored.
[[nodiscard]] BETTERCAD_CORE_EXPORT bool equivalent(const Document& a, const Document& b);

} // namespace bettercad
