#include "D64Image.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

static const unsigned int SECTOR_SIZE   = 256;
static const unsigned int DIR_TRACK     = 18;
static const unsigned int DIR_SECTOR    = 1;
static const unsigned int MIN_TRACKS    = 35;
static const unsigned int MAX_TRACKS    = 42;
static const unsigned int SLOTS_PER_DIR = 8;
static const unsigned int SLOT_SIZE     = 32;

// Directory slot layout
static const int SLOT_TYPE         = 2;
static const int SLOT_FIRST_TRACK  = 3;
static const int SLOT_FIRST_SECTOR = 4;
static const int SLOT_NAME         = 5;
static const int SLOT_NAME_LEN     = 16;
static const int SLOT_NR_BLOCKS    = 30;

// CBM DOS file types
static const uint8_t FT_MASK   = 0x07;
static const uint8_t FT_PRG    = 0x02;
static const uint8_t FT_CLOSED = 0x80;

static bool readFully(int fd, void* buf, size_t len)
{
    uint8_t* p = static_cast<uint8_t*>(buf);
    while (len > 0) {
        ssize_t got = read(fd, p, len);
        if (got <= 0) return false;
        p   += got;
        len -= static_cast<size_t>(got);
    }
    return true;
}

unsigned int D64Image::sectorsPerTrack(unsigned int track)
{
    if (track < 1 || track > MAX_TRACKS) return 0;
    if (track <= 17) return 21;
    if (track <= 24) return 19;
    if (track <= 30) return 18;
    return 17;  // tracks 31 and up, including the non standard extra tracks
}

D64Image::~D64Image()
{
    close();
}

void D64Image::close()
{
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
    dirEntries.clear();
    imageEntries.clear();
    tracks = MIN_TRACKS;
}

bool D64Image::readSector(unsigned int track, unsigned int sector, uint8_t* buf) const
{
    if (fd < 0) return false;
    unsigned int perTrack = sectorsPerTrack(track);
    if (perTrack == 0 || track > tracks || sector >= perTrack) return false;

    // Sum the sectors on every track before this one to find the offset.
    off_t offset = 0;
    for (unsigned int t = 1; t < track; t++) {
        offset += static_cast<off_t>(sectorsPerTrack(t)) * SECTOR_SIZE;
    }
    offset += static_cast<off_t>(sector) * SECTOR_SIZE;

    if (lseek(fd, offset, SEEK_SET) < 0) return false;
    return readFully(fd, buf, SECTOR_SIZE);
}

