/************************************************************************

    sector_stacker.cpp

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

#include "sector_stacker.h"
#include "logging.h"

#include <algorithm>

namespace {

// A pair of decodes of the same disc should agree on every sector both of them
// recovered. Anything above this is taken as evidence that they are not aligned
const double ALIGNMENT_DISAGREEMENT_LIMIT = 0.1;

// How many runs of remaining bad sectors to name in the summary report
const size_t MAX_RUNS_AT_INFO = 20;

// Badly matched sources can disagree in bulk, so the lists of individual sectors
// kept for the reports are samples. The counters beside them are the real totals
const size_t MAX_RECORDED_SECTORS = 4096;

// How many conflicting sectors to hex dump, and how many differing rows of each
const size_t MAX_CONFLICTS_AT_INFO = 20;
const size_t MAX_DIFF_ROWS_AT_INFO = 8;

// Bitmasks hold one bit per source, so the split analysis stops above this
const size_t MAX_SOURCES_FOR_PATTERNS = 64;

// A split that recurs over at least this share of the conflicts is systematic
// rather than incidental
const double PATTERN_DOMINANT_SHARE = 0.6;

// Below this many conflicts there is not enough to draw a conclusion from
const uint64_t PATTERN_MINIMUM_SECTORS = 4;

std::string megabytes(uint64_t bytes)
{
    return fmt::format("{:.1f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
}

std::string percent(uint64_t part, uint64_t whole)
{
    if (whole == 0) return "0.000%";
    return fmt::format("{:.3f}%", 100.0 * static_cast<double>(part) / static_cast<double>(whole));
}

size_t roleIndex(SectorRole role)
{
    return static_cast<size_t>(role);
}

// The values the decoder leaves behind where it recovered nothing
bool isFillByte(uint8_t value)
{
    return value == 0x00 || value == 0x20 || value == 0xFF;
}

} // namespace

// StackSource ---------------------------------------------------------------

bool StackSource::open(const std::string &imageFilename)
{
    m_imageFilename = imageFilename;

    // The decoder names the bad sector map after the image it wrote, by
    // appending .bsm to the whole filename rather than replacing the extension
    m_bsmFilename = imageFilename + ".bsm";

    m_file.open(imageFilename, std::ios::in | std::ios::binary);
    if (!m_file.is_open()) {
        LOG_ERROR("Could not open input image {} for reading", imageFilename);
        return false;
    }

    m_file.seekg(0, std::ios::end);
    m_imageSize = static_cast<uint64_t>(m_file.tellg());
    m_sectorCount = m_imageSize / EFM_SECTOR_SIZE;

    if (m_sectorCount == 0) {
        LOG_ERROR("Input image {} is smaller than one EFM sector ({} bytes)", imageFilename, EFM_SECTOR_SIZE);
        return false;
    }

    std::ifstream bsmTest(m_bsmFilename);
    if (!bsmTest.is_open()) {
        LOG_ERROR("Could not find the bad sector map {} for input image {}", m_bsmFilename, imageFilename);
        LOG_ERROR("The decoder writes this file alongside the image, named by appending .bsm to the "
                  "image filename");
        return false;
    }
    bsmTest.close();

    if (!m_badSectors.open(m_bsmFilename)) {
        return false;
    }

    return true;
}

bool StackSource::readSector(uint64_t sector, std::vector<uint8_t> &buffer)
{
    if (sector >= m_sectorCount) return false;

    buffer.resize(EFM_SECTOR_SIZE);
    m_file.seekg(static_cast<std::streamoff>(sector * EFM_SECTOR_SIZE), std::ios::beg);
    m_file.read(reinterpret_cast<char *>(buffer.data()), EFM_SECTOR_SIZE);

    if (static_cast<size_t>(m_file.gcount()) != EFM_SECTOR_SIZE) {
        m_file.clear();
        return false;
    }

    return true;
}

// StackedImageReader --------------------------------------------------------

size_t StackedImageReader::read(uint64_t offset, uint8_t *buffer, size_t length)
{
    const uint64_t imageSize = size();
    if (offset >= imageSize) return 0;

    const size_t available = static_cast<size_t>(std::min<uint64_t>(length, imageSize - offset));
    size_t done = 0;

    while (done < available) {
        const uint64_t position = offset + done;
        const uint64_t sector = position / EFM_SECTOR_SIZE;
        const size_t within = static_cast<size_t>(position % EFM_SECTOR_SIZE);
        const size_t chunk = std::min(available - done, EFM_SECTOR_SIZE - within);

        if (!m_stacker.readStackedSector(sector, m_sector)) return done;

        std::copy(m_sector.begin() + static_cast<long>(within),
                  m_sector.begin() + static_cast<long>(within + chunk),
                  buffer + done);
        done += chunk;
    }

    return done;
}

// SectorStacker -------------------------------------------------------------

SectorStacker::SectorStacker(const StackerOptions &options) :
    m_options(options)
{}

bool SectorStacker::addSource(const std::string &imageFilename)
{
    if (m_sources.size() >= SectorPlan::NO_SOURCE) {
        LOG_ERROR("Too many input images; at most {} can be stacked", SectorPlan::NO_SOURCE);
        return false;
    }

    auto source = std::make_unique<StackSource>();
    if (!source->open(imageFilename)) return false;

    m_outputSectors = std::max(m_outputSectors, source->sectorCount());
    m_sources.push_back(std::move(source));
    return true;
}

bool SectorStacker::isFillSector(const std::vector<uint8_t> &buffer)
{
    if (buffer.empty()) return true;

    const uint8_t first = buffer[0];
    if (first != 0x00 && first != 0x20 && first != 0xFF) return false;

    for (uint8_t value : buffer) {
        if (value != first) return false;
    }

    return true;
}

size_t SectorStacker::mostCommonContent(const std::vector<size_t> &candidates,
                                        const std::vector<std::vector<uint8_t>> &buffers,
                                        uint32_t &agreementCount,
                                        uint32_t *runnerUpCount)
{
    size_t best = candidates.front();
    agreementCount = 0;

    for (size_t i = 0; i < candidates.size(); ++i) {
        uint32_t count = 0;
        for (size_t j = 0; j < candidates.size(); ++j) {
            if (buffers[candidates[i]] == buffers[candidates[j]]) ++count;
        }

        if (count > agreementCount) {
            agreementCount = count;
            best = candidates[i];
        }
    }

    // The largest group holding anything but the winning content. A large runner
    // up means the sources are split into camps rather than merely noisy
    if (runnerUpCount != nullptr) {
        *runnerUpCount = 0;
        for (size_t i = 0; i < candidates.size(); ++i) {
            if (buffers[candidates[i]] == buffers[best]) continue;

            uint32_t count = 0;
            for (size_t j = 0; j < candidates.size(); ++j) {
                if (buffers[candidates[i]] == buffers[candidates[j]]) ++count;
            }
            if (count > *runnerUpCount) *runnerUpCount = count;
        }
    }

    return best;
}

bool SectorStacker::chooseFallback(const std::vector<size_t> &present,
                                   const std::vector<std::vector<uint8_t>> &buffers,
                                   size_t &chosen, uint32_t &agreementCount)
{
    if (present.empty()) return false;

    // Nothing here is trustworthy, but the sources still vote: content several
    // of them arrived at independently beats content only one of them holds,
    // even when none of them will vouch for it. Sources holding nothing but
    // padding are left out of the vote, since padding is what a decoder writes
    // when it recovered nothing at all
    std::vector<size_t> candidates;
    for (size_t index : present) {
        if (!isFillSector(buffers[index])) candidates.push_back(index);
    }

    if (candidates.empty()) {
        chosen = present.front();
        agreementCount = 0;
        return true;
    }

    chosen = mostCommonContent(candidates, buffers, agreementCount);
    return true;
}

void SectorStacker::recordConflict(uint64_t sector, SectorOrigin origin, size_t chosen,
                                   const std::vector<size_t> &candidates,
                                   const std::vector<std::vector<uint8_t>> &buffers,
                                   uint32_t agreement, uint32_t runnerUp)
{
    ++m_result.conflictCount;
    if (m_result.conflicts.size() >= MAX_RECORDED_SECTORS) return;
    if (m_sources.size() > MAX_SOURCES_FOR_PATTERNS) return;

    ConflictSector conflict;
    conflict.sector = static_cast<uint32_t>(sector);
    conflict.origin = origin;
    conflict.agreement = agreement;
    conflict.runnerUp = runnerUp;
    conflict.contested = (runnerUp >= 2);

    // Who sided with the content that was chosen, and who held the largest
    // alternative to it. Everything else is a lone dissenter and says nothing
    // about how the sources line up
    const std::vector<uint8_t> *runnerUpContent = nullptr;
    uint32_t runnerUpSeen = 0;

    for (size_t index : candidates) {
        if (buffers[index] == buffers[chosen]) continue;

        uint32_t count = 0;
        for (size_t other : candidates) {
            if (buffers[index] == buffers[other]) ++count;
        }
        if (count > runnerUpSeen) {
            runnerUpSeen = count;
            runnerUpContent = &buffers[index];
        }
    }

    for (size_t index : candidates) {
        const uint64_t bit = uint64_t(1) << index;
        if (buffers[index] == buffers[chosen]) {
            conflict.winners |= bit;
        } else if (runnerUpContent != nullptr && buffers[index] == *runnerUpContent) {
            conflict.losers |= bit;
        }
    }

    // How far apart the two sides are. Damage touches a few bytes; different
    // content differs all the way through
    if (runnerUpContent != nullptr) {
        const std::vector<uint8_t> &a = buffers[chosen];
        const std::vector<uint8_t> &b = *runnerUpContent;
        const size_t length = std::min(a.size(), b.size());
        bool inRun = false;

        for (size_t i = 0; i < length; ++i) {
            if (a[i] != b[i]) {
                ++conflict.differingBytes;
                // One side padding and the other data means one decode simply
                // got further here; both sides holding data means they disagree
                if (isFillByte(a[i]) != isFillByte(b[i])) ++conflict.fillOnlyBytes;
                if (!inRun) {
                    ++conflict.differingRuns;
                    inRun = true;
                }
            } else {
                inRun = false;
            }
        }
    }

    m_result.conflicts.push_back(conflict);
}

// The same sources dissenting over and over is not something decode damage does
void SectorStacker::summariseConflictPatterns()
{
    m_result.conflictPatterns.clear();

    for (const ConflictSector &conflict : m_result.conflicts) {
        auto it = std::find_if(m_result.conflictPatterns.begin(), m_result.conflictPatterns.end(),
            [&conflict](const ConflictPattern &pattern) {
                return pattern.winners == conflict.winners && pattern.losers == conflict.losers;
            });

        if (it == m_result.conflictPatterns.end()) {
            ConflictPattern pattern;
            pattern.winners = conflict.winners;
            pattern.losers = conflict.losers;
            pattern.sectors = 1;
            pattern.totalDifferingBytes = conflict.differingBytes;
            pattern.totalFillOnlyBytes = conflict.fillOnlyBytes;
            m_result.conflictPatterns.push_back(pattern);
        } else {
            ++it->sectors;
            it->totalDifferingBytes += conflict.differingBytes;
            it->totalFillOnlyBytes += conflict.fillOnlyBytes;
        }
    }

    std::sort(m_result.conflictPatterns.begin(), m_result.conflictPatterns.end(),
        [](const ConflictPattern &a, const ConflictPattern &b) { return a.sectors > b.sectors; });
}

std::string SectorStacker::sourceList(uint64_t mask) const
{
    std::string list;

    for (size_t index = 0; index < m_sources.size(); ++index) {
        if ((mask & (uint64_t(1) << index)) == 0) continue;
        if (!list.empty()) list += ", ";
        list += m_sources[index]->filename();
    }

    return list.empty() ? "(none)" : list;
}

uint32_t SectorStacker::consensusRequirement(size_t present) const
{
    switch (m_options.consensusMode) {
    case ConsensusMode::Off:
        return 0;

    case ConsensusMode::Fixed:
        return m_options.consensusThreshold;

    case ConsensusMode::Auto:
        // A majority of the sources that hold the sector, and never fewer than
        // two: one source agreeing with itself is not evidence of anything
        return std::max<uint32_t>(2, static_cast<uint32_t>(present / 2 + 1));
    }

    return 0;
}

void SectorStacker::gatherSector(uint64_t sector, std::vector<std::vector<uint8_t>> &buffers,
                                 std::vector<size_t> &good, std::vector<size_t> &present)
{
    good.clear();
    present.clear();

    for (size_t index = 0; index < m_sources.size(); ++index) {
        if (!m_sources[index]->hasSector(sector)) continue;
        if (!m_sources[index]->readSector(sector, buffers[index])) {
            LOG_WARN("Could not read EFM sector {} from {}", sector, m_sources[index]->filename());
            continue;
        }
        present.push_back(index);
        if (m_sources[index]->isGood(sector)) good.push_back(index);
    }
}

bool SectorStacker::checkAlignment()
{
    if (m_sources.size() < 2) return false;

    LOG_INFO("Cross-checking the sources against each other over {} EFM sector(s)...", m_outputSectors);

    m_alignment.clear();
    for (size_t a = 0; a < m_sources.size(); ++a) {
        for (size_t b = a + 1; b < m_sources.size(); ++b) {
            AlignmentPair pair;
            pair.sourceA = a;
            pair.sourceB = b;
            pair.identicalBadSectors =
                m_sources[a]->badSectors().sectors() == m_sources[b]->badSectors().sectors();
            m_alignment.push_back(pair);
        }
    }

    std::vector<std::vector<uint8_t>> buffers(m_sources.size());
    std::vector<bool> loaded(m_sources.size(), false);

    for (uint64_t sector = 0; sector < m_outputSectors; ++sector) {
        std::fill(loaded.begin(), loaded.end(), false);

        for (AlignmentPair &pair : m_alignment) {
            if (!m_sources[pair.sourceA]->isGood(sector) || !m_sources[pair.sourceB]->isGood(sector)) continue;

            for (size_t index : {pair.sourceA, pair.sourceB}) {
                if (loaded[index]) continue;
                if (!m_sources[index]->readSector(sector, buffers[index])) {
                    LOG_WARN("Could not read EFM sector {} from {} while cross-checking",
                        sector, m_sources[index]->filename());
                    buffers[index].assign(EFM_SECTOR_SIZE, 0);
                }
                loaded[index] = true;
            }

            ++pair.compared;
            if (buffers[pair.sourceA] != buffers[pair.sourceB]) {
                ++pair.disagreed;

                // Keep a sample so that a refused stack can still be explained.
                // Sectors arrive in order and several pairs can fall out over
                // the same one, so only the first mention of each is kept
                if (m_alignmentDisagreements.size() < MAX_RECORDED_SECTORS &&
                    (m_alignmentDisagreements.empty() ||
                     m_alignmentDisagreements.back() != static_cast<uint32_t>(sector))) {
                    m_alignmentDisagreements.push_back(static_cast<uint32_t>(sector));
                }
            }
        }
    }

    m_alignmentSuspect = false;
    m_duplicateSuspect = false;
    for (const AlignmentPair &pair : m_alignment) {
        if (pair.compared > 0 && pair.disagreementPercent() > ALIGNMENT_DISAGREEMENT_LIMIT) {
            m_alignmentSuspect = true;
        }
        if (pair.identicalBadSectors) m_duplicateSuspect = true;
    }

    m_alignmentChecked = true;
    return true;
}

bool SectorStacker::plan()
{
    if (!m_alignmentChecked) {
        LOG_ERROR("SectorStacker::plan() - checkAlignment() must be run before planning");
        return false;
    }

    if (m_alignmentSuspect && !m_options.force) {
        LOG_ERROR("Refusing to stack sources that do not agree with each other - see the alignment "
                  "cross-check above");
        LOG_ERROR("These images are either decodes of different discs, or one of them has gained or "
                  "lost sectors during decoding, which displaces everything after it");
        LOG_ERROR("The disagreement report below says which of the two it looks like; add "
                  "--show-conflicts to see the bytes themselves. Use --force to stack them anyway");
        return false;
    }

    LOG_INFO("Stacking {} EFM sector(s) from {} source(s)...", m_outputSectors, m_sources.size());

    m_result = StackResult();
    m_result.outputSectors = m_outputSectors;
    m_capturedSectors = m_outputSectors;
    m_plan.assign(static_cast<size_t>(m_outputSectors), SectorPlan());

    std::vector<std::vector<uint8_t>> buffers(m_sources.size());
    std::vector<size_t> good;
    std::vector<size_t> present;

    for (uint64_t sector = 0; sector < m_outputSectors; ++sector) {
        gatherSector(sector, buffers, good, present);

        SectorPlan decision;

        if (!good.empty()) {
            uint32_t agreement = 0;
            uint32_t runnerUp = 0;
            const size_t chosen = mostCommonContent(good, buffers, agreement, &runnerUp);
            decision.source = static_cast<uint16_t>(chosen);
            decision.agreement = static_cast<uint16_t>(agreement);

            if (agreement == good.size()) {
                decision.origin = SectorOrigin::Unanimous;
                ++m_result.unanimous;
            } else if (agreement * 2 > good.size()) {
                decision.origin = SectorOrigin::Majority;
                ++m_result.majority;
                LOG_DEBUG("EFM sector {} - {} of {} source(s) that recovered it disagree; taking the "
                          "majority content from {}",
                    sector, good.size() - agreement, good.size(), m_sources[chosen]->filename());
            } else {
                decision.origin = SectorOrigin::Split;
                ++m_result.split;
                LOG_DEBUG("EFM sector {} - the {} source(s) that recovered it all disagree; taking the "
                          "content from {}",
                    sector, good.size(), m_sources[chosen]->filename());
            }

            // Sources that vouched for a sector and still disagree are the
            // strongest evidence of all that they are not the same disc: each
            // decoder is telling us it read this cleanly
            if (agreement < good.size()) {
                recordConflict(sector, decision.origin, chosen, good, buffers, agreement, runnerUp);
            }

            ++m_sources[chosen]->sectorsUsed;
            if (good.size() == 1) ++m_sources[good.front()]->uniqueContribution;
        } else if (!present.empty()) {
            // Nothing vouches for this sector, but independent decoders do not
            // make the same mistake twice: if enough of them produced identical
            // content, and that content is not just padding, it is almost
            // certainly right
            const uint32_t required = consensusRequirement(present.size());

            uint32_t agreement = 0;
            uint32_t runnerUp = 0;
            const size_t candidate = mostCommonContent(present, buffers, agreement, &runnerUp);

            if (required > 0 && agreement >= required && !isFillSector(buffers[candidate])) {
                decision.origin = SectorOrigin::Consensus;
                decision.source = static_cast<uint16_t>(candidate);
                decision.agreement = static_cast<uint16_t>(agreement);
                ++m_result.consensus;
                ++m_sources[candidate]->sectorsUsed;

                if (agreement == present.size()) ++m_result.consensusUnanimous;

                // Not an alternative to being unanimous: with two sources a
                // sector is both, and resting on two sources is the fact worth
                // knowing about it
                if (agreement == 2) {
                    ++m_result.consensusThinCount;
                    if (m_result.consensusThin.size() < MAX_RECORDED_SECTORS) {
                        m_result.consensusThin.push_back(static_cast<uint32_t>(sector));
                    }
                }

                // Two or more sources holding a different answer is not noise;
                // it is the sources disagreeing about what the disc says
                if (runnerUp >= 2) {
                    ++m_result.consensusContestedCount;
                    if (m_result.consensusContested.size() < MAX_RECORDED_SECTORS) {
                        m_result.consensusContested.push_back(static_cast<uint32_t>(sector));
                    }
                    recordConflict(sector, SectorOrigin::Consensus, candidate, present, buffers,
                        agreement, runnerUp);
                }

                LOG_DEBUG("EFM sector {} - flagged bad by every source, but {} of {} agree on real "
                          "content; accepting it",
                    sector, agreement, present.size());
            }
        }

        if (decision.origin == SectorOrigin::Unrecovered) {
            ++m_result.unrecovered;
            m_result.outputBadSectors.push_back(static_cast<uint32_t>(sector));

            // Nothing here is trustworthy, but the output is the same length
            // whatever happens, so keep the copy the most sources arrived at
            size_t fallback = 0;
            uint32_t agreement = 0;
            if (chooseFallback(present, buffers, fallback, agreement)) {
                decision.source = static_cast<uint16_t>(fallback);
                decision.agreement = static_cast<uint16_t>(agreement);
            }
        }

        m_plan[static_cast<size_t>(sector)] = decision;
    }

    summariseConflictPatterns();

    m_planned = true;
    return true;
}

bool SectorStacker::readStackedSector(uint64_t sector, std::vector<uint8_t> &buffer)
{
    if (!m_planned || sector >= m_outputSectors) return false;

    const SectorPlan &decision = m_plan[static_cast<size_t>(sector)];

    if (decision.source == SectorPlan::NO_SOURCE) {
        // No source held this sector at all
        buffer.assign(EFM_SECTOR_SIZE, 0);
        return true;
    }

    if (!m_sources[decision.source]->readSector(sector, buffer)) {
        buffer.assign(EFM_SECTOR_SIZE, 0);
    }

    return true;
}

bool SectorStacker::analyseFilesystem()
{
    if (!m_planned) {
        LOG_ERROR("SectorStacker::analyseFilesystem() - plan() must be run first");
        return false;
    }

    LOG_INFO("Reading the filesystem of the stacked image...");

    m_analysis = FilesystemAnalysis();

    // Parse the filesystem straight out of the merge, so that the same answer is
    // given whether or not the output is actually written
    if (!m_stackedImage.attach(std::make_unique<StackedImageReader>(*this))) {
        LOG_WARN("No ADFS filesystem could be located in the stacked image");
        return false;
    }

    if (!m_map.load(m_stackedImage, false)) {
        LOG_WARN("The filesystem of the stacked image could not be read");
        return false;
    }

    m_analysis.loaded = true;

    // The filesystem knows how long the disc is, which is not always how much of
    // it the captures reached. Do this before classifying, since anything added
    // here is part of what the output still lacks
    padToDeclaredLength();

    // Which of the sectors that are still bad does the filesystem depend on?
    std::vector<StackedObjectDamage> damage;
    std::map<std::string, size_t> damageIndex;

    for (uint32_t efmSector : m_result.outputBadSectors) {
        const SectorRole role = m_map.classify(efmSector);
        ++m_analysis.byRole[roleIndex(role)];

        if (!isVitalRole(role)) continue;
        m_analysis.vitalSectors.push_back(efmSector);

        const VfsObject *object = m_map.objectForEfmSector(efmSector);
        if (object == nullptr) continue;

        auto it = damageIndex.find(object->name);
        if (it == damageIndex.end()) {
            StackedObjectDamage entry;
            entry.name = object->name;
            entry.byteLength = object->byteLength;
            damageIndex.emplace(object->name, damage.size());
            it = damageIndex.find(object->name);
            damage.push_back(entry);
        }

        StackedObjectDamage &entry = damage[it->second];
        ++entry.damagedEfmSectors;
        entry.damagedBytes += VfsMap::overlapBytes(*object, efmSector);
    }

    m_analysis.damagedObjects = damage;

    measureSourcesAgainstMap();

    return true;
}

// A capture that stopped short leaves an image shorter than the disc it came
// from, which is not damage the bad sector map can express: the sectors are not
// bad, they are absent. The free space map says how long the disc is, so the
// tail can be restored as empty sectors. Everything then sits at the offset the
// filesystem expects, and whatever depended on the missing tail is reported as
// missing rather than quietly ignored
void SectorStacker::padToDeclaredLength()
{
    if (!m_options.pad) return;

    const uint32_t discSectors = m_map.fsm().numberOfSectors();
    if (discSectors == 0) return;

    // A partial EFM sector at the end still has to be written whole
    const uint64_t declaredSize = m_map.declaredImageSize();
    const uint64_t declaredSectors = (declaredSize + EFM_SECTOR_SIZE - 1) / EFM_SECTOR_SIZE;

    if (declaredSectors <= m_outputSectors) {
        if (declaredSectors < m_outputSectors) {
            // Not padded and not truncated: the extra may be run-out the
            // filesystem does not describe, and throwing it away would be worse
            // than carrying it
            LOG_INFO("The sources run {} EFM sector(s) past the end of the disc the filesystem "
                     "describes; the extra is kept as it is",
                m_outputSectors - declaredSectors);
        }
        return;
    }

    // The length comes entirely from the free space map, so it is worth only as
    // much as the free space map is. Padding to a length read out of damaged
    // metadata would invent an image rather than complete one
    if (!m_map.locatedByValidation() || !m_map.fsmChecksumOk()) {
        LOG_WARN("The image is short of the {} EFM sector(s) the filesystem describes, but the free "
                 "space map did not validate, so its disc length cannot be trusted; leaving the "
                 "output at {} EFM sector(s)",
            declaredSectors, m_outputSectors);
        return;
    }

    if (!m_map.fsmTotalsConsistent()) {
        LOG_WARN("The image is short of the {} EFM sector(s) the filesystem describes, but the free "
                 "space map's own totals ({} used + {} free) do not add up to the {} sector(s) it "
                 "claims the disc holds; leaving the output at {} EFM sector(s)",
            declaredSectors, m_map.fsm().usedSectors(), m_map.fsm().freeSectors(), discSectors,
            m_outputSectors);
        return;
    }

    const uint64_t added = declaredSectors - m_outputSectors;

    // Nothing supplies these, so they take the default plan: no source, which
    // readStackedSector() already writes out as an empty sector
    m_plan.resize(static_cast<size_t>(declaredSectors));
    for (uint64_t sector = m_outputSectors; sector < declaredSectors; ++sector) {
        m_result.outputBadSectors.push_back(static_cast<uint32_t>(sector));
    }

    m_outputSectors = declaredSectors;
    m_result.outputSectors = m_outputSectors;
    m_result.padded = added;

    // classify() answers "past the end of the image" from the length it was
    // given at load, so it has to be told the image grew
    m_map.extendImageSize(m_outputSectors * EFM_SECTOR_SIZE);

    LOG_INFO("The captures reach {} EFM sector(s) but the filesystem describes a disc of {} ({}); "
             "padding the output with {} empty sector(s) ({})",
        m_capturedSectors, declaredSectors, megabytes(declaredSize), added,
        megabytes(added * EFM_SECTOR_SIZE));

    // A tail longer than what was captured is not a decode that stopped just
    // short, and zero-filling that much is worth a second look
    if (added > m_capturedSectors) {
        LOG_WARN("The padding is larger than the captured image itself - check that these sources are "
                 "complete decodes and that the disc length above is right (--no-pad leaves the "
                 "output at its captured length)");
    }
}

// For each source, how much of what the filesystem depends on it could not
// supply, and how much of it nothing else could
void SectorStacker::measureSourcesAgainstMap()
{
    for (uint64_t sector = 0; sector < m_outputSectors; ++sector) {
        const uint32_t efmSector = static_cast<uint32_t>(sector);
        if (!isVitalRole(m_map.classify(efmSector))) continue;

        size_t goodCount = 0;
        size_t lastGood = 0;

        for (size_t index = 0; index < m_sources.size(); ++index) {
            if (m_sources[index]->isGood(efmSector)) {
                ++goodCount;
                lastGood = index;
            } else {
                ++m_sources[index]->vitalBad;
            }
        }

        if (goodCount == 1) ++m_sources[lastGood]->uniqueVitalContribution;
    }
}

bool SectorStacker::write(const std::string &outputFilename)
{
    if (!m_planned) {
        LOG_ERROR("SectorStacker::write() - plan() must be run first");
        return false;
    }

    std::ofstream output(outputFilename, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        LOG_ERROR("Could not open output image {} for writing", outputFilename);
        return false;
    }

    LOG_INFO("Writing {} ({})...", outputFilename, megabytes(outputSize()));

    std::vector<uint8_t> buffer;
    for (uint64_t sector = 0; sector < m_outputSectors; ++sector) {
        readStackedSector(sector, buffer);

        output.write(reinterpret_cast<const char *>(buffer.data()), EFM_SECTOR_SIZE);
        if (!output) {
            LOG_ERROR("Write failed at EFM sector {} of output image {}", sector, outputFilename);
            return false;
        }
    }

    output.close();
    if (!output) {
        LOG_ERROR("Write failed on output image {}", outputFilename);
        return false;
    }

    return writeBadSectorMap(outputFilename + ".bsm");
}

bool SectorStacker::writeBadSectorMap(const std::string &filename) const
{
    std::ofstream file(filename, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        LOG_ERROR("Could not open output bad sector map {} for writing", filename);
        return false;
    }

    for (uint32_t sector : m_result.outputBadSectors) {
        file << sector << "\n";
    }

    file.close();
    if (!file) {
        LOG_ERROR("Write failed on output bad sector map {}", filename);
        return false;
    }

    return true;
}

// Print a list of sectors as runs, naming the object each run falls in when the
// filesystem is known
void SectorStacker::logSectorRuns(const std::vector<uint32_t> &sectors, const VfsMap *map,
                                  const std::string &prefix, size_t maxRunsAtInfo, bool asWarning)
{
    size_t runs = 0;
    size_t index = 0;

    while (index < sectors.size()) {
        size_t end = index;
        while (end + 1 < sectors.size() && sectors[end + 1] == sectors[end] + 1) ++end;

        const uint32_t length = static_cast<uint32_t>(end - index + 1);
        std::string line = (length == 1)
            ? fmt::format("{}{}", prefix, sectors[index])
            : fmt::format("{}{}-{} ({} sectors, {})", prefix, sectors[index], sectors[end], length,
                  megabytes(static_cast<uint64_t>(length) * EFM_SECTOR_SIZE));

        if (map != nullptr) {
            const VfsObject *object = map->objectForEfmSector(sectors[index]);
            if (object != nullptr) {
                line += fmt::format(" in {}", object->name);
            } else {
                line += fmt::format(" - {}", sectorRoleName(map->classify(sectors[index])));
            }
        }

        if (runs >= maxRunsAtInfo) {
            LOG_DEBUG("{}", line);
        } else if (asWarning) {
            LOG_WARN("{}", line);
        } else {
            LOG_INFO("{}", line);
        }

        ++runs;
        index = end + 1;
    }

    if (runs > maxRunsAtInfo) {
        LOG_INFO("{}...and {} further run(s), listed at debug level", prefix, runs - maxRunsAtInfo);
    }
}

void SectorStacker::reportSources() const
{
    LOG_INFO("Sources:");
    LOG_INFO("    {:<40} {:>10} {:>12} {:>10} {:>9}", "Image", "EFMSectors", "Size", "BadSectors", "Bad");

    for (const auto &source : m_sources) {
        LOG_INFO("    {:<40} {:>10} {:>12} {:>10} {:>9}",
            source->filename(),
            source->sectorCount(),
            megabytes(source->imageSize()),
            source->badSectors().count(),
            percent(source->badSectors().count(), source->sectorCount()));

        if (source->trailingBytes() != 0) {
            LOG_WARN("  {} is not a whole number of EFM sectors - the trailing {} byte(s) will be dropped",
                source->filename(), source->trailingBytes());
        }
        if (source->badSectors().malformedLines() > 0) {
            LOG_WARN("  {} contains {} malformed line(s), which were ignored",
                source->bsmFilename(), source->badSectors().malformedLines());
        }
    }

    // The sources need not be the same length; the shorter ones simply have
    // nothing to offer past their end
    bool ragged = false;
    for (const auto &source : m_sources) {
        if (source->sectorCount() != m_outputSectors) ragged = true;
    }

    if (ragged) {
        LOG_INFO("  Sources differ in length; the output holds {} EFM sector(s) ({}), the longest of them",
            m_outputSectors, megabytes(m_outputSectors * EFM_SECTOR_SIZE));
    }
}

void SectorStacker::reportAlignment() const
{
    LOG_INFO("Alignment cross-check:");
    LOG_INFO("  Decodes of the same disc must agree wherever both recovered a sector");
    LOG_INFO("    {:<40} {:>10} {:>10} {:>10}", "Pair", "Compared", "Disagreed", "Disagree");

    for (const AlignmentPair &pair : m_alignment) {
        LOG_INFO("    {:<40} {:>10} {:>10} {:>10}",
            fmt::format("{} vs {}", pair.sourceA + 1, pair.sourceB + 1),
            pair.compared,
            pair.disagreed,
            percent(pair.disagreed, pair.compared));
    }

    if (m_alignmentSuspect) {
        LOG_ERROR("  The sources do not agree with each other - they are not aligned");
    } else {
        LOG_INFO("  All source pairs agree - the images are aligned with each other");
    }

    // Stacking assumes the sources failed independently. A pair that failed in
    // exactly the same places probably did not
    if (m_duplicateSuspect) {
        for (const AlignmentPair &pair : m_alignment) {
            if (!pair.identicalBadSectors) continue;
            LOG_WARN("  {} and {} have identical bad sector maps - they look like the same decode "
                     "rather than two independent attempts",
                m_sources[pair.sourceA]->filename(), m_sources[pair.sourceB]->filename());
        }
        LOG_WARN("  Sources that are not independent add nothing to the stack, and they make sectors "
                 "look better agreed-upon than they are");
    }
}

void SectorStacker::reportResult() const
{
    const uint64_t covered = m_result.outputSectors - m_result.totalBad();

    LOG_INFO("Stacking result:");
    LOG_INFO("  Output holds {} EFM sector(s) ({})",
        m_result.outputSectors, megabytes(m_result.outputSectors * EFM_SECTOR_SIZE));

    if (m_result.padded > 0) {
        LOG_INFO("  Of which captured            : {} ({}); the rest is padding to the disc length",
            m_capturedSectors, megabytes(m_capturedSectors * EFM_SECTOR_SIZE));
    }
    LOG_INFO("  Recovered by agreement       : {} ({})",
        m_result.unanimous, percent(m_result.unanimous, m_result.outputSectors));

    if (m_result.majority > 0) {
        LOG_WARN("  Recovered by majority vote   : {} - sources that recovered these disagreed",
            m_result.majority);
    }
    if (m_result.split > 0) {
        LOG_WARN("  Recovered but split          : {} - no majority; the first source was taken",
            m_result.split);
    }
    if (m_options.consensusMode != ConsensusMode::Off) {
        const std::string rule = (m_options.consensusMode == ConsensusMode::Fixed)
            ? fmt::format("{}+ sources agree", m_options.consensusThreshold)
            : "a majority of sources agree";
        LOG_INFO("  Recovered by consensus       : {} - flagged bad everywhere, but {}",
            m_result.consensus, rule);

        if (m_result.consensus > 0) {
            LOG_INFO("    Every source agreed        : {}", m_result.consensusUnanimous);

            // Two sources agreeing is the least the tool will act on, so say so
            // rather than letting it pass as though it were unanimous
            if (m_result.consensusThinCount > 0) {
                LOG_WARN("    Accepted on two sources    : {} - the weakest evidence the stack acts on; "
                         "use --no-consensus to reject them",
                    m_result.consensusThinCount);
                logSectorRuns(m_result.consensusThin, m_analysis.loaded ? &m_map : nullptr,
                    "      ", MAX_RUNS_AT_INFO, true);
            }

            // Sources splitting into camps is a different problem from noise
            if (m_result.consensusContestedCount > 0) {
                LOG_WARN("    Contested                  : {} - two or more sources held a different "
                         "answer; see the source disagreement report below",
                    m_result.consensusContestedCount);
                logSectorRuns(m_result.consensusContested, m_analysis.loaded ? &m_map : nullptr,
                    "      ", MAX_RUNS_AT_INFO, true);
            }
        }
    } else if (m_result.unrecovered > 0) {
        LOG_INFO("  Consensus recovery is off; sectors every source flagged as bad were left bad even "
                 "where the sources agree on their content");
    }

    LOG_INFO("  Still bad in every source    : {} ({})",
        m_result.unrecovered, percent(m_result.unrecovered, m_result.outputSectors));

    if (m_result.padded > 0) {
        LOG_WARN("  Padding at the end           : {} ({}) - no source reached this far, so these are "
                 "empty and listed as bad",
            m_result.padded, percent(m_result.padded, m_result.outputSectors));
    }

    LOG_INFO("  Good sectors in the output   : {} of {} ({})",
        covered, m_result.outputSectors, percent(covered, m_result.outputSectors));

    // "Vital" columns are only meaningful once the filesystem has been read
    const bool haveMap = m_analysis.loaded;

    LOG_INFO("Source contribution:");
    if (haveMap) {
        LOG_INFO("    {:<40} {:>10} {:>10} {:>10} {:>10}",
            "Image", "Used", "OnlyGood", "VitalOnly", "VitalBad");
        for (const auto &source : m_sources) {
            LOG_INFO("    {:<40} {:>10} {:>10} {:>10} {:>10}",
                source->filename(), source->sectorsUsed, source->uniqueContribution,
                source->uniqueVitalContribution, source->vitalBad);
        }
        LOG_INFO("  OnlyGood  - sectors no other source recovered; what stacking that source gained");
        LOG_INFO("  VitalOnly - of those, the ones the filesystem actually depends on");
        LOG_INFO("  VitalBad  - sectors the filesystem depends on that this source could not supply");
    } else {
        LOG_INFO("    {:<40} {:>10} {:>10}", "Image", "Used", "OnlyGood");
        for (const auto &source : m_sources) {
            LOG_INFO("    {:<40} {:>10} {:>10}",
                source->filename(), source->sectorsUsed, source->uniqueContribution);
        }
        LOG_INFO("  OnlyGood counts sectors no other source recovered - what stacking that source gained");
    }

    // Measure the result against the best the sources could manage alone
    uint64_t bestSingle = UINT64_MAX;
    std::string bestSingleName;
    for (const auto &source : m_sources) {
        // A source that is short of the output length is also missing everything
        // past its end, and its map may name sectors it never had
        uint64_t bad = 0;
        for (uint64_t sector = 0; sector < m_outputSectors; ++sector) {
            if (!source->isGood(sector)) ++bad;
        }

        if (bad < bestSingle) {
            bestSingle = bad;
            bestSingleName = source->filename();
        }
    }

    // Both figures are measured over the padded length, so a source that stopped
    // short is charged for the tail it never reached, as the output is
    LOG_INFO("Improvement:");
    LOG_INFO("  Best single source  : {} with {} bad EFM sector(s)", bestSingleName, bestSingle);
    LOG_INFO("  Stacked output      : {} bad EFM sector(s)", m_result.totalBad());
    if (bestSingle > m_result.totalBad()) {
        LOG_INFO("  Stacking recovered {} EFM sector(s) ({}) that the best single source lacked",
            bestSingle - m_result.totalBad(),
            megabytes((bestSingle - m_result.totalBad()) * EFM_SECTOR_SIZE));
    }
}

// One 16-byte row of a sector, in the usual hex-and-ASCII form
static std::string hexRow(const std::vector<uint8_t> &buffer, size_t offset)
{
    std::string hex;
    std::string ascii;

    for (size_t i = 0; i < 16; ++i) {
        if (offset + i < buffer.size()) {
            const uint8_t value = buffer[offset + i];
            hex += fmt::format("{:02x} ", value);
            ascii += (value >= 0x20 && value < 0x7F) ? static_cast<char>(value) : '.';
        } else {
            hex += "   ";
            ascii += ' ';
        }
    }

    return fmt::format("{}|{}|", hex, ascii);
}

// Show one sector as the sources hold it: one block per distinct content, and
// only the rows where they differ
void SectorStacker::dumpConflict(const ConflictSector &conflict, bool asWarning)
{
    std::vector<std::vector<uint8_t>> buffers(m_sources.size());
    std::vector<size_t> good;
    std::vector<size_t> present;
    gatherSector(conflict.sector, buffers, good, present);

    if (present.empty()) {
        LOG_WARN("  EFM sector {} could not be re-read from any source", conflict.sector);
        return;
    }

    // Group the sources by the content they hold, commonest first, with the
    // content that was chosen leading
    std::vector<std::vector<size_t>> variants;
    for (size_t index : present) {
        auto it = std::find_if(variants.begin(), variants.end(),
            [&](const std::vector<size_t> &variant) { return buffers[variant.front()] == buffers[index]; });

        if (it == variants.end()) {
            variants.push_back({index});
        } else {
            it->push_back(index);
        }
    }

    std::sort(variants.begin(), variants.end(),
        [&](const std::vector<size_t> &a, const std::vector<size_t> &b) {
            const bool aChosen = (conflict.winners & (uint64_t(1) << a.front())) != 0;
            const bool bChosen = (conflict.winners & (uint64_t(1) << b.front())) != 0;
            if (aChosen != bChosen) return aChosen;
            return a.size() > b.size();
        });

    const char *originName = "";
    switch (conflict.origin) {
    case SectorOrigin::Majority:  originName = "majority won"; break;
    case SectorOrigin::Split:     originName = "no majority; first source taken"; break;
    case SectorOrigin::Consensus: originName = "consensus recovery, contested"; break;
    default:                      originName = "disagreement"; break;
    }

    std::string where;
    if (m_analysis.loaded) {
        const VfsObject *object = m_map.objectForEfmSector(conflict.sector);
        where = (object != nullptr)
            ? fmt::format(" in {}", object->name)
            : fmt::format(" - {}", sectorRoleName(m_map.classify(conflict.sector)));
    }

    const std::string header = fmt::format("  EFM sector {}{} at 0x{:X} - {}, {} byte(s) differ in "
                                           "{} run(s) ({} padding against data)",
        conflict.sector, where, static_cast<uint64_t>(conflict.sector) * EFM_SECTOR_SIZE, originName,
        conflict.differingBytes, conflict.differingRuns, conflict.fillOnlyBytes);

    if (asWarning) LOG_WARN("{}", header); else LOG_INFO("{}", header);

    // Which sources hold which content, and whether each vouched for it
    char letter = 'A';
    for (const std::vector<size_t> &variant : variants) {
        std::string names;
        for (size_t index : variant) {
            if (!names.empty()) names += ", ";
            names += fmt::format("{} ({})", m_sources[index]->filename(),
                m_sources[index]->isGood(conflict.sector) ? "good" : "bad");
        }

        const bool chosen = (conflict.winners & (uint64_t(1) << variant.front())) != 0;
        LOG_INFO("    {} - {} source(s){}: {}", letter, variant.size(), chosen ? ", chosen" : "", names);
        ++letter;
    }

    // Only the rows that actually differ are worth printing: a 2048-byte sector
    // in full would bury the handful of bytes in question
    size_t rowsShown = 0;
    for (size_t offset = 0; offset < EFM_SECTOR_SIZE; offset += 16) {
        std::string markers;
        bool rowDiffers = false;

        for (size_t i = 0; i < 16; ++i) {
            bool byteDiffers = false;
            for (const std::vector<size_t> &variant : variants) {
                const std::vector<uint8_t> &first = buffers[variants.front().front()];
                const std::vector<uint8_t> &other = buffers[variant.front()];
                if (offset + i < first.size() && offset + i < other.size() &&
                    first[offset + i] != other[offset + i]) {
                    byteDiffers = true;
                    break;
                }
            }

            markers += byteDiffers ? "^^ " : "   ";
            if (byteDiffers) rowDiffers = true;
        }

        if (!rowDiffers) continue;

        // Past the cap the rest still goes to the log file, just not the console
        const bool atInfo = (rowsShown < MAX_DIFF_ROWS_AT_INFO);
        const std::string offsetField = fmt::format("0x{:04X}", offset);
        const std::string blankField(offsetField.size(), ' ');

        letter = 'A';
        for (const std::vector<size_t> &variant : variants) {
            const std::string line = fmt::format("      {}  {}  {}",
                offsetField, letter, hexRow(buffers[variant.front()], offset));
            if (atInfo) LOG_INFO("{}", line); else LOG_DEBUG("{}", line);
            ++letter;
        }

        const std::string markerLine = fmt::format("      {}     {}", blankField, markers);
        if (atInfo) LOG_INFO("{}", markerLine); else LOG_DEBUG("{}", markerLine);

        ++rowsShown;
    }

    if (rowsShown > MAX_DIFF_ROWS_AT_INFO) {
        LOG_INFO("      ...and {} further differing row(s), listed at debug level",
            rowsShown - MAX_DIFF_ROWS_AT_INFO);
    }
}

void SectorStacker::reportAlignmentDisagreements()
{
    if (m_alignmentDisagreements.empty()) return;

    LOG_INFO("Examining what the sources fell out about...");

    // Nothing has been planned, so build the conflict records from the sample
    // the cross-check kept
    m_result.conflicts.clear();
    m_result.conflictCount = 0;

    std::vector<std::vector<uint8_t>> buffers(m_sources.size());
    std::vector<size_t> good;
    std::vector<size_t> present;

    for (uint32_t sector : m_alignmentDisagreements) {
        gatherSector(sector, buffers, good, present);
        if (good.size() < 2) continue;

        uint32_t agreement = 0;
        uint32_t runnerUp = 0;
        const size_t chosen = mostCommonContent(good, buffers, agreement, &runnerUp);
        if (agreement == good.size()) continue;

        const SectorOrigin origin = (agreement * 2 > good.size())
            ? SectorOrigin::Majority : SectorOrigin::Split;
        recordConflict(sector, origin, chosen, good, buffers, agreement, runnerUp);
    }

    summariseConflictPatterns();
    reportConflicts();
}

void SectorStacker::reportConflicts()
{
    if (m_result.conflictCount == 0) {
        if (m_options.showConflicts) {
            LOG_INFO("Source disagreement: none - wherever two sources both held a sector, they held "
                     "the same bytes");
        }
        return;
    }

    LOG_WARN("Source disagreement ({} sector(s) where the sources held different content):",
        m_result.conflictCount);

    // The question this answers is whether the sources are the same disc. Decode
    // damage is random, so the dissenting sources change from sector to sector.
    // A different pressing is not random: the same sources dissent every time
    if (!m_result.conflictPatterns.empty()) {
        LOG_INFO("  How the sources split:");

        size_t shown = 0;
        for (const ConflictPattern &pattern : m_result.conflictPatterns) {
            if (shown >= 4) {
                LOG_INFO("    ...and {} further split(s)", m_result.conflictPatterns.size() - shown);
                break;
            }

            LOG_INFO("    {} sector(s): {}", pattern.sectors, sourceList(pattern.winners));
            LOG_INFO("      differing from            : {}", sourceList(pattern.losers));
            LOG_INFO("      average {} byte(s) of {} differ per sector, {} of them padding on one "
                     "side and data on the other",
                pattern.totalDifferingBytes / std::max<uint64_t>(1, pattern.sectors), EFM_SECTOR_SIZE,
                pattern.totalFillOnlyBytes / std::max<uint64_t>(1, pattern.sectors));
            ++shown;
        }

        const ConflictPattern &top = m_result.conflictPatterns.front();
        const double share = static_cast<double>(top.sectors) /
                             static_cast<double>(m_result.conflicts.size());
        const bool consistentSplit =
            m_result.conflicts.size() >= PATTERN_MINIMUM_SECTORS &&
            share >= PATTERN_DOMINANT_SHARE && top.losers != 0;

        // Whether a source vouched for the sector decides which question is
        // even being asked. A decoder that flagged a sector good is asserting it
        // read the disc correctly, so two of them differing means the discs
        // differ. Where nothing vouched for it, both sides are guesses and the
        // difference is far more likely to be how much each one recovered
        uint64_t vouched = 0;
        uint64_t unvouchedDiffering = 0;
        uint64_t unvouchedFillOnly = 0;

        for (const ConflictSector &conflict : m_result.conflicts) {
            if (conflict.origin != SectorOrigin::Consensus) {
                ++vouched;
            } else {
                unvouchedDiffering += conflict.differingBytes;
                unvouchedFillOnly += conflict.fillOnlyBytes;
            }
        }

        LOG_INFO("  Interpreting this:");

        if (vouched > 0) {
            LOG_WARN("    {} of these are sectors the sources vouched for and still disagree on. A "
                     "decoder that flags a sector good believes it read the disc correctly, so two "
                     "of them differing means the discs themselves differ",
                vouched);

            if (consistentSplit) {
                LOG_WARN("    The same sources take opposite sides in {} of {} cases. Decode damage "
                         "does not repeat like that - these are different versions of the disc",
                    top.sectors, m_result.conflicts.size());
                LOG_WARN("    Stack each version separately rather than merging them, or the output "
                         "will be a splice of both");
            } else {
                LOG_WARN("    Which sources dissent varies from sector to sector, so this looks more "
                         "like one decoder wrongly vouching for damaged sectors than a difference "
                         "between versions - but it is worth looking at the bytes");
            }
        } else {
            // Nothing vouched for any of them, so the fill test is the useful
            // one: padding against data is a recovery difference, and data
            // against data in an area no decoder trusted is simply noise
            const bool mostlyRecovery =
                unvouchedDiffering > 0 &&
                static_cast<double>(unvouchedFillOnly) /
                    static_cast<double>(unvouchedDiffering) >= 0.5;

            LOG_INFO("    Every one of these is a sector no source vouched for, and the sources agree "
                     "byte-for-byte everywhere they all decoded cleanly. Whatever they are, they are "
                     "not a disagreement about readable content");

            if (mostlyRecovery) {
                LOG_INFO("    Most of the differing bytes are padding on one side and data on the "
                         "other, which is one decode having got further than the other rather than "
                         "the two holding different content");
            } else if (consistentSplit) {
                LOG_INFO("    The same sources take opposite sides each time, which in an area no "
                         "decoder trusted usually means those sources share a capture problem there "
                         "rather than being a different version");
            } else {
                LOG_INFO("    Which sources dissent varies from sector to sector, which is what "
                         "decode damage looks like");
            }
        }
    }

    if (!m_options.showConflicts) {
        LOG_INFO("  Use --show-conflicts to hex dump what the sources disagree about");
        return;
    }

    if (m_result.conflictCount > m_result.conflicts.size()) {
        LOG_INFO("  Dumping the first {} of {}", m_result.conflicts.size(), m_result.conflictCount);
    }

    size_t dumped = 0;
    for (const ConflictSector &conflict : m_result.conflicts) {
        if (dumped >= MAX_CONFLICTS_AT_INFO) {
            LOG_INFO("  ...and {} further sector(s), not dumped",
                m_result.conflicts.size() - dumped);
            break;
        }

        dumpConflict(conflict, conflict.origin != SectorOrigin::Consensus);
        ++dumped;
    }
}

void SectorStacker::reportFilesystem(const std::string &outputFilename) const
{
    const std::string target = outputFilename.empty() ? "the stacked image" : outputFilename;

    if (!m_analysis.loaded) {
        LOG_ERROR("Filesystem check:");
        LOG_ERROR("  No ADFS filesystem could be read from the stacked image, so there is no way to "
                  "tell whether the sectors that are still bad matter");
        if (m_result.totalBad() > 0) {
            LOG_INFO("Remaining bad sectors ({} distinct sector(s), bad in every source):",
                m_result.totalBad());
            logSectorRuns(m_result.outputBadSectors, nullptr, "    ", MAX_RUNS_AT_INFO);
        }
        LOG_ERROR("SectorStacker - RESULT: UNKNOWN - the filesystem of {} could not be read", target);
        return;
    }

    LOG_INFO("Filesystem check:");
    LOG_INFO("  ADFS sector 0 at file offset 0x{:X} ({} bytes into the image)",
        m_map.sector0Position(), m_map.sector0Position());

    if (m_map.locatedByValidation()) {
        LOG_INFO("  Located by validated \"Hugo\" signature (free space map checksums and root "
                 "directory header/footer both agree)");
    } else {
        LOG_WARN("  No \"Hugo\" signature passed validation - the filesystem was located by falling "
                 "back to the first signature found and may be wrong");
    }

    LOG_INFO("  Directory \"{}\" title \"{}\" - {} object(s) occupying {} EFM sector(s)",
        m_map.directory().directoryName(), m_map.directory().directoryTitle(),
        m_map.objects().size(), m_map.fileEfmSectors().size());
    LOG_INFO("  Free space map describes {} ADFS sector(s); {} used, {} free",
        m_map.fsm().numberOfSectors(), m_map.fsm().usedSectors(), m_map.fsm().freeSectors());

    if (!m_map.fsmChecksumOk()) {
        LOG_ERROR("  Free space map checksums FAILED - the map above may be wrong");
    }
    if (!m_map.directoryOk()) {
        LOG_ERROR("  Root directory header/footer do not agree - the object list above may be wrong");
    }

    // What the filesystem makes of the sectors that are still bad
    if (m_result.padded > 0) {
        LOG_INFO("Remaining bad sectors by role ({} sector(s): {} bad in every source, {} padding):",
            m_result.totalBad(), m_result.unrecovered, m_result.padded);
    } else {
        LOG_INFO("Remaining bad sectors by role ({} sector(s) still bad in every source):",
            m_result.unrecovered);
    }
    LOG_INFO("  Within file data             : {}", m_analysis.byRole[roleIndex(SectorRole::FileData)]);
    LOG_INFO("  Filesystem metadata          : {}", m_analysis.byRole[roleIndex(SectorRole::Metadata)]);
    LOG_INFO("  Allocated, not in any object : {}",
        m_analysis.byRole[roleIndex(SectorRole::AllocatedUnlisted)]);
    LOG_INFO("  Before the filesystem        : {} (harmless)",
        m_analysis.byRole[roleIndex(SectorRole::BeforeFilesystem)]);
    LOG_INFO("  Free space                   : {} (harmless)",
        m_analysis.byRole[roleIndex(SectorRole::FreeSpace)]);
    LOG_INFO("  Past the end of the image    : {} (harmless)",
        m_analysis.byRole[roleIndex(SectorRole::PastEndOfImage)]);

    if (!m_analysis.damagedObjects.empty()) {
        LOG_WARN("Objects still damaged:");
        LOG_WARN("    {:<10} {:>12} {:>10} {:>12} {:>9}",
            "Object", "Length", "BadEFMSec", "BytesLost", "Intact");
        for (const StackedObjectDamage &damage : m_analysis.damagedObjects) {
            const double intact = (damage.byteLength == 0) ? 100.0
                : 100.0 * (1.0 - (static_cast<double>(damage.damagedBytes) /
                                  static_cast<double>(damage.byteLength)));
            LOG_WARN("    {:<10} {:>12} {:>10} {:>12} {:>8.3f}%",
                damage.name, damage.byteLength, damage.damagedEfmSectors, damage.damagedBytes, intact);
        }
    }

    if (!m_analysis.vitalSectors.empty()) {
        LOG_WARN("Sectors the filesystem depends on that are still bad:");
        logSectorRuns(m_analysis.vitalSectors, &m_map, "    ", MAX_RUNS_AT_INFO, true);
    }

    // The harmless remainder, for completeness
    if (m_result.totalBad() > m_analysis.vitalCount()) {
        LOG_INFO("Remaining bad sectors that cost nothing ({} sector(s)):",
            m_result.totalBad() - m_analysis.vitalCount());
        std::vector<uint32_t> harmless;
        for (uint32_t efmSector : m_result.outputBadSectors) {
            if (!isVitalRole(m_map.classify(efmSector))) harmless.push_back(efmSector);
        }
        logSectorRuns(harmless, &m_map, "    ", MAX_RUNS_AT_INFO);
    }

    // The verdict
    if (m_analysis.vitalCount() == 0) {
        LOG_INFO("SectorStacker - RESULT: GOOD - every sector the filesystem depends on was recovered; "
                 "{} still holds {} bad sector(s) but none of them affect file data or metadata",
            target, m_result.totalBad());
    } else {
        LOG_ERROR("SectorStacker - RESULT: INCOMPLETE - {} sector(s) that {} still lacks are needed by "
                  "the filesystem ({} in file data, {} in metadata, {} allocated but unlisted); more "
                  "sources are needed to fill them",
            m_analysis.vitalCount(), target,
            m_analysis.byRole[roleIndex(SectorRole::FileData)],
            m_analysis.byRole[roleIndex(SectorRole::Metadata)],
            m_analysis.byRole[roleIndex(SectorRole::AllocatedUnlisted)]);

        // Some of what is missing may be within reach of a looser rule. Say so
        // rather than leaving the reader to guess that there is a knob at all
        uint32_t within = 0;
        uint32_t lowest = UINT32_MAX;
        for (uint32_t efmSector : m_analysis.vitalSectors) {
            const uint16_t agreement = m_plan[efmSector].agreement;
            if (agreement < 2) continue;
            ++within;
            lowest = std::min(lowest, static_cast<uint32_t>(agreement));
        }

        if (within > 0) {
            if (m_options.consensusMode == ConsensusMode::Off) {
                LOG_WARN("  {} of them have {}+ sources holding identical content; consensus recovery "
                         "is switched off, and dropping --no-consensus would accept them",
                    within, lowest);
            } else {
                LOG_WARN("  {} of them have {}+ sources holding identical content, which is below the "
                         "threshold in force; --consensus {} would accept them, on weaker evidence "
                         "than the default rule asks for",
                    within, lowest, lowest);
            }
        }
    }
}
