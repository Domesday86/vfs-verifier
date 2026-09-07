/************************************************************************

    adfs_verifier.cpp

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

#include "adfs_verifier.h"
#include "logging.h"
#include <fmt/format.h>
#include <algorithm>

// The filesystem metadata occupies logical sectors 0 and 1 (the free space map)
// and 2 to 6 (the root directory)
static const uint32_t METADATA_SECTORS = 7;

static std::string megabytes(uint64_t bytes)
{
    return fmt::format("{:.1f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
}

AdfsVerifier::AdfsVerifier() :
    m_verificationPassed(false)
{}

void AdfsVerifier::reportFilesystemLocation() const
{
    LOG_INFO("Filesystem location:");
    LOG_INFO("  ADFS sector 0 at file offset 0x{:X} ({} bytes into the image)",
        m_image.sector0Position(), m_image.sector0Position());

    if (m_image.locatedByValidation()) {
        LOG_INFO("  Located by validated \"Hugo\" signature (free space map checksums and root "
                 "directory header/footer both agree)");
    } else {
        LOG_WARN("  No \"Hugo\" signature passed validation - the filesystem below was located by "
                 "falling back to the first signature found and may be wrong");
    }
}

void AdfsVerifier::reportMetadataIntegrity(const VfsMap &map, const BadSectors &badSectors)
{
    const AdfsFsm &fsm = map.fsm();
    const AdfsDirectory &directory = map.directory();
    const bool fsmChecksumOk = map.fsmChecksumOk();
    const bool fsmReadComplete = map.fsmReadComplete();
    const bool directoryReadComplete = map.directoryReadComplete();

    LOG_INFO("Filesystem metadata:");

    // Free space map sectors 0 and 1
    if (!fsmReadComplete) {
        LOG_ERROR("  Free space map (sectors 0-1) : COULD NOT BE READ IN FULL");
        m_verificationPassed = false;
    } else if (!fsmChecksumOk) {
        LOG_ERROR("  Free space map (sectors 0-1) : CHECKSUM FAILED");
        m_verificationPassed = false;
    } else {
        LOG_INFO("  Free space map (sectors 0-1) : checksums OK");
    }

    // Root directory sectors 2 to 6
    if (!directoryReadComplete || !directory.isValid()) {
        LOG_ERROR("  Root directory (sectors 2-6) : COULD NOT BE READ IN FULL");
        m_verificationPassed = false;
    } else if (directory.isBroken()) {
        LOG_ERROR("  Root directory (sectors 2-6) : BROKEN - header (0x{:02X} \"{}\") does not match "
                  "footer (0x{:02X} \"{}\")",
            directory.headerSequenceNumber(), directory.headerIdentifier(),
            directory.footerSequenceNumber(), directory.footerIdentifier());
        m_verificationPassed = false;
    } else {
        LOG_INFO("  Root directory (sectors 2-6) : header/footer consistent (\"{}\" sequence 0x{:02X})",
            directory.headerIdentifier(), directory.headerSequenceNumber());
    }

    if (directory.isValid() && !directory.entriesTerminated()) {
        LOG_WARN("  Root directory entries are not properly terminated - the entry list may be truncated "
                 "or corrupt");
    }

    if (directory.isValid()) {
        LOG_INFO("  Directory name \"{}\" title \"{}\"", directory.directoryName(), directory.directoryTitle());
    }
    if (fsm.isValid()) {
        LOG_INFO("  Disc name \"{}\" disc ID 0x{:04X} boot option 0x{:02X}",
            fsm.discName(), fsm.discId(), fsm.bootOption());
    }

    // Are the metadata sectors themselves listed as bad?
    const std::set<uint32_t> &metadata = map.metadataEfmSectors();
    std::vector<uint32_t> badMetadata;
    for (uint32_t efmSector : metadata) {
        if (badSectors.isSectorBad(efmSector)) badMetadata.push_back(efmSector);
    }

    if (badMetadata.empty()) {
        LOG_INFO("  Metadata occupies EFM sector(s) {}-{} : none are listed as bad",
            *metadata.begin(), *metadata.rbegin());
    } else {
        std::string list;
        for (size_t i = 0; i < badMetadata.size(); ++i) {
            if (i > 0) list += ", ";
            list += std::to_string(badMetadata.at(i));
        }
        LOG_ERROR("  Metadata occupies EFM sector(s) {}-{} : {} of these are listed as BAD ({}) - "
                  "the free space map and directory listing above may be unreliable",
            *metadata.begin(), *metadata.rbegin(), badMetadata.size(), list);
        m_verificationPassed = false;
    }
}

void AdfsVerifier::reportImageGeometry(const AdfsFsm &fsm) const
{
    const uint32_t discSectors = fsm.numberOfSectors();
    const uint32_t imageSectors = m_image.sectorsInImage();

    LOG_INFO("Image geometry:");
    LOG_INFO("  Free space map describes {} sectors ({})",
        discSectors, megabytes(static_cast<uint64_t>(discSectors) * ADFS_SECTOR_SIZE));
    LOG_INFO("  Image holds {} sectors from sector 0 to end of file ({})",
        imageSectors, megabytes(static_cast<uint64_t>(imageSectors) * ADFS_SECTOR_SIZE));

    if (imageSectors >= discSectors) {
        LOG_INFO("  The image covers the whole of the described filesystem");
        return;
    }

    const uint32_t missing = discSectors - imageSectors;

    // Are the missing sectors free space, or would real data be lost?
    uint32_t missingAllocated = 0;
    for (uint32_t s = imageSectors; s < discSectors; ++s) {
        if (!fsm.isFree(s)) ++missingAllocated;
    }

    if (missingAllocated == 0) {
        LOG_WARN("  Image is TRUNCATED - {} sectors ({}) of the described filesystem are missing from "
                 "the end of the image, but the free space map marks all of them as free, so no file "
                 "data is lost",
            missing, megabytes(static_cast<uint64_t>(missing) * ADFS_SECTOR_SIZE));
    } else {
        LOG_ERROR("  Image is TRUNCATED - {} sectors ({}) of the described filesystem are missing from "
                  "the end of the image, and {} of them ({}) are marked as allocated, so file data is lost",
            missing, megabytes(static_cast<uint64_t>(missing) * ADFS_SECTOR_SIZE),
            missingAllocated, megabytes(static_cast<uint64_t>(missingAllocated) * ADFS_SECTOR_SIZE));
    }
}

void AdfsVerifier::reportObjectDamage(const std::vector<ObjectDamage> &damage) const
{
    std::vector<const ObjectDamage *> damaged;
    for (const ObjectDamage &d : damage) {
        if (d.damagedEfmSectors > 0 || d.extendsPastEndOfImage) damaged.push_back(&d);
    }

    LOG_INFO("Object damage:");
    if (damaged.empty()) {
        LOG_INFO("  All {} object(s) are free of bad sectors", damage.size());
        return;
    }

    LOG_INFO("  {} of {} object(s) are affected by bad sectors:", damaged.size(), damage.size());
    LOG_DEBUG("  (per-sector detail and hex dumps follow at debug level)");
    LOG_INFO("    {:<10} {:>10} {:>10} {:>12} {:>9}", "Object", "Length", "BadEFMSec", "BytesLost", "Intact");
    for (const ObjectDamage *d : damaged) {
        const double intact = (d->byteLength == 0) ? 100.0
            : 100.0 * static_cast<double>(d->byteLength - std::min<uint64_t>(d->damagedBytes, d->byteLength))
                    / static_cast<double>(d->byteLength);
        LOG_INFO("    {:<10} {:>10} {:>10} {:>12} {:>8.3f}%",
            d->name, d->byteLength, d->damagedEfmSectors, d->damagedBytes, intact);
        if (d->extendsPastEndOfImage) {
            LOG_ERROR("    {:<10} extends beyond the end of the image - the object is incomplete", d->name);
        }
    }
}

void AdfsVerifier::reportMapAnalysis(const MapAnalysis &analysis, const BadSectors &badSectors) const
{
    LOG_INFO("Bad sector map analysis ({} distinct sectors):", badSectors.count());
    LOG_INFO("  Before the filesystem        : {}", analysis.beforeFilesystem);
    LOG_INFO("  Within file data             : {}", analysis.withinFileData);
    LOG_INFO("  Filesystem metadata          : {}", analysis.filesystemMetadata);
    LOG_INFO("  Allocated, not in any object : {}", analysis.allocatedButUnlisted);
    LOG_INFO("  Free space (harmless)        : {}", analysis.freeSpace);
    LOG_INFO("  Past the end of the image    : {}", analysis.pastEndOfImage);

    if (analysis.allocatedButUnlisted > 0) {
        LOG_WARN("  {} bad sector(s) lie in allocated space that no listed object accounts for - these "
                 "may belong to objects in subdirectories, which this tool does not descend into",
            analysis.allocatedButUnlisted);
    }
}

void AdfsVerifier::reportObjectContent(const std::vector<ObjectContentReport> &content) const
{
    std::vector<const ObjectContentReport *> withFill;
    uint64_t totalSectors = 0;
    uint64_t totalFill = 0;
    uint64_t totalFillNotFlagged = 0;
    uint32_t emptyObjects = 0;

    for (const ObjectContentReport &c : content) {
        totalSectors += c.content.sectorsExamined;
        totalFill += c.content.fillSectors;
        totalFillNotFlagged += c.content.fillSectorsNotFlagged;
        if (c.content.entirelyFill) ++emptyObjects;
        if (c.content.fillSectors > 0) withFill.push_back(&c);
    }

    LOG_INFO("Object content:");
    if (totalSectors == 0) {
        LOG_INFO("  No object data could be examined");
        return;
    }

    if (withFill.empty()) {
        LOG_INFO("  All {} object(s) carry data throughout - no fill sectors found", content.size());
        return;
    }

    LOG_INFO("  {} of {} object(s) contain sectors that carry no data:", withFill.size(), content.size());
    LOG_INFO("    {:<10} {:>10} {:>10} {:>11} {:>11} {:>8}",
        "Object", "Sectors", "FillSecs", "LongestRun", "NotFlagged", "Fill");
    for (const ObjectContentReport *c : withFill) {
        const double percent = 100.0 * static_cast<double>(c->content.fillSectors)
                                     / static_cast<double>(c->content.sectorsExamined);
        LOG_INFO("    {:<10} {:>10} {:>10} {:>11} {:>11} {:>7.1f}%{}",
            c->name, c->content.sectorsExamined, c->content.fillSectors,
            c->content.longestFillRun, c->content.fillSectorsNotFlagged, percent,
            c->content.entirelyFill ? "  (empty)" : "");
    }

    if (emptyObjects > 0) {
        LOG_WARN("  {} object(s) consist entirely of fill and carry no data at all", emptyObjects);
    }

    LOG_INFO("  Fill totals: {} of {} sector(s) ({:.1f}%) carry no data",
        totalFill, totalSectors,
        100.0 * static_cast<double>(totalFill) / static_cast<double>(totalSectors));

    if (totalFillNotFlagged > 0) {
        LOG_WARN("  {} fill sector(s) are not accounted for by the bad sector map - the decoder believed "
                 "it recovered these, so they are either blank on the disc or were lost without being "
                 "detected", totalFillNotFlagged);
    }
}

void AdfsVerifier::reportBoundaryCheck(const std::vector<BoundaryCheck> &checks) const
{
    LOG_INFO("Free space boundary check:");
    if (checks.empty()) {
        LOG_INFO("  The free space map declares no extents to check");
        return;
    }

    uint32_t displaced = 0;
    uint32_t withResidualData = 0;

    for (const BoundaryCheck &check : checks) {
        const uint32_t extentEnd = check.extentStart + check.extentLength - 1;

        // Data at the start of a free extent is an erased object that was never
        // overwritten - normal, and worth knowing about, but not a fault
        if (check.fillBeginsOffset > 0) {
            ++withResidualData;
            LOG_DEBUG("  Free extent {}-{} : begins with {} sector(s) of residual data",
                check.extentStart, extentEnd, check.fillBeginsOffset);
        }

        if (check.dataResumesOffset > 0) {
            ++displaced;
            LOG_WARN("  Free extent {}-{} : data resumes {} sector(s) later than the free space map "
                     "declares", check.extentStart, extentEnd, check.dataResumesOffset);
        }
    }

    if (displaced == 0) {
        LOG_INFO("  All {} free extent(s) agree with the content present in the image", checks.size());
    } else {
        LOG_WARN("  {} of {} free extent(s) disagree with the content present in the image - a run of "
                 "sectors may have been lost or duplicated during decoding, which would displace every "
                 "object after it", displaced, checks.size());
    }

    if (withResidualData > 0) {
        LOG_INFO("  {} free extent(s) begin with residual data from erased objects (normal; the sectors "
                 "were freed but never overwritten)", withResidualData);
    }
}

void AdfsVerifier::reportAllocationCrossCheck(const AdfsFsm &fsm, const AdfsDirectory &directory) const
{
    if (!fsm.isValid() || !directory.isValid()) return;

    uint64_t accounted = METADATA_SECTORS;
    for (const AdfsDirectoryEntry &entry : directory.entries()) {
        accounted += entry.sectorLength();
    }

    const uint32_t used = fsm.usedSectors();

    LOG_INFO("Allocation cross-check:");
    LOG_INFO("  Free space map      : {} sectors used, {} free, {} total",
        used, fsm.freeSectors(), fsm.numberOfSectors());
    LOG_INFO("  Directory accounts for : {} sectors ({} metadata + {} object(s))",
        accounted, METADATA_SECTORS, directory.entries().size());

    if (accounted > used) {
        const uint64_t excess = accounted - used;
        LOG_WARN("  The directory accounts for {} sector(s) ({}) MORE than the free space map marks as "
                 "used - the free space map and the directory disagree",
            excess, megabytes(excess * ADFS_SECTOR_SIZE));
    } else {
        const uint64_t unaccounted = used - accounted;
        if (unaccounted == 0) {
            LOG_INFO("  The two agree exactly");
        } else {
            LOG_INFO("  Unaccounted         : {} sector(s) ({}) are marked as used but belong to no "
                     "listed object - allocation slack, or objects in subdirectories",
                unaccounted, megabytes(unaccounted * ADFS_SECTOR_SIZE));
        }
    }
}

bool AdfsVerifier::process(const std::string &filename, const std::string &bsmFilename)
{
    m_verificationPassed = true;

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

    reportFilesystemLocation();
    if (!m_image.locatedByValidation()) m_verificationPassed = false;

    // Read the free space map and the root directory
    VfsMap map;
    if (!map.load(m_image)) {
        LOG_CRITICAL("AdfsVerifier::process() - Could not read the filesystem of VFS image file {}",
            filename);
        return false;
    }

    const AdfsFsm &adfsFsm = map.fsm();
    const AdfsDirectory &adfsDirectory = map.directory();

    reportMetadataIntegrity(map, badSectors);
    reportImageGeometry(adfsFsm);

    std::set<uint32_t> damagedEfmSectors;   // bad sectors that intersect file data
    std::set<uint32_t> dumpedEfmSectors;    // so each bad sector is dumped only once
    std::vector<ObjectDamage> objectDamage;
    std::vector<ObjectContentReport> objectContent;
    AdfsContentCheck contentCheck(m_image, badSectors);

    // Verify the root directory entries one at a time
    const std::vector<VfsObject> &objects = map.objects();
    for (size_t i = 0; i < objects.size(); ++i) {
        const VfsObject &object = objects.at(i);

        LOG_DEBUG("Directory entry {} start sector {} length {} sectors - object name {}",
            i, object.startSector, object.sectorLength, object.name);

        ObjectDamage damage;
        damage.name = object.name;
        damage.startSector = object.startSector;
        damage.sectorLength = object.sectorLength;
        damage.byteLength = object.byteLength;
        damage.damagedEfmSectors = 0;
        damage.damagedBytes = 0;
        damage.extendsPastEndOfImage = object.extendsPastEndOfImage;

        if (object.sectorLength > 0) {
            for (uint32_t efmSector = object.firstEfmSector; efmSector <= object.lastEfmSector; ++efmSector) {
                if (!badSectors.isSectorBad(efmSector)) continue;

                ++damage.damagedEfmSectors;
                damagedEfmSectors.insert(efmSector);

                // How much of this object does the bad sector actually cover?
                damage.damagedBytes += VfsMap::overlapBytes(object, efmSector);

                // Report and dump each bad sector once
                if (dumpedEfmSectors.insert(efmSector).second) {
                    // The first ADFS sector of this object that falls in the bad EFM sector
                    const uint64_t efmStart = static_cast<uint64_t>(efmSector) * EFM_SECTOR_SIZE;
                    uint32_t adfsSector = object.startSector;
                    if (efmStart > object.startOffset) {
                        adfsSector = object.startSector +
                            static_cast<uint32_t>((efmStart - object.startOffset) / ADFS_SECTOR_SIZE);
                    }
                    LOG_DEBUG("AdfsVerifier::process() - Bad EFM sector {} found in object {} ADFS sector {}",
                        efmSector, object.name, toString24bits(adfsSector));

                    // Display the data of the ADFS sector that falls within the bad EFM sector
                    std::vector<uint8_t> badSectorData = m_image.readSectors(adfsSector, 1, false);
                    hexDump(badSectorData, adfsSector);
                }
            }
        }

        objectDamage.push_back(damage);

        // Measure how much of the object actually carries data
        ObjectContentReport contentReport;
        contentReport.name = object.name;
        contentReport.content = contentCheck.checkObject(object.startSector, object.sectorLength);
        objectContent.push_back(contentReport);
    }

    reportObjectDamage(objectDamage);
    reportObjectContent(objectContent);
    reportBoundaryCheck(contentCheck.checkFreeSpaceBoundaries(adfsFsm));

    // Classify every entry in the bad sector map
    MapAnalysis analysis = {0, 0, 0, 0, 0, 0};

    for (uint32_t efmSector : badSectors.sectors()) {
        switch (map.classify(efmSector)) {
        case SectorRole::BeforeFilesystem:  ++analysis.beforeFilesystem; break;
        case SectorRole::FileData:          ++analysis.withinFileData; break;
        case SectorRole::Metadata:          ++analysis.filesystemMetadata; break;
        case SectorRole::AllocatedUnlisted: ++analysis.allocatedButUnlisted; break;
        case SectorRole::FreeSpace:         ++analysis.freeSpace; break;
        case SectorRole::PastEndOfImage:    ++analysis.pastEndOfImage; break;
        }
    }

    reportMapAnalysis(analysis, badSectors);
    reportAllocationCrossCheck(adfsFsm, adfsDirectory);

    // Did verification fail?
    if (!damagedEfmSectors.empty()) {
        m_verificationPassed = false;
        LOG_INFO("AdfsVerifier::process() - Verification failed - {} bad sector(s) affect file data in "
                 "VFS image file {}", damagedEfmSectors.size(), filename);
    } else if (m_verificationPassed) {
        LOG_INFO("AdfsVerifier::process() - Verification passed - no bad sectors affect file data in "
                 "VFS image file {}", filename);
    } else {
        LOG_INFO("AdfsVerifier::process() - Verification failed - no bad sectors affect file data, but "
                 "problems were reported above for VFS image file {}", filename);
    }

    // Close the image
    m_image.close();
    badSectors.close();
    return true;
}

// Display a hex dump of a series of ADFS sectors
void AdfsVerifier::hexDump(const std::vector<uint8_t> &data, uint32_t startSector) const
{
    const int bytesPerLine = 32;
    for (size_t i = 0; i < data.size(); i += bytesPerLine) {
        const uint64_t address = (static_cast<uint64_t>(startSector) * ADFS_SECTOR_SIZE) + i;
        std::string line = fmt::format("{:08x}: ", address);

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
