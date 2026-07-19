#include "MenuController.hpp"
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
    // ESP_LOGI(TAG, "Menu items: %d", items.size());
    size_t      i     = 0;
    for (const auto& item : items) {
        if (full_update || i == currentMenu->getPreviousSelectedIndex() ||
            i == currentMenu->getCurrentSelectedIndex()) {
            uint32_t    color = currentMenu->getSelectedItemIndex() == i ? 0xFFFF0000 : 0xFF002255;
            std::string title;
            switch (item.type) {
                case MenuItemType::TOGGLE: {
                    bool checked = menuDataStore->getBool(item.value_name, false);
                    title        = (((currentMenu->getSelectedItemIndex() == i) ? "> " : "  ") + item.title +
                                    (checked ? "On" : "Off"));
                    break;
                }
                case MenuItemType::SPACER: {
                    title = "";
                    break;
                }
                default: {
                    title = (((currentMenu->getSelectedItemIndex() == i) ? "> " : "  ") + item.title);
                    break;
                }
            }
            // ESP_LOGI(TAG, "Menu Item %d: %s", i, title.c_str());
            pax_draw_rect(fb, 0xFFFFFFFF, 0, 60 + i * 20, 800, 20);
            pax_draw_text(fb, color, pax_font_saira_regular, 18, 30, 60 + i * 20, title.c_str());
        }
        ++i;
    }

    if (currentMenu == rootMenu && (full_update || prevJoystick != currentJoystick)) {
        i += 2;
        pax_draw_rect(fb, 0xFFFFFFFF, 0, 60 + i * 20, 800, 480 - 60 - i * 20);
        pax_draw_line(fb, 0xFFFF0000, 0, 60 + i * 20, 800, 60 + i * 20);
        i += 1;
        pax_draw_image(fb, get_icon(ICON_F5), 10, 60 + i * 20);
        pax_draw_text(fb, 0xFF002255, pax_font_saira_regular, 18, 40, 60 + i * 20 + 8,
                      "Switch between this menu and the Commodore 64");
        i += 1;
        pax_draw_image(fb, get_icon(ICON_F6), 10, 60 + i * 20);
        pax_draw_text(fb, 0xFF002255, pax_font_saira_regular, 18, 40, 60 + i * 20 + 8,
                      ("Switch joystick emulation between joystick port 1 and 2, current port: " +
                       std::to_string(currentJoystick))
                          .c_str());

        i += 2;
        pax_draw_text(fb, 0xFF002255, pax_font_saira_regular, 18, 10, 60 + i * 20,
                      "To enable joystick emulation set 'Joystick emulation' to 'On' in this menu.\n"
                      "You can change the volume of the speaker and headphone output using the volume up\n"
                      "and down keys on the right side of the device.\n");
        i += 4;

        pax_draw_text(fb, 0xFF002255, pax_font_saira_regular, 18, 10, 60 + i * 20,
                      " - Loading D64 images is not supported, you can only load PRG images.");

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