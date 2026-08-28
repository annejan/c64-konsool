#pragma once
/*
 Copyright (C) 2024 retroelec <retroelec42@gmail.com>

 This program is free software; you can redistribute it and/or modify it
 under the terms of the GNU General Public License as published by the
 Free Software Foundation; either version 3 of the License, or (at your
 option) any later version.

 This program is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 for more details.

 For the complete text of the GNU General Public License see
 http://www.gnu.org/licenses/.
*/

#include <sys/stat.h>
#include <cstdint>
#include <string>
#include <vector>
#include "driver/sdmmc_default_configs.h"
#include "driver/sdmmc_host.h"

class SDCard {
   private:
    bool initialized;

   public:
    SDCard();
    ~SDCard();

    bool                     init();
    //   uint16_t load(const char *path, uint8_t *ram, size_t len);
    uint16_t                 load(const char* path, uint8_t* ram, size_t len = 0);
    uint16_t                 load_auto(const char* path, uint8_t* ram, size_t len = 0);
    bool                     save(const char* path, const uint8_t* ram, size_t len = 0);
    // Lists the programs in `path`, sorted, with the .prg extension taken off.
    // Static so it serves the USB disk as well as the card. A directory with
    // more than MAX_LISTED_FILES programs is truncated, with a warning.
    static const size_t      MAX_LISTED_FILES = 512;
    static std::vector<std::string> listProgramFiles(const char* path);
    bool                     listNextEntry(uint8_t* nextEntry, size_t entrySize, bool start);
};
