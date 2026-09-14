#pragma once

namespace bettercad::geometry::occt {

/// Configures process-wide kernel state; safe to call repeatedly and from
/// several threads. Entry points that use kernel components which report
/// through the kernel's messenger (the data-exchange translators) call it
/// first.
void initializeSession();

} // namespace bettercad::geometry::occt
