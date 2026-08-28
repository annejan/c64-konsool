#include "MenuController.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include "C64Emu.hpp"
#include "DisplayDriver.hpp"
#include "MainMenu.hpp"
// #include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "icons.h"
#include "menuoverlay/MenuDataStore.hpp"
#include "menuoverlay/MenuTypes.hpp"
#include "menuoverlay/PetsciiText.hpp"
#include "pax_fonts.h"
#include "pax_gfx.h"
#include "pax_text.h"

__attribute__((unused)) static const char* TAG = "MenuController";

extern "C" {
#include "bsp/audio.h"
extern uint8_t* fb_memory;
}

MenuController::MenuController()
{
    return;
}

void MenuController::init(C64Emu* c64emu)
{
    load_icons();
    MenuController::c64emu = c64emu;
    // Setup the main menu
    rootMenu               = new MainMenu("Main Menu", nullptr, this);
    currentMenu            = rootMenu;
    // Initialize the menu overlay
    DisplayDriver* driver  = c64emu->cpu.vic->getDriver();
    fb                     = driver->getMenuFb();
    // HID Pax framebuffer
    pax_buf_init(fb, fb_memory, 800, 480, PAX_BUF_16_565RGB);
    pax_buf_reversed(fb, false);
    pax_background(fb, 0xFF000000);

    // Initialize the menus
    rootMenu->init();

    visible = true;
    driver->enableMenuOverlay(visible);

    // Call render function to initialize the menu overlay
    render();
}

