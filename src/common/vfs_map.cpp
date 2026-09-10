/************************************************************************

    vfs_map.cpp

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

#include "vfs_map.h"
#include "logging.h"

#include <algorithm>

bool isVitalRole(SectorRole role)
{
    switch (role) {
    case SectorRole::FileData:
    case SectorRole::Metadata:
    case SectorRole::AllocatedUnlisted:
        return true;
    case SectorRole::BeforeFilesystem:
    case SectorRole::FreeSpace:
    case SectorRole::PastEndOfImage:
        return false;
    }

    return false;
}

const char *sectorRoleName(SectorRole role)
{
    switch (role) {
    case SectorRole::BeforeFilesystem:  return "before the filesystem";
    case SectorRole::FileData:          return "file data";
    case SectorRole::Metadata:          return "filesystem metadata";
    case SectorRole::AllocatedUnlisted: return "allocated, not in any object";
    case SectorRole::FreeSpace:         return "free space";
    case SectorRole::PastEndOfImage:    return "past the end of the image";
    }

    return "unknown";
}

bool VfsMap::load(AdfsImage &image, bool listEntries)
{
    m_loaded = false;

    if (!image.isValid()) {
        LOG_DEBUG("VfsMap::load() - The image is not a valid ADFS filesystem");
        return false;
    }

    m_sector0Position = image.sector0Position();
    m_imageSize = image.imageSize();
    m_locatedByValidation = image.locatedByValidation();

    // Read the free space map with checksum verification
    const std::vector<uint8_t> fsmData = image.readSectors(0, 2, true);
    m_fsmChecksumOk = image.lastChecksumOk();
    m_fsmReadComplete = image.lastReadComplete();
    m_fsm = std::make_unique<AdfsFsm>(fsmData);

    // Read the root directory
    const std::vector<uint8_t> dirData = image.readSectors(2, 5, false);
    m_directoryReadComplete = image.lastReadComplete();
    m_directory = std::make_unique<AdfsDirectory>(dirData, listEntries);

    m_discSectors = m_fsm->numberOfSectors();

    // The EFM sectors that hold the free space map and the root directory
    m_metadataEfmSectors.clear();
    for (uint32_t sector = 0; sector < METADATA_SECTORS; ++sector) {
        m_metadataEfmSectors.insert(image.adfsSectorToEfmSector(sector));
    }

    // The EFM sectors each object of the root directory occupies
    m_objects.clear();
    m_fileEfmSectors.clear();
    m_efmToObject.clear();

    const std::vector<AdfsDirectoryEntry> entries = m_directory->entries();
    for (const AdfsDirectoryEntry &entry : entries) {
        VfsObject object;
        object.name = entry.objectName();
        object.startSector = entry.startSector();
        object.sectorLength = entry.sectorLength();
        object.byteLength = entry.byteLength();
        object.startOffset = m_sector0Position +
            (static_cast<uint64_t>(object.startSector) * ADFS_SECTOR_SIZE);
        object.endOffset = object.startOffset + object.byteLength;
        object.extendsPastEndOfImage = (object.endOffset > m_imageSize);

        if (object.sectorLength > 0) {
            object.firstEfmSector = image.adfsSectorToEfmSector(object.startSector);
            object.lastEfmSector =
                image.adfsSectorToEfmSector(object.startSector + object.sectorLength - 1);

            const size_t objectIndex = m_objects.size();
            for (uint32_t efmSector = object.firstEfmSector; efmSector <= object.lastEfmSector; ++efmSector) {
                m_fileEfmSectors.insert(efmSector);
                // Objects can share the EFM sector they meet in; the first one wins
                m_efmToObject.emplace(efmSector, objectIndex);
            }
        }

        m_objects.push_back(object);
    }

    m_loaded = true;

    LOG_DEBUG("VfsMap::load() - Filesystem holds {} object(s) covering {} EFM sector(s), plus {} "
              "metadata EFM sector(s)",
        m_objects.size(), m_fileEfmSectors.size(), m_metadataEfmSectors.size());

    return true;
}

bool VfsMap::directoryOk() const
{
    return m_directory && m_directory->isValid() && !m_directory->isBroken();
}

void VfsMap::extendImageSize(uint64_t imageSize)
{
    if (imageSize <= m_imageSize) return;

    m_imageSize = imageSize;

    // An object that ran off the end of the image may no longer do so
    for (VfsObject &object : m_objects) {
        object.extendsPastEndOfImage = (object.endOffset > m_imageSize);
    }

    LOG_DEBUG("VfsMap::extendImageSize() - The image is now {} bytes", m_imageSize);
}

bool VfsMap::fsmTotalsConsistent() const
{
    if (!m_fsm) return false;

    // Free and used are derived from different parts of the map, so their sum
    // agreeing with the disc size is a genuine cross-check
    const uint64_t total =
        static_cast<uint64_t>(m_fsm->freeSectors()) + static_cast<uint64_t>(m_fsm->usedSectors());

    return total == static_cast<uint64_t>(m_discSectors);
}

SectorRole VfsMap::classify(uint32_t efmSector) const
{
    const uint64_t efmStart = static_cast<uint64_t>(efmSector) * EFM_SECTOR_SIZE;
    const uint64_t efmEnd = efmStart + EFM_SECTOR_SIZE;

    if (efmEnd <= m_sector0Position) return SectorRole::BeforeFilesystem;
    if (efmStart >= m_imageSize) return SectorRole::PastEndOfImage;
    if (m_fileEfmSectors.count(efmSector) > 0) return SectorRole::FileData;
    if (m_metadataEfmSectors.count(efmSector) > 0) return SectorRole::Metadata;

    // Which ADFS sectors does this EFM sector cover?
    const uint32_t firstAdfs = (efmStart >= m_sector0Position)
        ? static_cast<uint32_t>((efmStart - m_sector0Position) / ADFS_SECTOR_SIZE) : 0;
    const uint32_t lastAdfs =
        static_cast<uint32_t>((efmEnd - 1 - m_sector0Position) / ADFS_SECTOR_SIZE);

    for (uint32_t adfsSector = firstAdfs; adfsSector <= lastAdfs; ++adfsSector) {
        if (adfsSector >= m_discSectors) break;
        if (!m_fsm->isFree(adfsSector)) return SectorRole::AllocatedUnlisted;
    }

    return SectorRole::FreeSpace;
}

const VfsObject *VfsMap::objectForEfmSector(uint32_t efmSector) const
{
    const auto it = m_efmToObject.find(efmSector);
    if (it == m_efmToObject.end()) return nullptr;
    return &m_objects.at(it->second);
}

uint64_t VfsMap::overlapBytes(const VfsObject &object, uint32_t efmSector)
{
    const uint64_t efmStart = static_cast<uint64_t>(efmSector) * EFM_SECTOR_SIZE;
    const uint64_t efmEnd = efmStart + EFM_SECTOR_SIZE;
    const uint64_t overlapStart = std::max(object.startOffset, efmStart);
    const uint64_t overlapEnd = std::min(object.endOffset, efmEnd);

    return (overlapEnd > overlapStart) ? (overlapEnd - overlapStart) : 0;
}
