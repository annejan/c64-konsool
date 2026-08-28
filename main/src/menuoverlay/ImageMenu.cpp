#include "ImageMenu.hpp"
#include <cstdio>
#include "C64Emu.hpp"
#include "ExternalCmds.hpp"
#include "SDCard.hpp"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "images/D64Image.hpp"
#include "images/T64Image.hpp"
#include "menuoverlay/MenuController.hpp"
#include "menuoverlay/MenuTypes.hpp"
#include "portmacro.h"

const static char* TAG = "ImageMenu";

ImageMenu::ImageMenu(std::string title, MenuBaseClass* previousMenu, MenuController* menuController)
    : MenuBaseClass(title, previousMenu, menuController)
{
    c64emu = menuController->getC64Emu();
}

ImageMenu::~ImageMenu()
{
}

bool ImageMenu::init()
{
    return true;
}

bool ImageMenu::openImage(const std::string& filename)
{
    imageName = filename;
    imageEntries.clear();
    currentPage = 0;
    nextPage    = 0;

    ImageFormat format = imageFormatFromName(filename);
    T64Image    t64;
    D64Image    d64;
    CbmImage*   image = nullptr;
    if (format == ImageFormat::T64) {
        image = &t64;
    } else if (format == ImageFormat::D64) {
        image = &d64;
    } else {
        ESP_LOGE(TAG, "%s is not a container", filename.c_str());
        return false;
    }

    std::string path = SDCard::fullPath(filename.c_str());
    if (!image->open(path.c_str())) {
        ESP_LOGE(TAG, "cannot read %s", path.c_str());
        return false;
    }

    // Copy the directory out so the image file does not have to stay open
    // while the user is browsing.
    imageEntries = image->entries();
    image->close();

    ESP_LOGI(TAG, "%s holds %zu programs", filename.c_str(), imageEntries.size());

    title = filename;
    displayMenu();
    navigateBegin();
    return !imageEntries.empty();
}

void ImageMenu::displayMenu()
{
    items.clear();

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

    size_t start = static_cast<size_t>(currentPage) * pageSize;
    size_t shown = 0;
    for (size_t i = start; i < imageEntries.size() && shown < pageSize; i++, shown++) {
        const ImageEntry& entry = imageEntries[i];

        char label[40];
        snprintf(label, sizeof(label), "%-16s %4u", entry.name.c_str(), static_cast<unsigned>(entry.blocks));

        MenuItem item = MenuItem();
        item.id       = entry.index;
        item.title    = label;
        item.type     = MenuItemType::ACTION;
        uint16_t idx  = entry.index;
        item.action   = [this, idx](MenuItem* menuItem) {
            (void)menuItem;
            this->loadEntry(idx);
        };
        items.push_back(item);
    }

    if (start + shown < imageEntries.size()) {
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
        empty.title    = "(no programs found)";
        empty.type     = MenuItemType::SPACER;
        items.push_back(empty);
    }
}

void ImageMenu::toNextPage()
{
    nextPage++;
    navigateBegin();
}

void ImageMenu::toPrevPage()
{
    if (nextPage > 0) {
        nextPage--;
        navigateBegin();
    }
}

void ImageMenu::loadEntry(uint16_t index)
{
    ExternalCmds* ext = &c64emu->externalCmds;

    // Same sequence the .prg path uses: reset first so the program lands in a
    // clean machine, then give the kernal time to finish its start up before
    // dropping the program into RAM.
    ext->reset();
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    ext->loadImageEntry(imageName.c_str(), index);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    menuController->hide();
}

void ImageMenu::update()
{
    if (currentPage != nextPage) {
        currentPage = nextPage;
        displayMenu();
    }
}
