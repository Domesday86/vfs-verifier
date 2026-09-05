/************************************************************************

    bad_sectors.cpp

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

#include "bad_sectors.h"
#include "logging.h"
#include <cctype>
#include <stdexcept>

BadSectors::BadSectors() :
    m_isOpen(false),
    m_duplicateLines(0),
    m_malformedLines(0)
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
    uint32_t lineNumber = 0;
    while (std::getline(m_file, line)) {
        ++lineNumber;

        // Strip whitespace (including any trailing CR from a DOS line ending)
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        size_t last = line.find_last_not_of(" \t\r\n");
        line = line.substr(first, last - first + 1);

        // The line must be a plain unsigned decimal number
        bool numeric = true;
        for (char c : line) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                numeric = false;
                break;
            }
        }

        if (!numeric) {
            LOG_WARN("BadSectors::open() - Ignoring malformed line {} of {}: \"{}\"",
                lineNumber, filename, line);
            ++m_malformedLines;
            continue;
        }

        uint32_t sector = 0;
        try {
            unsigned long value = std::stoul(line);
            if (value > UINT32_MAX) throw std::out_of_range("sector number too large");
            sector = static_cast<uint32_t>(value);
        } catch (const std::exception &) {
            LOG_WARN("BadSectors::open() - Ignoring out-of-range sector number on line {} of {}: \"{}\"",
                lineNumber, filename, line);
            ++m_malformedLines;
            continue;
        }

        if (!m_badSectors.insert(sector).second) {
            ++m_duplicateLines;
        }
    }

    LOG_DEBUG("BadSectors::open() - Read {} distinct bad sectors from file {}",
        m_badSectors.size(), filename);
    if (m_duplicateLines > 0) {
        LOG_WARN("BadSectors::open() - Bad sector map contains {} duplicate entries", m_duplicateLines);
    }

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
    return m_badSectors.find(sector) != m_badSectors.end();
}
