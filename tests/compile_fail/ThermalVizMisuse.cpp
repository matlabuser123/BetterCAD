// Build-failure tests for the thermal visualisation and inspection (see CMakeLists.txt in this directory).
// Built once without any BETTERCAD_CF_* macro as a control, which must compile -- so every failure below is
// attributable to its own line and not to a missing header.
//
// Five claims of P18-VIZ-001 are enforced here rather than reviewed:
//
//   1. A THERMAL VIEW READS, IT DOES NOT AUTHOR. A field, a legend, an inspection, a glyph and an overlay are
//      products of a read: there is no setter on a field's extremes, no editable stale flag on the display
//      state, no write through a const view, and no function that recovers, smooths, interpolates, rescales or
//      rebinds anything. Their absence from the API is the proof that the view owns no thermal authority.
//
//   2. THE UNITS CANNOT BE CONFUSED. A temperature (K), a gradient (K/m) and a heat flux (W/m^2) are three
//      dimensions; none converts to another or to a plain double, and there is no Celsius or millimetre
//      quantity to rescale into.
//
//   3. THE ENUMERATIONS ARE NOT BOOLEANS OR INTEGERS. A result state, a validation badge, a field
//      availability, a boundary class status and an overlay role are distinct enumerations; a badge is not
//      the core's validation status, and "a partial classification" cannot be read as "complete".
//
//   4. THE IDENTITY SPACES DO NOT MIX. A node and an element are different identifiers, and a frame index is
//      not a time: a playback frame IS a stored state and its time is a stored time.
//
//   5. THE PLAYBACK AND THE RESULT VIEW ARE BOUND, NOT CONSTRUCTED. `of(...)` is the only way in, so a view
//      cannot exist without having been checked against the result it describes.
#include <bettercad/renderer/ThermalContour.hpp>
#include <bettercad/renderer/ThermalInspection.hpp>
#include <bettercad/renderer/ThermalOverlay.hpp>
#include <bettercad/renderer/ThermalResultView.hpp>

#include <cstddef>

using namespace bettercad;
using namespace bettercad::renderer;

