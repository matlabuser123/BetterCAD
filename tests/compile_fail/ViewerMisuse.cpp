// Build-failure tests for the 3D viewer (see CMakeLists.txt in this
// directory). Built once without any BETTERCAD_CF_* macro as a control, which
// must compile -- so every failure below is attributable to its own line and
// not to a missing header.
//
// What these prove is INFRA-VIEWER-001's central restriction: THE VIEWER OWNS
// NO ENGINEERING STATE. A runtime fingerprint shows that today's
// implementation does not change a body; these show that no implementation
// behind this API could, and that the viewer's own handles cannot be mistaken
// for engineering identity.
//
// They also pin the layering rules, which a GUI is the most likely place to
// break: this file includes the viewer's public header and needs neither Qt
// nor OCCT to do it.
#include <bettercad/core/Id.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/renderer/Viewer.hpp>

using namespace bettercad;
using namespace bettercad::renderer;

namespace {

/// The signatures the API really has. Spelling them out means a widening of
/// either is a build failure here rather than a silent change.
using ObservingDisplay = Result<PresentationId> (Viewer::*)(ObjectId, const geometry::Body&);
using ObservingRender = Result<RenderedImage> (Viewer::*)();

} // namespace

int main() {
    // The control: the observing API, bound through the pointer types above,
    // and a rendered image read for its values.
    const ObservingDisplay display = &Viewer::display;
    const ObservingRender render = &Viewer::render;
    (void)display;
    (void)render;

    RenderedImage image;
    image.width = 1;
    image.height = 1;
    image.rgb = {0, 0, 0};
    (void)image.coverage(Rgb{});
    (void)image.at(0, 0);

    [[maybe_unused]] const PresentationId presentation = PresentationId::fromValue(1);

#ifdef BETTERCAD_CF_DISPLAY_THROUGH_A_MUTABLE_BODY
    // If display took a mutable Body it could heal, re-triangulate or
    // otherwise change the geometry it is supposed to be showing.
    using MutatingDisplay = Result<PresentationId> (Viewer::*)(ObjectId, geometry::Body&);
    const MutatingDisplay mutating = &Viewer::display;
    (void)mutating;
#endif

#ifdef BETTERCAD_CF_PRESENTATION_ID_AS_OBJECT_ID
    // A presentation handle is viewer-local and is NOT a document identity.
    // Confusing the two is how a viewer index ends up stored as engineering
    // intent.
    const ObjectId object = PresentationId::fromValue(1);
    (void)object;
#endif

#ifdef BETTERCAD_CF_OBJECT_ID_AS_PRESENTATION_ID
    const PresentationId backwards = ObjectId::fromValue(1);
    (void)backwards;
#endif

#ifdef BETTERCAD_CF_PRESENTATION_ID_FROM_INTEGER
    // Nor is it an integer a caller may invent: it comes from display().
    const PresentationId fabricated = 7;
    (void)fabricated;
#endif

#ifdef BETTERCAD_CF_COPY_A_VIEWER
    // A viewer owns a graphic driver, a window and a view. Copying one would
    // mean two objects believing they own the same native resources.
    const Viewer* original = nullptr;
    const Viewer copy = *original;
    (void)copy;
#endif

#ifdef BETTERCAD_CF_REACH_A_DOCUMENT_THROUGH_THE_VIEWER
    // The viewer holds no Document and no feature state: its only link to the
    // model is the ObjectId a caller supplied, so there is nothing to reach
    // through and nothing to mutate.
    const Viewer* viewer = nullptr;
    (void)viewer->document();
#endif

    return 0;
}
