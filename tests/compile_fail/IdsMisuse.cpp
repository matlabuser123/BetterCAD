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
    // A material is a document object (ADR-025), so this widening is intended
    // and must keep compiling; the cases below are what must not.
    takesObject(MaterialId::fromValue(3));

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
#elif defined(BETTERCAD_CF_OBJECT_TO_MATE_ID)
    // A MateId widens to ObjectId, never the other way (P13-MATE-001).
    [[maybe_unused]] MateId narrowed = ObjectId::fromValue(1);
#elif defined(BETTERCAD_CF_MATE_ID_AS_COMPONENT_ID)
    // A mate relates components; it is not one of them.
    [[maybe_unused]] ComponentId asComponent = MateId::fromValue(1);
#elif defined(BETTERCAD_CF_OBJECT_TO_SHEET_ID)
    // A SheetId widens to ObjectId, never the other way (P14-SHEET-001).
    [[maybe_unused]] SheetId narrowed = ObjectId::fromValue(1);
#elif defined(BETTERCAD_CF_SHEET_ID_AS_COMPONENT_ID)
    // A sheet is not a component, though both widen to ObjectId. This is the
    // ADR-017 property that a drawing's identities are its own.
    [[maybe_unused]] ComponentId asComponent = SheetId::fromValue(1);
#elif defined(BETTERCAD_CF_OBJECT_TO_MATERIAL_ID)
    // A MaterialId widens to ObjectId, never the other way: an object is not a
    // material just because it has an ID (P15-MAT-001).
    [[maybe_unused]] MaterialId narrowed = ObjectId::fromValue(1);
#elif defined(BETTERCAD_CF_MATERIAL_ID_AS_FEATURE_ID)
    // Nor is a material a feature, though both widen to ObjectId. A material
    // describes what a part is made of; it builds no geometry.
    [[maybe_unused]] FeatureId asFeature = MaterialId::fromValue(1);
#elif defined(BETTERCAD_CF_SHEET_ID_AS_MATERIAL_ID)
    // Two unrelated document-object kinds do not convert to each other.
    [[maybe_unused]] MaterialId asMaterial = SheetId::fromValue(1);
#elif defined(BETTERCAD_CF_INTEGER_TO_MATERIAL_ID)
    // Identity never comes from a bare number, so a row index or a loop counter
    // cannot become a MaterialId by accident.
    [[maybe_unused]] MaterialId fromInteger = 7;
#elif defined(BETTERCAD_CF_MATERIAL_ID_TO_INTEGER)
    [[maybe_unused]] std::uint64_t value = MaterialId::fromValue(1);
#elif defined(BETTERCAD_CF_ENTITY_ID_AS_OBJECT_ID)
    takesObject(EntityId::fromValue(1));
#endif

    return 0;
}
