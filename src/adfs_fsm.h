/************************************************************************

    adfs_fsm.h

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

    uint32_t size() const { return m_freeSpaceMap.size(); }
    uint32_t freeSpace(uint32_t index) const { return m_freeSpaceMap.at(index); }
    uint32_t freeSpaceLength(uint32_t index) const { return m_freeSpaceLengths.at(index); }

private:
    std::vector<uint32_t> m_freeSpaceMap;
    std::vector<uint32_t> m_freeSpaceLengths;
    std::string m_RiscOsDiscName;
    uint16_t m_discId;
    uint32_t m_numberOfSectors;
    uint8_t m_lengthOfFreeSpaceMap;

    void showStarFree();
    void showStarMap();
    void show();
};

#endif // ADFS_FSM_H