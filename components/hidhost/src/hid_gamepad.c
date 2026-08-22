// USB HID gamepad support: keeps the latest gamepad state around as a C64 joystick byte.
//
// Gamepads do not agree on a report layout: one pad puts its stick in a hat switch at byte 1,
// the next one reports X and Y at byte 3 and 4 and never touches the hat. Rather than guess,
// the report descriptor is parsed on connect to find where X, Y, the hat switch and the buttons
// live, and reports are decoded using those bit offsets.

#include "hid_gamepad.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "hid_gamepad";

// Report descriptor item prefix, see HID 1.11 section 6.2.2.2
#define HID_ITEM_SIZE(prefix) ((prefix) & 0x03)
#define HID_ITEM_TAG(prefix)  ((prefix) & 0xfc)

#define HID_ITEM_INPUT          0x80
#define HID_ITEM_COLLECTION     0xa0
#define HID_ITEM_END_COLLECTION 0xc0
#define HID_ITEM_USAGE_PAGE     0x04
#define HID_ITEM_LOGICAL_MIN    0x14
#define HID_ITEM_LOGICAL_MAX    0x24
#define HID_ITEM_REPORT_SIZE    0x74
#define HID_ITEM_REPORT_ID      0x84
#define HID_ITEM_REPORT_COUNT   0x94
#define HID_ITEM_USAGE          0x08
#define HID_ITEM_USAGE_MIN      0x18
#define HID_ITEM_USAGE_MAX      0x28

// Input item is constant instead of data, so padding rather than a control
#define HID_INPUT_CONSTANT 0x01

#define HID_USAGE_PAGE_GENERIC_DESKTOP 0x01
#define HID_USAGE_PAGE_BUTTON          0x09

#define HID_USAGE_X           0x30
#define HID_USAGE_Y           0x31
#define HID_USAGE_HAT_SWITCH  0x39

// Most usages one input item can name before the rest is ignored
#define MAX_LOCAL_USAGES 32

typedef struct {
    bool     present;
    uint16_t bit_offset;
    uint8_t  bit_size;
    int32_t  logical_min;
    int32_t  logical_max;
} hid_gamepad_field_t;

typedef struct {
    bool                valid;
    uint8_t             report_id;  // Zero when the reports carry no report ID
    hid_gamepad_field_t x;
    hid_gamepad_field_t y;
    hid_gamepad_field_t hat;
    hid_gamepad_field_t buttons;  // bit_size is one, the count is in button_count
    uint16_t            button_count;
} hid_gamepad_layout_t;

// Written by the HID host task, read by the emulator task
static volatile uint8_t     joy_value = HID_GAMEPAD_C64_IDLE;
static hid_gamepad_layout_t layout;

bool hid_gamepad_connected(void)
{
    return layout.valid;
}

uint8_t hid_gamepad_get_c64_joy(void)
{
    return joy_value;
}

void hid_gamepad_disconnect(void)
{
    memset(&layout, 0, sizeof(layout));
    // Never leave a direction stuck when the gamepad is unplugged
    joy_value = HID_GAMEPAD_C64_IDLE;
}

/// @brief Read the data of a report descriptor item, unsigned
static uint32_t item_data(const uint8_t *item, uint8_t size)
{
    uint32_t value = 0;
    for (uint8_t i = 0; i < size; i++) {
        value |= (uint32_t)item[i] << (8 * i);
    }
    return value;
}

/// @brief Read the data of a report descriptor item, sign extended
static int32_t item_data_signed(const uint8_t *item, uint8_t size)
{
    uint32_t value = item_data(item, size);
    if (size > 0 && size < 4 && (value & (1u << (8 * size - 1)))) {
        value |= 0xffffffffu << (8 * size);
    }
    return (int32_t)value;
}

