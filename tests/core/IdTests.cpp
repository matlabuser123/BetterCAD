#include <bettercad/core/Id.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <format>
#include <map>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

using namespace bettercad;

namespace {

// ---------------------------------------------------------------------------
// Compile-time type safety. Build-failure tests in tests/compile_fail/ show
// the diagnostics for direct misuse.
// ---------------------------------------------------------------------------
template <typename A, typename B>
concept EqualityComparable = requires(A a, B b) { a == b; };

// Every ID kind is a distinct type.
static_assert(!std::is_same_v<SketchId, FeatureId>);
static_assert(!std::is_convertible_v<SketchId, FeatureId>);
static_assert(!std::is_convertible_v<FeatureId, SketchId>);
static_assert(!std::is_convertible_v<ParameterId, BodyId>);
static_assert(!std::is_convertible_v<EntityId, ConstraintId>);
static_assert(!std::is_convertible_v<FaceId, EdgeId>);
static_assert(!std::is_convertible_v<EdgeId, VertexId>);
static_assert(!EqualityComparable<SketchId, FeatureId>);
static_assert(!EqualityComparable<FaceId, EdgeId>);

// No conversion to or from raw integers.
static_assert(!std::is_convertible_v<std::uint64_t, SketchId>);
static_assert(!std::is_constructible_v<SketchId, std::uint64_t>);
static_assert(!std::is_convertible_v<SketchId, std::uint64_t>);
static_assert(!std::is_convertible_v<int, EntityId>);
static_assert(!EqualityComparable<SketchId, int>);

// Document object IDs widen to ObjectId; nothing narrows implicitly.
static_assert(std::is_convertible_v<SketchId, ObjectId>);
static_assert(std::is_convertible_v<FeatureId, ObjectId>);
static_assert(std::is_convertible_v<ParameterId, ObjectId>);
static_assert(std::is_convertible_v<BodyId, ObjectId>);
static_assert(!std::is_convertible_v<ObjectId, SketchId>);
static_assert(!std::is_convertible_v<ObjectId, FeatureId>);
// Sketch-scoped and topology IDs are not document objects.
static_assert(!std::is_convertible_v<EntityId, ObjectId>);
static_assert(!std::is_convertible_v<ConstraintId, ObjectId>);
static_assert(!std::is_convertible_v<FaceId, ObjectId>);
static_assert(!std::is_convertible_v<DocumentId, ObjectId>);

// IDs are cheap value types.
static_assert(std::is_trivially_copyable_v<SketchId>);
static_assert(sizeof(SketchId) == sizeof(std::uint64_t));
static_assert(sizeof(DocumentId) == 16);

// Usable in constant expressions.
static_assert(!SketchId{}.isValid());
static_assert(SketchId::fromValue(7).value() == 7);
static_assert(SketchId::fromValue(1) < SketchId::fromValue(2));

} // namespace

TEST_CASE("Default-constructed IDs are invalid", "[ids]") {
    CHECK_FALSE(SketchId{}.isValid());
    CHECK_FALSE(static_cast<bool>(FeatureId{}));
    CHECK_FALSE(ObjectId::fromValue(0).isValid());
    CHECK_FALSE(DocumentId{}.isValid());
    CHECK(SketchId{} == SketchId::fromValue(0));
}

TEST_CASE("IDs carry their value and compare by value", "[ids]") {
    const auto a = SketchId::fromValue(42);
    const auto b = SketchId::fromValue(42);
    const auto c = SketchId::fromValue(43);

    CHECK(a.isValid());
    CHECK(a.value() == 42);
    CHECK(a == b);
    CHECK(a != c);
    CHECK(a < c);
    CHECK(c > b);
}

TEST_CASE("Document object IDs widen to ObjectId", "[ids]") {
    const auto sketch = SketchId::fromValue(5);
    const ObjectId object = sketch;

    CHECK(object.value() == 5);
    CHECK(object == sketch);
    CHECK(ObjectId{FeatureId::fromValue(9)} == ObjectId::fromValue(9));
}

