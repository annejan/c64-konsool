/*
 Host side tests for the CBM DOS layer.

 CbmDos talks to a DiskImage, so these tests hand it an in-memory disk and
 drive it through the same IEC primitives the Kernal would use. No ESP-IDF and
 no hardware needed:

     make -C main/src/drive/test run
*/

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../CbmDos.hpp"
#include "../D64Disk.hpp"
#include "../DiskImage.hpp"

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

/* ------------------------------------------------------- in memory disk -- */

class MemDisk : public DiskImage {
   public:
    std::vector<uint8_t> bytes;
    unsigned int         trackCount = 35;

    MemDisk()
    {
        size_t blocks = 0;
        for (unsigned int t = 1; t <= trackCount; t++) blocks += sectorsPerTrack(t);
        bytes.assign(blocks * CBM_SECTOR_SIZE, 0);
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

struct TestFile {
    const char*          name;
    std::vector<uint8_t> data;  // including the two byte load address
    uint8_t              type;  // 0x82 = closed PRG
};

static void putPetName(std::vector<uint8_t>& v, size_t off, const char* name)
{
    for (size_t i = 0; i < 16; i++) {
        v[off + i] = (i < strlen(name)) ? static_cast<uint8_t>(name[i]) : 0xA0;
    }
}

// Lays files out from track 1 and writes a directory and BAM on track 18.
static void populate(MemDisk& disk, const std::vector<TestFile>& files)
{
    unsigned int nextTrack  = 1;
    unsigned int nextSector = 0;

    struct Placed {
        unsigned int track, sector;
    };
    std::vector<Placed> placed;

    for (size_t f = 0; f < files.size(); f++) {
        const std::vector<uint8_t>& data      = files[f].data;
        size_t                      pos       = 0;
        Placed                      first     = {0, 0};
        bool                        haveFirst = false;

        while (pos < data.size() || !haveFirst) {
            if (nextTrack == 18) nextTrack = 19;
            unsigned int t = nextTrack, s = nextSector;
            if (!haveFirst) {
                first     = {t, s};
                haveFirst = true;
            }
            size_t chunk = data.size() - pos;
            if (chunk > 254) chunk = 254;
            size_t off = disk.offset(t, s);
            memcpy(&disk.bytes[off + 2], &data[pos], chunk);
            pos += chunk;

            nextSector++;
            if (nextSector >= disk.sectorsPerTrack(nextTrack)) {
                nextSector = 0;
                nextTrack++;
                if (nextTrack == 18) nextTrack = 19;
            }

            if (pos < data.size()) {
                disk.bytes[off]     = static_cast<uint8_t>(nextTrack);
                disk.bytes[off + 1] = static_cast<uint8_t>(nextSector);
            } else {
                disk.bytes[off]     = 0;
                disk.bytes[off + 1] = static_cast<uint8_t>(chunk + 1);
            }
        }
        placed.push_back(first);
    }

    // BAM at 18/0: disk name, id, dos type, and a free count per track.
    size_t bam          = disk.offset(18, 0);
    disk.bytes[bam]     = 18;
    disk.bytes[bam + 1] = 1;
    for (unsigned int t = 1; t <= 35; t++) {
        disk.bytes[bam + 4 + (t - 1) * 4] = static_cast<uint8_t>(t == 18 ? 0 : 5);
    }
    putPetName(disk.bytes, bam + 0x90, "TESTDISK");
    // A formatted disk pads this area with shifted spaces, not nulls.
    for (size_t i = 0xA0; i <= 0xAA; i++) disk.bytes[bam + i] = 0xA0;
    disk.bytes[bam + 0xA2] = 'I';
    disk.bytes[bam + 0xA3] = 'D';
    disk.bytes[bam + 0xA5] = '2';
    disk.bytes[bam + 0xA6] = 'A';

    // Directory at 18/1.
    size_t dir          = disk.offset(18, 1);
    disk.bytes[dir]     = 0;
    disk.bytes[dir + 1] = 0xff;
    for (size_t f = 0; f < files.size() && f < 8; f++) {
        size_t slot          = dir + f * 32;
        disk.bytes[slot + 2] = files[f].type;
        disk.bytes[slot + 3] = static_cast<uint8_t>(placed[f].track);
        disk.bytes[slot + 4] = static_cast<uint8_t>(placed[f].sector);
        putPetName(disk.bytes, slot + 5, files[f].name);
        uint16_t blocks       = static_cast<uint16_t>((files[f].data.size() + 253) / 254);
        disk.bytes[slot + 30] = static_cast<uint8_t>(blocks & 0xff);
        disk.bytes[slot + 31] = static_cast<uint8_t>(blocks >> 8);
    }
}

static std::vector<uint8_t> makePrg(uint16_t loadAddr, size_t payloadLen, uint8_t seed)
{
    std::vector<uint8_t> prg;
    prg.push_back(static_cast<uint8_t>(loadAddr & 0xff));
    prg.push_back(static_cast<uint8_t>(loadAddr >> 8));
    for (size_t i = 0; i < payloadLen; i++) prg.push_back(static_cast<uint8_t>((seed + i) & 0xff));
    return prg;
}

/* ------------------------------------------------- Kernal-shaped helpers -- */

// Performs what the Kernal does for OPEN: LISTEN, secondary with the open
// bits, the name, then UNLISTEN.
static void kernalOpen(CbmDos& dos, uint8_t channel, const std::string& name)
{
    dos.listen(static_cast<uint8_t>(IEC_SEC_OPEN | channel));
    for (size_t i = 0; i < name.size(); i++) {
        dos.write(static_cast<uint8_t>(name[i]));
    }
    dos.unlisten();
}

// Performs what the Kernal does to read a file: TALK, secondary, then ACPTR
// until the device flags the last byte.
static std::vector<uint8_t> kernalRead(CbmDos& dos, uint8_t channel, size_t limit = 100000)
{
    std::vector<uint8_t> out;
    dos.talk(static_cast<uint8_t>(IEC_SEC_DATA | channel));
    while (out.size() < limit) {
        uint8_t value = 0;
        bool    eoi   = false;
        if (!dos.read(&value, &eoi)) break;
        out.push_back(value);
        if (eoi) break;
    }
    dos.untalk();
    return out;
}

static void kernalClose(CbmDos& dos, uint8_t channel)
{
    dos.listen(static_cast<uint8_t>(IEC_SEC_CLOSE | channel));
    dos.unlisten();
}

/* ------------------------------------------------------------- the tests -- */

static void testLoadFile()
{
    printf("DOS: LOAD\"NAME\",8 returns the file byte for byte\n");
    MemDisk               disk;
    std::vector<TestFile> files;
    files.push_back({"GAME", makePrg(0x0801, 500, 7), 0x82});
    populate(disk, files);

    CbmDos dos;
    dos.setDisk(&disk);
    CHECK(dos.present(), "device reports absent with a disk attached");

    kernalOpen(dos, 0, "GAME");
    std::vector<uint8_t> got = kernalRead(dos, 0);

    CHECK(got.size() == 502, "read %zu bytes, expected 502", got.size());
    CHECK(got[0] == 0x01 && got[1] == 0x08, "load address was $%02x%02x", got[1], got[0]);
    for (size_t i = 0; i < 500 && i + 2 < got.size(); i++) {
        if (got[i + 2] != static_cast<uint8_t>(7 + i)) {
            CHECK(false, "payload mismatch at %zu", i);
            break;
        }
    }
    kernalClose(dos, 0);
}

static void testLoadSpanningSectors()
{
    printf("DOS: a file crossing many sectors and a track boundary\n");
    MemDisk               disk;
    std::vector<TestFile> files;
    files.push_back({"BIG", makePrg(0x0801, 6000, 3), 0x82});
    populate(disk, files);

    CbmDos dos;
    dos.setDisk(&disk);
    kernalOpen(dos, 0, "BIG");
    std::vector<uint8_t> got = kernalRead(dos, 0);

    CHECK(got.size() == 6002, "read %zu bytes, expected 6002", got.size());
    for (size_t i = 0; i < 6000 && i + 2 < got.size(); i++) {
        if (got[i + 2] != static_cast<uint8_t>(3 + i)) {
            CHECK(false, "payload mismatch at %zu", i);
            break;
        }
    }
}

static void testWildcards()
{
    printf("DOS: wildcard and drive prefix name matching\n");
    MemDisk               disk;
    std::vector<TestFile> files;
    files.push_back({"ALPHA", makePrg(0x0801, 10, 1), 0x82});
    files.push_back({"BETA", makePrg(0x1000, 20, 2), 0x82});
    populate(disk, files);

    CbmDos dos;
    dos.setDisk(&disk);

    kernalOpen(dos, 0, "AL*");
    std::vector<uint8_t> a = kernalRead(dos, 0);
    CHECK(a.size() == 12 && a[0] == 0x01 && a[1] == 0x08, "AL* did not find ALPHA (%zu bytes)", a.size());

    kernalOpen(dos, 0, "0:BETA");
    std::vector<uint8_t> b = kernalRead(dos, 0);
    CHECK(b.size() == 22 && b[1] == 0x10, "0:BETA did not load (%zu bytes)", b.size());

    kernalOpen(dos, 0, "B?TA");
    std::vector<uint8_t> c = kernalRead(dos, 0);
    CHECK(c.size() == 22, "B?TA did not match BETA (%zu bytes)", c.size());

    // An exact name must not match a longer one.
    CHECK(!CbmDos::nameMatches(reinterpret_cast<const uint8_t*>("ALPHAX\xa0\xa0\xa0\xa0\xa0\xa0\xa0\xa0\xa0\xa0"),
                               "ALPHA"),
          "ALPHA wrongly matched ALPHAX");
}

static void testFileNotFound()
{
    printf("DOS: missing file sets 62 on the command channel\n");
    MemDisk               disk;
    std::vector<TestFile> files;
    files.push_back({"GAME", makePrg(0x0801, 10, 1), 0x82});
    populate(disk, files);

    CbmDos dos;
    dos.setDisk(&disk);
    kernalOpen(dos, 0, "NOSUCH");

    std::vector<uint8_t> st = kernalRead(dos, CbmDos::CMD_CHANNEL);
    std::string          s(st.begin(), st.end());
    CHECK(s.compare(0, 2, "62") == 0, "status was '%s'", s.c_str());
    CHECK(s.find("FILE NOT FOUND") != std::string::npos, "status was '%s'", s.c_str());

    // Reading the status clears it, as on a real drive.
    std::vector<uint8_t> st2 = kernalRead(dos, CbmDos::CMD_CHANNEL);
    std::string          s2(st2.begin(), st2.end());
    CHECK(s2.compare(0, 2, "00") == 0, "status did not clear, was '%s'", s2.c_str());
}

static void testDirectory()
{
    printf("DOS: LOAD\"$\",8 builds a BASIC directory listing\n");
    MemDisk               disk;
    std::vector<TestFile> files;
    files.push_back({"ALPHA", makePrg(0x0801, 10, 1), 0x82});
    files.push_back({"BETA", makePrg(0x1000, 600, 2), 0x82});
    populate(disk, files);

    CbmDos dos;
    dos.setDisk(&disk);
    kernalOpen(dos, 0, "$");
    std::vector<uint8_t> dir = kernalRead(dos, 0);

    CHECK(dir.size() > 40, "directory was only %zu bytes", dir.size());
    CHECK(dir[0] == 0x01 && dir[1] == 0x04, "load address was $%02x%02x, expected $0401", dir[1], dir[0]);

    std::string text(dir.begin(), dir.end());
    CHECK(text.find("TESTDISK") != std::string::npos, "disk name missing from listing");
    CHECK(text.find("ALPHA") != std::string::npos, "ALPHA missing from listing");
    CHECK(text.find("BETA") != std::string::npos, "BETA missing from listing");
    CHECK(text.find("PRG") != std::string::npos, "file type missing from listing");
    CHECK(text.find("BLOCKS FREE.") != std::string::npos, "trailer missing from listing");

    // The listing has to end the way a BASIC program does.
    CHECK(dir.size() >= 2 && dir[dir.size() - 1] == 0x00 && dir[dir.size() - 2] == 0x00,
          "listing does not end with a null link");

    // A pattern narrows the listing.
    kernalOpen(dos, 0, "$AL*");
    std::vector<uint8_t> filtered = kernalRead(dos, 0);
    std::string          ftext(filtered.begin(), filtered.end());
    CHECK(ftext.find("ALPHA") != std::string::npos, "filtered listing lost ALPHA");
    CHECK(ftext.find("BETA") == std::string::npos, "filtered listing still shows BETA");
}

static void testWriteRefused()
{
    printf("DOS: writes are refused while the image is read only\n");
    MemDisk               disk;
    std::vector<TestFile> files;
    files.push_back({"GAME", makePrg(0x0801, 10, 1), 0x82});
    populate(disk, files);

    CbmDos dos;
    dos.setDisk(&disk);

    kernalOpen(dos, 1, "NEWFILE,P,W");
    std::vector<uint8_t> st = kernalRead(dos, CbmDos::CMD_CHANNEL);
    std::string          s(st.begin(), st.end());
    CHECK(s.compare(0, 2, "26") == 0, "open for write gave '%s'", s.c_str());

    // A scratch command must be refused too rather than silently accepted.
    kernalOpen(dos, CbmDos::CMD_CHANNEL, "S0:GAME");
    std::vector<uint8_t> st2 = kernalRead(dos, CbmDos::CMD_CHANNEL);
    std::string          s2(st2.begin(), st2.end());
    CHECK(s2.compare(0, 2, "26") == 0, "scratch gave '%s'", s2.c_str());
}

static void testNoDisk()
{
    printf("DOS: no image attached means the device is absent\n");
    CbmDos dos;
    CHECK(!dos.present(), "device claims to be present with no disk");

    kernalOpen(dos, 0, "GAME");
    uint8_t value = 0;
    bool    eoi   = false;
    dos.talk(IEC_SEC_DATA | 0);
    CHECK(!dos.read(&value, &eoi), "read succeeded with no disk attached");
}

static void testCircularChain()
{
    printf("DOS: a circular sector chain terminates\n");
    MemDisk               disk;
    std::vector<TestFile> files;
    files.push_back({"LOOP", makePrg(0x0801, 600, 2), 0x82});
    populate(disk, files);

    // Point the file's first sector back at itself.
    size_t first          = disk.offset(1, 0);
    disk.bytes[first]     = 1;
    disk.bytes[first + 1] = 0;

    CbmDos dos;
    dos.setDisk(&disk);
    kernalOpen(dos, 0, "LOOP");
    std::vector<uint8_t> got = kernalRead(dos, 0, 2000000);
    CHECK(got.size() < 2000000, "circular chain did not terminate (%zu bytes)", got.size());
}

static void testViceExactFormats()
{
    printf("DOS: status and directory bytes match VICE exactly\n");
    MemDisk               disk;
    std::vector<TestFile> files;
    // Block counts either side of the 10 and 100 boundaries, since the drive
    // pads the column differently for each.
    files.push_back({"SMALL", makePrg(0x0801, 10, 1), 0x82});      // 1 block
    files.push_back({"MEDIUM", makePrg(0x0801, 5000, 2), 0x82});   // 20 blocks
    files.push_back({"LOCKEDONE", makePrg(0x0801, 10, 3), 0xC2});  // locked PRG
    files.push_back({"SPLATFILE", makePrg(0x0801, 10, 4), 0x02});  // never closed
    populate(disk, files);

    CbmDos dos;
    dos.setDisk(&disk);

    // Status: VICE formats "%02d,%s,%02u,%02u\r" with no space after the
    // comma. The space in "00, OK" comes from the message text itself.
    kernalOpen(dos, 0, "NOSUCH");
    std::vector<uint8_t> st = kernalRead(dos, CbmDos::CMD_CHANNEL);
    std::string          s(st.begin(), st.end());
    CHECK(s == "62,FILE NOT FOUND,00,00\r", "status was '%s'", s.c_str());

    std::vector<uint8_t> ok = kernalRead(dos, CbmDos::CMD_CHANNEL);
    std::string          oks(ok.begin(), ok.end());
    CHECK(oks == "00, OK,00,00\r", "ok status was '%s'", oks.c_str());

    // Directory: two byte load address, then fixed 32 byte lines throughout.
    kernalOpen(dos, 0, "$");
    std::vector<uint8_t> dir = kernalRead(dos, 0);
    CHECK(dir.size() >= 2, "directory too short");

    // The header's own line number field is 00 00, so start the scan for its
    // terminator past that.
    size_t headerEnd = 6;
    while (headerEnd < dir.size() && dir[headerEnd] != 0x00) headerEnd++;
    headerEnd++;  // step past the terminator
    CHECK(headerEnd == 32, "header was %zu bytes, expected 32", headerEnd);

    // Everything after the header is 32 byte lines plus the two byte end link.
    size_t body = dir.size() - headerEnd;
    CHECK(body >= 2 && (body - 2) % 32 == 0, "body is %zu bytes, not 32 byte lines plus an end link", body);

    // Check the columns on the first entry line.
    const uint8_t* line = &dir[headerEnd];
    CHECK(line[0] == 0x01 && line[1] == 0x01, "entry line link was %02x %02x", line[0], line[1]);
    uint16_t blocks = static_cast<uint16_t>(line[2] | (line[3] << 8));
    CHECK(blocks == 1, "first entry claimed %u blocks, expected 1", blocks);

    // One block, so the drive pads three spaces before the opening quote.
    const uint8_t* l = line + 4 + 3;
    CHECK(l[0] == '"', "opening quote missing, found $%02x", l[0]);
    l++;  // step onto the name
    CHECK(memcmp(l, "SMALL", 5) == 0, "name field wrong");
    CHECK(l[5] == '"', "closing quote is not right after the name, found $%02x", l[5]);
    CHECK(l[17] == ' ', "closed flag wrong for a closed file, found $%02x", l[17]);
    CHECK(memcmp(l + 18, "PRG", 3) == 0, "type field wrong");
    CHECK(l[21] == ' ', "lock flag set on an unlocked file");
    CHECK(line[31] == 0x00, "entry line is not null terminated");

    // The locked entry carries '<' and the splat entry carries '*'.
    std::string text(dir.begin(), dir.end());
    size_t      locked = text.find("LOCKEDONE");
    CHECK(locked != std::string::npos, "locked entry missing");
    if (locked != std::string::npos) {
        // name starts at l+0, so the flags sit at fixed offsets from there
        CHECK(text[locked + 21] == '<', "locked marker missing, found '%c'", text[locked + 21]);
    }
    size_t splat = text.find("SPLATFILE");
    CHECK(splat != std::string::npos, "splat entry missing");
    if (splat != std::string::npos) {
        CHECK(text[splat + 17] == '*', "splat marker missing, found '%c'", text[splat + 17]);
    }

    // Trailer is a 32 byte line too, then the null link.
    const uint8_t* end = &dir[dir.size() - 34];
    CHECK(end[0] == 0x01 && end[1] == 0x01, "trailer link wrong");
    CHECK(memcmp(end + 4, "BLOCKS FREE.", 12) == 0, "trailer text wrong");
    CHECK(dir[dir.size() - 2] == 0x00 && dir[dir.size() - 1] == 0x00, "missing end of program link");
}

int main()
{
    testLoadFile();
    testLoadSpanningSectors();
    testWildcards();
    testFileNotFound();
    testDirectory();
    testWriteRefused();
    testNoDisk();
    testCircularChain();
    testViceExactFormats();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
