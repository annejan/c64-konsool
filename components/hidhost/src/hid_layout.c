// Parser for USB HID report descriptors.
//
// Devices of the same kind do not agree on a report layout: one gamepad puts its stick in a hat
// switch, the next reports X and Y and never touches the hat, and a mouse may or may not put a
// report ID in front of it all. Guessing by report length only ever works for the devices it was
// written against, so the descriptor is read instead and the controls are looked up by usage.

#include "hid_layout.h"
#include <string.h>

// Report descriptor item prefix, see HID 1.11 section 6.2.2.2
#define HID_ITEM_SIZE(prefix) ((prefix) & 0x03)
#define HID_ITEM_TAG(prefix)  ((prefix) & 0xfc)

#define HID_ITEM_INPUT          0x80
#define HID_ITEM_OUTPUT         0x90
#define HID_ITEM_FEATURE        0xb0
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

// Input item reports a change rather than a position, as a mouse does
#define HID_INPUT_RELATIVE 0x04

#define HID_USAGE_PAGE_GENERIC_DESKTOP 0x01
#define HID_USAGE_PAGE_BUTTON          0x09

#define HID_USAGE_X          0x30
#define HID_USAGE_Y          0x31
#define HID_USAGE_WHEEL      0x38
#define HID_USAGE_HAT_SWITCH 0x39

// A long item, which carries its own size and holds nothing this parser wants
#define HID_LONG_ITEM_PREFIX 0xfe

// Most usages one input item can name before the rest is ignored
#define MAX_LOCAL_USAGES 32

/// @brief Read the data of a report descriptor item, unsigned
static uint32_t item_data(const uint8_t* item, uint8_t size) {
    uint32_t value = 0;
    for (uint8_t i = 0; i < size; i++) {
        value |= (uint32_t)item[i] << (8 * i);
    }
    return value;
}

/// @brief Read the data of a report descriptor item, sign extended
static int32_t item_data_signed(const uint8_t* item, uint8_t size) {
    uint32_t value = item_data(item, size);
    if (size > 0 && size < 4 && (value & (1u << (8 * size - 1)))) {
        value |= 0xffffffffu << (8 * size);
    }
    return (int32_t)value;
}

int32_t hid_layout_read(const uint8_t* data, int length, const hid_field_t* field) {
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
    }

    return (int32_t)value;
}

bool hid_layout_read_button(const uint8_t* data, int length, const hid_layout_t* layout, uint16_t button) {
    hid_field_t field = layout->buttons;
    field.bit_offset += button;
    return hid_layout_read(data, length, &field) != 0;
}

bool hid_layout_strip_report_id(const hid_layout_t* layout, const uint8_t** data, int* length) {
    if (layout->report_id == 0) {
        return true;
    }
    if (*length < 1 || (*data)[0] != layout->report_id) {
        return false;
    }
    (*data)++;
    (*length)--;
    return true;
}

void hid_layout_axis_directions(const uint8_t* data, int length, const hid_field_t* field, bool* low, bool* high) {
    if (!field->present || field->relative || field->logical_max <= field->logical_min) {
        return;
    }

    int32_t value  = hid_layout_read(data, length, field);
    int32_t center = (field->logical_min + field->logical_max) / 2;
    int32_t margin = (field->logical_max - field->logical_min) / 4;

    if (value < center - margin) {
        *low = true;
    }
    if (value > center + margin) {
        *high = true;
    }
}

bool hid_layout_parse(const uint8_t* report_descriptor, size_t length, hid_layout_t* layout) {
    memset(layout, 0, sizeof(*layout));

    if (report_descriptor == NULL) {
        return false;
    }

    uint16_t usage_page   = 0;
    int32_t  logical_min  = 0;
    int32_t  logical_max  = 0;
    uint8_t  report_size  = 0;
    uint16_t report_count = 0;
    uint8_t  report_id    = 0;

    uint16_t usages[MAX_LOCAL_USAGES];
    uint8_t  usage_count = 0;
    uint32_t usage_min   = 0;
    uint32_t usage_max   = 0;
    bool     usage_range = false;

    // Bit offset within the report the next input item starts at
    uint16_t bit_offset = 0;

    size_t i = 0;
    while (i < length) {
        uint8_t prefix = report_descriptor[i];
        if (prefix == HID_LONG_ITEM_PREFIX) {
            // Long items are sized differently, and nothing past one can be trusted to line up
            break;
        }
        uint8_t size = HID_ITEM_SIZE(prefix);
        if (size == 3) {
            size = 4;  // A size field of three means four bytes
        }
        const uint8_t* data = &report_descriptor[i + 1];
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
                    report_id  = (uint8_t)item_data(data, size);
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
                usage_min   = item_data(data, size);
                usage_range = true;
                break;
            case HID_ITEM_USAGE_MAX:
                usage_max   = item_data(data, size);
                usage_range = true;
                break;
            case HID_ITEM_INPUT: {
                uint32_t flags = item_data(data, size);

                if (!(flags & HID_INPUT_CONSTANT)) {
                    if (usage_page == HID_USAGE_PAGE_BUTTON && !layout->buttons.present) {
                        layout->buttons.present     = true;
                        layout->buttons.bit_offset  = bit_offset;
                        layout->buttons.bit_size    = 1;
                        layout->buttons.logical_min = 0;
                        layout->buttons.logical_max = 1;
                        layout->button_count        = report_count;
                        if (usage_range && usage_max >= usage_min) {
                            uint32_t named = usage_max - usage_min + 1;
                            if (named < layout->button_count) {
                                layout->button_count = (uint16_t)named;
                            }
                        }
                    } else if (usage_page == HID_USAGE_PAGE_GENERIC_DESKTOP) {
                        for (uint16_t f = 0; f < report_count && f < usage_count; f++) {
                            hid_field_t* field = NULL;
                            switch (usages[f]) {
                                case HID_USAGE_X:
                                    field = &layout->x;
                                    break;
                                case HID_USAGE_Y:
                                    field = &layout->y;
                                    break;
                                case HID_USAGE_WHEEL:
                                    field = &layout->wheel;
                                    break;
                                case HID_USAGE_HAT_SWITCH:
                                    field = &layout->hat;
                                    break;
                                default:
                                    break;
                            }
                            if (field != NULL && !field->present) {
                                field->present     = true;
                                field->relative    = (flags & HID_INPUT_RELATIVE) != 0;
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
            case HID_ITEM_OUTPUT:
            case HID_ITEM_FEATURE:
            case HID_ITEM_COLLECTION:
            case HID_ITEM_END_COLLECTION:
                // These take up no space in an input report, but like every main item they
                // do use up the usages named before them
                usage_count = 0;
                usage_range = false;
                break;
            default:
                // Anything else is a global or local item that says nothing this parser wants.
                // Local items live on until a main item consumes them, so they stay put: a unit
                // or a physical range between a usage and its input item is perfectly normal.
                break;
        }
    }

    layout->report_id = report_id;
    layout->valid     = layout->x.present || layout->y.present || layout->hat.present || layout->buttons.present;

    return layout->valid;
}
