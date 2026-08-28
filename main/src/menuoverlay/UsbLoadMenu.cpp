#include "UsbLoadMenu.hpp"
#include <dirent.h>
#include <string>
#include "C64Emu.hpp"
#include "ExternalCmds.hpp"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "images/CbmImage.hpp"
#include "menuoverlay/ImageMenu.hpp"
#include "menuoverlay/MenuController.hpp"
#include "menuoverlay/MenuTypes.hpp"
#include "portmacro.h"

static const char* TAG = "UsbLoadMenu";

#define USB_PRG_PATH "/usb"

static std::string usbPath(const std::string& filename)
{
    return std::string(USB_PRG_PATH "/") + filename;
}

UsbLoadMenu::UsbLoadMenu(std::string title, MenuBaseClass* previousMenu, MenuController* menuController)
    : MenuBaseClass(title, previousMenu, menuController)
{
    c64emu = menuController->getC64Emu();
}

UsbLoadMenu::~UsbLoadMenu()
{
    delete imageMenu;
}

void UsbLoadMenu::displayMenu()
{
    items.clear();

    DIR* dir = opendir(USB_PRG_PATH);
    if (!dir) {
        MenuItem item = MenuItem();
        item.id       = 0;
        item.title    = "(No USB disk mounted)";
        item.type     = MenuItemType::SPACER;
        items.push_back(item);
        return;
    }

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

    // Only loadable files count towards a page, so the page boundaries do not
    // depend on whatever else happens to be on the disk.
    size_t         skip     = static_cast<size_t>(currentPage) * pageSize;
    size_t         matched  = 0;
    size_t         added    = 0;
    uint16_t       id_count = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr && added < pageSize) {
        std::string name = ent->d_name;
        if (imageFormatFromName(name) == ImageFormat::UNKNOWN) continue;
        if (matched++ < skip) continue;

        MenuItem item = MenuItem();
        item.id       = id_count++;
        item.title    = name;
        item.type     = MenuItemType::ACTION;
        item.action   = [this, name](MenuItem* menuItem) {
            (void)menuItem;
            this->openFile(name);
        };
        items.push_back(item);
        added++;
    }
    closedir(dir);

    // A short page is the last page, so only offer to go further when this one
    // was filled.
    if (added == pageSize) {
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

// Entering the menu starts again at the first page and rereads the directory,
// so a disk plugged in while the menu was closed shows up.
void UsbLoadMenu::navigateBegin()
{
    needsRefresh = true;
    nextPage     = 0;
    MenuBaseClass::navigateBegin();
}

void UsbLoadMenu::toNextPage()
{
    nextPage++;
    MenuBaseClass::navigateBegin();
    ESP_LOGI(TAG, "next page %u", nextPage);
}

void UsbLoadMenu::toPrevPage()
{
    if (nextPage > 0) {
        nextPage--;
        MenuBaseClass::navigateBegin();
    }
    ESP_LOGI(TAG, "previous page %u", nextPage);
}

void UsbLoadMenu::openFile(const std::string& filename)
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

void UsbLoadMenu::loadPrg(const std::string& filename)
{
    ExternalCmds* ext = &c64emu->externalCmds;

    // Reset first so the program lands in a clean machine, then give the
    // kernal time to finish starting up before dropping it into RAM.
    ext->reset();
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    ext->loadPrgFromPath(usbPath(filename).c_str());
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    menuController->hide();
}

void UsbLoadMenu::openImage(const std::string& filename)
{
    if (imageMenu == nullptr) {
        imageMenu = new ImageMenu(filename, this, menuController);
        imageMenu->init();
    }

    if (!imageMenu->openImage(usbPath(filename))) {
        ESP_LOGE(TAG, "no loadable programs in %s", filename.c_str());
        // openImage() still leaves a readable placeholder in the submenu, so
        // show it rather than silently ignoring the selection.
    }
    menuController->setCurrentMenu(imageMenu);
}

void UsbLoadMenu::update()
{
    if (needsRefresh || currentPage != nextPage) {
        needsRefresh = false;
        currentPage  = nextPage;
        displayMenu();
    }
}

bool UsbLoadMenu::init()
{
    ESP_LOGI(TAG, "initializing usb load menu");
    needsRefresh = true;
    currentPage  = 0;
    nextPage     = 0;
    displayMenu();
    return true;
}
