/************************************************************************

    cli_parser.h

    Simple command-line argument parser (replaces QCommandLineParser)
    Copyright (C) 2025-2026 Simon Inns

    This is free software: you can redistribute it and/or
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

#ifndef CLI_PARSER_H
#define CLI_PARSER_H

#include <string>
#include <vector>
#include <map>
#include <iostream>

class CliParser {
public:
    CliParser() : m_helpRequested(false) {}

    void setApplicationDescription(const std::string &description) {
        m_applicationDescription = description;
    }

    void addOption(const std::string &shortName, const std::string &longName,
                   const std::string &description, bool takesValue = false) {
        m_options[longName] = {shortName, description, takesValue};
    }

    void addPositionalArgument(const std::string &name, const std::string &description) {
        m_positionalArgs.push_back({name, description});
    }

    bool parse(int argc, char *argv[]) {
        m_programName = argv[0];

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];

            if (arg == "-h" || arg == "--help") {
                m_helpRequested = true;
                return true;
            }

            if (arg.substr(0, 2) == "--") {
                // Long option
                std::string optName = arg.substr(2);
                std::string optValue;
                size_t eqPos = optName.find('=');

                if (eqPos != std::string::npos) {
                    optValue = optName.substr(eqPos + 1);
                    optName = optName.substr(0, eqPos);
                }

                if (m_options.find(optName) != m_options.end()) {
                    if (m_options[optName].takesValue) {
                        if (optValue.empty()) {
                            if (i + 1 < argc) {
                                optValue = argv[++i];
                            }
                        }
                        m_values[optName] = optValue;
                    } else {
                        m_values[optName] = "true";
                    }
                } else {
                    std::cerr << "Unknown option: " << arg << "\n";
                    return false;
                }
            } else if (arg.substr(0, 1) == "-" && arg.length() > 1) {
                // Short option
                for (size_t j = 1; j < arg.length(); ++j) {
                    std::string shortOpt(1, arg[j]);

                    // Find the long option for this short option
                    std::string longOpt;
                    for (const auto &[name, info] : m_options) {
                        if (info.shortName == shortOpt) {
                            longOpt = name;
                            break;
                        }
                    }

                    if (!longOpt.empty()) {
                        if (m_options[longOpt].takesValue && j == arg.length() - 1) {
                            if (i + 1 < argc) {
                                m_values[longOpt] = argv[++i];
                            }
                        } else {
                            m_values[longOpt] = "true";
                        }
                    } else {
                        std::cerr << "Unknown option: -" << shortOpt << "\n";
                        return false;
                    }
                }
            } else {
                // Positional argument
                m_positionalValues.push_back(arg);
            }
        }

        return true;
    }

    bool isSet(const std::string &optionName) const {
        return m_values.find(optionName) != m_values.end();
    }

    std::string value(const std::string &optionName) const {
        auto it = m_values.find(optionName);
        if (it != m_values.end()) {
            return it->second;
        }
        return "";
    }

    std::vector<std::string> positionalArguments() const {
        return m_positionalValues;
    }

    bool helpRequested() const {
        return m_helpRequested;
    }

    void showHelp() const {
        std::cerr << m_applicationDescription << "\n\n";
        std::cerr << "Usage: " << m_programName << " [options] ";
        for (const auto &arg : m_positionalArgs) {
            std::cerr << arg.name << " ";
        }
        std::cerr << "\n\n";

        std::cerr << "Options:\n";
        std::cerr << "  -h, --help    Show this help message\n";
        for (const auto &[name, info] : m_options) {
            std::string optStr = "  ";
            if (!info.shortName.empty()) {
                optStr += "-" + info.shortName + ", ";
            }
            optStr += "--" + name;
            if (info.takesValue) {
                optStr += " <value>";
            }
            std::cerr << optStr << "\n";
            std::cerr << "    " << info.description << "\n";
        }

        if (!m_positionalArgs.empty()) {
            std::cerr << "\nArguments:\n";
            for (const auto &arg : m_positionalArgs) {
                std::cerr << "  " << arg.name << "    " << arg.description << "\n";
            }
        }
    }

private:
    struct OptionInfo {
        std::string shortName;
        std::string description;
        bool takesValue;
    };

    struct ArgumentInfo {
        std::string name;
        std::string description;
    };

    std::string m_programName;
    std::string m_applicationDescription;
    std::map<std::string, OptionInfo> m_options;
    std::vector<ArgumentInfo> m_positionalArgs;
    std::map<std::string, std::string> m_values;
    std::vector<std::string> m_positionalValues;
    bool m_helpRequested;
};

#endif // CLI_PARSER_H
