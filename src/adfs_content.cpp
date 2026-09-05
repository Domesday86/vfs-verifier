/************************************************************************

    adfs_content.cpp

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

#include "adfs_content.h"
#include "logging.h"
#include <algorithm>
#include <map>

// Sectors read per image access when scanning an object
static const uint32_t SCAN_CHUNK_SECTORS = 256;

// How far either side of a declared boundary to look for the data/fill transition
static const uint32_t BOUNDARY_SEARCH_SECTORS = 64;

AdfsContentCheck::AdfsContentCheck(AdfsImage &image, const BadSectors &badSectors) :
    m_image(image),
    m_badSectors(badSectors)
{}

// A sector is treated as fill if every byte is the same, and that byte is one of
// the padding values a decoder or a mastering process would leave behind. Any
// other uniform sector is left alone, so that legitimate data is not written off.
bool AdfsContentCheck::isFillBlock(const std::vector<uint8_t> &data, size_t offset, uint8_t &fillByte)
{
    if (offset + ADFS_SECTOR_SIZE > data.size()) return false;

    const uint8_t first = data.at(offset);
    if (first != 0x00 && first != 0x20 && first != 0xFF) return false;

    for (size_t i = 1; i < ADFS_SECTOR_SIZE; ++i) {
        if (data.at(offset + i) != first) return false;
    }

    fillByte = first;
    return true;
}

bool AdfsContentCheck::isFillSector(uint32_t adfsSector, bool &readable)
{
    std::vector<uint8_t> buffer = m_image.readSectors(adfsSector, 1, false);
    readable = (buffer.size() == ADFS_SECTOR_SIZE);
    if (!readable) return false;

    uint8_t fillByte = 0;
    return isFillBlock(buffer, 0, fillByte);
}

ObjectContent AdfsContentCheck::checkObject(uint32_t startSector, uint32_t sectorLength)
{
    ObjectContent result;
    std::map<uint8_t, uint32_t> fillByteCounts;

    uint32_t currentRun = 0;
    uint32_t currentRunStart = 0;

    for (uint32_t offset = 0; offset < sectorLength; offset += SCAN_CHUNK_SECTORS) {
        const uint32_t wanted = std::min(SCAN_CHUNK_SECTORS, sectorLength - offset);
        const std::vector<uint8_t> buffer = m_image.readSectors(startSector + offset, wanted, false);
        const uint32_t complete = static_cast<uint32_t>(buffer.size() / ADFS_SECTOR_SIZE);

        if (complete < wanted) result.shortRead = true;

        for (uint32_t i = 0; i < complete; ++i) {
            ++result.sectorsExamined;
            const uint32_t adfsSector = startSector + offset + i;

            uint8_t fillByte = 0;
            if (isFillBlock(buffer, static_cast<size_t>(i) * ADFS_SECTOR_SIZE, fillByte)) {
                ++result.fillSectors;
                ++fillByteCounts[fillByte];

                if (!m_badSectors.isSectorBad(m_image.adfsSectorToEfmSector(adfsSector))) {
                    ++result.fillSectorsNotFlagged;
                }

                if (currentRun == 0) currentRunStart = adfsSector;
                ++currentRun;
                if (currentRun > result.longestFillRun) {
                    result.longestFillRun = currentRun;
                    result.longestFillRunStart = currentRunStart;
                }
            } else {
                currentRun = 0;
            }
        }

        if (complete < wanted) break;
    }

    uint32_t best = 0;
    for (const auto &entry : fillByteCounts) {
        if (entry.second > best) {
            best = entry.second;
            result.dominantFillByte = entry.first;
        }
    }

    result.entirelyFill = (result.sectorsExamined > 0 && result.fillSectors == result.sectorsExamined);
    return result;
}

std::vector<BoundaryCheck> AdfsContentCheck::checkFreeSpaceBoundaries(const AdfsFsm &fsm)
{
    std::vector<BoundaryCheck> results;
    const uint32_t imageSectors = m_image.sectorsInImage();

    for (uint32_t i = 0; i < fsm.size(); ++i) {
        BoundaryCheck check;
        check.extentStart = fsm.freeSpace(i);
        check.extentLength = fsm.freeSpaceLength(i);

        const uint32_t extentEnd = check.extentStart + check.extentLength;

        // Where does fill actually begin, relative to the declared start?
        if (check.extentStart >= imageSectors) {
            check.fillBeginsOffset = BoundaryCheck::OFFSET_PAST_IMAGE;
        } else {
            for (uint32_t d = 0; d < BOUNDARY_SEARCH_SECTORS; ++d) {
                if (check.extentStart + d >= imageSectors) { check.fillBeginsOffset = BoundaryCheck::OFFSET_PAST_IMAGE; break; }
                bool readable = false;
                const bool fill = isFillSector(check.extentStart + d, readable);
                if (!readable) { check.fillBeginsOffset = BoundaryCheck::OFFSET_PAST_IMAGE; break; }
                if (fill) { check.fillBeginsOffset = static_cast<int32_t>(d); break; }
            }
        }

        // Where does data actually resume, relative to the declared end?
        if (extentEnd >= imageSectors) {
            check.dataResumesOffset = BoundaryCheck::OFFSET_PAST_IMAGE;
        } else {
            for (uint32_t d = 0; d < BOUNDARY_SEARCH_SECTORS; ++d) {
                if (extentEnd + d >= imageSectors) { check.dataResumesOffset = BoundaryCheck::OFFSET_PAST_IMAGE; break; }
                bool readable = false;
                const bool fill = isFillSector(extentEnd + d, readable);
                if (!readable) { check.dataResumesOffset = BoundaryCheck::OFFSET_PAST_IMAGE; break; }
                if (!fill) { check.dataResumesOffset = static_cast<int32_t>(d); break; }
            }
        }

        // Only a late resumption of data indicates a genuine disagreement: it
        // means allocated content sits further into the image than the map says,
        // which is what a lost or duplicated run of sectors looks like. Data at
        // the START of a free extent is just an erased file that was never
        // overwritten, which is normal, and a boundary that could not be found
        // means the neighbouring region is fill too, which a sparse disc does
        // legitimately.
        check.aligned = (check.dataResumesOffset <= 0);

        results.push_back(check);
    }

    return results;
}
