#pragma once
// DualShock 4 / DualSense read directly over HID (USB or Bluetooth), mapped
// to the XInput layout Skate reads. GTA V Legacy reads these pads natively,
// so a PlayStation player has no XInput device at all.
#include "skatev_runtime.h"
#include <cstddef>
#include <cstdint>

namespace pspad {

// Latest state of the first connected PlayStation pad; false when none.
// `name` (optional) gets e.g. "DualSense (USB)". Starts the reader on first use.
bool Read(SvPad& out, const char** name = nullptr);

// One input report -> pad (no packet number). `usb`: the device's input
// report is 64 bytes (USB or the DS4 wireless adapter); Bluetooth otherwise.
bool Parse(const std::uint8_t* report, std::size_t length, bool dualSense, bool usb, SvPad& out);

} // namespace pspad
