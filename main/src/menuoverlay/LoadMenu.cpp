#include "LoadMenu.hpp"
#include <dirent.h>
#include <string.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include "C64Emu.hpp"
#include "Config.hpp"
#include "ExternalCmds.hpp"
#include "SDCard.hpp"
#include "esp_log.h"
#include "freertos/idf_additions.h"
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

LoadMenu::~LoadMenu() {};

void LoadMenu::refreshEntries()
{
    entries = SDCard::listProgramFiles(SD_CARD_PRG_PATH);
}

size_t LoadMenu::pageCount() const
{
    if (entries.empty()) return 1;
    return (entries.size() + pageSize - 1) / pageSize;
}

void LoadMenu::toNextPage()
{
    // Wrapping means a program at the end of a long listing is one step back
    // from the first page rather than a walk through every page in between.
    nextPage = static_cast<uint16_t>((currentPage + 1) % pageCount());
    // Deliberately not navigateBegin(): that override jumps back to the first
    // page, which is right when entering the menu and wrong when paging.
    MenuBaseClass::navigateBegin();
    ESP_LOGI(TAG, "page %u of %zu", static_cast<unsigned>(nextPage + 1), pageCount());
}

void LoadMenu::toPrevPage()
{
    nextPage = static_cast<uint16_t>((currentPage + pageCount() - 1) % pageCount());
    MenuBaseClass::navigateBegin();
    ESP_LOGI(TAG, "page %u of %zu", static_cast<unsigned>(nextPage + 1), pageCount());
}

void LoadMenu::displayMenu()
{
    items.clear();

    DIR* dir = opendir(SD_CARD_PRG_PATH);
    if (dir == nullptr) {
        MenuItem item = MenuItem();
        item.id       = 0;
        item.title    = "(No SD card mounted)";
        item.type     = MenuItemType::SPACER;
        items.push_back(item);
        return;
    }
    closedir(dir);

    if (entries.empty()) {
        MenuItem item = MenuItem();
        item.id       = 0;
        item.title    = "(No programs found)";
        item.type     = MenuItemType::SPACER;
        items.push_back(item);
        return;
    }

    size_t pages = pageCount();
    if (currentPage >= pages) currentPage = 0;

    char label[40];
    snprintf(label, sizeof(label), "=== Prev page (%u/%u) ===", static_cast<unsigned>(currentPage + 1),
             static_cast<unsigned>(pages));

    if (pages > 1) {
        MenuItem prevPageItem = MenuItem();
        prevPageItem.id       = 0xfffe;
        prevPageItem.title    = label;
        prevPageItem.type     = MenuItemType::ACTION;
        prevPageItem.action   = [this](MenuItem* item) {
            (void)item;
            this->toPrevPage();
        };
        items.push_back(prevPageItem);
    }

    size_t   start    = static_cast<size_t>(currentPage) * pageSize;
    uint16_t id_count = 0;
    for (size_t i = start; i < entries.size() && i < start + pageSize; i++) {
        const std::string name = entries[i];

        MenuItem item = MenuItem();
        item.id       = id_count++;
        item.title    = name;
        item.type     = MenuItemType::ACTION;
        item.action   = [this, name](MenuItem* menuItem) {
            (void)menuItem;
            this->loadPrg(name);
        };
        items.push_back(item);
    }

    if (pages > 1) {
        snprintf(label, sizeof(label), "=== Next page (%u/%u) ===", static_cast<unsigned>(currentPage + 1),
                 static_cast<unsigned>(pages));
        MenuItem nextPageItem = MenuItem();
        nextPageItem.id       = 0xffff;
        nextPageItem.title    = label;
        nextPageItem.type     = MenuItemType::ACTION;
        nextPageItem.action   = [this](MenuItem* item) {
            (void)item;
            this->toNextPage();
        };
        items.push_back(nextPageItem);
    }
}

void LoadMenu::loadPrg(const std::string& name)
{
    ExternalCmds* ext = &c64emu->externalCmds;

    // Reset first so the program lands in a clean machine, then give the
    // kernal time to finish starting up before dropping it into RAM.
    ext->reset();
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    ext->loadPrg(name.c_str());
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    menuController->hide();
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
    if (needsRefresh) {
        needsRefresh = false;
        refreshEntries();
        currentPage = nextPage;
        displayMenu();
    } else if (currentPage != nextPage) {
        currentPage = nextPage;
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
    return true;
}
