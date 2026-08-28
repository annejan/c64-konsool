#include "LoadMenu.hpp"
#include <dirent.h>
#include <string.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "C64Emu.hpp"
#include "Config.hpp"
#include "ExternalCmds.hpp"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "images/CbmImage.hpp"
#include "menuoverlay/ImageMenu.hpp"
#include "menuoverlay/MenuController.hpp"
#include "menuoverlay/MenuTypes.hpp"
#include "portmacro.h"

const static char* TAG = "LoadMenu";

LoadMenu::LoadMenu(std::string title, MenuBaseClass* previousMenu, MenuController* menuController)
    : MenuBaseClass(title, previousMenu, menuController)
{
    c64emu = menuController->getC64Emu();
    sdcard = &c64emu->externalCmds.sdcard;
}

LoadMenu::~LoadMenu()
{
    delete imageMenu;
}

void LoadMenu::displayMenu()
{
    items.clear();

    DIR* dir = opendir(SD_CARD_PRG_PATH);
    if (!dir) {
        MenuItem item = MenuItem();
        item.id       = 0;
        item.title    = "(No SD card mounted)";
        item.type     = MenuItemType::SPACER;
        items.push_back(item);
        return;
    }
    closedir(dir);

    if (currentPage != 0) {
        MenuItem prevPageItem = MenuItem();
        prevPageItem.id       = 0xfffe;
        prevPageItem.title    = "=== Prev Page ===";
        prevPageItem.type     = MenuItemType::ACTION;
        prevPageItem.action   = [this](MenuItem* item) {
            (void)item;
            this->toPrevPage();
        };
        items.push_back(prevPageItem);
    }

    std::vector<std::string> entries = sdcard->listPagedEntries(SD_CARD_PRG_PATH, currentPage, pageSize);
    ESP_LOGI(TAG, "page %u holds %zu files", currentPage, entries.size());

    uint16_t id_count = 0;
    for (size_t i = 0; i < entries.size(); i++) {
        const std::string& filename = entries[i];

        MenuItem item = MenuItem();
        item.id       = id_count++;
        item.title    = filename;
        item.type     = MenuItemType::ACTION;
        item.action   = [this, filename](MenuItem* menuItem) {
            (void)menuItem;
            this->openFile(filename);
        };
        items.push_back(item);
    }

    // A short page is the last page, so only offer to go further when this
    // one was filled.
    if (entries.size() == pageSize) {
        MenuItem nextPageItem = MenuItem();
        nextPageItem.id       = 0xffff;
        nextPageItem.title    = "=== Next Page ===";
        nextPageItem.type     = MenuItemType::ACTION;
        nextPageItem.action   = [this](MenuItem* item) {
            (void)item;
            this->toNextPage();
        };
        items.push_back(nextPageItem);
    }

    if (items.empty()) {
        MenuItem empty = MenuItem();
        empty.id       = 0;
        empty.title    = "(no files found)";
        empty.type     = MenuItemType::SPACER;
        items.push_back(empty);
    }
}

void LoadMenu::toNextPage()
{
    nextPage++;
    MenuBaseClass::navigateBegin();
    ESP_LOGI(TAG, "next page %u", nextPage);
}

void LoadMenu::toPrevPage()
{
    if (nextPage > 0) {
        nextPage--;
        MenuBaseClass::navigateBegin();
    }
    ESP_LOGI(TAG, "previous page %u", nextPage);
}

void LoadMenu::openFile(const std::string& filename)
{
    switch (imageFormatFromName(filename)) {
        case ImageFormat::PRG:
            loadPrg(filename);
            break;
        case ImageFormat::T64:
        case ImageFormat::D64:
            openImage(filename);
            break;
        default:
            ESP_LOGE(TAG, "cannot load %s", filename.c_str());
            break;
    }
}

void LoadMenu::loadPrg(const std::string& filename)
{
    ExternalCmds* ext = &c64emu->externalCmds;

    // Reset first so the program lands in a clean machine, then give the
    // kernal time to finish starting up before dropping it into RAM.
    ext->reset();
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    ext->loadFile(filename.c_str());
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    menuController->hide();
}

void LoadMenu::openImage(const std::string& filename)
{
    if (imageMenu == nullptr) {
        imageMenu = new ImageMenu(filename, this, menuController);
        imageMenu->init();
    }

    if (!imageMenu->openImage(filename)) {
        ESP_LOGE(TAG, "no loadable programs in %s", filename.c_str());
        // openImage() still leaves a readable placeholder in the submenu, so
        // show it rather than silently ignoring the selection.
    }
    menuController->setCurrentMenu(imageMenu);
}

// Entering the menu starts again at the first page and rereads the directory,
// so a card swapped while the menu was closed shows up.
void LoadMenu::navigateBegin()
{
    needsRefresh = true;
    nextPage     = 0;
    MenuBaseClass::navigateBegin();
}

void LoadMenu::update()
{
    if (needsRefresh || currentPage != nextPage) {
        needsRefresh = false;
        currentPage  = nextPage;
        displayMenu();
    }
}

bool LoadMenu::init()
{
    sdcard->init();
    ESP_LOGI(TAG, "initializing load menu");
    needsRefresh = true;
    currentPage  = 0;
    nextPage     = 0;
    displayMenu();
    return true;
}
