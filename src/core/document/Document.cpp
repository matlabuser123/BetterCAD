#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/parameters/Expression.hpp>

#include <algorithm>
#include <format>

namespace bettercad {

namespace {

constexpr std::size_t kMaxDocumentNameLength = 255;

Result<void> validateDocumentName(std::string_view name) {
    if (name.empty()) {
        return makeError(ErrorCode::InvalidArgument, "document name must not be empty");
    }
    if (name.size() > kMaxDocumentNameLength) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("document name is longer than {} bytes", kMaxDocumentNameLength));
    }
    const bool hasControl = std::ranges::any_of(name, [](char c) {
        const auto byte = static_cast<unsigned char>(c);
        return byte < 0x20 || byte == 0x7F;
    });
    if (hasControl) {
        return makeError(ErrorCode::InvalidArgument,
                         "document name must not contain control characters");
    }
    return {};
}

} // namespace

Document::Document(std::string name)
    : Document(DocumentId::fromValue(Uuid::generateV4()), std::move(name)) {}

Document::Document(DocumentId id, std::string name) : id_(id), name_(std::move(name)) {}

Document::Document(Document&&) noexcept = default;
Document& Document::operator=(Document&&) noexcept = default;
Document::~Document() = default;

Document Document::clone() const {
    Document copy(id_, name_);
    copy.metadata_ = metadata_;
    copy.ids_ = ids_;
    copy.parameters_ = parameters_;
    for (const auto& [id, object] : objects_) {
        copy.objects_.emplace(id, object->clone());
    }
    copy.objectIdsByName_ = objectIdsByName_;
    copy.revision_ = revision_;
    copy.cleanRevision_ = cleanRevision_;
    return copy;
}

Result<bool> Document::bump(Result<bool> changed) noexcept {
    if (changed && *changed) {
        ++revision_;
    }
    return changed;
}

// --- Identity and metadata ----------------------------------------------------

Result<bool> Document::setName(std::string name) {
    if (auto valid = validateDocumentName(name); !valid) {
        return std::unexpected(valid.error());
    }
    if (name == name_) {
        return false;
    }
    name_ = std::move(name);
    return bump(true);
}

Result<bool> Document::setMetadata(DocumentMetadata metadata) {
    if (metadata == metadata_) {
        return false;
    }
    metadata_ = std::move(metadata);
    return bump(true);
}

// --- Parameters ---------------------------------------------------------------

Result<void> Document::requireNameAvailable(std::string_view name) const {
    if (parameters_.findByName(name) != nullptr || objectIdsByName_.contains(name)) {
        return makeError(ErrorCode::AlreadyExists,
                         std::format("the name '{}' is already used in this document", name));
    }
    return {};
}

Result<ParameterId> Document::createParameter(std::string name, double siValue,
                                              const UnitDescriptor& displayUnit) {
    if (auto available = requireNameAvailable(name); !available) {
        return std::unexpected(available.error());
    }
    // Reserve the ID only once the parameter is valid, so failed attempts do
    // not consume IDs and ID assignment depends only on successful edits.
    const auto id = ParameterId::fromValue(ids_.lastValue() + 1);
    auto parameter = Parameter::create(id, std::move(name), siValue, displayUnit);
    if (!parameter) {
        return std::unexpected(parameter.error());
    }
    if (auto added = parameters_.add(std::move(*parameter)); !added) {
        return std::unexpected(added.error());
    }
    ids_.reserveThrough(id.value());
    ++revision_;
    return id;
}

Result<void> Document::insertParameter(Parameter parameter) {
    const ParameterId id = parameter.id();
    if (contains(id)) {
        return makeError(ErrorCode::AlreadyExists, std::format("{} is already in use", ObjectId{id}));
    }
    if (auto available = requireNameAvailable(parameter.name()); !available) {
        return std::unexpected(available.error());
    }
    if (auto valid = checkExpressionSyntax(parameter.name(), parameter.expression()); !valid) {
        return std::unexpected(valid.error());
    }
    if (auto added = parameters_.add(std::move(parameter)); !added) {
        return std::unexpected(added.error());
    }
    ids_.reserveThrough(id.value());
    ++revision_;
    return {};
}

Result<Parameter> Document::removeParameter(ParameterId id) {
    auto removed = parameters_.remove(id);
    if (removed) {
        ++revision_;
    }
    return removed;
}

Result<void> Document::requireNotDriven(ParameterId id) const {
    const Parameter* parameter = parameters_.find(id);
    if (parameter != nullptr && parameter->expression()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("parameter '{}' is driven by the expression '{}'; clear the expression to "
                                     "set its value",
                                     parameter->name(), *parameter->expression()));
    }
    return {};
}

