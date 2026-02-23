/************************************************************************

    adfs_image.cpp

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

#include "adfs_image.h"
#include "logging.h"

AdfsImage::AdfsImage() :
    m_isValid(false),
    m_sector0Position(0)
{}

bool AdfsImage::open(const std::string& filename)
{
    // Open the input file
    m_file.open(filename, std::ios::in | std::ios::binary);
    if (!m_file.is_open()) {
        LOG_CRITICAL("AdfsImage::open() - Could not open file {} for reading", filename);
        return false;
    }
    LOG_DEBUG("AdfsImage::open() - Opened file {} for reading", filename);

    m_isValid = true;

    // Locate sector 0 position
    findSector0();

    return true;
}

void AdfsImage::close()
{
    if (m_file.is_open()) {
        LOG_DEBUG("AdfsImage::close() - Closed file");
        m_file.close();
    }
}

std::vector<uint8_t> AdfsImage::readSectors(uint64_t sector, uint64_t count, bool verifyChecksums)
{
    std::vector<uint8_t> buffer;

    if (!m_file.is_open()) {
        LOG_CRITICAL("AdfsImage::readSectors() - File is not open");
        return buffer;
    }

    // Seek to the correct position
    m_file.seekg(m_sector0Position + (sector * 256));
    
    // Read the data
    size_t bytes_to_read = count * 256;
    buffer.resize(bytes_to_read);
    m_file.read(reinterpret_cast<char*>(buffer.data()), bytes_to_read);

    if (verifyChecksums) {
        // Verify the checksums of the read sectors
        for (uint64_t i = 0; i < count; ++i) {
            std::vector<uint8_t> sectorData(buffer.begin() + (i * 256), buffer.begin() + ((i + 1) * 256));
            uint16_t storedChecksum = static_cast<uint16_t>(buffer.at((i * 256) + 255));
            uint16_t calculatedChecksum = calculateChecksum(sectorData);
            
            if (storedChecksum != calculatedChecksum) {
                LOG_CRITICAL("AdfsImage::readSectors() - Checksum failed for sector {} checksum {} expected {}", 
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

uint16_t AdfsImage::calculateChecksum(const std::vector<uint8_t> &buffer)
{
    uint16_t sum = 255;
    for (int a = 254; a >= 0; --a) {
        if (sum > 255) {
            sum = (sum & 0xff) + 1;
        }
        sum += static_cast<uint16_t>(buffer[a]);
    }

    return (sum+1) & 0xff;
}

void AdfsImage::findSector0()
{
    // Search for the ADFS signature of "Hugo" that marks the start of the root directory
    // This is an ASCII string, so we can search for it directly
    // Note: This is logical sector 2, so there are 2 sectors before it that must be captured
    char ch;
    std::string signature;
    
    while (m_file.get(ch)) {
        if (ch == 'H') {
            // Found the first character of the signature, so read the next 3 characters
            signature = "H";
            for (int i = 0; i < 3 && m_file.get(ch); ++i) {
                signature += ch;
            }
            
            if (signature == "Hugo") {
                // Found the signature
                m_sector0Position = m_file.tellg();
                m_sector0Position -= 5;  // Adjust for "Hugo" + 1 byte before
                LOG_DEBUG("AdfsImage::findSector0() - Found ADFS signature Hugo at offset 0x{:X}", m_sector0Position);
                break;
            }
        }
    }

    if (m_sector0Position != 0) {
        // Seek back to sector 0 and set the position correctly
        // Sectors are 256 bytes, so we need to seek back 512 bytes
        m_sector0Position -= 512;
        m_file.seekg(m_sector0Position);
    } else {
        // Not a valid image file
        LOG_DEBUG("AdfsImage::findSector0() - Could not find ADFS signature Hugo in file - input file is not a valid ADFS image");
        m_isValid = false;
    }
}

uint32_t AdfsImage::adfsSectorToEfmSector(uint32_t adfsSector)
{
    // ADFS sectors are 256 bytes, EFM sectors are 2048 bytes
    // EFM sector 0 is at the beginning of the file, so we
    // have to offset by the sector 0 position
    return ((adfsSector * 256) + m_sector0Position) / 2048;

}
