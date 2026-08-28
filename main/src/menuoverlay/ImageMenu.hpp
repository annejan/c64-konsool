#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "C64Emu.hpp"
#include "MenuBaseClass.hpp"
#include "images/CbmImage.hpp"
#include "menuoverlay/MenuTypes.hpp"

// Lists the programs inside a .t64 or .d64 and loads the one that is picked.
class ImageMenu : public MenuBaseClass {
   private:
    C64Emu*                 c64emu = nullptr;
    // The full path is what gets opened, the name is what gets shown.
    std::string             imagePath;
    std::string             imageName;
    std::vector<ImageEntry> imageEntries;
    uint16_t                currentPage = 0;
    uint16_t                nextPage    = 0;
    size_t                  pageSize    = 12;

    void displayMenu();
    void loadEntry(uint16_t index);
    void mountDisk();
    bool isDisk = false;
    void toPrevPage();
    void toNextPage();

   public:
    ImageMenu(std::string title, MenuBaseClass* previousMenu, MenuController* menuController);
    ~ImageMenu() override;

    // Reads the directory of the container at `path` and rebuilds the menu
    // around it. Works for any path, so a USB disk reads the same as the card.
    // Returns false when the container cannot be read or holds nothing
    // loadable.
    bool openImage(const std::string& path);

    bool init() override;
    void update() override;
};
