#include "core/geometry/occt/OcctSession.hpp"

#include <bettercad/core/geometry/Kernel.hpp>

#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_PrinterOStream.hxx>
#include <Standard_Version.hxx>

#include <mutex>

namespace bettercad::geometry {

KernelInfo geometryKernel() noexcept {
    return {"Open CASCADE Technology", OCC_VERSION_COMPLETE};
}

namespace occt {

void initializeSession() {
    static std::once_flag once;
    std::call_once(once, [] {
        // The kernel's default messenger prints diagnostics and translator
        // statistics to standard output. A library must not write to the
        // application's stdout (it would corrupt command-line output), and
        // BetterCAD reports problems as Result errors, so drop those printers.
        Message::DefaultMessenger()->RemovePrinters(STANDARD_TYPE(Message_PrinterOStream));
    });
}

} // namespace occt

} // namespace bettercad::geometry
