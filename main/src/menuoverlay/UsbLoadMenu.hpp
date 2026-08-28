#pragma once

#include <string>
#include "C64Emu.hpp"
#include "MenuBaseClass.hpp"
#include "menuoverlay/ImageMenu.hpp"
#include "menuoverlay/MenuTypes.hpp"

// Lists the loadable files in the root of a USB disk. A .prg is loaded
// straight away, a .t64 or .d64 opens a submenu listing what is inside it.
class UsbLoadMenu : public MenuBaseClass {
   private:
    C64Emu*    c64emu    = nullptr;
    ImageMenu* imageMenu = nullptr;

    void     openFile(const std::string& filename);
    void     loadPrg(const std::string& filename);
    void     openImage(const std::string& filename);
    uint16_t currentPage  = 0;
    uint16_t nextPage     = 0;
    size_t   pageSize     = 12;
    bool     needsRefresh = true;

   public:
    UsbLoadMenu(std::string title, MenuBaseClass* previousMenu, MenuController* menuController);
    ~UsbLoadMenu();

    bool init() override;
    void update() override;
    void navigateBegin() override;
    void toPrevPage();
    void toNextPage();
    void displayMenu();
};
