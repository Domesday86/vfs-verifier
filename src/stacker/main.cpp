/************************************************************************

    main.cpp

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

#include "logging.h"
#include "cli_parser.h"
#include "sector_stacker.h"

#include <string>

int main(int argc, char *argv[])
{
    // Set 'binary mode' for stdin and stdout on Windows
    setBinaryMode();

    // Create CLI parser
    CliParser parser;
    parser.setApplicationDescription(
            "vfs-stacker - Acorn VFS (Domesday) image stacker\n"
            "(c)2025-2026 Simon Inns\n"
            "GPLv3 Open-Source - github: https://github.com/domesday86/vfs-verifier");

    parser.addOption("o", "output", "Output VFS image file (the bad sector map is written to the same "
                                    "path with .bsm appended)", true);
    parser.addOption("", "consensus", "Override the consensus rule: accept a sector every source "
                                      "flagged as bad when this many sources hold identical data for "
                                      "it (minimum 2). The default is a majority of the sources", true);
    parser.addOption("", "no-consensus", "Never accept a sector that every source flagged as bad, "
                                         "however many sources agree on its content");
    parser.addOption("", "no-pad", "Leave the output at the length the sources reached, instead of "
                                   "padding a short image out to the disc length the filesystem "
                                   "describes");
    parser.addOption("", "show-conflicts", "Hex dump the sectors the sources hold different content "
                                           "for, to tell decode damage from the sources being "
                                           "different versions of the disc");
    parser.addOption("", "dry-run", "Report what stacking would produce without writing anything");
    parser.addOption("", "force", "Stack the sources even if they do not agree with each other");
    parser.addOption("", "log-level", "Set console log level: trace, debug, info, warn, error, critical, off", true);
    parser.addOption("", "log-file", "Write full debug logging to file", true);
    parser.addPositionalArgument("inputs",
        "Two or more VFS image files decoded from different copies of the same disc (each bad sector "
        "map is read from the image path with .bsm appended)");

    if (!parser.parse(argc, argv)) {
        return 1;
    }

    if (parser.helpRequested()) {
        parser.showHelp();
        return 0;
    }

    // Configure logging
    std::string logLevel = parser.value("log-level");
    if (logLevel.empty()) {
        logLevel = "info";
    }

    std::string logFile = parser.value("log-file");
    if (!configureLogging(logLevel, false, logFile)) {
        LOG_ERROR("Invalid --log-level value: {}", logLevel);
        return 1;
    }

    // Get the filename arguments from the parser
    auto inputFilenames = parser.positionalArguments();

    if (inputFilenames.size() < 2) {
        LOG_WARN("You must specify at least two input VFS image filenames to stack");
        parser.showHelp();
        return 1;
    }

    const bool dryRun = parser.isSet("dry-run");
    std::string outputFilename = parser.value("output");

    if (dryRun) {
        if (!outputFilename.empty()) {
            LOG_WARN("--output is ignored with --dry-run; nothing will be written");
        }
        outputFilename.clear();
    } else if (outputFilename.empty()) {
        LOG_WARN("You must specify the output VFS image filename with --output (or use --dry-run)");
        parser.showHelp();
        return 1;
    }

    // Writing the output over one of its own sources would corrupt the stack as
    // it ran, and the .bsm the output needs is one of the input maps
    for (const std::string &inputFilename : inputFilenames) {
        if (!outputFilename.empty() && outputFilename == inputFilename) {
            LOG_ERROR("The output image {} is also an input image; choose a different output filename",
                outputFilename);
            return 1;
        }
    }

    // Sectors every source flagged as bad are recovered from agreement between
    // the sources by default: independent decoders do not arrive at the same
    // 2048 bytes by accident, so leaving those sectors bad throws away recovery
    // the sources already paid for. The report says what each recovery rested on
    ConsensusMode consensusMode = ConsensusMode::Auto;
    uint32_t consensusThreshold = 0;

    const std::string consensusValue = parser.value("consensus");
    const bool noConsensus = parser.isSet("no-consensus");

    if (noConsensus && !consensusValue.empty()) {
        LOG_ERROR("--consensus and --no-consensus contradict each other; give only one of them");
        return 1;
    }

    if (noConsensus) {
        consensusMode = ConsensusMode::Off;
    } else if (!consensusValue.empty()) {
        try {
            const unsigned long value = std::stoul(consensusValue);
            if (value < 2 || value > inputFilenames.size()) {
                LOG_ERROR("Invalid --consensus value {}: it must be between 2 and the number of input "
                          "images ({}); use --no-consensus to switch consensus recovery off",
                    consensusValue, inputFilenames.size());
                return 1;
            }
            consensusMode = ConsensusMode::Fixed;
            consensusThreshold = static_cast<uint32_t>(value);
        } catch (const std::exception &) {
            LOG_ERROR("Invalid --consensus value: {}", consensusValue);
            return 1;
        }
    }

    LOG_INFO("Beginning VFS image stacking of {} source image(s)", inputFilenames.size());

    StackerOptions options;
    options.consensusMode = consensusMode;
    options.consensusThreshold = consensusThreshold;
    // A capture that stopped short leaves the image shorter than the disc, which
    // displaces nothing but does leave the file the wrong length. The filesystem
    // says how long the disc is, so the tail is restored as empty sectors
    options.pad = !parser.isSet("no-pad");
    options.force = parser.isSet("force");
    options.showConflicts = parser.isSet("show-conflicts");

    SectorStacker stacker(options);

    for (const std::string &inputFilename : inputFilenames) {
        if (!stacker.addSource(inputFilename)) {
            return 1;
        }
    }

    stacker.reportSources();

    if (!stacker.checkAlignment()) {
        return 1;
    }
    stacker.reportAlignment();

    // Decide where every sector comes from before anything is written, so that a
    // dry run and a real run reach exactly the same conclusions
    if (!stacker.plan()) {
        // Refusing to stack is not the end of the operator's question: whether
        // the sources are a bad decode or genuinely different versions of the
        // disc is answered by what they disagree about, so show that anyway
        if (stacker.alignmentSuspect()) {
            stacker.reportAlignmentDisagreements();
        }
        return 1;
    }

    // Read the filesystem out of the merge to find out whether the sectors that
    // are still bad are ones anything depends on
    stacker.analyseFilesystem();

    if (!outputFilename.empty()) {
        if (!stacker.write(outputFilename)) {
            return 1;
        }
    }

    stacker.reportResult();
    stacker.reportConflicts();
    stacker.reportFilesystem(outputFilename);

    if (outputFilename.empty()) {
        LOG_INFO("Dry run complete - nothing was written");
    } else {
        LOG_INFO("Wrote {} and {}.bsm", outputFilename, outputFilename);
    }

    // Quit with success
    return 0;
}