void MenuController::render()
{
    // Allow the current menu to update itself
    currentMenu->update();

    bool full_update = false;

    if (previousMenu != currentMenu) {
        previousMenu = currentMenu;
        full_update  = true;
    }

    int currentJoystick = menuDataStore->getInt("kb_joystick_port", 1);

    if (full_update) {
        pax_background(fb, 0xFFFFFFFF);
        pax_draw_rect(fb, 0xFF002255, 0, 0, 800, 40);
        pax_draw_text(fb, 0xFFFFFFFF, pax_font_saira_regular, 18, 10, 10, currentMenu->getTitle().c_str());
    }

    const auto& items = currentMenu->getItems();

    // The screen fits this many rows below the title bar. Longer lists scroll
    // with the selection instead of being broken into pages.
    static const size_t MENU_ROWS = 20;

    // A PETSCII name is drawn at two screen pixels per ROM pixel, so a
    // character is 16 wide and fits the 20 pixel row. A CBM name is 16
    // characters, plus one for the splat marker an unclosed file gets.
    static const int   PETSCII_SCALE   = 2;
    static const float PETSCII_COL_X   = 30;
    static const float PETSCII_COL_END = PETSCII_COL_X + 17 * PETSCII_CELL * PETSCII_SCALE + 10;

    size_t total = items.size();
    size_t rows  = total < MENU_ROWS ? total : MENU_ROWS;
    size_t first = currentMenu->getFirstVisibleItem();
    size_t sel   = currentMenu->getSelectedItemIndex();

    // Keep the selection on screen, moving the window by as little as possible.
    if (sel < first) first = sel;
    if (sel >= first + MENU_ROWS) first = sel - MENU_ROWS + 1;
    if (total <= MENU_ROWS) {
        first = 0;
    } else if (first + MENU_ROWS > total) {
        first = total - MENU_ROWS;
    }
    if (first != currentMenu->getFirstVisibleItem()) {
        // Everything moved, so the rows that were on screen are all stale.
        currentMenu->setFirstVisibleItem(first);
        full_update = true;
        pax_background(fb, 0xFFFFFFFF);
        pax_draw_rect(fb, 0xFF002255, 0, 0, 800, 40);
        pax_draw_text(fb, 0xFFFFFFFF, pax_font_saira_regular, 18, 10, 10, currentMenu->getTitle().c_str());
    }

    for (size_t row = 0; row < rows; row++) {
        size_t idx = first + row;
        if (idx >= total) break;
        const MenuItem& item = items[idx];

        if (full_update || idx == currentMenu->getPreviousSelectedIndex() ||
            idx == currentMenu->getCurrentSelectedIndex()) {
            uint32_t color    = currentMenu->getSelectedItemIndex() == idx ? 0xFFFF0000 : 0xFF002255;
            bool     selected = currentMenu->getSelectedItemIndex() == idx;
            float    rowY     = static_cast<float>(60 + row * 20);

            // A CBM name is PETSCII, and the menu font has no graphics
            // characters in it, so a directory row is drawn from the C64
            // character ROM instead. What sits either side of the name, the
            // selection marker and the block count, is ASCII and stays in the
            // menu font. Two pixels of leading centre the 16 pixel glyphs in
            // the 20 pixel row.
            if (!item.petscii.empty()) {
                pax_draw_rect(fb, 0xFFFFFFFF, 0, rowY, 800, 20);
                pax_draw_text(fb, color, pax_font_saira_regular, 18, 14, rowY, selected ? ">" : " ");
                pax_draw_petscii(fb, color, PETSCII_COL_X, rowY + 2, item.petscii, PETSCII_SCALE);
                pax_draw_text(fb, color, pax_font_saira_regular, 18, PETSCII_COL_END, rowY, item.title.c_str());
                continue;
            }

            std::string title;
            switch (item.type) {
                case MenuItemType::TOGGLE: {
                    bool checked = menuDataStore->getBool(item.value_name, false);
                    title        = (((currentMenu->getSelectedItemIndex() == idx) ? "> " : "  ") + item.title +
                                    (checked ? "On" : "Off"));
                    break;
                }
                case MenuItemType::SPACER: {
                    title = item.title;
                    break;
                }
                default: {
                    title = (((currentMenu->getSelectedItemIndex() == idx) ? "> " : "  ") + item.title);
                    break;
                }
            }
            pax_draw_rect(fb, 0xFFFFFFFF, 0, 60 + row * 20, 800, 20);
            pax_draw_text(fb, color, pax_font_saira_regular, 18, 30, 60 + row * 20, title.c_str());
        }
    }

    // Say where we are in a list that does not fit, so paging through a card
    // full of programs is not done blind.
    if (full_update && total > MENU_ROWS) {
        char counter[48];
        snprintf(counter, sizeof(counter), "%u-%u of %u", static_cast<unsigned>(first + 1),
                 static_cast<unsigned>(first + rows), static_cast<unsigned>(total));
        pax_draw_rect(fb, 0xFFFFFFFF, 600, 10, 200, 20);
        pax_draw_text(fb, 0xFFFFFFFF, pax_font_saira_regular, 18, 640, 10, counter);
    }

    size_t i = rows;

    if (currentMenu == rootMenu && (full_update || prevJoystick != currentJoystick)) {
        i += 2;
        pax_draw_rect(fb, 0xFFFFFFFF, 0, 60 + i * 20, 800, 480 - 60 - i * 20);
        pax_draw_line(fb, 0xFFFF0000, 0, 60 + i * 20, 800, 60 + i * 20);
        i += 1;
        pax_draw_image(fb, get_icon(ICON_F6), 10, 60 + i * 20);
        pax_draw_text(fb, 0xFF002255, pax_font_saira_regular, 18, 40, 60 + i * 20 + 8,
                      "Switch between this menu and the Commodore 64");
        i += 1;
        pax_draw_image(fb, get_icon(ICON_F5), 10, 60 + i * 20);
        pax_draw_text(fb, 0xFF002255, pax_font_saira_regular, 18, 40, 60 + i * 20 + 8,
                      ("Switch joystick emulation between joystick port 1 and 2, current port: " +
                       std::to_string(currentJoystick))
                          .c_str());

        i += 2;
        pax_draw_text(fb, 0xFF002255, pax_font_saira_regular, 18, 10, 60 + i * 20,
                      "To enable joystick emulation set 'Joystick emulation' to 'On' in this menu.\n"
                      "With 'Two players' on, the keyboard joystick keeps the port shown above and a USB\n"
                      "gamepad takes the other one, so two people can play at the same time.\n"
                      "You can change the volume of the speaker and headphone output using the volume up\n"
                      "and down keys on the right side of the device.\n");
        i += 6;

        pax_draw_text(fb, 0xFF002255, pax_font_saira_regular, 18, 10, 60 + i * 20,
                      " - PRG, T64 and D64 files load; a D64 can also be mounted as drive 8.");

        i += 1;
        pax_draw_text(fb, 0xFF002255, pax_font_saira_regular, 18, 10, 60 + i * 20,
                      " - Connecting an USB keyboard to the USB-A port is also supported");
        i += 1;
        pax_draw_text(fb, 0xFF002255, pax_font_saira_regular, 18, 10, 60 + i * 20,
                      " - Joystick emulation uses left shift for fire, arrow keys");
        i += 1;
        pax_draw_text(fb, 0xFF002255, pax_font_saira_regular, 18, 10, 60 + i * 20,
                      " - ('/' and right shift input left + up and right + up respectively)");

        prevJoystick = currentJoystick;
    }
}

void MenuController::setCurrentMenu(MenuBaseClass* menu)
{
    currentMenu = menu;
    menu->navigateBegin();
}

void MenuController::show()
{
    visible = true;
}

void MenuController::hide()
{
    visible = false;
}

void MenuController::toggle()
{
    visible = !visible;
}

bool MenuController::getVisible() const
{
    return visible;
}

void MenuController::handleInput(menu_overlay_input_type_t input)
{
    // Handle user input for menu navigation and selection
    switch (input) {
        case MENU_OVERLAY_INPUT_TYPE_UP:
            currentMenu->navigateUp();
            break;
        case MENU_OVERLAY_INPUT_TYPE_DOWN:
            currentMenu->navigateDown();
            break;
        case MENU_OVERLAY_INPUT_TYPE_SELECT:
            currentMenu->activateItem(currentMenu->getSelectedItemIndex());
            break;
        case MENU_OVERLAY_INPUT_TYPE_LAST:
            if (currentMenu->getParentMenu() != nullptr) {
                currentMenu = currentMenu->getParentMenu();
            }
        default:
            break;
    }
    render();
}

// Implement other methods defined in MenuController.hpp
