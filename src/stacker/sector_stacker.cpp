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

SectorStacker::SectorStacker(uint32_t consensusThreshold, bool force) :
    m_consensusThreshold(consensusThreshold),
    m_force(force)
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
                                        uint32_t &agreementCount)
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

    return best;
}

bool SectorStacker::chooseFallback(const std::vector<size_t> &present,
                                   const std::vector<std::vector<uint8_t>> &buffers,
                                   size_t &chosen)
{
    if (present.empty()) return false;

    for (size_t index : present) {
        if (!isFillSector(buffers[index])) {
            chosen = index;
            return true;
        }
    }

    chosen = present.front();
    return true;
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
            if (buffers[pair.sourceA] != buffers[pair.sourceB]) ++pair.disagreed;
        }
    }

    m_alignmentSuspect = false;
    for (const AlignmentPair &pair : m_alignment) {
        if (pair.compared > 0 && pair.disagreementPercent() > ALIGNMENT_DISAGREEMENT_LIMIT) {
            m_alignmentSuspect = true;
        }
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

    if (m_alignmentSuspect && !m_force) {
        LOG_ERROR("Refusing to stack sources that do not agree with each other - see the alignment "
                  "cross-check above");
        LOG_ERROR("These images are either decodes of different discs, or one of them has gained or "
                  "lost sectors during decoding, which displaces everything after it");
        LOG_ERROR("Use --force to stack them anyway");
        return false;
    }

    LOG_INFO("Stacking {} EFM sector(s) from {} source(s)...", m_outputSectors, m_sources.size());

    m_result = StackResult();
    m_result.outputSectors = m_outputSectors;
    m_plan.assign(static_cast<size_t>(m_outputSectors), SectorPlan());

    std::vector<std::vector<uint8_t>> buffers(m_sources.size());
    std::vector<size_t> good;
    std::vector<size_t> present;

    for (uint64_t sector = 0; sector < m_outputSectors; ++sector) {
        gatherSector(sector, buffers, good, present);

        SectorPlan decision;

        if (!good.empty()) {
            uint32_t agreement = 0;
            const size_t chosen = mostCommonContent(good, buffers, agreement);
            decision.source = static_cast<uint16_t>(chosen);

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

            ++m_sources[chosen]->sectorsUsed;
            if (good.size() == 1) ++m_sources[good.front()]->uniqueContribution;
        } else if (m_consensusThreshold > 0 && present.size() >= m_consensusThreshold) {
            // Nothing vouches for this sector, but independent decoders do not
            // make the same mistake twice: if enough of them produced identical
            // content, and that content is not just padding, it is almost
            // certainly right
            uint32_t agreement = 0;
            const size_t candidate = mostCommonContent(present, buffers, agreement);

            if (agreement >= m_consensusThreshold && !isFillSector(buffers[candidate])) {
                decision.origin = SectorOrigin::Consensus;
                decision.source = static_cast<uint16_t>(candidate);
                ++m_result.consensus;
                ++m_sources[candidate]->sectorsUsed;
                LOG_DEBUG("EFM sector {} - flagged bad by every source, but {} of {} agree on real "
                          "content; accepting it",
                    sector, agreement, present.size());
            }
        }

        if (decision.origin == SectorOrigin::Unrecovered) {
            ++m_result.unrecovered;
            m_result.outputBadSectors.push_back(static_cast<uint32_t>(sector));

            // Nothing here is trustworthy, but the output is the same length
            // whatever happens, so keep whichever copy holds the most data
            size_t fallback = 0;
            if (chooseFallback(present, buffers, fallback)) {
                decision.source = static_cast<uint16_t>(fallback);
            }
        }

        m_plan[static_cast<size_t>(sector)] = decision;
    }

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
}

void SectorStacker::reportResult() const
{
    const uint64_t covered = m_result.outputSectors - m_result.unrecovered;

    LOG_INFO("Stacking result:");
    LOG_INFO("  Output holds {} EFM sector(s) ({})",
        m_result.outputSectors, megabytes(m_result.outputSectors * EFM_SECTOR_SIZE));
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
    if (m_consensusThreshold > 0) {
        LOG_INFO("  Recovered by consensus       : {} - flagged bad everywhere, but {}+ sources agree",
            m_result.consensus, m_consensusThreshold);
    }

    LOG_INFO("  Still bad in every source    : {} ({})",
        m_result.unrecovered, percent(m_result.unrecovered, m_result.outputSectors));
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

    LOG_INFO("Improvement:");
    LOG_INFO("  Best single source  : {} with {} bad EFM sector(s)", bestSingleName, bestSingle);
    LOG_INFO("  Stacked output      : {} bad EFM sector(s)", m_result.unrecovered);
    if (bestSingle > m_result.unrecovered) {
        LOG_INFO("  Stacking recovered {} EFM sector(s) ({}) that the best single source lacked",
            bestSingle - m_result.unrecovered,
            megabytes((bestSingle - m_result.unrecovered) * EFM_SECTOR_SIZE));
    }
}

void SectorStacker::reportFilesystem(const std::string &outputFilename) const
{
    const std::string target = outputFilename.empty() ? "the stacked image" : outputFilename;

    if (!m_analysis.loaded) {
        LOG_ERROR("Filesystem check:");
        LOG_ERROR("  No ADFS filesystem could be read from the stacked image, so there is no way to "
                  "tell whether the sectors that are still bad matter");
        if (m_result.unrecovered > 0) {
            LOG_INFO("Remaining bad sectors ({} distinct sector(s), bad in every source):",
                m_result.unrecovered);
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
    LOG_INFO("Remaining bad sectors by role ({} sector(s) still bad in every source):",
        m_result.unrecovered);
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
    if (m_result.unrecovered > m_analysis.vitalCount()) {
        LOG_INFO("Remaining bad sectors that cost nothing ({} sector(s)):",
            m_result.unrecovered - m_analysis.vitalCount());
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
            target, m_result.unrecovered);
    } else {
        LOG_ERROR("SectorStacker - RESULT: INCOMPLETE - {} sector(s) that {} still lacks are needed by "
                  "the filesystem ({} in file data, {} in metadata, {} allocated but unlisted); more "
                  "sources are needed to fill them",
            m_analysis.vitalCount(), target,
            m_analysis.byRole[roleIndex(SectorRole::FileData)],
            m_analysis.byRole[roleIndex(SectorRole::Metadata)],
            m_analysis.byRole[roleIndex(SectorRole::AllocatedUnlisted)]);
    }
}
