#pragma once

#include <string>
#include <vector>
#include "C64Emu.hpp"
#include "MenuBaseClass.hpp"
#include "menuoverlay/MenuTypes.hpp"

// Lists the programs in the c64prg directory on the SD card, a page at a time.
class LoadMenu : public MenuBaseClass {
   private:
    C64Emu*                  c64emu = nullptr;
    // The whole sorted listing, read once when the menu is opened rather than
    // per page, so the pages stay consistent while walking through them.
    std::vector<std::string> entries;

    void   refreshEntries();
    size_t pageCount() const;
    void   loadPrg(const std::string& name);

    uint16_t currentPage  = 0;
    uint16_t nextPage     = 0;
    size_t   pageSize     = 12;
    bool     needsRefresh = true;

   public:
    LoadMenu(std::string title, MenuBaseClass* previousMenu, MenuController* menuController);
    ~LoadMenu();

    SDCard* sdcard;

    bool init() override;
    void update() override;
    void navigateBegin() override;
    void toPrevPage();
    void toNextPage();
    void displayMenu();
};
