/************************************************************************

    main.cpp

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

#include "logging.h"
#include "cli_parser.h"
#include "adfs_verifier.h"

#include <fstream>

int main(int argc, char *argv[])
{
    // Set 'binary mode' for stdin and stdout on Windows
    setBinaryMode();

    // Create CLI parser
    CliParser parser;
    parser.setApplicationDescription(
            "vfs-verifier - Acorn VFS (Domesday) image verifier\n"
            "(c)2025-2026 Simon Inns\n"
            "GPLv3 Open-Source - github: https://github.com/domesday86/vfs-verifier");

        parser.addOption("", "log-level", "Set console log level: trace, debug, info, warn, error, critical, off", true);
        parser.addOption("", "log-file", "Write full debug logging to file", true);
    parser.addPositionalArgument("input",
        "Specify input VFS image file (the bad sector map is read from the same path with .bsm appended)");

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
    auto positionalArgs = parser.positionalArguments();

    if (positionalArgs.empty()) {
        LOG_WARN("You must specify the input VFS image filename");
        parser.showHelp();
        return 1;
    }

    if (positionalArgs.size() > 1) {
        LOG_WARN("Only one argument is expected: the input VFS image filename. The bad sector map is "
                 "found automatically by appending .bsm to it");
        parser.showHelp();
        return 1;
    }

    std::string inputFilename = positionalArgs[0];

    // The decoder names the bad sector map after the image it wrote, by appending
    // .bsm to the whole filename rather than replacing the extension
    std::string bsmFilename = inputFilename + ".bsm";

    std::ifstream bsmTest(bsmFilename);
    if (!bsmTest.is_open()) {
        LOG_ERROR("Could not find the bad sector map {} for input image {}", bsmFilename, inputFilename);
        LOG_ERROR("The decoder writes this file alongside the image, named by appending .bsm to the "
                  "image filename");
        return 1;
    }
    bsmTest.close();

    // Perform the processing
    LOG_INFO("Beginning VFS image verification of {} using bad sector map metadata from {}", inputFilename, bsmFilename);
    AdfsVerifier adfsVerifier;

    if (!adfsVerifier.process(inputFilename, bsmFilename)) {
        return 1;
    }

    // Quit with success
    return 0;
}
