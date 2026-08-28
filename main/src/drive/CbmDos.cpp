/*
 The directory listing layout, the status message format and the error text
 table below are ported from VICE (src/vdrive/vdrive-dir.c,
 src/vdrive/vdrive-command.c and src/cbmdos.c), so that what a program reads
 back from this drive is byte for byte what it would get from a real one.

 VICE is Copyright (C) 1996-2024 the VICE team, distributed under the GNU
 General Public License version 2 or later, which is why it can be combined
 with this GPL version 3 or later project.
*/
#include "CbmDos.hpp"
#include <cstdio>
#include <cstring>

// Directory slot layout, same as the on disk directory
static const int SLOT_TYPE         = 2;
static const int SLOT_FIRST_TRACK  = 3;
static const int SLOT_FIRST_SECTOR = 4;
static const int SLOT_NAME         = 5;
static const int SLOT_NAME_LEN     = 16;
static const int SLOT_NR_BLOCKS    = 30;

static const uint8_t FT_MASK   = 0x07;
static const uint8_t FT_DEL    = 0x00;
static const uint8_t FT_SEQ    = 0x01;
static const uint8_t FT_PRG    = 0x02;
static const uint8_t FT_USR    = 0x03;
static const uint8_t FT_REL    = 0x04;
static const uint8_t FT_LOCKED = 0x40;
static const uint8_t FT_CLOSED = 0x80;

static const char* typeName(uint8_t type)
{
    switch (type & FT_MASK) {
        case FT_DEL:
            return "DEL";
        case FT_SEQ:
            return "SEQ";
        case FT_PRG:
            return "PRG";
        case FT_USR:
            return "USR";
        case FT_REL:
            return "REL";
        default:
            return "???";
    }
}

void CbmDos::setDisk(DiskImage* image, const std::string& name)
{
    disk     = image;
    diskName = name;
    reset();
}

void CbmDos::reset()
{
    for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
        channels[i] = Channel();
    }
    listening     = false;
    talking       = false;
    activeChannel = 0;
    pendingCmd    = 0;
    nameBuf.clear();

    if (disk != nullptr) {
        // The message a drive shows after power up or reset.
        setStatus(73);
    } else {
        setStatus(74);
    }
}

// Error texts, from VICE's cbmdos_error_messages table. Note that code 0
// carries a leading space, which is where the familiar "00, OK,00,00" spacing
// comes from: the format itself has no space after the comma.
struct CbmDosError {
    uint8_t     code;
    const char* text;
};

static const CbmDosError kErrorMessages[] = {
    {0, " OK"},
    {1, "FILES SCRATCHED"},
    {20, "READ ERROR"},
    {21, "READ ERROR"},
    {22, "READ ERROR"},
    {23, "READ ERROR"},
    {24, "READ ERROR"},
    {25, "WRITE ERROR"},
    {26, "WRITE PROTECT ON"},
    {27, "READ ERROR"},
    {28, "WRITE ERROR"},
    {29, "DISK ID MISMATCH"},
    {30, "SYNTAX ERROR"},
    {31, "SYNTAX ERROR"},
    {32, "SYNTAX ERROR"},
    {33, "SYNTAX ERROR"},
    {34, "SYNTAX ERROR"},
    {60, "WRITE FILE OPEN"},
    {61, "FILE NOT OPEN"},
    {62, "FILE NOT FOUND"},
    {63, "FILE EXISTS"},
    {64, "FILE TYPE MISMATCH"},
    {65, "NO BLOCK"},
    {66, "ILLEGAL TRACK OR SECTOR"},
    {70, "NO CHANNEL"},
    {71, "DIRECTORY ERROR"},
    {72, "DISK FULL"},
    // A real 1541 announces itself here. VICE says "VIRTUAL DRIVE EMULATION"
    // instead; the hardware string is the safer answer for anything that
    // sniffs the DOS version.
    {73, "CBM DOS V2.6 1541"},
    {74, "DRIVE NOT READY"},
    {75, "FORMAT ERROR"},
    {255, nullptr},
};

static const char* errorText(uint8_t code)
{
    for (size_t i = 0; kErrorMessages[i].text != nullptr; i++) {
        if (kErrorMessages[i].code == code) return kErrorMessages[i].text;
    }
    return "UNKNOWN";
}

void CbmDos::setStatus(uint8_t code, uint8_t track, uint8_t sector)
{
    char buf[64];
    // VICE: sprintf(p->buffer, "%02d,%s,%02u,%02u\015", ...)
    snprintf(buf, sizeof(buf), "%02u,%s,%02u,%02u\r", code, errorText(code), track, sector);
    status = buf;
}

