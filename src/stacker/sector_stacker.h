/************************************************************************

    sector_stacker.h

    vfs-stacker - Acorn VFS (Domesday) image stacker
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

#ifndef SECTOR_STACKER_H
#define SECTOR_STACKER_H

#include "adfs_image.h"
#include "bad_sectors.h"
#include "sector_sizes.h"
#include "vfs_map.h"

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

// One decode taking part in the stack: the image itself and the bad sector map
// the decoder wrote alongside it
class StackSource
{
public:
    bool open(const std::string &imageFilename);

    // Read one EFM sector; returns false if the source does not extend that far
    bool readSector(uint64_t sector, std::vector<uint8_t> &buffer);

    bool isGood(uint64_t sector) const
    {
        return sector < m_sectorCount && !m_badSectors.isSectorBad(static_cast<uint32_t>(sector));
    }

    bool hasSector(uint64_t sector) const { return sector < m_sectorCount; }

    const std::string &filename() const { return m_imageFilename; }
    const std::string &bsmFilename() const { return m_bsmFilename; }
    uint64_t sectorCount() const { return m_sectorCount; }
    uint64_t imageSize() const { return m_imageSize; }
    uint64_t trailingBytes() const { return m_imageSize % EFM_SECTOR_SIZE; }
    const BadSectors &badSectors() const { return m_badSectors; }

    // Number of output sectors this source was the only good candidate for
    uint64_t uniqueContribution = 0;
    // Of those, the ones the filesystem actually depends on
    uint64_t uniqueVitalContribution = 0;
    // Number of output sectors taken from this source
    uint64_t sectorsUsed = 0;
    // Sectors the filesystem depends on that this source alone could not supply
    uint64_t vitalBad = 0;

private:
    std::string m_imageFilename;
    std::string m_bsmFilename;
    std::ifstream m_file;
    BadSectors m_badSectors;
    uint64_t m_imageSize = 0;
    uint64_t m_sectorCount = 0;
};

// How each output sector was arrived at
enum class SectorOrigin {
    Unanimous,      // one or more sources vouch for it and they all agree
    Majority,       // sources that vouch for it disagree; the majority won
    Split,          // sources that vouch for it disagree with no majority
    Consensus,      // no source vouches for it, but enough sources agree on real content
    Unrecovered     // no source vouches for it and nothing could be salvaged
};

// The decision reached for one output sector
struct SectorPlan
{
    static constexpr uint16_t NO_SOURCE = 0xFFFF;

    SectorOrigin origin = SectorOrigin::Unrecovered;
    uint16_t source = NO_SOURCE;    // which source supplies the content

    // How many sources held the content that was chosen. Recorded for every
    // sector, including the ones nothing could be made of, so that the report
    // can say how close a sector that is still bad came to being recovered
    uint16_t agreement = 0;
};

// Whether a sector that every source flagged as bad may still be accepted on the
// strength of the sources agreeing with each other
enum class ConsensusMode {
    Off,        // never accept a sector no source vouches for
    Auto,       // accept when a majority of the sources hold identical content
    Fixed       // accept when a given number of sources hold identical content
};

// One sector the sources did not all hold the same content for, kept so that the
// conflict report can show what they disagree about.
//
// Two quite different things produce these. Decode noise is random: which
// sources dissent varies from sector to sector, and the damage is a byte or two.
// A genuine difference between pressings is systematic: the same sources dissent
// every time, because they really are a different disc. Recording who was on
// each side is what lets the two be told apart
struct ConflictSector
{
    uint32_t sector = 0;
    SectorOrigin origin = SectorOrigin::Unrecovered;
    uint32_t agreement = 0;     // sources holding the content that was chosen
    uint32_t runnerUp = 0;      // largest group holding anything else
    bool contested = false;     // two or more sources held that other answer

    // Which sources held the chosen content, and which held the runner-up.
    // Bit n is source n; only populated when there are at most 64 sources
    uint64_t winners = 0;
    uint64_t losers = 0;

    // How far apart the two are. A handful of bytes is damage; a sector that
    // differs throughout is different content
    uint32_t differingBytes = 0;
    uint32_t differingRuns = 0;

    // Of those, the bytes where one side holds a padding value and the other
    // holds data. That is not the two sides disagreeing about what the disc
    // says: it is one of them having recovered less of it
    uint32_t fillOnlyBytes = 0;
};

// How the sources split, aggregated over every conflict. The same split
// recurring is the signature of a version difference rather than decode damage
struct ConflictPattern
{
    uint64_t winners = 0;
    uint64_t losers = 0;
    uint64_t sectors = 0;
    uint64_t totalDifferingBytes = 0;
    uint64_t totalFillOnlyBytes = 0;
};

struct StackResult
{
    uint64_t outputSectors = 0;
    uint64_t unanimous = 0;
    uint64_t majority = 0;
    uint64_t split = 0;
    uint64_t consensus = 0;
    uint64_t unrecovered = 0;
    // Empty sectors added to the end to bring the image up to the length the
    // filesystem says the disc is. No source held these at all
    uint64_t padded = 0;

    // Consensus recoveries every source agreed on, which are beyond argument
    uint64_t consensusUnanimous = 0;
    // Consensus recoveries resting on two sources alone - the weakest evidence
    // the tool will act on
    uint64_t consensusThinCount = 0;
    std::vector<uint32_t> consensusThin;
    // Consensus recoveries made while two or more other sources held a
    // different answer, which suggests the sources are not all the same disc
    uint64_t consensusContestedCount = 0;
    std::vector<uint32_t> consensusContested;

    // Every sector the sources held differing content for. A badly matched pair
    // of sources can produce these in bulk, so the list is a capped sample and
    // conflictCount is the real total
    std::vector<ConflictSector> conflicts;
    uint64_t conflictCount = 0;

    // How the sources split over those conflicts, commonest split first
    std::vector<ConflictPattern> conflictPatterns;

    // The remaining bad sectors, which are what is written to the output map.
    // Both the sectors no source could supply and any padding added to the end
    std::vector<uint32_t> outputBadSectors;

    uint64_t totalBad() const { return outputBadSectors.size(); }
};

// The damage a still-bad sector does to one object of the root directory
struct StackedObjectDamage
{
    std::string name;
    uint32_t byteLength = 0;
    uint32_t damagedEfmSectors = 0;
    uint64_t damagedBytes = 0;
};

// What the filesystem makes of the sectors that are still bad
struct FilesystemAnalysis
{
    static constexpr size_t ROLE_COUNT = 6;

    bool loaded = false;
    uint32_t byRole[ROLE_COUNT] = {0, 0, 0, 0, 0, 0};

    // Sectors the filesystem depends on that are still bad
    std::vector<uint32_t> vitalSectors;
    std::vector<StackedObjectDamage> damagedObjects;

    uint32_t vitalCount() const { return static_cast<uint32_t>(vitalSectors.size()); }
};

// Pairwise comparison of two sources over the sectors both call good. Decodes of
// the same disc must agree; widespread disagreement means the images are not
// aligned with each other and stacking them would splice unrelated data together
struct AlignmentPair
{
    size_t sourceA = 0;
    size_t sourceB = 0;
    uint64_t compared = 0;
    uint64_t disagreed = 0;

    // Two decodes that failed on exactly the same sectors are unlikely to be
    // independent attempts. Consensus recovery leans on the sources failing
    // independently, so this is worth saying out loud
    bool identicalBadSectors = false;

    double disagreementPercent() const
    {
        return compared == 0 ? 0.0 : (100.0 * static_cast<double>(disagreed) / static_cast<double>(compared));
    }
};

struct StackerOptions
{
    // Whether sectors every source flagged as bad may be recovered from the
    // sources agreeing with each other. Under ConsensusMode::Fixed,
    // consensusThreshold is how many sources must agree byte-for-byte; under
    // Auto the requirement is worked out per sector and the threshold is unused
    ConsensusMode consensusMode = ConsensusMode::Auto;
    uint32_t consensusThreshold = 0;

    // Extend a short image to the length the filesystem says the disc is
    bool pad = true;

    // Stack sources that failed the alignment cross-check
    bool force = false;

    // Hex dump what the sources disagree about, sector by sector
    bool showConflicts = false;
};

class SectorStacker
{
public:
    explicit SectorStacker(const StackerOptions &options);

    bool addSource(const std::string &imageFilename);

    // Compare the sources against each other; must be run before plan()
    bool checkAlignment();

    // Decide where every output sector comes from, without writing anything
    bool plan();

    // Read the filesystem of the planned image and work out which of the
    // sectors that are still bad actually matter
    bool analyseFilesystem();

    // Write the planned image and its bad sector map
    bool write(const std::string &outputFilename);

    // Read one sector of the planned image; used by the stacked image reader
    bool readStackedSector(uint64_t sector, std::vector<uint8_t> &buffer);

    uint64_t outputSectors() const { return m_outputSectors; }
    uint64_t outputSize() const { return m_outputSectors * EFM_SECTOR_SIZE; }

    void reportSources() const;
    void reportAlignment() const;
    void reportResult() const;
    void reportFilesystem(const std::string &outputFilename) const;

    // What the sources disagree about, and whether that reads as decode damage
    // or as the sources being different versions of the disc. Re-reads the
    // sectors from the sources, so it is not const
    void reportConflicts();

    // The same report, built from the sectors the alignment cross-check found
    // the sources disagreeing over. Sources that fail the cross-check are never
    // planned, so this is the only way to see what they fell out about - which
    // is exactly what is needed to tell a bad decode from a different version
    void reportAlignmentDisagreements();

    bool alignmentSuspect() const { return m_alignmentSuspect; }

    size_t sourceCount() const { return m_sources.size(); }
    const StackResult &result() const { return m_result; }
    const FilesystemAnalysis &filesystemAnalysis() const { return m_analysis; }

private:
    // Every byte the same, and that byte one of the decoder's padding values.
    // Such a sector carries no data, so agreement between sources on it says
    // nothing about whether either of them recovered anything
    static bool isFillSector(const std::vector<uint8_t> &buffer);

    // Of the given candidates, the content held by the most of them, and how
    // many held it. Ties are broken in favour of the earliest source given.
    // runnerUpCount, when asked for, receives the size of the largest group
    // holding anything other than the winning content
    static size_t mostCommonContent(const std::vector<size_t> &candidates,
                                    const std::vector<std::vector<uint8_t>> &buffers,
                                    uint32_t &agreementCount,
                                    uint32_t *runnerUpCount = nullptr);

    // Best fallback for a sector nothing vouches for: the content held by the
    // most sources, ignoring the ones holding nothing but padding
    static bool chooseFallback(const std::vector<size_t> &present,
                               const std::vector<std::vector<uint8_t>> &buffers,
                               size_t &chosen, uint32_t &agreementCount);

    // How many sources must hold identical content before a sector no source
    // vouches for is accepted, given how many sources hold it at all
    uint32_t consensusRequirement(size_t present) const;

    // Extend a short image to the length the filesystem says the disc is. Must
    // be run after the map is loaded and before the sectors that are still bad
    // are classified, since the padding is part of what is still bad
    void padToDeclaredLength();

    // Record one sector the sources disagreed about, and how they split over it
    void recordConflict(uint64_t sector, SectorOrigin origin, size_t chosen,
                        const std::vector<size_t> &candidates,
                        const std::vector<std::vector<uint8_t>> &buffers,
                        uint32_t agreement, uint32_t runnerUp);

    // Collect the splits seen across every recorded conflict, commonest first
    void summariseConflictPatterns();

    // Names of the sources in a bitmask, for the report
    std::string sourceList(uint64_t mask) const;

    // Hex dump one sector, one row per distinct content, showing only the rows
    // that differ
    void dumpConflict(const ConflictSector &conflict, bool asWarning);

    // Read one output sector from every source that holds it, filling good with
    // the sources that vouch for it and present with all of them
    void gatherSector(uint64_t sector, std::vector<std::vector<uint8_t>> &buffers,
                      std::vector<size_t> &good, std::vector<size_t> &present);

    // What each source would have contributed, measured against the filesystem
    void measureSourcesAgainstMap();

    bool writeBadSectorMap(const std::string &filename) const;

    static void logSectorRuns(const std::vector<uint32_t> &sectors, const VfsMap *map,
                              const std::string &prefix, size_t maxRunsAtInfo, bool asWarning = false);

    std::vector<std::unique_ptr<StackSource>> m_sources;
    std::vector<AlignmentPair> m_alignment;
    // A capped sample of the sectors two vouching sources disagreed over
    std::vector<uint32_t> m_alignmentDisagreements;
    std::vector<SectorPlan> m_plan;
    StackResult m_result;
    FilesystemAnalysis m_analysis;

    AdfsImage m_stackedImage;
    VfsMap m_map;

    uint64_t m_outputSectors = 0;
    // How long the image was before any padding was added, so that the report
    // can say what the captures actually reached
    uint64_t m_capturedSectors = 0;
    StackerOptions m_options;
    bool m_alignmentChecked = false;
    bool m_alignmentSuspect = false;
    // Some pair of sources failed on exactly the same sectors
    bool m_duplicateSuspect = false;
    bool m_planned = false;
};

// Presents the planned stack as a readable image, so that the filesystem can be
// parsed out of a merge that has not been written to disc
class StackedImageReader : public ImageReader
{
public:
    StackedImageReader(SectorStacker &stacker) : m_stacker(stacker) {}

    uint64_t size() const override { return m_stacker.outputSize(); }
    size_t read(uint64_t offset, uint8_t *buffer, size_t length) override;
    std::string description() const override { return "the stacked image"; }

private:
    SectorStacker &m_stacker;
    std::vector<uint8_t> m_sector;
};

#endif // SECTOR_STACKER_H