TEST_CASE("IDs work as ordered and hashed container keys", "[ids]") {
    std::map<SketchId, int> ordered;
    std::unordered_map<SketchId, int> hashed;
    for (std::uint64_t v : {3u, 1u, 2u}) {
        ordered.emplace(SketchId::fromValue(v), static_cast<int>(v));
        hashed.emplace(SketchId::fromValue(v), static_cast<int>(v));
    }
    CHECK(ordered.begin()->first == SketchId::fromValue(1));
    CHECK(hashed.at(SketchId::fromValue(2)) == 2);

    std::unordered_set<DocumentId> documents;
    documents.insert(DocumentId::fromValue(Uuid::generateV4()));
    documents.insert(DocumentId::fromValue(Uuid::generateV4()));
    CHECK(documents.size() == 2);
}

TEST_CASE("IDs format as kind:value", "[ids]") {
    CHECK(std::format("{}", SketchId::fromValue(12)) == "sketch:12");
    CHECK(std::format("{}", ObjectId::fromValue(3)) == "object:3");
    CHECK(std::format("{}", FeatureId::fromValue(1)) == "feature:1");
    CHECK(std::format("{}", ParameterId::fromValue(2)) == "parameter:2");
    CHECK(std::format("{}", BodyId::fromValue(4)) == "body:4");
    CHECK(std::format("{}", EntityId::fromValue(5)) == "entity:5");
    CHECK(std::format("{}", ConstraintId::fromValue(6)) == "constraint:6");
    CHECK(std::format("{}", FaceId::fromValue(7)) == "face:7");
    CHECK(std::format("{}", EdgeId::fromValue(8)) == "edge:8");
    CHECK(std::format("{}", VertexId::fromValue(9)) == "vertex:9");

    const auto uuid = Uuid::parse("123e4567-e89b-42d3-a456-426614174000");
    REQUIRE(uuid.has_value());
    CHECK(std::format("{}", DocumentId::fromValue(*uuid)) ==
          "document:123e4567-e89b-42d3-a456-426614174000");
}

TEST_CASE("IdAllocator hands out increasing IDs starting at 1", "[ids][allocator]") {
    IdAllocator allocator;
    CHECK(allocator.lastValue() == 0);

    const auto first = allocator.allocate<SketchId>();
    const auto second = allocator.allocate<FeatureId>();
    const auto third = allocator.allocate<ParameterId>();

    CHECK(first.value() == 1);
    CHECK(second.value() == 2);
    CHECK(third.value() == 3);
    CHECK(allocator.lastValue() == 3);
    // Kinds sharing one allocator never collide in the object ID space.
    CHECK(ObjectId{first} != ObjectId{second});
}

TEST_CASE("IdAllocator never reuses values", "[ids][allocator]") {
    IdAllocator allocator;
    std::set<EntityId> live;
    for (int i = 0; i < 5; ++i) {
        live.insert(allocator.allocate<EntityId>());
    }
    // Deleting items must not free their IDs: identity is not an index.
    live.erase(EntityId::fromValue(2));
    live.erase(EntityId::fromValue(5));

    const auto next = allocator.allocate<EntityId>();
    CHECK(next.value() == 6);
    CHECK_FALSE(live.contains(next));
}

TEST_CASE("IdAllocator can reserve IDs loaded from a document", "[ids][allocator]") {
    IdAllocator allocator;
    allocator.reserveThrough(100);
    CHECK(allocator.allocate<ObjectId>().value() == 101);

    allocator.reserveThrough(50); // lower reservations never move backwards
    CHECK(allocator.allocate<ObjectId>().value() == 102);
}

TEST_CASE("IdAllocator reports exhaustion instead of wrapping", "[ids][allocator]") {
    IdAllocator allocator;
    allocator.reserveThrough(UINT64_MAX - 1);
    CHECK(allocator.allocate<ObjectId>().value() == UINT64_MAX);
    CHECK_THROWS_AS(allocator.allocate<ObjectId>(), std::overflow_error);
}

TEST_CASE("Independent allocators produce independent ID spaces", "[ids][allocator]") {
    IdAllocator sketchA;
    IdAllocator sketchB;
    CHECK(sketchA.allocate<EntityId>() == sketchB.allocate<EntityId>());
}
