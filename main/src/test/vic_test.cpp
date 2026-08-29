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

// What the VIC actually puts on the screen.
//
// Everything else that checks the VIC compares one build against another and
// can only say "this changed". That catches a regression and says nothing
// about whether either picture was right, which is how a wrong charset can
// survive every gate. These tests set the machine up by hand and assert the
// pixels from the chip's own rules instead.
//
// They assert structure, not colour: which pixels carry the foreground value
// and which the background. That is independent of the palette, so a change to
// the colour table cannot make a correct renderer look broken.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "HeadlessDisplay.hpp"
#include "VIC.hpp"
#include "roms/charset.h"
#include "sid/sid.hpp"

static int checks   = 0;
static int failures = 0;

#define CHECK(cond, ...)                     \
    do {                                     \
        checks++;                            \
        if (!(cond)) {                       \
            failures++;                      \
            printf("  FAIL: ");              \
            printf(__VA_ARGS__);             \
            printf("\n");                    \
        }                                    \
    } while (0)

static uint8_t ram[65536];
static uint8_t sidregs[32];

// drawRasterline writes screen row (rasterline - 0x32), so this is the raster
// line that lands on row 0 of the picture.
static const int FIRST_LINE = 0x32;

struct Machine {
    VIC vic;
    SID sid;

    Machine()
    {
        memset(ram, 0, sizeof(ram));
        memset(sidregs, 0, sizeof(sidregs));
        // A no-op sink rather than nullptr: raster_line() calls the callback
        // without checking it.
        sid.init(sidregs, [](int16_t*, size_t) {}, 8580);
        vic.init(ram, charset_rom, &sid);
        vic.initVarsAndRegs();

        // Standard text mode, screen at $0400, charset from the character ROM,
        // 25 rows and 40 columns, display enabled, YSCROLL at its default 3.
        vic.vicreg[0x11]    = 0x1b;
        vic.vicreg[0x16]    = 0x08;
        vic.vicreg[0x18]    = 0x15;
        vic.vicreg[0x20]    = 0x0e;
        vic.vicreg[0x21]    = 0x06;
        vic.vicmem          = 0x0000;
        vic.screenmemstart  = 0x0400;
        vic.charset         = charset_rom;
        vic.screenblank     = false;
        vic.screenHeight();
    }

    // Draws a whole frame, raster line 0 to 311, the way the emulation does.
    // It has to be the whole frame: the VIC carries VC, VCBASE and RC across
    // lines, and they are only set up from the top, so starting part way down
    // the screen leaves the counters in a state the hardware never reaches.
    // perLine is called before each line with the raster number, which is how
    // a test reprograms a register mid-screen.
    void drawFrame(void (*perLine)(VIC&, int) = nullptr)
    {
        for (int r = 0; r < 312; r++) {
            vic.rasterline = static_cast<uint16_t>(r);
            if (perLine) perLine(vic, r);
            vic.drawRasterline();
        }
        // Hand the finished frame to the display, once, as the emulation does.
        vic.refresh(true);
    }

    // Read the picture the way the panel would: through the display driver,
    // after refresh() has handed the frame over. That exercises the real
    // output path rather than reaching into the VIC's own buffer.
    uint16_t px(int row, int col)
    {
        HeadlessDisplay* d = static_cast<HeadlessDisplay*>(vic.getDriver());
        return d->screen[row * 320 + col];
    }
};

// The eight pixels of one character cell on one row, as a bit pattern: a set
// bit means that pixel is not the background.
static uint8_t cellBits(Machine& m, int screenRow, int cell, uint16_t bg)
{
    uint8_t bits = 0;
    for (int i = 0; i < 8; i++) {
        if (m.px(screenRow, cell * 8 + i) != bg) bits |= (0x80 >> i);
    }
    return bits;
}

static void testStandardTextMode()
{
    printf("VIC: a character comes out as the bits in the character ROM\n");
    Machine m;

    // "A" is screen code 1, and light blue on whatever the background is.
    for (int i = 0; i < 40; i++) {
        ram[0x0400 + i]        = 0x01;
        m.vic.colormap[i]      = 0x0e;
    }
    m.drawFrame();

    // Row 0 of "A" is blank in the ROM, so its leftmost pixel is the
    // background colour: read it from there rather than assuming a palette.
    uint16_t bg = m.px(0, 0);

    bool anySet = false;
    for (int row = 0; row < 8; row++) {
        uint8_t want = charset_rom[0x01 * 8 + row];
        uint8_t got  = cellBits(m, row, 0, bg);
        if (want) anySet = true;
        CHECK(got == want, "row %d of 'A' drew %02x, the character ROM says %02x", row, got, want);
    }
    CHECK(anySet, "the glyph tested was blank on every row, the test proves nothing");
}

