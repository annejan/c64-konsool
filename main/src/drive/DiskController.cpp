#include "DiskController.hpp"
#include <cstring>

DiskController::DiskController()
{
    memset(gcrTrack, GCR_GAP_BYTE, sizeof(gcrTrack));
}

void DiskController::reset()
{
    halfTrack   = 2 * 18;
    headPos     = 0;
    trackLoaded = false;
    memset(gcrTrack, GCR_GAP_BYTE, sizeof(gcrTrack));
}

void DiskController::setDisk(DiskImage* image)
{
    disk        = image;
    trackLoaded = false;

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
    memset(gcrTrack, GCR_GAP_BYTE, sizeof(gcrTrack));
    headPos = 0;

    if (disk == nullptr) return;
    unsigned int track = currentTrack();
    if (track < 1 || track > disk->tracks()) return;

    trackLoaded = gcrEncodeTrack(*disk, track, id1, id2, gcrTrack);
}

void DiskController::moveHeadOut()
{
    if (halfTrack > 2) {
        halfTrack--;
        loadTrack();
    }
}

void DiskController::moveHeadIn()
{
    // A 1541 can step past the last formatted track; the head just finds
    // nothing there.
    if (halfTrack < 2 * 42) {
        halfTrack++;
        loadTrack();
    }
}

uint8_t DiskController::readGcrByte()
{
    if (!trackLoaded) return GCR_GAP_BYTE;

    uint8_t value = gcrTrack[headPos];
    headPos++;
    if (headPos >= GCR_TRACK_SIZE) headPos = 0;
    return value;
}

bool DiskController::syncFound() const
{
    if (!trackLoaded) return false;
    return gcrTrack[headPos] == 0xff;
}

uint8_t DiskController::writeProtectBit() const
{
    // Bit 4 low means write protected. Nothing is written through this path
    // yet, so a disk always reads as protected here.
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
