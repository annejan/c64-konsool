// USB HID gamepad support: keeps the latest gamepad state around as a C64 joystick byte.
//
// Where the directions and buttons sit within a report comes from the report descriptor, see
// hid_layout.c. What the descriptor cannot express comes from the quirk table below.

#include "hid_gamepad.h"
#include <string.h>
#include "esp_log.h"
#include "hid_layout.h"

static const char* TAG = "hid_gamepad";

// A DualShock 3 enumerates and hands out its report descriptor, but stays silent until
// the host asks it to start reporting. It has no hat switch either, its d-pad sits in
// buttons five through eight.
static const uint8_t dualshock3_enable_reporting[] = {0x42, 0x0c, 0x00, 0x00};

static const hid_gamepad_quirk_t hid_gamepad_quirks[] = {
    {
        .vid                  = 0x054c,
        .pid                  = 0x0268,
        .name                 = "DualShock 3",
        .enable_report_id     = 0xf4,
        .enable_report        = dualshock3_enable_reporting,
        .enable_report_length = sizeof(dualshock3_enable_reporting),
        .dpad_first_button    = 4,
    },
};

const hid_gamepad_quirk_t* hid_gamepad_find_quirk(uint16_t vid, uint16_t pid) {
    for (size_t i = 0; i < sizeof(hid_gamepad_quirks) / sizeof(hid_gamepad_quirks[0]); i++) {
        if (hid_gamepad_quirks[i].vid == vid && hid_gamepad_quirks[i].pid == pid) {
            return &hid_gamepad_quirks[i];
        }
    }
    return NULL;
}

// Written by the HID host task, read by the emulator task
static volatile uint8_t joy_value = HID_GAMEPAD_C64_IDLE;

static struct {
    hid_layout_t layout;
    bool         dpad_is_buttons;  // Four of the buttons are a d-pad rather than fire buttons
    uint16_t     dpad_first;       // Index of the first of those, they run up, right, down, left
} gamepad;

bool hid_gamepad_connected(void) {
    return gamepad.layout.valid;
}

uint8_t hid_gamepad_get_c64_joy(void) {
    return joy_value;
}

void hid_gamepad_disconnect(void) {
    memset(&gamepad, 0, sizeof(gamepad));
    // Never leave a direction stuck when the gamepad is unplugged
    joy_value = HID_GAMEPAD_C64_IDLE;
}

bool hid_gamepad_connect(const uint8_t* report_descriptor, size_t length, uint16_t vid, uint16_t pid) {
    hid_gamepad_disconnect();

    if (!hid_layout_parse(report_descriptor, length, &gamepad.layout)) {
        ESP_LOGW(TAG, "Nothing usable in the report descriptor, ignoring this device");
        return false;
    }

    bool absolute_axes = (gamepad.layout.x.present && !gamepad.layout.x.relative) ||
                         (gamepad.layout.y.present && !gamepad.layout.y.relative);
    if (!absolute_axes && !gamepad.layout.hat.present) {
        ESP_LOGW(TAG, "No usable directions in the report descriptor, ignoring this device");
        hid_gamepad_disconnect();
        return false;
    }

    const hid_gamepad_quirk_t* quirk = hid_gamepad_find_quirk(vid, pid);
    if (quirk != NULL && quirk->dpad_first_button != HID_GAMEPAD_NO_DPAD_BUTTONS &&
        gamepad.layout.buttons.present && gamepad.layout.button_count > quirk->dpad_first_button + 3) {
        gamepad.dpad_is_buttons = true;
        gamepad.dpad_first      = (uint16_t)quirk->dpad_first_button;
        ESP_LOGI(TAG, "%s: buttons %d to %d are a d-pad", quirk->name, quirk->dpad_first_button + 1,
                 quirk->dpad_first_button + 4);
    }

    ESP_LOGI(TAG, "Gamepad layout: report id %d, x %d, y %d, hat %d, %d buttons at %d", gamepad.layout.report_id,
             gamepad.layout.x.present ? gamepad.layout.x.bit_offset : -1,
             gamepad.layout.y.present ? gamepad.layout.y.bit_offset : -1,
             gamepad.layout.hat.present ? gamepad.layout.hat.bit_offset : -1, gamepad.layout.button_count,
             gamepad.layout.buttons.present ? gamepad.layout.buttons.bit_offset : -1);

    return true;
}

void hid_gamepad_handle_report(const uint8_t* data, int length) {
    if (!gamepad.layout.valid || !hid_layout_strip_report_id(&gamepad.layout, &data, &length)) {
        return;
    }

    bool left = false, right = false, up = false, down = false, fire = false;

    hid_layout_axis_directions(data, length, &gamepad.layout.x, &left, &right);
    hid_layout_axis_directions(data, length, &gamepad.layout.y, &up, &down);

    if (gamepad.layout.hat.present) {
        // Eight directions clockwise starting at up, anything else means centered
        int32_t hat = hid_layout_read(data, length, &gamepad.layout.hat) - gamepad.layout.hat.logical_min;
        up          = up || (hat == 0 || hat == 1 || hat == 7);
        right       = right || (hat == 1 || hat == 2 || hat == 3);
        down        = down || (hat == 3 || hat == 4 || hat == 5);
        left        = left || (hat == 5 || hat == 6 || hat == 7);
    }

    for (uint16_t b = 0; b < gamepad.layout.button_count; b++) {
        bool pressed = hid_layout_read_button(data, length, &gamepad.layout, b);
        if (!pressed) {
            continue;
        }

        if (gamepad.dpad_is_buttons && b >= gamepad.dpad_first && b < gamepad.dpad_first + 4) {
            switch (b - gamepad.dpad_first) {
                case 0:
                    up = true;
                    break;
                case 1:
                    right = true;
                    break;
                case 2:
                    down = true;
                    break;
                default:
                    left = true;
                    break;
            }
            continue;
        }

        // Any other button fires, gamepads disagree far too much about which button is which
        fire = true;
    }

    uint8_t value = HID_GAMEPAD_C64_IDLE;
    if (up) {
        value &= ~HID_GAMEPAD_C64_UP;
    }
    if (down) {
        value &= ~HID_GAMEPAD_C64_DOWN;
    }
    if (left) {
        value &= ~HID_GAMEPAD_C64_LEFT;
    }
    if (right) {
        value &= ~HID_GAMEPAD_C64_RIGHT;
    }
    if (fire) {
        value &= ~HID_GAMEPAD_C64_FIRE;
    }

    if (value != joy_value) {
        ESP_LOGD(TAG, "Joystick value %02X", value);
    }
    joy_value = value;
}
