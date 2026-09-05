/************************************************************************

    adfs_fsm.h

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

#ifndef ADFS_FSM_H
#define ADFS_FSM_H

#include <vector>
#include <string>
#include <cstdint>

#include "getbits.h"

class AdfsFsm
{
public:
    AdfsFsm(const std::vector<uint8_t>& sectors);

    bool isValid() const { return m_isValid; }

    uint32_t size() const { return static_cast<uint32_t>(m_freeSpaceMap.size()); }
    uint32_t freeSpace(uint32_t index) const { return m_freeSpaceMap.at(index); }
    uint32_t freeSpaceLength(uint32_t index) const { return m_freeSpaceLengths.at(index); }

    // Disc geometry and identification
    uint32_t numberOfSectors() const { return m_numberOfSectors; }
    uint16_t discId() const { return m_discId; }
    uint8_t bootOption() const { return m_bootOption; }
    std::string discName() const { return m_RiscOsDiscName; }

    // Allocation totals, in ADFS sectors
    uint32_t freeSectors() const;
    uint32_t usedSectors() const;

    // True if the given ADFS sector lies within one of the free space extents
    bool isFree(uint32_t adfsSector) const;

private:
    bool m_isValid;
    std::vector<uint32_t> m_freeSpaceMap;
    std::vector<uint32_t> m_freeSpaceLengths;
    std::string m_RiscOsDiscName;
    uint16_t m_discId;
    uint32_t m_numberOfSectors;
    uint8_t m_bootOption;
    uint8_t m_lengthOfFreeSpaceMap;

    void showStarFree();
    void showStarMap();
    void show();
};

#endif // ADFS_FSM_H
