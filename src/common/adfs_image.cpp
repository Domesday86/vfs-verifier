/************************************************************************

    adfs_image.cpp

    vfs-tools - Acorn VFS (Domesday) image tools
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

#include "adfs_image.h"
#include "logging.h"
#include <algorithm>

// The maximum number of "Hugo" signature candidates that will be validated
// before giving up and falling back to the first one found
static const size_t MAX_SIGNATURE_CANDIDATES = 4096;

// A directory occupies five logical sectors
static const size_t DIRECTORY_BYTES = 5 * ADFS_SECTOR_SIZE;

AdfsImage::AdfsImage() :
    m_isValid(false),
    m_sector0Position(0),
    m_imageSize(0),
    m_lastReadComplete(true),
    m_lastChecksumOk(true),
    m_locatedByValidation(false)
{}

bool AdfsImage::open(const std::string& filename)
{
    // Open the input file
    auto reader = std::make_unique<FileImageReader>();
    if (!reader->open(filename)) {
        LOG_CRITICAL("AdfsImage::open() - Could not open file {} for reading", filename);
        return false;
    }

    return attach(std::move(reader));
}

bool AdfsImage::attach(std::unique_ptr<ImageReader> reader)
{
    m_reader = std::move(reader);
    if (!m_reader) {
        LOG_CRITICAL("AdfsImage::attach() - No image reader was supplied");
        return false;
    }

    // Determine the size of the image
    m_imageSize = m_reader->size();
    LOG_DEBUG("AdfsImage::attach() - Opened {} for reading ({} bytes)",
        m_reader->description(), m_imageSize);

    m_isValid = true;

    // Locate sector 0 position
    findSector0();

    return m_isValid;
}

void AdfsImage::close()
{
    if (m_reader) {
        LOG_DEBUG("AdfsImage::close() - Closed {}", m_reader->description());
        m_reader.reset();
    }
}

uint32_t AdfsImage::sectorsInImage() const
{
    if (m_imageSize <= m_sector0Position) return 0;
    return static_cast<uint32_t>((m_imageSize - m_sector0Position) / ADFS_SECTOR_SIZE);
}

// Read length bytes from an absolute offset within the image. The returned
// vector is truncated to the number of bytes actually read, and the stream is
// always left in a good state so that a short read cannot poison later reads.
std::vector<uint8_t> AdfsImage::readRaw(uint64_t offset, size_t length)
{
    std::vector<uint8_t> buffer;

    if (!m_reader) {
        LOG_CRITICAL("AdfsImage::readRaw() - No image is open");
        return buffer;
    }

    buffer.resize(length);
    const size_t bytesRead = m_reader->read(offset, buffer.data(), length);

    if (bytesRead != length) {
        // Short read - truncate the buffer to what was actually available
        buffer.resize(bytesRead);
    }

    return buffer;
}

std::vector<uint8_t> AdfsImage::readSectors(uint64_t sector, uint64_t count, bool verifyChecksums)
{
    m_lastReadComplete = true;
    m_lastChecksumOk = true;

    const uint64_t offset = m_sector0Position + (sector * ADFS_SECTOR_SIZE);
    const size_t bytesToRead = static_cast<size_t>(count * ADFS_SECTOR_SIZE);

    std::vector<uint8_t> buffer = readRaw(offset, bytesToRead);

    if (buffer.size() != bytesToRead) {
        m_lastReadComplete = false;
        LOG_WARN("AdfsImage::readSectors() - Short read at ADFS sector {} - wanted {} bytes, got {} "
                 "(image ends {} bytes into the request)",
            sector, bytesToRead, buffer.size(), buffer.size());
    }

    if (verifyChecksums) {
        // Verify the checksums of the read sectors (whole sectors only)
        const uint64_t completeSectors = buffer.size() / ADFS_SECTOR_SIZE;
        for (uint64_t i = 0; i < completeSectors; ++i) {
            std::vector<uint8_t> sectorData(
                buffer.begin() + static_cast<long>(i * ADFS_SECTOR_SIZE),
                buffer.begin() + static_cast<long>((i + 1) * ADFS_SECTOR_SIZE));
            const uint16_t storedChecksum = sectorData.at(ADFS_SECTOR_SIZE - 1);
            const uint16_t calculatedChecksum = calculateChecksum(sectorData);

            if (storedChecksum != calculatedChecksum) {
                m_lastChecksumOk = false;
                LOG_WARN("AdfsImage::readSectors() - Checksum failed for sector {} - stored {} calculated {}",
                    sector + i, storedChecksum, calculatedChecksum);
            }
        }
    }

    return buffer;
}

bool AdfsImage::isValid() const
{
    return m_isValid;
}

// The ADFS sector checksum: an 8-bit sum with carry taken over bytes 254..0,
// seeded with 255. The result is the value stored in byte 255 of the sector.
uint16_t AdfsImage::calculateChecksum(const std::vector<uint8_t> &buffer)
{
    uint16_t sum = 255;
    for (int a = 254; a >= 0; --a) {
        if (sum > 255) {
            sum = (sum & 0xff) + 1;
        }
        sum += static_cast<uint16_t>(buffer[a]);
    }

    return sum & 0xff;
}

// Collect the offsets of every "Hugo" directory identifier in the image
std::vector<uint64_t> AdfsImage::findSignatureCandidates()
{
    std::vector<uint64_t> candidates;

    if (!m_reader) return candidates;

    const size_t chunkSize = 1024 * 1024;
    const size_t overlap = 3;   // so a signature spanning a chunk boundary is still found
    std::vector<uint8_t> chunk(chunkSize);
    uint64_t chunkStart = 0;

    while (chunkStart < m_imageSize && candidates.size() < MAX_SIGNATURE_CANDIDATES) {
        const size_t bytesRead = m_reader->read(chunkStart, chunk.data(), chunkSize);
        if (bytesRead == 0) break;

        for (size_t i = 0; i + 4 <= bytesRead; ++i) {
            if (chunk[i] == 'H' && chunk[i+1] == 'u' && chunk[i+2] == 'g' && chunk[i+3] == 'o') {
                candidates.push_back(chunkStart + i);
                if (candidates.size() >= MAX_SIGNATURE_CANDIDATES) break;
            }
        }

        if (bytesRead < chunkSize) break;
        chunkStart += bytesRead - overlap;
    }

    return candidates;
}

// Check whether a candidate sector 0 position yields a plausible filesystem:
// the two free space map sectors must checksum correctly, and the root
// directory header and footer must agree.
bool AdfsImage::validateCandidate(uint64_t candidate, bool &checksumsOk, bool &directoryOk)
{
    checksumsOk = false;
    directoryOk = false;

    // Free space map: logical sectors 0 and 1
    std::vector<uint8_t> fsm = readRaw(candidate, 2 * ADFS_SECTOR_SIZE);
    if (fsm.size() == 2 * ADFS_SECTOR_SIZE) {
        std::vector<uint8_t> sector0(fsm.begin(), fsm.begin() + ADFS_SECTOR_SIZE);
        std::vector<uint8_t> sector1(fsm.begin() + ADFS_SECTOR_SIZE, fsm.end());
        checksumsOk = (sector0.at(255) == calculateChecksum(sector0)) &&
                      (sector1.at(255) == calculateChecksum(sector1));
    }

    // Root directory: five logical sectors starting at logical sector 2. A
    // directory is "broken" if the master sequence number and identifier in the
    // header do not match those in the footer.
    std::vector<uint8_t> dir = readRaw(candidate + (2 * ADFS_SECTOR_SIZE), DIRECTORY_BYTES);
    if (dir.size() == DIRECTORY_BYTES) {
        const bool identifierOk = (dir.at(0x001) == 'H' && dir.at(0x002) == 'u' &&
                                   dir.at(0x003) == 'g' && dir.at(0x004) == 'o');
        const bool footerMatches = (dir.at(0x000) == dir.at(0x4FA)) &&
                                   (dir.at(0x001) == dir.at(0x4FB)) &&
                                   (dir.at(0x002) == dir.at(0x4FC)) &&
                                   (dir.at(0x003) == dir.at(0x4FD)) &&
                                   (dir.at(0x004) == dir.at(0x4FE));
        directoryOk = identifierOk && footerMatches;
    }

    return checksumsOk && directoryOk;
}

void AdfsImage::findSector0()
{
    // The root directory is logical sector 2 and its identifier "Hugo" sits at
    // offset 1 within it, so a signature at file offset p implies that logical
    // sector 0 begins at p - 1 - 512.
    const std::vector<uint64_t> candidates = findSignatureCandidates();

    if (candidates.empty()) {
        LOG_CRITICAL("AdfsImage::findSector0() - Could not find ADFS signature Hugo in file - "
                     "input file is not a valid ADFS image");
        m_isValid = false;
        return;
    }

    LOG_DEBUG("AdfsImage::findSector0() - Found {} \"Hugo\" signature(s) in the image", candidates.size());

    uint64_t firstUsable = 0;
    bool haveFirstUsable = false;

    for (size_t i = 0; i < candidates.size(); ++i) {
        const uint64_t signature = candidates.at(i);
        if (signature < 1 + (2 * ADFS_SECTOR_SIZE)) continue;    // too close to the start of the file
        const uint64_t candidate = signature - 1 - (2 * ADFS_SECTOR_SIZE);

        if (!haveFirstUsable) {
            firstUsable = candidate;
            haveFirstUsable = true;
        }

        bool checksumsOk = false;
        bool directoryOk = false;
        if (validateCandidate(candidate, checksumsOk, directoryOk)) {
            m_sector0Position = candidate;
            m_locatedByValidation = true;
            LOG_DEBUG("AdfsImage::findSector0() - ADFS sector 0 at offset 0x{:X} "
                      "(root directory 0x{:X}); free space map checksums and root directory "
                      "header/footer both validate",
                m_sector0Position, m_sector0Position + (2 * ADFS_SECTOR_SIZE));
            return;
        }

        LOG_DEBUG("AdfsImage::findSector0() - Rejected \"Hugo\" at 0x{:X}: "
                  "free space map checksums {}, root directory header/footer {}",
            signature, checksumsOk ? "OK" : "FAILED", directoryOk ? "OK" : "MISMATCHED");
    }

    if (!haveFirstUsable) {
        LOG_CRITICAL("AdfsImage::findSector0() - Found a \"Hugo\" signature but it is too close to "
                     "the start of the file to be a root directory - input file is not a valid ADFS image");
        m_isValid = false;
        return;
    }

    // Nothing validated; fall back to the first candidate so that a badly
    // damaged image can still be inspected, but say so loudly.
    m_sector0Position = firstUsable;
    m_locatedByValidation = false;
    LOG_WARN("AdfsImage::findSector0() - No \"Hugo\" signature passed validation; falling back to the "
             "first one found (ADFS sector 0 at offset 0x{:X}). The filesystem structure below may be "
             "unreliable.", m_sector0Position);
}

uint32_t AdfsImage::adfsSectorToEfmSector(uint32_t adfsSector) const
{
    // ADFS sectors are 256 bytes, EFM sectors are 2048 bytes
    // EFM sector 0 is at the beginning of the file, so we
    // have to offset by the sector 0 position
    return static_cast<uint32_t>(
        ((static_cast<uint64_t>(adfsSector) * ADFS_SECTOR_SIZE) + m_sector0Position) / EFM_SECTOR_SIZE);
}
