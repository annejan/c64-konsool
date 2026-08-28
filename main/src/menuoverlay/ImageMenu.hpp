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
    std::string             imageName;
    std::vector<ImageEntry> imageEntries;
    uint16_t                currentPage = 0;
    uint16_t                nextPage    = 0;
    size_t                  pageSize    = 12;

    void displayMenu();
    void loadEntry(uint16_t index);
    void toPrevPage();
    void toNextPage();

   public:
    ImageMenu(std::string title, MenuBaseClass* previousMenu, MenuController* menuController);
    ~ImageMenu() override;

    // Reads the directory of `filename` and rebuilds the menu around it.
    // Returns false when the container cannot be read or holds nothing
    // loadable.
    bool openImage(const std::string& filename);

    bool init() override;
    void update() override;
};
