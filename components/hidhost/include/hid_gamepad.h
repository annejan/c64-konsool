#pragma once

#include <stdbool.h>
#include <stddef.h>
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

/// Value of dpad_first_button for gamepads whose buttons are all just buttons
#define HID_GAMEPAD_NO_DPAD_BUTTONS (-1)

/// @brief Everything known about a gamepad that its report descriptor does not tell us
///
/// Add a row to hid_gamepad_quirks in hid_gamepad.c to support another gamepad.
typedef struct {
    uint16_t    vid;
    uint16_t    pid;
    const char *name;

    /// Feature report that makes the gamepad start sending input reports, NULL when it needs no nudge
    uint8_t        enable_report_id;
    const uint8_t *enable_report;
    size_t         enable_report_length;

    /// Index of the first of four buttons that act as a d-pad, in the order up, right, down, left
    int dpad_first_button;
} hid_gamepad_quirk_t;

/// @brief Look up the quirks of a gamepad, NULL when it needs none
const hid_gamepad_quirk_t *hid_gamepad_find_quirk(uint16_t vid, uint16_t pid);

/// @brief Whether a usable USB gamepad is connected
bool hid_gamepad_connected(void);

/// @brief Latest gamepad state as a C64 joystick byte, active low
uint8_t hid_gamepad_get_c64_joy(void);

/// @brief Learn the report layout of a connected gamepad from its report descriptor
///
/// @return true when the descriptor describes something usable as a joystick
bool hid_gamepad_connect(const uint8_t *report_descriptor, size_t length, uint16_t vid, uint16_t pid);

/// @brief Forget the connected gamepad and release the joystick
void hid_gamepad_disconnect(void);

/// @brief Feed a raw HID input report from a gamepad, called by the HID host
void hid_gamepad_handle_report(const uint8_t *data, int length);

#ifdef __cplusplus
}
#endif
