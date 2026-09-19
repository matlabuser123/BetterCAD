// Build-failure tests for strong IDs (see CMakeLists.txt in this directory).
// Built once without any BETTERCAD_CF_* macro as a control, which must compile.
#include <bettercad/core/Id.hpp>

#include <cstdint>

using namespace bettercad;

namespace {

void takesFeature(FeatureId /*unused*/) {}
void takesObject(ObjectId /*unused*/) {}

} // namespace

int main() {
    const SketchId sketch = SketchId::fromValue(1);
    takesObject(sketch); // document object IDs widen to ObjectId
    takesFeature(FeatureId::fromValue(2));

#if defined(BETTERCAD_CF_SKETCH_ID_AS_FEATURE_ID)
    takesFeature(sketch);
#elif defined(BETTERCAD_CF_INTEGER_TO_ID)
    [[maybe_unused]] SketchId fromInteger = 5;
#elif defined(BETTERCAD_CF_ID_TO_INTEGER)
    [[maybe_unused]] std::uint64_t value = sketch;
#elif defined(BETTERCAD_CF_COMPARE_DIFFERENT_KINDS)
    [[maybe_unused]] bool same = sketch == FeatureId::fromValue(1);
#elif defined(BETTERCAD_CF_OBJECT_TO_SKETCH_ID)
    [[maybe_unused]] SketchId narrowed = ObjectId::fromValue(1);
#elif defined(BETTERCAD_CF_OBJECT_TO_COMPONENT_ID)
    // A ComponentId widens to ObjectId, never the other way: an object is
    // not a component just because it has an ID (P13-COMP-001).
    [[maybe_unused]] ComponentId narrowed = ObjectId::fromValue(1);
#elif defined(BETTERCAD_CF_COMPONENT_ID_AS_FEATURE_ID)
    // Nor is a component a feature, though both widen to ObjectId.
    [[maybe_unused]] FeatureId asFeature = ComponentId::fromValue(1);
#elif defined(BETTERCAD_CF_ENTITY_ID_AS_OBJECT_ID)
    takesObject(EntityId::fromValue(1));
#endif

    return 0;
}
