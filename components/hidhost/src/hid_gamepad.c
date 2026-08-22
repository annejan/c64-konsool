// USB HID gamepad support: keeps the latest gamepad state around as a C64 joystick byte.
//
// The report parser is ported from https://github.com/annejan/konsool-HID

#include "hid_gamepad.h"
#include "esp_log.h"

static const char *TAG = "hid_gamepad";

// Distance from the center (0x80) before an analog stick counts as a direction
#define STICK_DEADZONE 48

// Minimum length of a gamepad input report
#define GAMEPAD_REPORT_LENGTH 10

// Written by the HID host task, read by the emulator task
static volatile uint8_t joy_value = HID_GAMEPAD_C64_IDLE;
static volatile int connect_count = 0;

bool hid_gamepad_connected(void)
{
    return connect_count > 0;
}

uint8_t hid_gamepad_get_c64_joy(void)
{
    return joy_value;
}

void hid_gamepad_set_connected(bool connected)
{
    if (connected) {
        connect_count++;
    } else if (connect_count > 0) {
        connect_count--;
    }

    if (connect_count == 0) {
        // Never leave a direction stuck when the gamepad is unplugged
        joy_value = HID_GAMEPAD_C64_IDLE;
    }
}

void hid_gamepad_handle_report(const uint8_t *data, int length)
{
    if (length < GAMEPAD_REPORT_LENGTH) {
        ESP_LOGD(TAG, "Ignoring %d byte report", length);
        return;
    }

    // Hat switch, eight directions clockwise starting at up
    uint8_t hat = data[1];
    bool up = (hat == 0x00 || hat == 0x01 || hat == 0x07);
    bool right = (hat == 0x01 || hat == 0x02 || hat == 0x03);
    bool down = (hat == 0x03 || hat == 0x04 || hat == 0x05);
    bool left = (hat == 0x05 || hat == 0x06 || hat == 0x07);

    uint8_t b1 = data[2];
    uint8_t b2 = data[3];

    bool a = (b2 >> 6) & 1;
    bool b = (b2 >> 5) & 1;
    bool x = (b2 >> 4) & 1;
    bool y = (b2 >> 3) & 1;
    bool l1 = (b2 >> 0) & 1;
    bool r1 = (b1 >> 7) & 1;
    bool l2 = (b2 >> 2) & 1;
    bool r2 = (b2 >> 1) & 1;

    // The left stick doubles as a d-pad
    uint8_t lx = data[4];
    uint8_t ly = data[5];
    left = left || lx < (0x80 - STICK_DEADZONE);
    right = right || lx > (0x80 + STICK_DEADZONE);
    up = up || ly < (0x80 - STICK_DEADZONE);
    down = down || ly > (0x80 + STICK_DEADZONE);

    // Any of the face buttons and shoulder buttons fires
    bool fire = a || b || x || y || l1 || r1 || l2 || r2;

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
