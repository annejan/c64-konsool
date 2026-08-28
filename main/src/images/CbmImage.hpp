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

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Size of the emulated C64 address space. Loading never writes past this.
static const size_t C64_RAM_SIZE = 1 << 16;

// Container formats we can pick individual programs out of.
enum class ImageFormat {
    UNKNOWN,
    PRG,  // raw program, 2 byte load address followed by data
    T64,  // C64S tape container
    D64,  // 1541 disk image
};

// Returns the format implied by the file name extension (case insensitive).
ImageFormat imageFormatFromName(const std::string& filename);

// One loadable program inside a container.
struct ImageEntry {
    std::string name;    // display name, converted from PETSCII
    uint16_t    index;   // pass back to extract()
    uint16_t    blocks;  // size in 254 byte blocks, for display
};

// Converts a PETSCII file name field to something printable, dropping the
// trailing padding ($20 on tape, $A0 on disk).
std::string petsciiToDisplay(const uint8_t* petscii, size_t len);

// Common interface for the container readers. A reader keeps the image file
// open between open() and close() so entries can be extracted on demand.
class CbmImage {
   public:
    virtual ~CbmImage()
    {
    }

    // Opens and parses the container. Returns false if it is not a valid
    // image of this type.
    virtual bool open(const char* path) = 0;
    virtual void close()                = 0;

    virtual const std::vector<ImageEntry>& entries() const = 0;

    // Copies the program at `index` into `ram`. On success `endAddr` receives
    // the address one past the last byte written, which is what BASIC's VARTAB
    // needs to point at. Returns false if the entry could not be read.
    virtual bool extract(uint16_t index, uint8_t* ram, uint16_t* endAddr) = 0;
};
