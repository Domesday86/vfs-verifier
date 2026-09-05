/************************************************************************

    adfs_fsm.cpp

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

#include "adfs_fsm.h"
#include "logging.h"

AdfsFsm::AdfsFsm(const std::vector<uint8_t>& sectors) :
    m_isValid(false),
    m_discId(0),
    m_numberOfSectors(0),
    m_bootOption(0),
    m_lengthOfFreeSpaceMap(0)
{
    // Expecting 2 sectors of data 2*256 bytes
    if (sectors.size() != 512) {
        LOG_CRITICAL("AdfsFsm::AdfsFsm() - Incorrect number of bytes {}", sectors.size());
        return;
    }

    m_freeSpaceLengths.clear();
    m_freeSpaceMap.clear();

    // The pointer to the end of the free space map is 0xFE (sector 1)
    m_lengthOfFreeSpaceMap = get8(sectors, 0x1FE);

    // The free space map is from 0x00 to 0xF5 inclusive (sector 0)
    // Each free space is 3 bytes. (Maximum of 82 entries)
    // The length of each free space is from 0x00 to 0xF5 inclusive (sector 1)
    // Each free space length is 3 bytes.
    if (m_lengthOfFreeSpaceMap > 0xF6) {
        LOG_WARN("AdfsFsm::AdfsFsm() - Free space map end pointer {} is out of range - "
                 "the free space map may be corrupt", m_lengthOfFreeSpaceMap);
        m_lengthOfFreeSpaceMap = 0xF6;
    }

    for (int i = 0; i < m_lengthOfFreeSpaceMap; i += 3) {
        m_freeSpaceMap.push_back(get24(sectors, i));
        m_freeSpaceLengths.push_back(get24(sectors, 0x100 + i));
    }

    // Interleave the odd and even characters to get the RISC OS disc name.
    // Note: the exact extent of the name field is not verified by this tool -
    // the discs seen so far leave it blank.
    m_RiscOsDiscName.clear();
    for (int i = 0xF6; i <= 0xFB; i++) {
        m_RiscOsDiscName += static_cast<char>(sectors.at(i));
        if (i != 0xFB) { // Skip last sector1 char
            m_RiscOsDiscName += static_cast<char>(sectors.at(0x100 + i));
        }
    }

    // Trim padding so that a blank name reads as blank rather than as control characters
    while (!m_RiscOsDiscName.empty()) {
        const unsigned char c = static_cast<unsigned char>(m_RiscOsDiscName.back());
        if (c == 0x00 || c == 0x0D || c == ' ') m_RiscOsDiscName.pop_back();
        else break;
    }

    // The total number of sectors is 0xFC to 0xFE inclusive (sector 0)
    m_numberOfSectors = get24(sectors, 0x0FC);

    // The disc ID is 0xFB to 0xFC inclusive (sector 1)
    m_discId = get16(sectors, 0x1FB);

    // The boot option is 0xFD (sector 1)
    m_bootOption = get8(sectors, 0x1FD);

    m_isValid = true;
    show();
}

uint32_t AdfsFsm::freeSectors() const
{
    uint32_t total = 0;
    for (size_t i = 0; i < m_freeSpaceLengths.size(); ++i) {
        total += m_freeSpaceLengths.at(i);
    }
    return total;
}

uint32_t AdfsFsm::usedSectors() const
{
    const uint32_t free = freeSectors();
    if (free > m_numberOfSectors) return 0;
    return m_numberOfSectors - free;
}

bool AdfsFsm::isFree(uint32_t adfsSector) const
{
    for (size_t i = 0; i < m_freeSpaceMap.size(); ++i) {
        const uint32_t start = m_freeSpaceMap.at(i);
        const uint32_t length = m_freeSpaceLengths.at(i);
        if (adfsSector >= start && adfsSector < start + length) return true;
    }
    return false;
}

void AdfsFsm::showStarFree()
{
    // Show the number of free and used sectors, as the Acorn *FREE command does
    const uint32_t free = freeSectors();
    const uint32_t used = usedSectors();

    LOG_DEBUG("*FREE");
    LOG_DEBUG(" {}={} Bytes Free", toString24bits(free), static_cast<uint64_t>(free) * 256);
    LOG_DEBUG(" {}={} Bytes Used", toString24bits(used), static_cast<uint64_t>(used) * 256);
}

void AdfsFsm::showStarMap()
{
    LOG_DEBUG("*MAP");
    LOG_DEBUG("  Address   :  Length");
    for (size_t i = 0; i < m_freeSpaceMap.size(); ++i) {
        LOG_DEBUG(" {} : {}", toString24bits(m_freeSpaceMap.at(i)), toString24bits(m_freeSpaceLengths.at(i)));
    }
}

void AdfsFsm::show()
{
    showStarFree();
    showStarMap();
}
