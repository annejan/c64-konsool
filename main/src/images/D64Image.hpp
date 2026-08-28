#pragma once
/*
 Copyright (C) 2025 Konsool 64 contributors

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

#include <cstdint>
#include <vector>
#include "CbmImage.hpp"

// Read only reader for 1541 disk images.
//
// This walks the directory on track 18 and follows a file's sector chain to
// copy it into RAM, which covers plain PRG files. It is not a 1541: there is
// no IEC bus and no drive CPU, so titles that talk to the drive directly or
// use a custom loader will not work from a .d64.
class D64Image : public CbmImage {
   private:
    struct Entry {
        uint8_t track;  // first data sector
        uint8_t sector;
    };

    int                     fd     = -1;
    unsigned int            tracks = 35;
    std::vector<Entry>      dirEntries;
    std::vector<ImageEntry> imageEntries;

    bool readSector(unsigned int track, unsigned int sector, uint8_t* buf) const;

   public:
    D64Image()
    {
    }
    ~D64Image() override;

    bool open(const char* path) override;
    void close() override;

    const std::vector<ImageEntry>& entries() const override
    {
        return imageEntries;
    }

    bool extract(uint16_t index, uint8_t* ram, uint16_t* endAddr) override;

    // Number of sectors on `track` of a 1541 disk, 0 if out of range.
    static unsigned int sectorsPerTrack(unsigned int track);
};