// Matches a 16 byte directory name against a pattern that may hold the usual
// wildcards. Both sides are PETSCII, so this compares bytes.
bool CbmDos::nameMatches(const uint8_t* entryName, const std::string& pattern)
{
    size_t p = 0;
    size_t e = 0;
    while (p < pattern.size() && e < SLOT_NAME_LEN) {
        uint8_t pc = static_cast<uint8_t>(pattern[p]);
        if (pc == '*') {
            return true;  // matches everything from here on
        }
        uint8_t ec = entryName[e];
        if (ec == 0xA0) break;  // entry name ended
        if (pc != '?' && pc != ec) return false;
        p++;
        e++;
    }
    if (p < pattern.size() && static_cast<uint8_t>(pattern[p]) == '*') return true;
    // Both have to have ended together for an exact match.
    bool entryEnded   = (e >= SLOT_NAME_LEN) || (entryName[e] == 0xA0);
    bool patternEnded = (p >= pattern.size());
    return entryEnded && patternEnded;
}

bool CbmDos::findEntry(const std::string& pattern, uint8_t typeWanted, uint8_t* track, uint8_t* sector)
{
    if (disk == nullptr) return false;

    unsigned int t       = disk->dirTrack();
    unsigned int s       = disk->dirSector();
    unsigned int visited = 0;
    uint8_t      buf[CBM_SECTOR_SIZE];

    while (visited < 64) {
        if (!disk->readSector(t, s, buf)) return false;
        visited++;

        for (unsigned int slot = 0; slot < 8; slot++) {
            const uint8_t* e    = buf + slot * DIR_ENTRY_SIZE;
            uint8_t        type = e[SLOT_TYPE];
            if ((type & FT_MASK) != typeWanted) continue;
            if (!(type & FT_CLOSED)) continue;  // a splat file has no usable chain
            if (!nameMatches(e + SLOT_NAME, pattern)) continue;

            unsigned int ft = e[SLOT_FIRST_TRACK];
            unsigned int fs = e[SLOT_FIRST_SECTOR];
            if (ft < 1 || ft > disk->tracks()) continue;
            if (fs >= disk->sectorsPerTrack(ft)) continue;

            *track  = static_cast<uint8_t>(ft);
            *sector = static_cast<uint8_t>(fs);
            return true;
        }

        if (buf[0] == 0) break;
        t = buf[0];
        s = buf[1];
    }
    return false;
}

// Loads the sector a channel is pointing at and works out how much of it is
// real data. Returns false once the chain ends or goes wrong.
bool CbmDos::advanceSector(Channel& ch)
{
    if (disk == nullptr) return false;
    if (ch.visited > 4096) return false;  // circular chain
    if (!disk->readSector(ch.track, ch.sector, ch.buf)) return false;
    ch.visited++;

    if (ch.buf[0] == 0) {
        // Last sector: the second byte points at the last byte in use.
        ch.used = (ch.buf[1] >= 2) ? ch.buf[1] + 1u : 2u;
    } else {
        ch.used = CBM_SECTOR_SIZE;
    }
    ch.pos = 2;  // skip the link bytes
    return ch.used > ch.pos;
}

void CbmDos::openFile(uint8_t channel, const std::string& request)
{
    Channel& ch = channels[channel];
    ch          = Channel();

    if (disk == nullptr) {
        setStatus(74);
        return;
    }

    // "0:NAME,P,R" -> drive prefix, name, then type and mode letters.
    std::string body  = request;
    size_t      colon = body.find(':');
    if (colon != std::string::npos && colon <= 2) {
        body = body.substr(colon + 1);
    }

    std::string name  = body;
    uint8_t     type  = FT_PRG;
    size_t      comma = body.find(',');
    if (comma != std::string::npos) {
        name             = body.substr(0, comma);
        std::string rest = body.substr(comma + 1);
        if (!rest.empty()) {
            switch (rest[0]) {
                case 'S':
                case 's':
                    type = FT_SEQ;
                    break;
                case 'U':
                case 'u':
                    type = FT_USR;
                    break;
                case 'P':
                case 'p':
                default:
                    type = FT_PRG;
                    break;
            }
        }
        // A write or append mode needs a writable disk, which we do not have.
        size_t modeComma = rest.find(',');
        if (modeComma != std::string::npos && modeComma + 1 < rest.size()) {
            char mode = rest[modeComma + 1];
            if (mode == 'W' || mode == 'w' || mode == 'A' || mode == 'a') {
                setStatus(26);
                return;
            }
        }
    }

    if (name.empty()) {
        setStatus(33);
        return;
    }

    uint8_t track  = 0;
    uint8_t sector = 0;
    if (!findEntry(name, type, &track, &sector)) {
        setStatus(62);
        return;
    }

    ch.open      = true;
    ch.streaming = true;
    ch.track     = track;
    ch.sector    = sector;
    if (!advanceSector(ch)) {
        ch = Channel();
        setStatus(62);
        return;
    }
    setStatus(0);
}

