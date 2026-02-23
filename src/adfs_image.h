/************************************************************************

    adfs_image.h

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

#ifndef ADFS_IMAGE_H
#define ADFS_IMAGE_H

#include <string>
#include <vector>
#include <fstream>
#include <cstdint>

class AdfsImage
{
public:
    AdfsImage();

    bool open(const std::string& filename);
    void close();
    std::vector<uint8_t> readSectors(uint64_t sector, uint64_t count, bool verifyChecksum);
    uint32_t adfsSectorToEfmSector(uint32_t adfsSector);
    bool isValid() const;

private:
    bool m_isValid;
    std::ifstream m_file;
    uint64_t m_sector0Position;

    void findSector0();
    uint16_t calculateChecksum(const std::vector<uint8_t> &buffer);
};

#endif // ADFS_IMAGE_H