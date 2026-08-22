#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief One control within an input report
typedef struct {
    bool     present;
    bool     relative;  // Reports a change rather than a position, as a mouse does
    uint16_t bit_offset;
    uint8_t  bit_size;
    int32_t  logical_min;
    int32_t  logical_max;
} hid_field_t;

/// @brief Where the controls of a device sit within its input report
///
/// USB HID devices do not agree on a report layout, they describe it. This holds the part of
/// that description a keyboardless device needs: the axes, the wheel, the hat switch and the
/// buttons of the first input report.
typedef struct {
    bool        valid;
    uint8_t     report_id;  // Zero when the reports carry no report ID
    hid_field_t x;
    hid_field_t y;
    hid_field_t wheel;
    hid_field_t hat;
    hid_field_t buttons;  // bit_size is one, the number of them is in button_count
    uint16_t    button_count;
} hid_layout_t;

/// @brief Work out the layout of the input reports of a device from its report descriptor
///
/// @return true when the descriptor held anything usable
bool hid_layout_parse(const uint8_t* report_descriptor, size_t length, hid_layout_t* layout);

/// @brief Strip the report ID from a report, when the layout says it has one
///
/// The bit offsets in the layout do not count the report ID, so it has to come off first.
///
/// @return false when the report belongs to another report ID and should be ignored
bool hid_layout_strip_report_id(const hid_layout_t* layout, const uint8_t** data, int* length);

/// @brief Read one field out of an input report
int32_t hid_layout_read(const uint8_t* data, int length, const hid_field_t* field);

/// @brief Read one button out of an input report
bool hid_layout_read_button(const uint8_t* data, int length, const hid_layout_t* layout, uint16_t button);

/// @brief Whether an axis is pushed far enough from the center of its range to count as a direction
void hid_layout_axis_directions(const uint8_t* data, int length, const hid_field_t* field, bool* low, bool* high);

#ifdef __cplusplus
}
#endif