void CbmDos::openDirectory(uint8_t channel, const std::string& pattern)
{
    Channel& ch = channels[channel];
    ch          = Channel();

    if (disk == nullptr) {
        setStatus(74);
        return;
    }

    uint8_t bam[CBM_SECTOR_SIZE];
    if (!disk->readSector(disk->dirTrack(), 0, bam)) {
        setStatus(74);
        return;
    }

    std::vector<uint8_t>& out = ch.data;

    // Header line, following VICE's vdrive_dir_first_directory.
    out.push_back(0x01);
    out.push_back(0x04);  // load address $0401
    out.push_back(0x01);
    out.push_back(0x01);  // line link
    out.push_back(0x00);
    out.push_back(0x00);  // drive number, always 0 on a 1541
    out.push_back(0x12);  // RVS ON
    out.push_back('"');
    for (int i = 0; i < 16; i++) {
        uint8_t c = bam[0x90 + i];
        out.push_back(c == 0xA0 ? ' ' : c);
    }
    out.push_back('"');
    out.push_back(' ');
    // Five bytes covering the id, the separator and the dos type.
    for (int i = 0; i < 5; i++) {
        uint8_t c = bam[0xA2 + i];
        out.push_back(c == 0xA0 ? ' ' : c);
    }
    out.push_back(0x00);

    // One line per matching entry. Each is exactly 32 bytes with the quote,
    // the type and the flags at fixed columns, as vdrive_dir_next_directory
    // builds them.
    unsigned int t       = disk->dirTrack();
    unsigned int s       = disk->dirSector();
    unsigned int visited = 0;
    uint8_t      buf[CBM_SECTOR_SIZE];

    while (visited < 64) {
        if (!disk->readSector(t, s, buf)) break;
        visited++;

        for (unsigned int slot = 0; slot < 8; slot++) {
            const uint8_t* e    = buf + slot * DIR_ENTRY_SIZE;
            uint8_t        type = e[SLOT_TYPE];
            if (type == 0) continue;  // never used, or scratched
            if (!pattern.empty() && !nameMatches(e + SLOT_NAME, pattern)) continue;

            uint16_t blocks = static_cast<uint16_t>(e[SLOT_NR_BLOCKS] | (e[SLOT_NR_BLOCKS + 1] << 8));

            uint8_t line[DIR_ENTRY_SIZE];
            line[0] = 0x01;
            line[1] = 0x01;
            line[2] = static_cast<uint8_t>(blocks & 0xff);
            line[3] = static_cast<uint8_t>(blocks >> 8);
            memset(line + 4, ' ', 27);
            line[31] = 0x00;

            // BASIC prints the block count as the line number, so the drive
            // pads the rest of the column by hand.
            uint8_t* l = line + 4;
            if (blocks < 10) l++;
            if (blocks < 100) l++;
            l++;

            *l++        = '"';
            int nameLen = 0;
            for (int i = 0; i < SLOT_NAME_LEN; i++) {
                uint8_t c = e[SLOT_NAME + i];
                l[i]      = (c == 0xA0) ? ' ' : c;
            }
            while (nameLen < SLOT_NAME_LEN && e[SLOT_NAME + nameLen] != 0xA0) {
                nameLen++;
            }
            l[nameLen] = '"';

            l[17]          = (type & FT_CLOSED) ? ' ' : '*';
            const char* tn = typeName(type);
            l[18]          = static_cast<uint8_t>(tn[0]);
            l[19]          = static_cast<uint8_t>(tn[1]);
            l[20]          = static_cast<uint8_t>(tn[2]);
            l[21]          = (type & FT_LOCKED) ? '<' : ' ';

            for (unsigned int i = 0; i < DIR_ENTRY_SIZE; i++) {
                out.push_back(line[i]);
            }
        }

        if (buf[0] == 0) break;
        t = buf[0];
        s = buf[1];
    }

    // Trailer: free blocks from the per track counts in the BAM.
    unsigned int free = 0;
    for (unsigned int track = 1; track <= disk->tracks() && track <= 35; track++) {
        if (track == disk->dirTrack()) continue;
        free += bam[4 + (track - 1) * 4];
    }
    uint8_t trailer[DIR_ENTRY_SIZE];
    trailer[0] = 0x01;
    trailer[1] = 0x01;
    trailer[2] = static_cast<uint8_t>(free & 0xff);
    trailer[3] = static_cast<uint8_t>(free >> 8);
    memcpy(trailer + 4, "BLOCKS FREE.", 12);
    memset(trailer + 16, ' ', 15);
    trailer[31] = 0x00;
    for (unsigned int i = 0; i < DIR_ENTRY_SIZE; i++) {
        out.push_back(trailer[i]);
    }

    // End of the BASIC program.
    out.push_back(0x00);
    out.push_back(0x00);

    ch.open      = true;
    ch.streaming = false;
    ch.dataPos   = 0;
    setStatus(0);
}