Result<bool> Document::setParameterSiValue(ParameterId id, Dimension dimension, double siValue) {
    if (auto free = requireNotDriven(id); !free) {
        return std::unexpected(free.error());
    }
    return bump(parameters_.setSiValue(id, dimension, siValue));
}

Result<bool> Document::setParameterValue(ParameterId id, double value, const UnitDescriptor& unit) {
    if (auto free = requireNotDriven(id); !free) {
        return std::unexpected(free.error());
    }
    return bump(parameters_.setValue(id, value, unit));
}

Result<bool> Document::setParameterDisplayUnit(ParameterId id, const UnitDescriptor& unit) {
    return bump(parameters_.setDisplayUnit(id, unit));
}

Result<bool> Document::setParameterExpression(ParameterId id,
                                              std::optional<std::string> expression) {
    const Parameter* parameter = parameters_.find(id);
    if (parameter != nullptr) {
        if (auto valid = checkExpressionSyntax(parameter->name(), expression); !valid) {
            return std::unexpected(valid.error());
        }
    }
    return bump(parameters_.setExpression(id, std::move(expression)));
}

Result<bool> Document::storeExpressionValue(ParameterId id, const DimensionedValue& value) {
    const Parameter* parameter = parameters_.find(id);
    if (parameter == nullptr || !parameter->expression()) {
        return makeError(ErrorCode::Internal,
                         std::format("{} is not a driven parameter; no expression value can be stored", id));
    }
    return bump(parameters_.setSiValue(id, value.dimension, value.siValue));
}

Result<void> Document::checkExpressionSyntax(std::string_view parameter,
                                             const std::optional<std::string>& expression) {
    if (!expression) {
        return {};
    }
    if (auto parsed = Expression::parse(*expression); !parsed) {
        return makeError(parsed.error().code,
                         std::format("parameter '{}': expression '{}': {}", parameter, *expression,
                                     parsed.error().message));
    }
    return {};
}

Result<bool> Document::restoreParameter(const Parameter& state) {
    const Parameter* current = parameters_.find(state.id());
    if (current == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("{} does not exist", state.id()));
    }
    if (current->dimension() != state.dimension()) {
        return makeError(ErrorCode::DimensionMismatch,
                         std::format("cannot restore {}: the stored state has a different dimension",
                                     state.id()));
    }
    if (auto valid = checkExpressionSyntax(state.name(), state.expression()); !valid) {
        return std::unexpected(valid.error());
    }

    // Renaming is the only step that can fail for a valid state (the name may
    // be taken), so it runs first and a failure leaves everything unchanged.
    auto renamed = rename(state.id(), state.name());
    if (!renamed) {
        return renamed;
    }
    bool changed = *renamed;

    const auto apply = [&](Result<bool> step) -> Result<void> {
        if (!step) {
            return makeError(ErrorCode::Internal, std::format("restoring {} failed: {}", state.id(),
                                                              step.error().message));
        }
        if (*step) {
            changed = true;
            ++revision_;
        }
        return {};
    };
    if (auto r = apply(parameters_.setDisplayUnit(state.id(), state.displayUnit())); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = apply(parameters_.setSiValue(state.id(), state.dimension(), state.siValue())); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = apply(parameters_.setExpression(state.id(), state.expression())); !r) {
        return std::unexpected(r.error());
    }
    return changed;
}

// --- Objects -----------------------------------------------------------------

std::unexpected<Error> Document::objectNotFound(ObjectId id) {
    return makeError(ErrorCode::NotFound, std::format("{} does not exist", id));
}

Result<ObjectId> Document::addObject(std::unique_ptr<DocumentObject> object) {
    if (object == nullptr) {
        return makeError(ErrorCode::InvalidArgument, "cannot add a null object");
    }
    if (object->id().isValid()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("object '{}' already has {}; use insertObject()",
                                     object->name(), object->id()));
    }
    if (auto valid = validateObjectName(object->name()); !valid) {
        return std::unexpected(valid.error());
    }
    if (auto available = requireNameAvailable(object->name()); !available) {
        return std::unexpected(available.error());
    }
    const ObjectId id = ids_.allocate<ObjectId>();
    object->id_ = id;
    objectIdsByName_.emplace(object->name(), id);
    objects_.emplace(id, std::move(object));
    ++revision_;
    return id;
}

Result<void> Document::insertObject(std::unique_ptr<DocumentObject> object) {
    if (object == nullptr) {
        return makeError(ErrorCode::InvalidArgument, "cannot insert a null object");
    }
    const ObjectId id = object->id();
    if (!id.isValid()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("object '{}' has no ID; use addObject()", object->name()));
    }
    if (contains(id)) {
        return makeError(ErrorCode::AlreadyExists, std::format("{} is already in use", id));
    }
    if (auto valid = validateObjectName(object->name()); !valid) {
        return std::unexpected(valid.error());
    }
    if (auto available = requireNameAvailable(object->name()); !available) {
        return std::unexpected(available.error());
    }
    objectIdsByName_.emplace(object->name(), id);
    objects_.emplace(id, std::move(object));
    ids_.reserveThrough(id.value());
    ++revision_;
    return {};
}

