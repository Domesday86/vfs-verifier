/************************************************************************

    adfs_fsm.cpp

    vfs-verifier - Acorn VFS (Domesday) image verifier
    Copyright (C) 2025 Simon Inns

    This file is part of ld-decode-tools.

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

AdfsFsm::AdfsFsm(const std::vector<uint8_t>& sectors)
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
    for (int i = 0; i < m_lengthOfFreeSpaceMap; i += 3) {
        m_freeSpaceMap.push_back(get24(sectors, i));
        m_freeSpaceLengths.push_back(get24(sectors, 0x100 + i));
    }

    // Interleave the odd and even characters to get the RISC OS disc name
    m_RiscOsDiscName.clear();
    for (int i = 0xF6; i <= 0xFB; i++) {
        m_RiscOsDiscName += static_cast<char>(sectors.at(i));
        if (i != 0xFB) { // Skip last sector1 char
            m_RiscOsDiscName += static_cast<char>(sectors.at(0x100 + i));
        }
    }

    // The total number of sectors is 0xFC to 0xFE inclusive (sector 0)
    m_numberOfSectors = get24(sectors, 0x0FC);

    // The disc ID is 0xFB to 0xFC inclusive (sector 1)
    m_discId = get16(sectors, 0x1FB);

    show();
}

void AdfsFsm::showStarFree()
{
    // Calculate and show the number of used and free sectors
    uint32_t usedSectors = 0;
    for (size_t i = 0; i < m_freeSpaceLengths.size(); ++i) {
        usedSectors += m_freeSpaceLengths.at(i);
    }

    uint32_t freeSectors = m_numberOfSectors - usedSectors;

    LOG_DEBUG("*FREE");
    LOG_DEBUG(" {}={} Bytes Free", toString24bits(usedSectors), usedSectors * 256);
    LOG_DEBUG(" {}={} Bytes Used", toString24bits(freeSectors), freeSectors * 256);
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

