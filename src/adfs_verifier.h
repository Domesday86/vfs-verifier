/************************************************************************

    adfs_verifier.h

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

#ifndef ADFS_VERIFIER_H
#define ADFS_VERIFIER_H

#include <string>
#include <vector>
#include <set>
#include <cstdint>

#include "adfs_image.h"
#include "adfs_fsm.h"
#include "adfs_directory.h"
#include "adfs_content.h"
#include "bad_sectors.h"

class AdfsVerifier
{
public:
    AdfsVerifier();

    bool process(const std::string &filename, const std::string &bsmFilename);

    // True if the image completed verification with no damage to file data
    bool verificationPassed() const { return m_verificationPassed; }

private:
    // The damage sustained by a single object in the directory
    struct ObjectDamage {
        std::string name;
        uint32_t startSector;
        uint32_t sectorLength;
        uint32_t byteLength;
        uint32_t damagedEfmSectors;
        uint64_t damagedBytes;
        bool extendsPastEndOfImage;
    };

    // The content measured for each object, in the same order as the damage list
    struct ObjectContentReport {
        std::string name;
        ObjectContent content;
    };

    // How each entry in the bad sector map relates to the filesystem
    struct MapAnalysis {
        uint32_t beforeFilesystem;
        uint32_t withinFileData;
        uint32_t filesystemMetadata;
        uint32_t allocatedButUnlisted;
        uint32_t freeSpace;
        uint32_t pastEndOfImage;
    };

    AdfsImage m_image;
    bool m_verificationPassed;

    std::set<uint32_t> metadataEfmSectors() const;

    void reportFilesystemLocation() const;
    void reportMetadataIntegrity(const AdfsFsm &fsm, const AdfsDirectory &directory,
                                 const BadSectors &badSectors, bool fsmChecksumOk,
                                 bool fsmReadComplete, bool directoryReadComplete);
    void reportImageGeometry(const AdfsFsm &fsm) const;
    void reportObjectDamage(const std::vector<ObjectDamage> &damage) const;
    void reportMapAnalysis(const MapAnalysis &analysis, const BadSectors &badSectors) const;
    void reportObjectContent(const std::vector<ObjectContentReport> &content) const;
    void reportBoundaryCheck(const std::vector<BoundaryCheck> &checks) const;
    void reportAllocationCrossCheck(const AdfsFsm &fsm, const AdfsDirectory &directory) const;

    void hexDump(const std::vector<uint8_t> &data, uint32_t startSector) const;
};

#endif // ADFS_VERIFIER_H
