#pragma once

#include "Utils/Config.hpp"
#include "Utils/Diagnostics.hpp"

#include <cmath>
#include <optional>

namespace OW {

    inline bool IsPlausibleMouseDpi(float value)
    {
        return std::isfinite(value) && value >= 100.0f && value <= 64000.0f;
    }

    inline std::optional<float> DmaReadHostMouseDpi()
    {
        // HKCU\Control Panel\Mouse\MouseSensitivity is the Windows pointer-speed
        // slider (1-20), not hardware DPI. True automatic DPI detection needs
        // KMBox firmware support for reporting the active DPI stage, or a
        // device-specific kernel/HID-driver read on the controlled host.
        Diagnostics::Aim(
            "host_mouse_dpi.detect unavailable reason=no_dma_dpi_source registry_mouse_sensitivity_is_pointer_speed kmbox_hid_report_has_no_dpi configured=%.3f",
            Config::hostMouseDpi);
        return std::nullopt;
    }

    inline void RefreshHostMouseDpi()
    {
        Config::hostMouseDpiAutoDetected = false;
        Config::detectedHostMouseDpi = 0.0f;

        const std::optional<float> detected = DmaReadHostMouseDpi();
        if (detected && IsPlausibleMouseDpi(*detected)) {
            Config::detectedHostMouseDpi = *detected;
            Config::hostMouseDpi = *detected;
            Config::hostMouseDpiAutoDetected = true;
            Diagnostics::Aim("host_mouse_dpi.detected value=%.3f source=dma", *detected);
            return;
        }

        if (!IsPlausibleMouseDpi(Config::hostMouseDpi)) {
            Diagnostics::Aim("host_mouse_dpi.fallback invalid_manual=%.3f fallback=1600.000",
                Config::hostMouseDpi);
            Config::hostMouseDpi = 1600.0f;
        }

        Diagnostics::Aim("host_mouse_dpi.fallback manual=%.3f autoDetected=0",
            Config::hostMouseDpi);
    }

} // namespace OW