static void testEveryColumnIsFetched()
{
    printf("VIC: all forty columns come from consecutive screen memory\n");
    Machine m;

    // A different character in each cell, so a column drawn from the wrong
    // address shows up immediately.
    for (int i = 0; i < 40; i++) {
        ram[0x0400 + i]   = static_cast<uint8_t>(i + 1);
        m.vic.colormap[i] = 0x0e;
    }
    m.drawFrame();

    // Row 4 of the ROM's glyphs is where most characters have something.
    const int row = 4;
    // Take the background from a cell whose glyph row is empty, if there is
    // one; otherwise compare relative patterns only.
    uint16_t background = 0;
    bool     haveBg     = false;
    for (int c = 0; c < 40 && !haveBg; c++) {
        if (charset_rom[(c + 1) * 8 + row] == 0x00) {
            background = m.px(row, c * 8);
            haveBg     = true;
        }
    }
    CHECK(haveBg, "no empty glyph row to read the background from");
    if (!haveBg) return;

    int wrong = 0;
    for (int c = 0; c < 40; c++) {
        uint8_t want = charset_rom[(c + 1) * 8 + row];
        uint8_t got  = cellBits(m, row, c, background);
        if (got != want) wrong++;
    }
    CHECK(wrong == 0, "%d of 40 columns did not match the character ROM", wrong);
}

// The point of the badline model. With YSCROLL left alone the display advances
// a character row every eight lines; when a demo moves YSCROLL so the badline
// never comes, the VIC stops fetching and the row must not advance.
static void testRowAdvancesEveryEightLines()
{
    printf("VIC: the character row advances once every eight raster lines\n");
    Machine m;

    // Row 0 all "A", row 1 all "B", so which row is being drawn is readable
    // from the pixels.
    for (int i = 0; i < 40; i++) {
        ram[0x0400 + i]      = 0x01;  // A
        ram[0x0400 + 40 + i] = 0x02;  // B
        m.vic.colormap[i]      = 0x0e;
        m.vic.colormap[40 + i] = 0x0e;
    }
    m.drawFrame();

    uint16_t bg = m.px(0, 0);
    // Line 8 is the first line of the second character row, so it must show
    // row 0 of "B", not row 0 of "A" again.
    uint8_t want = charset_rom[0x02 * 8 + 0];
    uint8_t got  = cellBits(m, 8, 0, bg);
    CHECK(got == want, "line 8 drew %02x, expected row 0 of 'B' which is %02x", got, want);

    // And line 7 is still the last line of the first row.
    want = charset_rom[0x01 * 8 + 7];
    got  = cellBits(m, 7, 0, bg);
    CHECK(got == want, "line 7 drew %02x, expected row 7 of 'A' which is %02x", got, want);
}


// FLD: a demo moves YSCROLL every raster line so the bad line never fires. The
// VIC then stops fetching, RC freezes, VCBASE stops advancing and the display
// stretches. Before there were counters to freeze, YSCROLL only shifted the
// finished picture, so the row went on advancing regardless -- which is the
// whole reason this model exists.
static void testBadLineSuppressionStopsTheRow()
{
    printf("VIC: suppressing bad lines stops the character row advancing\n");
    Machine m;

    for (int i = 0; i < 40; i++) {
        ram[0x0400 + i]        = 0x01;  // A
        ram[0x0400 + 40 + i]   = 0x02;  // B
        m.vic.colormap[i]      = 0x0e;
        m.vic.colormap[40 + i] = 0x0e;
    }

    // Let the first character row start normally, then keep YSCROLL away from
    // the line the VIC actually compares against so no bad line can match
    // again. The model tests hwline, which is rasterline + 1, so the value has
    // to be chosen against that and not against the raster number itself.
    m.drawFrame([](VIC& v, int raster) {
        if (raster >= 0x39) {
            int hw = (raster + 1) & 7;
            v.vicreg[0x11] = static_cast<uint8_t>(0x18 | ((hw + 1) & 7));
        }
    });

    uint16_t bg   = m.px(0, 0);
    uint8_t  bRow = charset_rom[0x02 * 8 + 0];
    uint8_t  got  = cellBits(m, 8, 0, bg);

    CHECK(got != bRow, "line 8 drew row 0 of 'B' (%02x) even though no bad line fired, so the row advanced anyway",
          bRow);

    // And the first row, drawn before the suppression starts, must be intact:
    // this must not be a test that passes because the whole screen broke.
    uint8_t want = charset_rom[0x01 * 8 + 1];
    uint8_t r1   = cellBits(m, 1, 0, bg);
    CHECK(r1 == want, "row 1 of 'A' drew %02x before any suppression, expected %02x", r1, want);
}

int main()
{
    printf("VIC rendering\n\n");
    testStandardTextMode();
    testEveryColumnIsFetched();
    testRowAdvancesEveryEightLines();
    testBadLineSuppressionStopsTheRow();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