int main() {
    // Dead code that must still COMPILE in the control, so every call below is checked against the real
    // signatures without needing a document at run time.
    const ThermalScalarField* field = nullptr;
    const ThermalContourLegend* legend = nullptr;
    const ThermalDisplayState* display = nullptr;
    const ThermalNodeInspection* nodeInspection = nullptr;
    const ThermalElementInspection* elementInspection = nullptr;
    const ThermalPlayback* playback = nullptr;
    const ThermalOverlay* overlay = nullptr;
    const ThermalExtreme* extreme = nullptr;
    const BoundaryCardinality* cardinality = nullptr;
    const AppliedHeatFluxGlyph* applied = nullptr;
    const HeatFluxVectorGlyph* vector = nullptr;
    if (field != nullptr && legend != nullptr && display != nullptr && nodeInspection != nullptr &&
        elementInspection != nullptr && playback != nullptr && overlay != nullptr && extreme != nullptr &&
        cardinality != nullptr && applied != nullptr && vector != nullptr) {
        (void)field->complete();
        (void)field->uniform();
        (void)field->minimum;
        (void)field->maximum;
        (void)field->expected;
        (void)field->classified;
        (void)legend->bandOf(0.0);
        (void)legend->partitionsItsRange();
        (void)display->resultMayDrawAsCurrent();
        (void)display->resultInspectable();
        (void)display->intentMayDraw();
        (void)display->staleReasons;
        (void)display->badge;
        (void)thermalFieldAvailability(*display);
        (void)thermalBanner(thermalFieldAvailability(*display));
        (void)nodeInspection->temperature;
        (void)elementInspection->heatFlux;
        (void)elementInspection->gradientMagnitude;
        (void)playback->frame();
        (void)playback->time();
        (void)playback->frameCount();
        (void)overlay->glyphs;
        (void)cardinality->status;
        (void)applied->flux;
        (void)vector->magnitude;
        (void)extreme->value;
        (void)descriptorOf(ThermalChannel::Temperature).unit;
    }

#if defined(BETTERCAD_CF_A_TEMPERATURE_IS_NOT_A_DOUBLE)
    // 300.0 is not a temperature: there is no implicit conversion from a plain number.
    [[maybe_unused]] Temperature t = 300.0;
#endif

#if defined(BETTERCAD_CF_A_QUANTITY_IS_NOT_A_DOUBLE)
    if (nodeInspection != nullptr) {
        [[maybe_unused]] double t = nodeInspection->temperature;
    }
#endif

#if defined(BETTERCAD_CF_A_TEMPERATURE_IS_NOT_A_HEAT_FLUX)
    [[maybe_unused]] HeatFlux q = Temperature::fromSi(300.0);
#endif

#if defined(BETTERCAD_CF_A_GRADIENT_IS_NOT_A_HEAT_FLUX)
    // grad(T) in K/m and q in W/m^2 differ by the conductivity, which a view does not hold.
    [[maybe_unused]] HeatFlux q = TemperatureGradient::fromSi(1.0);
#endif

#if defined(BETTERCAD_CF_A_HEAT_FLUX_IS_NOT_A_GRADIENT)
    [[maybe_unused]] TemperatureGradient g = HeatFlux::fromSi(1.0);
#endif

#if defined(BETTERCAD_CF_A_FRAME_INDEX_IS_NOT_A_TIME)
    // A frame is a stored state; its time is a stored time, never a count.
    if (playback != nullptr) {
        [[maybe_unused]] Time t = playback->frame();
    }
#endif

#if defined(BETTERCAD_CF_A_CHANNEL_IS_NOT_AN_INTEGER)
    [[maybe_unused]] ThermalChannel channel = 1;
#endif

#if defined(BETTERCAD_CF_A_CHANNEL_IS_NOT_A_POST_FIELD)
    [[maybe_unused]] thermal::ThermalPostField source = ThermalChannel::Temperature;
#endif

#if defined(BETTERCAD_CF_A_RESULT_STATE_IS_NOT_A_BOOL)
    if (display != nullptr) {
        [[maybe_unused]] bool current = display->result;
    }
#endif

#if defined(BETTERCAD_CF_A_BADGE_IS_NOT_A_BOOL)
    if (display != nullptr) {
        [[maybe_unused]] bool passed = display->badge;
    }
#endif

#if defined(BETTERCAD_CF_A_BADGE_IS_NOT_A_VALIDATION_STATUS)
    // The badge is the view's presentation of the core's verdict; it is not the verdict and cannot be passed
    // where the core's status is wanted.
    [[maybe_unused]] thermal::ValidationStatus status = ThermalValidationBadge::Pass;
#endif

#if defined(BETTERCAD_CF_AN_AVAILABILITY_IS_NOT_A_BOOL)
    if (display != nullptr) {
        [[maybe_unused]] bool available = thermalFieldAvailability(*display);
    }
#endif

#if defined(BETTERCAD_CF_A_BOUNDARY_STATUS_IS_NOT_A_BOOL)
    // "Is it complete?" is a question about the status, answered by comparing it; it is not a truth value.
    if (cardinality != nullptr) {
        [[maybe_unused]] bool complete = cardinality->status;
    }
#endif

#if defined(BETTERCAD_CF_A_BOUNDARY_STATUS_IS_NOT_AN_INTEGER)
    [[maybe_unused]] BoundaryClassStatus status = 0;
#endif

#if defined(BETTERCAD_CF_AN_OVERLAY_ROLE_IS_NOT_AN_INTEGER)
    [[maybe_unused]] ThermalOverlayRole role = 2;
#endif

#if defined(BETTERCAD_CF_A_TARGET_KIND_IS_NOT_AN_OVERLAY_ROLE)
    [[maybe_unused]] ThermalOverlayRole role = BoundaryTargetKind::AppliedHeatFlux;
#endif

#if defined(BETTERCAD_CF_AN_EXTREME_KIND_IS_NOT_A_BOOL)
    if (extreme != nullptr) {
        [[maybe_unused]] bool maximum = extreme->kind;
    }
#endif

#if defined(BETTERCAD_CF_A_NODE_ID_IS_NOT_AN_ELEMENT_ID)
    // A node and an element are different identity spaces.
    const meshing::Mesh* mesh = nullptr;
    const thermal::ThermalPostResult* post = nullptr;
    if (mesh != nullptr && post != nullptr) {
        (void)inspectThermalElement(*mesh, *post, 0U, meshing::NodeId{}, renderer::ThermalResultState::Current);
    }
#endif

#if defined(BETTERCAD_CF_AN_ELEMENT_ID_IS_NOT_A_NODE_ID)
    const meshing::Mesh* mesh = nullptr;
    const thermal::ThermalDofMap* map = nullptr;
    const thermal::ThermalPostResult* post = nullptr;
    if (mesh != nullptr && map != nullptr && post != nullptr) {
        (void)inspectThermalNode(*mesh, *map, *post, 0U, meshing::ElementId{}, renderer::ThermalResultState::Current);
    }
#endif

#if defined(BETTERCAD_CF_AN_INSPECTION_MUST_STATE_THE_RESULT_STATE)
    // P18-QUAL-001. The inspection used to default the result's currentness to `Current`, so a caller that forgot to ask would
    // have labelled a stale result current. The state is now a required argument.
    const meshing::Mesh* mesh = nullptr;
    const thermal::ThermalPostResult* post = nullptr;
    if (mesh != nullptr && post != nullptr) {
        (void)inspectThermalElement(*mesh, *post, 0U, meshing::ElementId{});
    }
#endif

#if defined(BETTERCAD_CF_THE_DISPLAY_STATE_HAS_NO_STALE_FLAG)
    // Staleness is derived from the sources on every call; there is no flag a GUI could set or forget to clear.
    if (display != nullptr) {
        (void)display->stale;
    }
#endif

#if defined(BETTERCAD_CF_THE_DISPLAY_STATE_HAS_NO_OVERRIDE)
    if (display != nullptr) {
        (void)display->forceCurrent;
    }
#endif

#if defined(BETTERCAD_CF_THE_FIELD_HAS_NO_SMOOTHED_VALUES)
    // Element fields are shown element-wise, unsmoothed.
    if (field != nullptr) {
        (void)field->smoothed;
    }
#endif

#if defined(BETTERCAD_CF_THE_FIELD_HAS_NO_CELSIUS)
    if (field != nullptr) {
        (void)field->celsius;
    }
#endif

#if defined(BETTERCAD_CF_THE_FIELD_HAS_NO_INTERPOLATED_TIME)
    if (field != nullptr) {
        (void)field->interpolatedTime;
    }
#endif

#if defined(BETTERCAD_CF_THE_LEGEND_HAS_NO_RESCALED_UNIT)
    if (legend != nullptr) {
        (void)legend->displayScale;
    }
#endif

#if defined(BETTERCAD_CF_THE_NODE_INSPECTION_HAS_NO_MILLIMETRE_POSITION)
    // The inspection reports the canonical position; the one metre-to-millimetre conversion is the adapter's.
    if (nodeInspection != nullptr) {
        (void)nodeInspection->positionMm;
    }
#endif

#if defined(BETTERCAD_CF_THE_ELEMENT_INSPECTION_HAS_NO_NORMALISED_FLUX)
    // The heat flux is the stored vector; it is not normalised in storage.
    if (elementInspection != nullptr) {
        (void)elementInspection->unitHeatFlux;
    }
#endif

#if defined(BETTERCAD_CF_THE_ELEMENT_INSPECTION_HAS_NO_CONDUCTIVITY)
    // The view keeps no copy of k.
    if (elementInspection != nullptr) {
        (void)elementInspection->conductivity;
    }
#endif

#if defined(BETTERCAD_CF_THE_ELEMENT_INSPECTION_HAS_NO_MATERIAL)
    if (elementInspection != nullptr) {
        (void)elementInspection->material;
    }
#endif

#if defined(BETTERCAD_CF_THE_PLAYBACK_HAS_NO_TIME_STEP)
    // A frame time is a stored time; it is not `frame * dt`.
    if (playback != nullptr) {
        (void)playback->timeStep();
    }
#endif

#if defined(BETTERCAD_CF_THE_PLAYBACK_HAS_NO_INTERPOLATION)
    if (playback != nullptr) {
        (void)playback->interpolate(0.5);
    }
#endif

#if defined(BETTERCAD_CF_THE_PLAYBACK_HAS_NO_SET_TIME)
    ThermalPlayback* mutablePlayback = nullptr;
    if (mutablePlayback != nullptr) {
        (void)mutablePlayback->setTime(0.5);
    }
#endif

#if defined(BETTERCAD_CF_THE_GLYPH_HAS_NO_NORMALISED_DIRECTION_STORE)
    if (vector != nullptr) {
        (void)vector->unitVector;
    }
#endif

#if defined(BETTERCAD_CF_THE_OVERLAY_HAS_NO_RESULT_STORE)
    if (overlay != nullptr) {
        (void)overlay->result;
    }
#endif

#if defined(BETTERCAD_CF_THERE_IS_NO_GRADIENT_RECOVERY_IN_THE_VIEW)
    (void)renderer::recoverThermalGradient();
#endif

#if defined(BETTERCAD_CF_THERE_IS_NO_SMOOTHING)
    (void)renderer::smoothThermalField();
#endif

#if defined(BETTERCAD_CF_THERE_IS_NO_FRAME_INTERPOLATION)
    (void)renderer::interpolateThermalFrame();
#endif

#if defined(BETTERCAD_CF_THERE_IS_NO_CELSIUS_CONVERSION)
    (void)renderer::toCelsius();
#endif

#if defined(BETTERCAD_CF_THERE_IS_NO_NEAREST_FACE_REBINDING)
    // A boundary symbol follows the canonical load or condition through the qualified mapping; there is no
    // geometric rebinding.
    (void)renderer::rebindToNearestFace();
#endif

#if defined(BETTERCAD_CF_THERE_IS_NO_VIEW_SIDE_SOLVE)
    (void)renderer::solveThermalFromView();
#endif

#if defined(BETTERCAD_CF_THERE_IS_NO_STALE_SETTER)
    (void)renderer::setThermalStale();
#endif

#if defined(BETTERCAD_CF_QUANTITIES_HAVE_NO_CELSIUS_FACTORY)
    (void)Temperature::fromCelsius(20.0);
#endif

#if defined(BETTERCAD_CF_THE_FIELD_IS_READ_ONLY_THROUGH_A_CONST_VIEW)
    if (field != nullptr) {
        field->minimum = 0.0;
    }
#endif

#if defined(BETTERCAD_CF_THE_EXTREME_IS_READ_ONLY_THROUGH_A_CONST_VIEW)
    if (extreme != nullptr) {
        extreme->value = 0.0;
    }
#endif

#if defined(BETTERCAD_CF_THE_DISPLAY_STATE_IS_READ_ONLY_THROUGH_A_CONST_VIEW)
    if (display != nullptr) {
        display->result = ThermalResultState::Current;
    }
#endif

#if defined(BETTERCAD_CF_THE_CARDINALITY_IS_READ_ONLY_THROUGH_A_CONST_VIEW)
    // A caller cannot turn a partial classification into a complete one.
    if (cardinality != nullptr) {
        cardinality->status = BoundaryClassStatus::Complete;
    }
#endif

#if defined(BETTERCAD_CF_THE_INSPECTION_IS_READ_ONLY_THROUGH_A_CONST_VIEW)
    if (elementInspection != nullptr) {
        elementInspection->heatFluxMagnitude = HeatFlux::fromSi(0.0);
    }
#endif

#if defined(BETTERCAD_CF_THE_PLAYBACK_CURSOR_IS_NOT_MOVED_THROUGH_A_CONST_VIEW)
    if (playback != nullptr) {
        playback->first();
    }
#endif

#if defined(BETTERCAD_CF_THE_BANNER_TEXT_IS_READ_ONLY)
    if (display != nullptr) {
        const std::string_view banner = thermalBanner(thermalFieldAvailability(*display));
        banner.remove_prefix(1);
        banner = "";
    }
#endif

#if defined(BETTERCAD_CF_A_PLAYBACK_IS_BOUND_NOT_CONSTRUCTED)
    ThermalPlayback unbound;
#endif

#if defined(BETTERCAD_CF_A_RESULT_VIEW_IS_BOUND_NOT_CONSTRUCTED)
    ThermalResultView unbound;
#endif

    return 0;
}
