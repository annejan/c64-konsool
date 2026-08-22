#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bits of the C64 joystick byte, all active low
#define HID_GAMEPAD_C64_UP    (1 << 0)
#define HID_GAMEPAD_C64_DOWN  (1 << 1)
#define HID_GAMEPAD_C64_LEFT  (1 << 2)
#define HID_GAMEPAD_C64_RIGHT (1 << 3)
#define HID_GAMEPAD_C64_FIRE  (1 << 4)

// Idle value of the C64 joystick byte
#define HID_GAMEPAD_C64_IDLE 0xff

/// @brief Whether at least one USB gamepad is connected
bool hid_gamepad_connected(void);

/// @brief Latest gamepad state as a C64 joystick byte, active low
uint8_t hid_gamepad_get_c64_joy(void);

/// @brief Track gamepad connects and disconnects, called by the HID host
void hid_gamepad_set_connected(bool connected);

/// @brief Feed a raw HID input report from a gamepad, called by the HID host
void hid_gamepad_handle_report(const uint8_t *data, int length);

#ifdef __cplusplus
}
#endif