/// @brief Pull a field out of an input report
static int32_t extract_field(const uint8_t *data, int length, const hid_gamepad_field_t *field)
{
    uint32_t value = 0;

    for (uint8_t i = 0; i < field->bit_size; i++) {
        uint16_t bit = field->bit_offset + i;
        if (bit / 8 >= (uint16_t)length) {
            break;
        }
        if (data[bit / 8] & (1 << (bit % 8))) {
            value |= 1u << i;
        }
    }

    // Fields with a negative logical minimum hold signed values
    if (field->logical_min < 0 && field->bit_size < 32 && (value & (1u << (field->bit_size - 1)))) {
        value |= 0xffffffffu << field->bit_size;
        return (int32_t)value;
    }

    return (int32_t)value;
}

bool hid_gamepad_connect(const uint8_t *report_descriptor, size_t length)
{
    hid_gamepad_disconnect();

    if (report_descriptor == NULL) {
        return false;
    }

    uint16_t usage_page = 0;
    int32_t  logical_min = 0;
    int32_t  logical_max = 0;
    uint8_t  report_size = 0;
    uint16_t report_count = 0;
    uint8_t  report_id = 0;

    uint16_t usages[MAX_LOCAL_USAGES];
    uint8_t  usage_count = 0;
    uint32_t usage_min = 0;
    uint32_t usage_max = 0;
    bool     usage_range = false;

    // Bit offset within the report the next input item starts at
    uint16_t bit_offset = 0;

    size_t i = 0;
    while (i < length) {
        uint8_t prefix = report_descriptor[i];
        uint8_t size = HID_ITEM_SIZE(prefix);
        if (size == 3) {
            size = 4;  // A size field of three means four bytes
        }
        const uint8_t *data = &report_descriptor[i + 1];
        if (i + 1 + size > length) {
            break;
        }
        i += 1 + size;

        switch (HID_ITEM_TAG(prefix)) {
            case HID_ITEM_USAGE_PAGE:
                usage_page = (uint16_t)item_data(data, size);
                break;
            case HID_ITEM_LOGICAL_MIN:
                logical_min = item_data_signed(data, size);
                break;
            case HID_ITEM_LOGICAL_MAX:
                // Only signed when the minimum is, otherwise 0xff means 255 rather than -1
                logical_max = logical_min < 0 ? item_data_signed(data, size) : (int32_t)item_data(data, size);
                break;
            case HID_ITEM_REPORT_SIZE:
                report_size = (uint8_t)item_data(data, size);
                break;
            case HID_ITEM_REPORT_COUNT:
                report_count = (uint16_t)item_data(data, size);
                break;
            case HID_ITEM_REPORT_ID:
                // Every report ID starts its own report, only the first one is looked at
                if (report_id == 0) {
                    report_id = (uint8_t)item_data(data, size);
                    bit_offset = 0;
                } else {
                    // A second report ID, stop before mixing offsets of different reports
                    i = length;
                }
                break;
            case HID_ITEM_USAGE:
                if (usage_count < MAX_LOCAL_USAGES) {
                    usages[usage_count++] = (uint16_t)item_data(data, size);
                }
                break;
            case HID_ITEM_USAGE_MIN:
                usage_min = item_data(data, size);
                usage_range = true;
                break;
            case HID_ITEM_USAGE_MAX:
                usage_max = item_data(data, size);
                usage_range = true;
                break;
            case HID_ITEM_INPUT: {
                uint32_t flags = item_data(data, size);

                if (!(flags & HID_INPUT_CONSTANT)) {
                    if (usage_page == HID_USAGE_PAGE_BUTTON && !layout.buttons.present) {
                        layout.buttons.present     = true;
                        layout.buttons.bit_offset  = bit_offset;
                        layout.buttons.bit_size    = 1;
                        layout.buttons.logical_min = 0;
                        layout.buttons.logical_max = 1;
                        layout.button_count        = report_count;
                        if (usage_range && usage_max >= usage_min) {
                            uint32_t named = usage_max - usage_min + 1;
                            if (named < layout.button_count) {
                                layout.button_count = (uint16_t)named;
                            }
                        }
                    } else if (usage_page == HID_USAGE_PAGE_GENERIC_DESKTOP) {
                        for (uint16_t f = 0; f < report_count && f < usage_count; f++) {
                            hid_gamepad_field_t *field = NULL;
                            switch (usages[f]) {
                                case HID_USAGE_X:
                                    field = &layout.x;
                                    break;
                                case HID_USAGE_Y:
                                    field = &layout.y;
                                    break;
                                case HID_USAGE_HAT_SWITCH:
                                    field = &layout.hat;
                                    break;
                                default:
                                    break;
                            }
                            if (field != NULL && !field->present) {
                                field->present     = true;
                                field->bit_offset  = bit_offset + f * report_size;
                                field->bit_size    = report_size;
                                field->logical_min = logical_min;
                                field->logical_max = logical_max;
                            }
                        }
                    }
                }

                bit_offset += (uint16_t)(report_size * report_count);
                usage_count = 0;
                usage_range = false;
                break;
            }
            case HID_ITEM_COLLECTION:
            case HID_ITEM_END_COLLECTION:
                usage_count = 0;
                usage_range = false;
                break;
            default:
                // Output and feature items do not take up space in an input report
                usage_count = 0;
                usage_range = false;
                break;
        }
    }

    bool has_directions = layout.x.present || layout.y.present || layout.hat.present;
    if (!has_directions) {
        ESP_LOGW(TAG, "No usable directions in the report descriptor, ignoring this device");
        hid_gamepad_disconnect();
        return false;
    }

    layout.report_id = report_id;
    layout.valid     = true;

    ESP_LOGI(TAG, "Gamepad layout: report id %d, x %d, y %d, hat %d, %d buttons at %d", layout.report_id,
             layout.x.present ? layout.x.bit_offset : -1, layout.y.present ? layout.y.bit_offset : -1,
             layout.hat.present ? layout.hat.bit_offset : -1, layout.button_count,
             layout.buttons.present ? layout.buttons.bit_offset : -1);

    return true;
}

