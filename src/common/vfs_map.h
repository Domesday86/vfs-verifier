/************************************************************************

    vfs_map.h

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

#ifndef VFS_MAP_H
#define VFS_MAP_H

#include "adfs_directory.h"
#include "adfs_fsm.h"
#include "adfs_image.h"
#include "sector_sizes.h"

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

// What an EFM sector holds, as far as the filesystem is concerned. Only the
// first four matter: damage to free space, to the run-in before the filesystem,
// or past the end of the image costs nothing
enum class SectorRole {
    BeforeFilesystem,
    FileData,
    Metadata,
    AllocatedUnlisted,
    FreeSpace,
    PastEndOfImage
};

// True for the roles where losing a sector means losing something
bool isVitalRole(SectorRole role);

const char *sectorRoleName(SectorRole role);

// One object of the root directory, with the span of the image it occupies
struct VfsObject {
    std::string name;
    uint32_t startSector = 0;       // ADFS sector
    uint32_t sectorLength = 0;      // ADFS sectors
    uint32_t byteLength = 0;
    uint64_t startOffset = 0;       // byte offset within the image
    uint64_t endOffset = 0;
    uint32_t firstEfmSector = 0;
    uint32_t lastEfmSector = 0;
    bool extendsPastEndOfImage = false;
};

// The free space map and root directory of an image, reduced to the question
// both tools need to answer: for a given EFM sector, does anything depend on it?
class VfsMap
{
public:
    // Parse the filesystem of an image that has already been opened. Returns
    // false if the metadata could not be read at all
    bool load(AdfsImage &image, bool listEntries = true);

    bool isLoaded() const { return m_loaded; }

    // True if the filesystem was found by a validated signature, and the
    // metadata it was read from checksums and reads back consistently
    bool locatedByValidation() const { return m_locatedByValidation; }
    bool fsmChecksumOk() const { return m_fsmChecksumOk; }
    bool fsmReadComplete() const { return m_fsmReadComplete; }
    bool directoryReadComplete() const { return m_directoryReadComplete; }
    bool directoryOk() const;

    SectorRole classify(uint32_t efmSector) const;

    // The first object occupying an EFM sector, or nullptr if none does
    const VfsObject *objectForEfmSector(uint32_t efmSector) const;

    const std::vector<VfsObject> &objects() const { return m_objects; }
    const std::set<uint32_t> &fileEfmSectors() const { return m_fileEfmSectors; }
    const std::set<uint32_t> &metadataEfmSectors() const { return m_metadataEfmSectors; }

    // How many bytes of the given object the given EFM sector actually covers
    static uint64_t overlapBytes(const VfsObject &object, uint32_t efmSector);

    const AdfsFsm &fsm() const { return *m_fsm; }
    const AdfsDirectory &directory() const { return *m_directory; }

    uint64_t sector0Position() const { return m_sector0Position; }
    uint64_t imageSize() const { return m_imageSize; }

private:
    // The free space map and root directory occupy the first seven ADFS sectors
    static const uint32_t METADATA_SECTORS = 7;

    std::unique_ptr<AdfsFsm> m_fsm;
    std::unique_ptr<AdfsDirectory> m_directory;

    std::vector<VfsObject> m_objects;
    std::set<uint32_t> m_fileEfmSectors;
    std::set<uint32_t> m_metadataEfmSectors;
    std::map<uint32_t, size_t> m_efmToObject;

    uint64_t m_sector0Position = 0;
    uint64_t m_imageSize = 0;
    uint32_t m_discSectors = 0;

    bool m_loaded = false;
    bool m_locatedByValidation = false;
    bool m_fsmChecksumOk = false;
    bool m_fsmReadComplete = false;
    bool m_directoryReadComplete = false;
};

#endif // VFS_MAP_H
