/************************************************************************

    adfs_verifier.cpp

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

#include "adfs_verifier.h"
#include "logging.h"
#include <fmt/format.h>
#include <algorithm>

AdfsVerifier::AdfsVerifier()
{}

bool AdfsVerifier::process(const std::string &filename, const std::string &bsmFilename)
{
    // Open the VFS image file
    m_image.open(filename);
    if (!m_image.isValid()) {
        LOG_CRITICAL("AdfsVerifier::process() - Could not open VFS image file {}", filename);
        return false;
    }

    // Open the BSM file
    BadSectors badSectors;
    if (!badSectors.open(bsmFilename)) {
        LOG_CRITICAL("AdfsVerifier::process() - Could not open BSM metadata file {}", bsmFilename);
        return false;
    }

    // Read the free space map
    AdfsFsm adfsFsm(m_image.readSectors(0, 2, true));

    // Read the root directory
    AdfsDirectory adfsDirectory(m_image.readSectors(2, 5, false));

    std::vector<uint32_t> usedEfmSectors;
    std::vector<uint32_t> errorEfmSectors;

    // Verify the root directory entries one at a time
    for (size_t i = 0; i < adfsDirectory.entries().size(); ++i) {
        
        int32_t startSector = adfsDirectory.entries().at(i).startSector();
        int32_t byteLength = adfsDirectory.entries().at(i).byteLength();
        int32_t sectorLength = (byteLength + 255) / 256;

        // Show the file data
        LOG_DEBUG("Directory entry {} start sector {} length {} sectors - object name {}", 
            i, startSector, sectorLength, adfsDirectory.entries().at(i).objectName());

        // Ensure that all the used sectors are not in the bad sector list
        for (int j = 0; j < sectorLength; ++j) {
            uint32_t efmSector = m_image.adfsSectorToEfmSector(startSector + j);
            if (badSectors.isSectorBad(efmSector) && 
                std::find(errorEfmSectors.begin(), errorEfmSectors.end(), efmSector) == errorEfmSectors.end()) {
                LOG_WARN("AdfsVerifier::process() - Bad EFM sector {} found in file {} ADFS sector {}", 
                    efmSector, adfsDirectory.entries().at(i).objectName(), toString24bits(startSector + j));
                errorEfmSectors.push_back(efmSector);

                // Display the bad sector data
                std::vector<uint8_t> badSectorData = m_image.readSectors(efmSector, 1, false);
                hexDump(badSectorData, startSector + j);
            }
        }
    }

    // Did verification fail?
    if (errorEfmSectors.size() > 0) {
        LOG_INFO("AdfsVerifier::process() - Verification failed - {} bad sectors found in VFS image file {}", 
            errorEfmSectors.size(), filename);
    } else {
        LOG_INFO("AdfsVerifier::process() - Verification passed - no bad sectors found in VFS image file {}", filename);
    }

    // Close the image
    m_image.close();
    badSectors.close();
    return true;
}

// Display a hex dump of a series of ADFS sectors
void AdfsVerifier::hexDump(std::vector<uint8_t> &data, int32_t startSector) const
{
    const int bytesPerLine = 32;
    for (size_t i = 0; i < data.size(); i += bytesPerLine) {
        std::string line = fmt::format("{:08x}: ", i);
        
        // Hex values
        for (int j = 0; j < bytesPerLine; ++j) {
            if (i + j < data.size())
                line += fmt::format("{:02x} ", static_cast<uint8_t>(data[i + j]));
            else
                line += "   ";
        }
        
        line += " |";
        
        // ASCII representation
        for (int j = 0; j < bytesPerLine && i + j < data.size(); ++j) {
            char c = data[i + j];
            line += (c >= 32 && c <= 126) ? c : '.';
        }
        line += "|";
        
        LOG_DEBUG("{}", line);
    }
}