Result<void> Document::restoreObject(ObjectId id, std::unique_ptr<DocumentObject> object) {
    if (object == nullptr) {
        return makeError(ErrorCode::InvalidArgument, "cannot restore a null object");
    }
    if (object->id().isValid()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("object '{}' already has {}", object->name(), object->id()));
    }
    if (!id.isValid()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("object '{}' needs a valid ID", object->name()));
    }
    object->id_ = id;
    return insertObject(std::move(object));
}

Result<std::unique_ptr<DocumentObject>> Document::removeObject(ObjectId id) {
    auto node = objects_.extract(id);
    if (node.empty()) {
        return objectNotFound(id);
    }
    objectIdsByName_.erase(node.mapped()->name());
    ++revision_;
    return std::move(node.mapped());
}

const DocumentObject* Document::findObject(ObjectId id) const noexcept {
    const auto it = objects_.find(id);
    return it == objects_.end() ? nullptr : it->second.get();
}

DocumentObject* Document::findMutableObject(ObjectId id) noexcept {
    const auto it = objects_.find(id);
    return it == objects_.end() ? nullptr : it->second.get();
}

const DocumentObject* Document::findObjectByName(std::string_view name) const noexcept {
    const auto it = objectIdsByName_.find(name);
    return it == objectIdsByName_.end() ? nullptr : findObject(it->second);
}

// --- Items of either kind -------------------------------------------------------

bool Document::contains(ObjectId id) const noexcept {
    return asParameter(id).has_value() || objects_.contains(id);
}

std::optional<ParameterId> Document::asParameter(ObjectId id) const noexcept {
    const auto parameterId = ParameterId::fromValue(id.value());
    if (parameters_.contains(parameterId)) {
        return parameterId;
    }
    return std::nullopt;
}

std::optional<std::string_view> Document::nameOf(ObjectId id) const noexcept {
    if (const auto parameterId = asParameter(id)) {
        return parameters_.find(*parameterId)->name();
    }
    if (const DocumentObject* object = findObject(id)) {
        return object->name();
    }
    return std::nullopt;
}

std::optional<ObjectId> Document::findByName(std::string_view name) const noexcept {
    if (const Parameter* parameter = parameters_.findByName(name)) {
        return ObjectId{parameter->id()};
    }
    if (const DocumentObject* object = findObjectByName(name)) {
        return object->id();
    }
    return std::nullopt;
}

Result<bool> Document::rename(ObjectId id, std::string name) {
    const std::optional<std::string_view> current = nameOf(id);
    if (!current) {
        return objectNotFound(id);
    }
    if (*current == name) {
        return false;
    }
    const std::optional<ParameterId> parameterId = asParameter(id);
    auto valid = parameterId ? validateParameterName(name) : validateObjectName(name);
    if (!valid) {
        return std::unexpected(valid.error());
    }
    if (auto available = requireNameAvailable(name); !available) {
        return std::unexpected(available.error());
    }

    if (parameterId) {
        return bump(parameters_.rename(*parameterId, std::move(name)));
    }
    DocumentObject* object = findMutableObject(id);
    objectIdsByName_.emplace(name, id);
    objectIdsByName_.erase(object->name_);
    object->name_ = std::move(name);
    ++object->revision_;
    return bump(true);
}

std::optional<std::uint64_t> Document::revisionOf(ObjectId id) const noexcept {
    if (const auto parameterId = asParameter(id)) {
        return parameters_.find(*parameterId)->revision();
    }
    if (const DocumentObject* object = findObject(id)) {
        return object->revision();
    }
    return std::nullopt;
}

std::vector<ObjectId> Document::itemIds() const {
    std::vector<ObjectId> ids;
    ids.reserve(parameters_.size() + objects_.size());
    for (const Parameter& parameter : parameters_.all()) {
        ids.push_back(parameter.id());
    }
    for (const auto& [id, object] : objects_) {
        ids.push_back(id);
    }
    std::ranges::sort(ids);
    return ids;
}

std::string Document::uniqueName(std::string_view base) const {
    for (std::uint64_t n = 1;; ++n) {
        std::string candidate = std::format("{}{}", base, n);
        if (!findByName(candidate)) {
            return candidate;
        }
    }
}

bool equivalent(const Document& a, const Document& b) {
    return a.id() == b.id() && a.name() == b.name() && a.metadata() == b.metadata() &&
           equivalent(a.parameters(), b.parameters()) &&
           std::ranges::equal(a.objects(), b.objects(),
                              [](const DocumentObject& x, const DocumentObject& y) {
                                  return equivalent(x, y);
                              });
}

} // namespace bettercad
