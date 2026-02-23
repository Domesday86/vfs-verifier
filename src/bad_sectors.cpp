/************************************************************************

    bad_sectors.cpp

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

#include "bad_sectors.h"
#include "logging.h"
#include <algorithm>

BadSectors::BadSectors() :
    m_isOpen(false)
{}

bool BadSectors::open(const std::string& filename)
{
    m_file.open(filename, std::ios::in);
    if (!m_file.is_open()) {
        LOG_CRITICAL("BadSectors::open() - Could not open file {} for reading", filename);
        return false;
    }
    LOG_DEBUG("BadSectors::open() - Opened file {} for reading", filename);

    // Read the bad sector list (this is a text file with one sector number per line)
    std::string line;
    while (std::getline(m_file, line)) {
        if (!line.empty()) {
            uint32_t sector = std::stoul(line);
            m_badSectors.push_back(sector);
        }
    }

    LOG_DEBUG("BadSectors::open() - Read {} bad sectors from file {}", m_badSectors.size(), filename);

    m_isOpen = true;
    return true;
}

void BadSectors::close()
{
    if (m_file.is_open()) {
        m_file.close();
    }
    m_isOpen = false;
}

bool BadSectors::isSectorBad(uint32_t sector) const
{
    return std::find(m_badSectors.begin(), m_badSectors.end(), sector) != m_badSectors.end();
}
