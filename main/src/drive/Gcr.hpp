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
#include "DiskImage.hpp"

// GCR encoding, which is how bytes actually sit on a 1541 disk surface.
//
// A .d64 holds decoded 256 byte sectors, but a real drive's read routines pull
// raw GCR off the head and decode it themselves. Anything with its own loader
// does exactly that, so the drive has to be handed a GCR track rather than
// tidy sectors.
//
// The layout follows Frodo's 1541job.cpp: per sector a sync, the header, a
// gap, another sync, the data block and a second gap. Frodo writes a single
// sync byte where hardware writes five; five is used here since a full track
// still has room for it.
//
// Frodo (C) 1994-1997, 2002 Christian Bauer, GPL version 2 or later.

static const unsigned int GCR_SYNC_BYTES   = 5;
static const unsigned int GCR_HEADER_BYTES = 10;  // 8 decoded bytes
static const unsigned int GCR_HEADER_GAP   = 9;
static const unsigned int GCR_DATA_BYTES   = 325;  // 260 decoded bytes
static const unsigned int GCR_DATA_GAP     = 8;

static const unsigned int GCR_SECTOR_SIZE =
    GCR_SYNC_BYTES + GCR_HEADER_BYTES + GCR_HEADER_GAP + GCR_SYNC_BYTES + GCR_DATA_BYTES + GCR_DATA_GAP;

// Every track is given the same buffer, sized for the longest one, so the
// head position does not have to be rescaled when stepping between tracks.
static const unsigned int GCR_MAX_SECTORS = 21;
static const unsigned int GCR_TRACK_SIZE  = GCR_SECTOR_SIZE * GCR_MAX_SECTORS;

// The byte that fills gaps and any unused tail of a track. It cannot be
// mistaken for a sync mark.
static const uint8_t GCR_GAP_BYTE = 0x55;

// Encodes one 256 byte sector, header included, into GCR_SECTOR_SIZE bytes.
void gcrEncodeSector(const uint8_t* block, unsigned int track, unsigned int sector, uint8_t id1, uint8_t id2,
                     uint8_t* dest);

// Encodes a whole track off `disk` into `dest`, which must hold
// GCR_TRACK_SIZE bytes. Any space past the last sector is filled with gap.
// Returns false if the track could not be read.
bool gcrEncodeTrack(DiskImage& disk, unsigned int track, uint8_t id1, uint8_t id2, uint8_t* dest);

// Decodes five GCR bytes back into four. Returns false on an invalid symbol.
bool gcrDecode5(const uint8_t* gcr, uint8_t* out);

// Decodes the data block of one encoded sector back into 256 bytes, checking
// the data mark and checksum.
bool gcrDecodeSector(const uint8_t* sectorGcr, uint8_t* block);
