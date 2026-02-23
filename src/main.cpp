/************************************************************************

    main.cpp

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

#include "logging.h"
#include "cli_parser.h"
#include "adfs_verifier.h"

int main(int argc, char *argv[])
{
    // Set 'binary mode' for stdin and stdout on Windows
    setBinaryMode();

    // Create CLI parser
    CliParser parser;
    parser.setApplicationDescription(
            "vfs-verifier - Acorn VFS (Domesday) image verifier\n"
            "(c)2025 Simon Inns\n"
            "GPLv3 Open-Source - github: https://github.com/happycube/ld-decode");

        parser.addOption("", "log-level", "Set console log level: trace, debug, info, warn, error, critical, off", true);
        parser.addOption("", "log-file", "Write full debug logging to file", true);
    parser.addPositionalArgument("input", "Specify input VFS image file");
    parser.addPositionalArgument("bad-sector-map", "Specify bad sector map metadata file");

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

    if (positionalArgs.size() != 2) {
        LOG_WARN("You must specify the input VFS image filename and the bad sector map metadata filename");
        parser.showHelp();
        return 1;
    }
    
    std::string inputFilename = positionalArgs[0];
    std::string bsmFilename = positionalArgs[1];

    // Perform the processing
    LOG_INFO("Beginning VFS image verification of {} using bad sector map metadata from {}", inputFilename, bsmFilename);
    AdfsVerifier adfsVerifier;

    if (!adfsVerifier.process(inputFilename, bsmFilename)) {
        return 1;
    }

    // Quit with success
    return 0;
}