bool D64Image::open(const char* path)
{
    close();

    fd = ::open(path, O_RDONLY);
    if (fd < 0) return false;

    off_t fileSize = lseek(fd, 0, SEEK_SET) == 0 ? lseek(fd, 0, SEEK_END) : -1;
    if (fileSize < 0) {
        close();
        return false;
    }

    // Walk 35 up to 42 tracks and see which track count the file size matches,
    // with or without the trailing error info byte per block. Anything else is
    // not a d64.
    unsigned int candidateTracks = MIN_TRACKS;
    off_t        blocks          = 683;  // blocks on a 35 track disk
    bool         matched         = false;
    while (true) {
        off_t bytes = blocks * static_cast<off_t>(SECTOR_SIZE);
        // Either the bare sectors, or the sectors plus one error info byte
        // per block appended at the end.
        if (fileSize == bytes || fileSize == bytes + blocks) {
            matched = true;
            break;
        }
        candidateTracks++;
        blocks += 17;
        if (candidateTracks > MAX_TRACKS) break;
    }
    if (!matched) {
        close();
        return false;
    }
    tracks = candidateTracks;

    // The directory always starts at 18/1, even when the link bytes in the
    // BAM sector at 18/0 claim otherwise.
    unsigned int track   = DIR_TRACK;
    unsigned int sector  = DIR_SECTOR;
    unsigned int visited = 0;

    // A directory cannot be longer than the disk, and a corrupt image can
    // point its chain back at itself, so cap the walk.
    const unsigned int maxDirSectors = 40;

    uint8_t buf[SECTOR_SIZE];
    while (visited < maxDirSectors) {
        if (!readSector(track, sector, buf)) break;
        visited++;

        for (unsigned int slot = 0; slot < SLOTS_PER_DIR; slot++) {
            const uint8_t* e    = buf + slot * SLOT_SIZE;
            uint8_t        type = e[SLOT_TYPE];

            if ((type & FT_MASK) != FT_PRG) continue;  // only PRG can be loaded

            unsigned int ft = e[SLOT_FIRST_TRACK];
            unsigned int fs = e[SLOT_FIRST_SECTOR];
            if (ft < 1 || ft > tracks) continue;
            if (fs >= sectorsPerTrack(ft)) continue;

            Entry entry;
            entry.track  = static_cast<uint8_t>(ft);
            entry.sector = static_cast<uint8_t>(fs);

            ImageEntry item;
            item.name   = petsciiToDisplay(e + SLOT_NAME, SLOT_NAME_LEN);
            item.index  = static_cast<uint16_t>(dirEntries.size());
            item.blocks = static_cast<uint16_t>(e[SLOT_NR_BLOCKS] | (e[SLOT_NR_BLOCKS + 1] << 8));
            if (item.name.empty()) item.name = "(unnamed)";
            // An unclosed "splat" file was never written completely. Show it,
            // marked the way a C64 directory listing marks it, but expect the
            // sector chain to be broken.
            if (!(type & FT_CLOSED)) item.name += "*";

            dirEntries.push_back(entry);
            imageEntries.push_back(item);
        }

        if (buf[0] == 0) break;  // end of the directory chain
        track  = buf[0];
        sector = buf[1];
    }

    if (dirEntries.empty()) {
        close();
        return false;
    }
    return true;
}

bool D64Image::extract(uint16_t index, uint8_t* ram, uint16_t* endAddr)
{
    if (fd < 0 || index >= dirEntries.size()) return false;

    unsigned int track  = dirEntries[index].track;
    unsigned int sector = dirEntries[index].sector;

    uint8_t buf[SECTOR_SIZE];
    if (!readSector(track, sector, buf)) return false;

    // The first two bytes of the first sector are the link, and the file's own
    // byte stream starts right after. For a PRG that stream opens with the
    // load address.
    unsigned int used = (buf[0] == 0) ? (buf[1] >= 2 ? buf[1] - 1u : 0u) : 254u;
    if (used < 2) return false;

    uint16_t     startAddr = static_cast<uint16_t>(buf[2] | (buf[3] << 8));
    unsigned int pos       = startAddr;
    unsigned int skip      = 2;  // the load address, only on the first sector

    // Follow the chain, bounded by the number of blocks the disk can hold so a
    // circular chain cannot spin forever.
    unsigned int maxSectors = 0;
    for (unsigned int t = 1; t <= tracks; t++) {
        maxSectors += sectorsPerTrack(t);
    }

    for (unsigned int count = 0; count < maxSectors; count++) {
        unsigned int avail = used - skip;
        if (avail > 0) {
            // Stop at the top of the address space rather than running past
            // the end of the 64K RAM buffer.
            if (pos >= C64_RAM_SIZE - 1) break;
            unsigned int room = (C64_RAM_SIZE - 1) - pos;
            if (avail > room) avail = room;
            memcpy(ram + pos, buf + 2 + skip, avail);
            pos += avail;
        }

        if (buf[0] == 0) break;  // last sector of the file

        track  = buf[0];
        sector = buf[1];
        if (!readSector(track, sector, buf)) break;

        used = (buf[0] == 0) ? (buf[1] >= 2 ? buf[1] - 1u : 0u) : 254u;
        skip = 0;
    }

    if (pos == startAddr) return false;
    *endAddr = static_cast<uint16_t>(pos);
    return true;
}
