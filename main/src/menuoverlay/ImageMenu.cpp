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
    isDisk             = (format == ImageFormat::D64);
    T64Image  t64;
    D64Image  d64;
    CbmImage* image = nullptr;
    if (format == ImageFormat::T64) {
        image = &t64;
    } else if (format == ImageFormat::D64) {
        image = &d64;
    } else {
        ESP_LOGE(TAG, "%s is not a container", filename.c_str());
    }

    bool ok = false;
    if (image != nullptr) {
        std::string path = SDCard::fullPath(filename.c_str());
        if (image->open(path.c_str())) {
            // Copy the directory out so the image file does not have to stay
            // open while the user is browsing.
            imageEntries = image->entries();
            image->close();
            ok = !imageEntries.empty();
            ESP_LOGI(TAG, "%s holds %zu programs", filename.c_str(), imageEntries.size());
        } else {
            ESP_LOGE(TAG, "cannot read %s", path.c_str());
        }
    }

    // Rebuild either way. Leaving the previous image's entries on screen after
    // a failed open would let the wrong program be picked, and a disk with
    // nothing extractable on it can still be mounted.
    title = filename;
    displayMenu();
    navigateBegin();
    return ok;
}

void ImageMenu::displayMenu()
{
    items.clear();

    // A disk can be handed to the C64 whole, so it can LOAD from it the way it
    // would from a real drive. That is the only route that works for anything
    // that loads more than one part.
    if (isDisk && currentPage == 0) {
        MenuItem mountItem = MenuItem();
        mountItem.id       = 0xfffd;
        mountItem.title    = "=== Mount as drive 8 ===";
        mountItem.type     = MenuItemType::ACTION;
        mountItem.action   = [this](MenuItem* item) {
            (void)item;
            this->mountDisk();
        };
        items.push_back(mountItem);
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

void ImageMenu::mountDisk()
{
    ExternalCmds* ext = &c64emu->externalCmds;

    // Reset first so the Kernal starts clean with the drive already attached.
    ext->reset();
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    if (ext->mountDisk(imageName.c_str())) {
        ESP_LOGI(TAG, "%s mounted as drive 8", imageName.c_str());
    } else {
        ESP_LOGE(TAG, "could not mount %s", imageName.c_str());
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
    menuController->hide();
}

void ImageMenu::update()
{
    if (currentPage != nextPage) {
        currentPage = nextPage;
        displayMenu();
    }
}
