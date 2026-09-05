/************************************************************************

    adfs_content.h

    vfs-verifier - Acorn VFS (Domesday) image verifier
    Copyright (C) 2025-2026 Simon Inns

    This application is free software: you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

************************************************************************/

#ifndef ADFS_CONTENT_H
#define ADFS_CONTENT_H

#include <vector>
#include <cstdint>

#include "adfs_image.h"
#include "adfs_fsm.h"
#include "bad_sectors.h"

// ADFS carries no checksum for file data, so the content of an object cannot be
// verified against anything. What can be measured is whether an object holds any
// data at all: a sector consisting entirely of one fill byte carries nothing,
// whether because the disc was written that way or because the decode silently
// substituted padding. Fill that the bad sector map does not account for is the
// interesting case - the decoder believed it recovered those sectors.
struct ObjectContent {
    uint32_t sectorsExamined = 0;
    uint32_t fillSectors = 0;
    uint32_t fillSectorsNotFlagged = 0;
    uint32_t longestFillRun = 0;
    uint32_t longestFillRunStart = 0;
    uint8_t dominantFillByte = 0;
    bool entirelyFill = false;
    bool shortRead = false;
};

// Where the transition between data and fill actually occurs, relative to the
// boundary the free space map declares. A non-zero offset means the image
// content and the filesystem map disagree about where free space begins or
// ends, which is what a slipped or duplicated sector run looks like.
struct BoundaryCheck {
    uint32_t extentStart = 0;
    uint32_t extentLength = 0;
    int32_t fillBeginsOffset = OFFSET_NOT_FOUND;
    int32_t dataResumesOffset = OFFSET_NOT_FOUND;
    bool aligned = false;

    static const int32_t OFFSET_NOT_FOUND = -1;   // no transition within the search window
    static const int32_t OFFSET_PAST_IMAGE = -2;  // the boundary lies beyond the image
};

class AdfsContentCheck
{
public:
    AdfsContentCheck(AdfsImage &image, const BadSectors &badSectors);

    // Measure how much of an object consists of fill sectors
    ObjectContent checkObject(uint32_t startSector, uint32_t sectorLength);

    // Check every free space extent against the content actually present
    std::vector<BoundaryCheck> checkFreeSpaceBoundaries(const AdfsFsm &fsm);

private:
    AdfsImage &m_image;
    const BadSectors &m_badSectors;

    static bool isFillBlock(const std::vector<uint8_t> &data, size_t offset, uint8_t &fillByte);
    bool isFillSector(uint32_t adfsSector, bool &readable);
};

#endif // ADFS_CONTENT_H
