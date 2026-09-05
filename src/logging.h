/************************************************************************

    logging.h

    vfs-verifier - Centralized logging using spdlog
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

#ifndef LOGGING_H
#define LOGGING_H

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <sstream>
#include <memory>
#include <string>
#include <cctype>
#include <algorithm>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

// Get or create the default logger
inline std::shared_ptr<spdlog::logger>& get_logger() {
    static std::shared_ptr<spdlog::logger> logger;
    if (!logger) {
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_level(spdlog::level::info);
        consoleSink->set_pattern("[%^%l%$] %v");

        std::vector<spdlog::sink_ptr> sinks = {consoleSink};
        logger = std::make_shared<spdlog::logger>("vfs-verifier", sinks.begin(), sinks.end());
        logger->set_level(spdlog::level::trace);
        logger->set_pattern("[%^%l%$] %v");
        spdlog::set_default_logger(logger);
    }
    return logger;
}

inline bool parseLogLevel(const std::string &logLevel, spdlog::level::level_enum &level)
{
    std::string lowerLogLevel = logLevel;
    std::transform(lowerLogLevel.begin(), lowerLogLevel.end(), lowerLogLevel.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lowerLogLevel == "trace") {
        level = spdlog::level::trace;
        return true;
    }
    if (lowerLogLevel == "debug") {
        level = spdlog::level::debug;
        return true;
    }
    if (lowerLogLevel == "info") {
        level = spdlog::level::info;
        return true;
    }
    if (lowerLogLevel == "warn" || lowerLogLevel == "warning") {
        level = spdlog::level::warn;
        return true;
    }
    if (lowerLogLevel == "error") {
        level = spdlog::level::err;
        return true;
    }
    if (lowerLogLevel == "critical") {
        level = spdlog::level::critical;
        return true;
    }
    if (lowerLogLevel == "off") {
        level = spdlog::level::off;
        return true;
    }

    return false;
}

inline bool configureLogging(const std::string &logLevel = "info", bool quiet = false, const std::string &logFile = "")
{
    spdlog::level::level_enum consoleLevel;
    if (!parseLogLevel(logLevel, consoleLevel)) {
        return false;
    }

    if (quiet && consoleLevel < spdlog::level::info) {
        consoleLevel = spdlog::level::info;
    }

    try {
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_level(consoleLevel);
        consoleSink->set_pattern("[%^%l%$] %v");

        std::vector<spdlog::sink_ptr> sinks = {consoleSink};

        if (!logFile.empty()) {
            auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFile, true);
            fileSink->set_level(spdlog::level::debug);
            fileSink->set_pattern("[%l] %v");
            sinks.push_back(fileSink);
        }

        auto logger = std::make_shared<spdlog::logger>("vfs-verifier", sinks.begin(), sinks.end());
        logger->set_level(spdlog::level::trace);
        spdlog::set_default_logger(logger);
        get_logger() = logger;

        return true;
    } catch (const spdlog::spdlog_ex &) {
        return false;
    }
}

inline void setLogLevel(spdlog::level::level_enum level)
{
    get_logger()->set_level(level);
}

// Utility function to set binary mode on stdin/stdout (Windows compatibility)
inline void setBinaryMode([[maybe_unused]] bool enable = true) {
#ifdef _WIN32
    if (enable) {
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
        _setmode(_fileno(stderr), _O_BINARY);
    }
#endif
    // On Unix/Linux/macOS, binary mode is the default
}

// Utility function for debug level configuration
inline void setDebug(bool enabled) {
    if (enabled) {
        setLogLevel(spdlog::level::debug);
    } else {
        setLogLevel(spdlog::level::info);
    }
}

#define LOG_TRACE(...) get_logger()->trace(__VA_ARGS__)
#define LOG_DEBUG(...) get_logger()->debug(__VA_ARGS__)
#define LOG_INFO(...) get_logger()->info(__VA_ARGS__)
#define LOG_WARN(...) get_logger()->warn(__VA_ARGS__)
#define LOG_ERROR(...) get_logger()->error(__VA_ARGS__)
#define LOG_CRITICAL(...) get_logger()->critical(__VA_ARGS__)

#endif // LOGGING_H