void CbmDos::executeCommand(const std::string& command)
{
    if (command.empty()) return;

    switch (command[0]) {
        case 'I':
        case 'i':
            // Initialise: re-read the disk. Nothing cached, so just report OK.
            setStatus(0);
            break;
        case 'S':
        case 's':
        case 'N':
        case 'n':
        case 'R':
        case 'r':
        case 'C':
        case 'c':
            // Scratch, new, rename and copy all modify the disk.
            setStatus(26);
            break;
        default:
            setStatus(31);
            break;
    }
}

void CbmDos::listen(uint8_t secondary)
{
    listening     = true;
    talking       = false;
    activeChannel = secondary & IEC_SEC_MASK;
    pendingCmd    = secondary & 0xF0;
    nameBuf.clear();

    if (pendingCmd == IEC_SEC_CLOSE) {
        channels[activeChannel] = Channel();
    }
}

void CbmDos::talk(uint8_t secondary)
{
    talking       = true;
    listening     = false;
    activeChannel = secondary & IEC_SEC_MASK;

    if (activeChannel == CMD_CHANNEL) {
        // Reading the command channel hands back the current status, and
        // clears it to OK the way a real drive does.
        Channel& ch  = channels[CMD_CHANNEL];
        ch           = Channel();
        ch.open      = true;
        ch.streaming = false;
        for (size_t i = 0; i < status.size(); i++) {
            ch.data.push_back(static_cast<uint8_t>(status[i]));
        }
        setStatus(0);
    }
}

void CbmDos::unlisten()
{
    if (!listening) return;
    listening = false;

    if (pendingCmd == IEC_SEC_OPEN) {
        if (activeChannel == CMD_CHANNEL) {
            executeCommand(nameBuf);
        } else if (!nameBuf.empty() && nameBuf[0] == '$') {
            openDirectory(activeChannel, nameBuf.substr(1));
        } else {
            openFile(activeChannel, nameBuf);
        }
    } else if (activeChannel == CMD_CHANNEL) {
        executeCommand(nameBuf);
    }

    pendingCmd = 0;
    nameBuf.clear();
}

void CbmDos::untalk()
{
    talking = false;
}

bool CbmDos::write(uint8_t value)
{
    if (disk == nullptr) return false;
    if (!listening) return false;

    if (pendingCmd == IEC_SEC_OPEN || activeChannel == CMD_CHANNEL) {
        if (nameBuf.size() < 256) {
            nameBuf.push_back(static_cast<char>(value));
        }
        return true;
    }

    // Writing to a data channel would change the disk.
    setStatus(26);
    return false;
}

bool CbmDos::read(uint8_t* value, bool* eoi)
{
    if (disk == nullptr) return false;

    Channel& ch = channels[activeChannel];
    if (!ch.open) {
        *value = 0;
        *eoi   = true;
        return false;
    }

    if (!ch.streaming) {
        if (ch.dataPos >= ch.data.size()) {
            *value = 0;
            *eoi   = true;
            return false;
        }
        *value = ch.data[ch.dataPos++];
        *eoi   = (ch.dataPos >= ch.data.size());
        return true;
    }

    if (ch.eof) {
        *value = 0;
        *eoi   = true;
        return false;
    }

    *value = ch.buf[ch.pos++];

    // Work out whether that was the last byte of the file.
    bool lastInSector = (ch.pos >= ch.used);
    bool lastSector   = (ch.buf[0] == 0);
    if (lastInSector && lastSector) {
        ch.eof = true;
        *eoi   = true;
        return true;
    }
    if (lastInSector) {
        ch.track  = ch.buf[0];
        ch.sector = ch.buf[1];
        if (!advanceSector(ch)) {
            ch.eof = true;
            *eoi   = true;
            return true;
        }
    }
    *eoi = false;
    return true;
}
