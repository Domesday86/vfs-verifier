/************************************************************************

    bad_sectors.h

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

#ifndef BAD_SECTORS_H
#define BAD_SECTORS_H

#include <set>
#include <fstream>
#include <string>
#include <cstdint>

class BadSectors
{
public:
    BadSectors();

    bool open(const std::string& filename);
    void close();

    bool isSectorBad(uint32_t sector) const;

    // The complete set of bad EFM sectors, in ascending order
    const std::set<uint32_t>& sectors() const { return m_badSectors; }
    size_t count() const { return m_badSectors.size(); }

    // Number of duplicate and malformed lines seen when the map was read
    uint32_t duplicateLines() const { return m_duplicateLines; }
    uint32_t malformedLines() const { return m_malformedLines; }

private:
    std::set<uint32_t> m_badSectors;
    std::ifstream m_file;
    bool m_isOpen;
    uint32_t m_duplicateLines;
    uint32_t m_malformedLines;
};

#endif // BAD_SECTORS_H
