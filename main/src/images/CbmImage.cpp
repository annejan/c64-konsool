#include "CbmImage.hpp"
#include <cctype>

ImageFormat imageFormatFromName(const std::string& filename)
{
    size_t dot = filename.find_last_of('.');
    if (dot == std::string::npos) {
        return ImageFormat::UNKNOWN;
    }
    std::string ext = filename.substr(dot + 1);
    for (size_t i = 0; i < ext.size(); i++) {
        ext[i] = static_cast<char>(tolower(static_cast<unsigned char>(ext[i])));
    }
    if (ext == "prg") return ImageFormat::PRG;
    if (ext == "t64") return ImageFormat::T64;
    if (ext == "d64") return ImageFormat::D64;
    return ImageFormat::UNKNOWN;
}

// PETSCII file names use $41-$5A for the letters, and the shifted charset
// repeats them at $C1-$DA. Everything printable is passed through as is,
// anything else becomes an underscore so a corrupt name cannot smuggle
// control characters into the menu.
static char petToAscii(uint8_t c)
{
    if (c >= 0xC1 && c <= 0xDA) {
        c = static_cast<uint8_t>(c - 0x80);
    }
    if (c >= 0x41 && c <= 0x5A) return static_cast<char>(c);  // A-Z
    if (c >= 0x20 && c <= 0x40) return static_cast<char>(c);  // space, digits, punctuation
    if (c == 0x5B || c == 0x5D) return static_cast<char>(c);  // [ ]
    return '_';
}

std::string petsciiToDisplay(const uint8_t* petscii, size_t len)
{
    // Tape records pad with $20, disk directory slots pad with $A0.
    while (len > 0 && (petscii[len - 1] == 0xA0 || petscii[len - 1] == 0x20 || petscii[len - 1] == 0x00)) {
        len--;
    }
    std::string out;
    out.reserve(len);
    for (size_t i = 0; i < len; i++) {
        out.push_back(petToAscii(petscii[i]));
    }
    return out;
}
