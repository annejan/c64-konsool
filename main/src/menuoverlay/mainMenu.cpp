#include "MainMenu.hpp"
#include "C64Emu.hpp"
#include "LoadMenu.hpp"
#include "MenuDataStore.hpp"
#include "UsbLoadMenu.hpp"
#include "esp_log.h"
#include "menuoverlay/MenuController.hpp"
#include "menuoverlay/MenuDataStore.hpp"
#include "menuoverlay/MenuTypes.hpp"

extern "C" {
#include "bsp/audio.h"
#include "bsp/device.h"
}

MainMenu::MainMenu(std::string title, MenuBaseClass* previousMenu, MenuController* menuController)
    : MenuBaseClass(title, previousMenu, menuController)
{
    // Nothing else to do here
    c64emu = menuController->getC64Emu();
}

MainMenu::~MainMenu() {};

void MainMenu::resetC64(MenuItem* item)
{
    ExternalCmds* ext = &c64emu->externalCmds;

    ext->reset();
}

void MainMenu::exitToLauncher(MenuItem* item)
{
    bsp_device_restart_to_launcher();
}

bool MainMenu::init()
{
    int id_count = 1;
    loadMenu     = new LoadMenu("Load PRG file from SD card (c64prg folder)", this, menuController);
    loadMenu->init();
    usbLoadMenu = new UsbLoadMenu("Load PRG file from USB disk (disk root)", this, menuController);
    usbLoadMenu->init();

    MenuDataStore* menuDataStore = MenuDataStore::getInstance();

    // Setup the menu entries
    MenuItem* load_prg = new MenuItem();
    load_prg->id       = id_count++;
    load_prg->title    = "Load PRG file from SD card (c64prg folder)";
    load_prg->type     = MenuItemType::SUBMENU;
    load_prg->submenu  = loadMenu;
    items.push_back(*load_prg);

    MenuItem* usb_load_prg = new MenuItem();
    usb_load_prg->id       = id_count++;
    usb_load_prg->title    = "Load PRG file from USB disk (disk root)";
    usb_load_prg->type     = MenuItemType::SUBMENU;
    usb_load_prg->submenu  = usbLoadMenu;
    items.push_back(*usb_load_prg);

    // Add menu items here
    MenuItem* joystick_emu   = new MenuItem();
    joystick_emu->id         = id_count++;
    joystick_emu->title      = "Joystick emulation: ";
    joystick_emu->type       = MenuItemType::TOGGLE;
    joystick_emu->value_name = "kb_joystick_emu";
    menuDataStore->set("kb_joystick_emu", false);
    items.push_back(*joystick_emu);

    // Speaker audio enable/disable
    MenuItem* speaker_emu   = new MenuItem();
    speaker_emu->id         = id_count++;
    speaker_emu->title      = "Speaker audio: ";
    speaker_emu->type       = MenuItemType::TOGGLE;
    speaker_emu->value_name = "speaker_ena";
    menuDataStore->set("speaker_ena", true);
    speaker_emu->action = [](MenuItem* item) {
        MenuDataStore* menuDataStore = MenuDataStore::getInstance();
        bool           enabled       = menuDataStore->getBool("speaker_ena", true);
        ESP_LOGI("APM", "Toggling speaker audio: %s", enabled ? "enabled" : "disabled");
        bsp_audio_set_amplifier(enabled);
    };
    items.push_back(*speaker_emu);

    // Add menu items here
    MenuItem* reset_item = new MenuItem();
    reset_item->id       = id_count++;
    reset_item->title    = "Reset C64";
    reset_item->type     = MenuItemType::ACTION;
    reset_item->action   = [this](MenuItem* item) { this->resetC64(item); };
    items.push_back(*reset_item);

    MenuItem* launcher_item = new MenuItem();
    launcher_item->id       = id_count++;
    launcher_item->title    = "Exit and return to launcher";
    launcher_item->type     = MenuItemType::ACTION;
    launcher_item->action   = [this](MenuItem* item) { this->exitToLauncher(item); };
    items.push_back(*launcher_item);

    /*MenuItem* perf_mon   = new MenuItem();
    perf_mon->id         = id_count++;
    perf_mon->title      = "Performance Monitor: ";
    perf_mon->type       = MenuItemType::TOGGLE;
    perf_mon->value_name = "perf_mon_ena";
    menuDataStore->set("perf_mon_ena", false);
    perf_mon->action = [this, menuDataStore](MenuItem* item) {
        bool enabled       = menuDataStore->getBool("perf_mon_ena", true);
        // Set the performance monitor enabled/disabled in C64Emu
        this->c64emu->perf = enabled;
    };
    items.push_back(*perf_mon);*/

    return true;
}
