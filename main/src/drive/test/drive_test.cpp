/*
 Host side tests for the 1541's VIAs, the bus lines and the disk controller.

     make -C main/src/drive/test drive

 The drive CPU itself needs a DOS ROM, which is not shipped, so what is
 checked here is everything around it: the VIA registers and timers, the
 wired-and behaviour of the serial lines, the attention acknowledge gate, and
 the head stepping.
*/

#include <cstdio>
#include <cstring>
#include <vector>
#include "../D64Disk.hpp"
#include "../DiskController.hpp"
#include "../Drive1541.hpp"
#include "../IecLines.hpp"
#include "../Via6522.hpp"

static int failures = 0;
static int checks   = 0;

#define CHECK(cond, ...)                                  \
    do {                                                  \
        checks++;                                         \
        if (!(cond)) {                                    \
            failures++;                                   \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while (0)

class MemDisk : public DiskImage {
   public:
    std::vector<uint8_t> bytes;
    unsigned int         trackCount = 35;

    MemDisk()
    {
        size_t blocks = 0;
        for (unsigned int t = 1; t <= trackCount; t++) blocks += sectorsPerTrack(t);
        bytes.assign(blocks * CBM_SECTOR_SIZE, 0);
        for (unsigned int t = 1; t <= trackCount; t++) {
            for (unsigned int s = 0; s < sectorsPerTrack(t); s++) {
                size_t off = offset(t, s);
                for (unsigned int i = 0; i < CBM_SECTOR_SIZE; i++) {
                    bytes[off + i] = static_cast<uint8_t>((t * 5 + s * 3 + i) & 0xff);
                }
            }
        }
        // A plausible disk id in the BAM, which ends up in every sector header.
        size_t bam        = offset(18, 0);
        bytes[bam + 0xA2] = 'I';
        bytes[bam + 0xA3] = 'D';
    }

    size_t offset(unsigned int track, unsigned int sector) const
    {
        size_t off = 0;
        for (unsigned int t = 1; t < track; t++) off += sectorsPerTrack(t) * CBM_SECTOR_SIZE;
        return off + sector * CBM_SECTOR_SIZE;
    }
    bool readSector(unsigned int track, unsigned int sector, uint8_t* buf) override
    {
        if (track < 1 || track > trackCount || sector >= sectorsPerTrack(track)) return false;
        memcpy(buf, &bytes[offset(track, sector)], CBM_SECTOR_SIZE);
        return true;
    }
    bool writable() const override
    {
        return false;
    }
    unsigned int tracks() const override
    {
        return trackCount;
    }
    unsigned int sectorsPerTrack(unsigned int track) const override
    {
        return D64Disk::sectorsOnTrack(track);
    }
};

/* ------------------------------------------------------------------ VIA -- */

static void testViaPorts()
{
    printf("VIA: output bits read back, input bits read the pins\n");
    Via6522 via;
    via.reset();

    via.write(Via6522::REG_DDRB, 0x0f);  // low nibble out, high nibble in
    via.write(Via6522::REG_PRB, 0xaa);

    uint8_t got = via.read(Via6522::REG_PRB, 0xff, 0x50);
    CHECK((got & 0x0f) == 0x0a, "output bits read back as $%02x", got & 0x0f);
    CHECK((got & 0xf0) == 0x50, "input bits read as $%02x, expected $50", got & 0xf0);

    CHECK(via.read(Via6522::REG_DDRB, 0, 0) == 0x0f, "DDRB did not read back");
}

static void testViaTimer1()
{
    printf("VIA: timer 1 flags on underflow, reloads only when free running\n");
    Via6522 via;
    via.reset();

    // One shot: writing the high byte loads the counter and clears the flag.
    via.write(Via6522::REG_ACR, 0x00);
    via.write(Via6522::REG_T1CL, 100);
    via.write(Via6522::REG_T1CH, 0);
    CHECK((via.ifr & Via6522::IRQ_T1) == 0, "flag set straight after loading");

    via.countTimers(50);
    CHECK((via.ifr & Via6522::IRQ_T1) == 0, "flag set before the timer ran out");
    via.countTimers(60);
    CHECK((via.ifr & Via6522::IRQ_T1) != 0, "flag missing after the timer ran out");

    // Reading the low byte clears it.
    via.read(Via6522::REG_T1CL, 0, 0);
    CHECK((via.ifr & Via6522::IRQ_T1) == 0, "reading T1CL did not clear the flag");

    // Free running: it reloads and keeps flagging.
    via.reset();
    via.write(Via6522::REG_ACR, 0x40);
    via.write(Via6522::REG_T1CL, 10);
    via.write(Via6522::REG_T1CH, 0);
    via.countTimers(20);
    CHECK((via.ifr & Via6522::IRQ_T1) != 0, "free running timer did not flag");
    via.read(Via6522::REG_T1CL, 0, 0);
    via.countTimers(20);
    CHECK((via.ifr & Via6522::IRQ_T1) != 0, "free running timer did not flag again");
}

static void testViaInterruptGating()
{
    printf("VIA: an interrupt only asserts once it is enabled\n");
    Via6522 via;
    via.reset();

    via.write(Via6522::REG_ACR, 0x00);
    via.write(Via6522::REG_T1CL, 5);
    via.write(Via6522::REG_T1CH, 0);
    via.countTimers(10);

    CHECK((via.ifr & Via6522::IRQ_T1) != 0, "flag missing");
    CHECK(!via.irqAsserted(), "interrupt asserted while disabled");

    via.write(Via6522::REG_IER, 0x80 | Via6522::IRQ_T1);  // set enable
    CHECK(via.irqAsserted(), "interrupt not asserted once enabled");
    CHECK((via.read(Via6522::REG_IFR, 0, 0) & Via6522::IRQ_ANY) != 0, "IFR bit 7 not set");

    via.write(Via6522::REG_IFR, Via6522::IRQ_T1);  // writing a one clears
    CHECK(!via.irqAsserted(), "interrupt still asserted after clearing the flag");

    via.write(Via6522::REG_IER, Via6522::IRQ_T1);  // bit 7 clear disables
    CHECK((via.ier & Via6522::IRQ_T1) == 0, "IER bit did not clear");
}

/* ---------------------------------------------------------------- lines -- */

static void testLinesAreWiredAnd()
{
    printf("Lines: any participant pulling low wins\n");
    IecLines lines;
    lines.reset();

    CHECK(!lines.clkLow() && !lines.dataLow() && !lines.atnLow(), "lines not released after reset");

    lines.c64Clk = true;
    CHECK(lines.clkLow(), "the C64 could not pull CLK low");
    lines.c64Clk   = false;
    lines.driveClk = true;
    CHECK(lines.clkLow(), "the drive could not pull CLK low");

    // Both pulling, then one releasing, still leaves the line low.
    lines.c64Clk = true;
    CHECK(lines.clkLow(), "both pulling should be low");
    lines.c64Clk = false;
    CHECK(lines.clkLow(), "line went high while the drive was still pulling");
    lines.driveClk = false;
    CHECK(!lines.clkLow(), "line stayed low with nobody pulling");

    // ATN is driven by the C64 alone.
    lines.c64Atn = true;
    CHECK(lines.atnLow(), "ATN did not follow the C64");
}

/* -------------------------------------------------- the acknowledge gate -- */

// Pokes VIA 1 through the drive's own memory map, which is how the ROM does it.
static void setVia1PortB(Drive1541& drive, uint8_t ddrb, uint8_t prb)
{
    drive.setMem(0x1802, ddrb);  // DDRB
    drive.setMem(0x1800, prb);   // PRB
}

static void testAtnAcknowledgeGate()
{
    printf("Drive: DATA is held low until the drive acknowledges ATN\n");
    IecLines  lines;
    Drive1541 drive;
    lines.reset();
    drive.setLines(&lines);

    // Bits 1, 3 and 4 are the outputs the drive drives: DATA, CLK and ATNA.
    const uint8_t DDRB = 0x1a;

    // Nothing asserted anywhere: the bus is free.
    setVia1PortB(drive, DDRB, 0x00);
    CHECK(!lines.dataLow(), "DATA low with nothing going on");

    // The C64 asserts ATN. With the acknowledge still low, the gate pulls
    // DATA down, which is how a drive says "I am here".
    lines.c64Atn = true;
    drive.refreshIecOutputs();
    CHECK(lines.dataLow(), "DATA not pulled low when ATN was asserted");

    // The drive acknowledges by raising ATNA, releasing DATA again.
    setVia1PortB(drive, DDRB, 0x10);
    CHECK(!lines.dataLow(), "DATA still low after the drive acknowledged");

    // With ATN gone but the acknowledge left set, the gate pulls DATA low
    // again; the two have to agree.
    lines.c64Atn = false;
    drive.refreshIecOutputs();
    CHECK(lines.dataLow(), "the gate did not react to ATN being released");

    setVia1PortB(drive, DDRB, 0x00);
    CHECK(!lines.dataLow(), "DATA not released once both agreed");

    // The drive can also pull DATA low on its own, whatever ATN is doing.
    setVia1PortB(drive, DDRB, 0x02);
    CHECK(lines.dataLow(), "the drive could not pull DATA low itself");
    setVia1PortB(drive, DDRB, 0x08);
    CHECK(lines.clkLow(), "the drive could not pull CLK low");
    CHECK(!lines.dataLow(), "DATA low when only CLK was asserted");
}

static void testDriveReadsBusState()
{
    printf("Drive: port B reports the bus, low lines reading as ones\n");
    IecLines  lines;
    Drive1541 drive;
    lines.reset();
    drive.setLines(&lines);

    // All of port B as inputs, so the pins show through.
    drive.setMem(0x1802, 0x00);

    uint8_t idle = drive.getMem(0x1800);
    CHECK((idle & 0x01) == 0, "DATA read as low on an idle bus");
    CHECK((idle & 0x04) == 0, "CLK read as low on an idle bus");
    CHECK((idle & 0x80) == 0, "ATN read as low on an idle bus");

    lines.c64Data = true;
    lines.c64Clk  = true;
    lines.c64Atn  = true;
    uint8_t busy  = drive.getMem(0x1800);
    CHECK((busy & 0x01) != 0, "DATA low did not read as a one");
    CHECK((busy & 0x04) != 0, "CLK low did not read as a one");
    CHECK((busy & 0x80) != 0, "ATN low did not read as a one");
}

/* ------------------------------------------------------------ head/disk -- */

static void testHeadStepping()
{
    printf("Controller: the stepper walks the head a half track at a time\n");
    MemDisk        disk;
    DiskController controller;
    controller.setDisk(&disk);

    CHECK(controller.currentTrack() == 18, "head did not start on track 18");

    unsigned int start = controller.currentHalfTrack();
    controller.moveHeadIn();
    controller.moveHeadIn();
    CHECK(controller.currentHalfTrack() == start + 2, "two half steps did not move two");
    CHECK(controller.currentTrack() == 19, "two half steps did not reach the next track");

    controller.moveHeadOut();
    controller.moveHeadOut();
    CHECK(controller.currentTrack() == 18, "stepping back did not return to track 18");

    // The head cannot walk off the inside edge.
    for (int i = 0; i < 200; i++) controller.moveHeadOut();
    CHECK(controller.currentTrack() >= 1, "head walked past track 1");

    for (int i = 0; i < 400; i++) controller.moveHeadIn();
    CHECK(controller.currentTrack() <= 42, "head walked past the last track");
}

static void testControllerReadsTrack()
{
    printf("Controller: the head reads back the GCR of the track it is on\n");
    MemDisk        disk;
    DiskController controller;
    controller.setDisk(&disk);

    // Track 18 sector 0 should be recoverable from the byte stream the head
    // hands over, which is what the drive's own read routines do.
    std::vector<uint8_t> seen;
    for (unsigned int i = 0; i < GCR_TRACK_SIZE; i++) {
        seen.push_back(controller.readGcrByte());
    }

    uint8_t back[CBM_SECTOR_SIZE];
    CHECK(gcrDecodeSector(seen.data(), back), "the first sector under the head did not decode");

    uint8_t expect[CBM_SECTOR_SIZE];
    disk.readSector(18, 0, expect);
    CHECK(memcmp(back, expect, CBM_SECTOR_SIZE) == 0, "the head returned the wrong sector");

    // The track wraps rather than running off the end.
    uint8_t first = controller.readGcrByte();
    CHECK(first == seen[0], "the track did not wrap round");

    // Sync marks are found where the encoder put them.
    DiskController fresh;
    fresh.setDisk(&disk);
    CHECK(fresh.syncFound(), "no sync at the start of a track");

    // With no disk the head finds gap, not stale data.
    DiskController empty;
    CHECK(!empty.hasDisk(), "an empty controller claims to have a disk");
    CHECK(empty.readGcrByte() == GCR_GAP_BYTE, "an empty drive returned something other than gap");
    CHECK(!empty.syncFound(), "an empty drive found a sync mark");
}

static void testSpeedZones()
{
    printf("Controller: speed zone follows the track number\n");
    MemDisk        disk;
    DiskController controller;
    controller.setDisk(&disk);

    struct {
        unsigned int track;
        uint8_t      zone;
    } expected[] = {{1, 3}, {17, 3}, {18, 2}, {24, 2}, {25, 1}, {30, 1}, {31, 0}, {35, 0}};

    for (unsigned int i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        // Walk the head to the wanted track.
        while (controller.currentTrack() > expected[i].track) controller.moveHeadOut();
        while (controller.currentTrack() < expected[i].track) controller.moveHeadIn();
        CHECK(controller.speedZone() == expected[i].zone, "track %u gave zone %u, expected %u", expected[i].track,
              controller.speedZone(), expected[i].zone);
    }
}

static void testDriveMemoryMap()
{
    printf("Drive: RAM mirrors, and the VIAs sit where the ROM expects\n");
    Drive1541 drive;
    IecLines  lines;
    drive.setLines(&lines);

    drive.setMem(0x0000, 0x42);
    CHECK(drive.getMem(0x0000) == 0x42, "RAM did not hold a byte");
    // 2K of RAM appears again every 2K up to the VIAs.
    CHECK(drive.getMem(0x0800) == 0x42, "RAM did not mirror at $0800");
    CHECK(drive.getMem(0x1000) == 0x42, "RAM did not mirror at $1000");

    drive.setMem(0x07ff, 0x99);
    CHECK(drive.getMem(0x0fff) == 0x99, "the top of RAM did not mirror");

    // VIA 1 answers across its whole quarter, VIA 2 across the next.
    drive.setMem(0x1803, 0x5a);  // DDRA
    CHECK(drive.getMem(0x1803) == 0x5a, "VIA 1 DDRA did not read back");
    CHECK(drive.getMem(0x1903) == 0x5a, "VIA 1 did not mirror at $1903");

    drive.setMem(0x1c03, 0x3c);
    CHECK(drive.getMem(0x1c03) == 0x3c, "VIA 2 DDRA did not read back");
    CHECK(drive.getMem(0x1d03) == 0x3c, "VIA 2 did not mirror at $1d03");

    // With no ROM the drive reports itself unusable rather than running wild.
    CHECK(!drive.ready(), "a drive with no ROM claimed to be ready");
    CHECK(drive.getMem(0xc000) == 0, "unmapped ROM space returned something");
}

int main()
{
    testViaPorts();
    testViaTimer1();
    testViaInterruptGating();
    testLinesAreWiredAnd();
    testAtnAcknowledgeGate();
    testDriveReadsBusState();
    testHeadStepping();
    testControllerReadsTrack();
    testSpeedZones();
    testDriveMemoryMap();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