/// @brief Whether an axis is pushed far enough from its center to count as a direction
static void axis_directions(const uint8_t *data, int length, const hid_gamepad_field_t *field, bool *low, bool *high)
{
    if (!field->present || field->logical_max <= field->logical_min) {
        return;
    }

    int32_t value  = extract_field(data, length, field);
    int32_t center = (field->logical_min + field->logical_max) / 2;
    int32_t margin = (field->logical_max - field->logical_min) / 4;

    if (value < center - margin) {
        *low = true;
    }
    if (value > center + margin) {
        *high = true;
    }
}

void hid_gamepad_handle_report(const uint8_t *data, int length)
{
    if (!layout.valid || length < 1) {
        return;
    }

    if (layout.report_id != 0) {
        if (data[0] != layout.report_id) {
            return;
        }
        // The report ID is not part of the bit offsets in the descriptor
        data++;
        length--;
    }

    bool left = false, right = false, up = false, down = false, fire = false;

    axis_directions(data, length, &layout.x, &left, &right);
    axis_directions(data, length, &layout.y, &up, &down);

    if (layout.hat.present) {
        // Eight directions clockwise starting at up, anything else means centered
        int32_t hat = extract_field(data, length, &layout.hat) - layout.hat.logical_min;
        up    = up || (hat == 0 || hat == 1 || hat == 7);
        right = right || (hat == 1 || hat == 2 || hat == 3);
        down  = down || (hat == 3 || hat == 4 || hat == 5);
        left  = left || (hat == 5 || hat == 6 || hat == 7);
    }

    // Any button fires, gamepads disagree far too much about which button is which
    for (uint16_t b = 0; b < layout.button_count; b++) {
        hid_gamepad_field_t button = layout.buttons;
        button.bit_offset += b;
        if (extract_field(data, length, &button)) {
            fire = true;
            break;
        }
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
