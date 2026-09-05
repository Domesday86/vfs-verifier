/************************************************************************

    adfs_image.h

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

#ifndef ADFS_IMAGE_H
#define ADFS_IMAGE_H

#include <string>
#include <vector>
#include <fstream>
#include <cstdint>

// Sector sizes used throughout the verifier
static const uint32_t ADFS_SECTOR_SIZE = 256;
static const uint32_t EFM_SECTOR_SIZE = 2048;

class AdfsImage
{
public:
    AdfsImage();

    bool open(const std::string& filename);
    void close();
    std::vector<uint8_t> readSectors(uint64_t sector, uint64_t count, bool verifyChecksum);
    uint32_t adfsSectorToEfmSector(uint32_t adfsSector) const;
    bool isValid() const;

    // Image geometry
    uint64_t sector0Position() const { return m_sector0Position; }
    uint64_t imageSize() const { return m_imageSize; }
    uint32_t sectorsInImage() const;

    // Status of the most recent readSectors() call
    bool lastReadComplete() const { return m_lastReadComplete; }
    bool lastChecksumOk() const { return m_lastChecksumOk; }

    // True if the filesystem located by open() passed full validation
    bool locatedByValidation() const { return m_locatedByValidation; }

private:
    bool m_isValid;
    std::ifstream m_file;
    uint64_t m_sector0Position;
    uint64_t m_imageSize;
    bool m_lastReadComplete;
    bool m_lastChecksumOk;
    bool m_locatedByValidation;

    void findSector0();
    std::vector<uint64_t> findSignatureCandidates();
    bool validateCandidate(uint64_t candidate, bool &checksumsOk, bool &directoryOk);
    std::vector<uint8_t> readRaw(uint64_t offset, size_t length);
    static uint16_t calculateChecksum(const std::vector<uint8_t> &buffer);
};

#endif // ADFS_IMAGE_H
