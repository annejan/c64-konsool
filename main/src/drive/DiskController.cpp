#include "DiskController.hpp"
#include <cstring>

DiskController::DiskController()
{
    memset(gcrTrack, GCR_GAP_BYTE, sizeof(gcrTrack));
}

void DiskController::reset()
{
    flushTrack();
    halfTrack = 2 * 18;
    headPos   = 0;  // a reset does park the head
    // Reload rather than just clearing: a reset parks the head on the
    // directory track, it does not take the disk out. Clearing without
    // reloading leaves the head reading gap forever, which looks exactly like
    // an unformatted disk.
    loadTrack();
}

void DiskController::setDisk(DiskImage* image)
{
    flushTrack();
    disk        = image;
    trackLoaded = false;
    trackDirty  = false;

    id1 = 0;
    id2 = 0;
    if (disk != nullptr) {
        // The disk id lives in the BAM and is part of every sector header, so
        // the drive's own checks only pass if it matches the real disk.
        uint8_t bam[CBM_SECTOR_SIZE];
        if (disk->readSector(disk->dirTrack(), 0, bam)) {
            id1 = bam[0xA2];
            id2 = bam[0xA3];
        }
    }
    loadTrack();
}

void DiskController::loadTrack()
{
    trackLoaded = false;
    trackDirty  = false;
    memset(gcrTrack, GCR_GAP_BYTE, sizeof(gcrTrack));

    // headPos is deliberately left alone. The disk keeps turning while the
    // head steps, so the head comes down wherever the rotation has got to,
    // not at the start of a sector. Zeroing it here restarts the revolution
    // on every half step, and a 1541 half steps constantly while it settles
    // on a track, so the DOS never got to see the sectors near the end of a
    // track at all. Every track is encoded into the same sized buffer, so the
    // position stays in range.

    if (disk == nullptr) return;
    unsigned int track = currentTrack();
    if (track < 1 || track > disk->tracks()) return;

    trackLoaded = gcrEncodeTrack(*disk, track, id1, id2, gcrTrack);
}

void DiskController::moveHeadOut()
{
    if (halfTrack > 2) {
        // Anything written to this track has to reach the image before the
        // head leaves it.
        flushTrack();
        halfTrack--;
        loadTrack();
    }
}

void DiskController::moveHeadIn()
{
    // A 1541 can step past the last formatted track; the head just finds
    // nothing there.
    if (halfTrack < 2 * 42) {
        flushTrack();
        halfTrack++;
        loadTrack();
    }
}

void DiskController::flush()
{
    flushTrack();
}

// Walks the track looking for sector headers, and writes back the data block
// that follows each one. The drive only rewrites data blocks, leaving headers
// alone, so the headers are what say where each block belongs.
void DiskController::flushTrack()
{
    if (!trackDirty || !trackLoaded || disk == nullptr || !disk->writable()) {
        trackDirty = false;
        return;
    }

    unsigned int sectors = disk->sectorsPerTrack(currentTrack());
    for (unsigned int sector = 0; sector < sectors; sector++) {
        const uint8_t* sectorGcr = gcrTrack + sector * GCR_SECTOR_SIZE;

        // Confirm the header still says what we think it does before writing
        // anywhere, so a garbled track cannot scribble over the wrong sector.
        uint8_t header[4];
        if (!gcrDecode5(sectorGcr + GCR_SYNC_BYTES, header)) continue;
        if (header[0] != 0x08) continue;
        if (header[2] != sector || header[3] != currentTrack()) continue;

        uint8_t block[CBM_SECTOR_SIZE];
        if (!gcrDecodeSector(sectorGcr, block)) continue;

        disk->writeSector(currentTrack(), sector, block);
    }

    trackDirty = false;
}

void DiskController::writeGcrByte(uint8_t value)
{
    if (!trackLoaded) return;

    gcrTrack[headPos] = value;
    trackDirty        = true;

    headPos++;
    if (headPos >= GCR_TRACK_SIZE) headPos = 0;
}

uint8_t DiskController::readGcrByte()
{
    if (!trackLoaded) return GCR_GAP_BYTE;

    uint8_t value = gcrTrack[headPos];
    headPos++;
    if (headPos >= GCR_TRACK_SIZE) headPos = 0;
    return value;
}

void DiskController::rotate()
{
    if (!trackLoaded) return;
    headPos++;
    if (headPos >= GCR_TRACK_SIZE) headPos = 0;
}

bool DiskController::syncFound() const
{
    if (!trackLoaded) return false;
    return gcrTrack[headPos] == 0xff;
}

uint8_t DiskController::writeProtectBit() const
{
    // Bit 4 low means write protected.
    if (disk != nullptr && disk->writable()) return 0x10;
    return 0x00;
}

uint8_t DiskController::speedZone() const
{
    unsigned int track = currentTrack();
    if (track <= 17) return 3;
    if (track <= 24) return 2;
    if (track <= 30) return 1;
    return 0;
}
