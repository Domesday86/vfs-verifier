/************************************************************************

    adfs_directory.cpp

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

#include "adfs_directory.h"
#include "logging.h"

// Strings in directories are terminated with 0x0D or 0x00 if shorter than the
// field, they are not space padded as with most other filesystems.
static std::string trimAcornString(const std::vector<uint8_t>& data, size_t offset, size_t maxLength)
{
    std::string result;
    for (size_t i = 0; i < maxLength; ++i) {
        const uint8_t c = data.at(offset + i) & 0x7F;
        if (c == 0x00 || c == 0x0D) break;
        result += static_cast<char>(c);
    }
    while (!result.empty() && result.back() == ' ') result.pop_back();
    return result;
}

AdfsDirectoryEntry::AdfsDirectoryEntry(const std::vector<uint8_t>& data) :
    m_readable(false), m_writable(false), m_locked(false), m_directory(false),
    m_executeOnly(false), m_publiclyReadable(false), m_publiclyWritable(false),
    m_publiclyExecuteOnly(false), m_private(false), m_endOfDirectory(true),
    m_loadAddress(0), m_execAddress(0), m_byteLength(0), m_startSector(0), m_sequenceNumber(0)
{
    // Expecting 26 bytes of data (0x00 to 0x19)
    if (data.size() != 26) {
        LOG_CRITICAL("AdfsDirectoryEntry::AdfsDirectoryEntry() - Incorrect number of bytes {}", data.size());
        return;
    }

    // Directory entries:
    // 000-009 	Object name and access bits
    //     Bytes are 8 bits - object name is 10 7-bit ASCII characters
    //     The 8th bit of every character is the access bit flags:
    //     000: 'R' - object readable
    //     001: 'W' - object writable
    //     002: 'L' - object locked
    //     003: 'D' - object is a directory
    //     004: 'E' - object execute-only
    //     005: 'r' - object publicly readable
    //     006: 'w' - object publicly writable
    //     007: 'e' - object publicly execute-only
    //     008: 'P' - object private
    //     009:     - unused
    //
    // 00A-00D 	Load address
    // 00E-011 	Execution address
    // 012-015 	Length
    // 016-018 	Start sector/allocation number
    // 019		Sequence number on small-sector disks

    // A zero first character of the object name marks the end of the directory
    m_endOfDirectory = ((data.at(0) & 0x7F) == 0x00);
    if (m_endOfDirectory) return;

    // Object name, with the access bits masked off
    m_objectName = trimAcornString(data, 0, 10);

    // Flags
    m_readable = (data.at(0) & 0x80) != 0;
    m_writable = (data.at(1) & 0x80) != 0;
    m_locked = (data.at(2) & 0x80) != 0;
    m_directory = (data.at(3) & 0x80) != 0;
    m_executeOnly = (data.at(4) & 0x80) != 0;
    m_publiclyReadable = (data.at(5) & 0x80) != 0;
    m_publiclyWritable = (data.at(6) & 0x80) != 0;
    m_publiclyExecuteOnly = (data.at(7) & 0x80) != 0;
    m_private = (data.at(8) & 0x80) != 0;

    // Load address
    m_loadAddress = get32(data, 0x0A);

    // Execution address
    m_execAddress = get32(data, 0x0E);

    // Length
    m_byteLength = get32(data, 0x12);

    // Start sector
    m_startSector = get24(data, 0x16);

    // Sequence number
    m_sequenceNumber = get8(data, 0x19);
};

std::string AdfsDirectoryEntry::flagString() const
{
    std::string flags = "";
    if (m_directory) flags += "D";
    if (m_locked) flags += "L";
    if (m_readable) flags += "R";
    if (m_writable) flags += "W";
    if (m_executeOnly) flags += "E";
    if (m_publiclyReadable) flags += "r";
    if (m_publiclyWritable) flags += "w";
    if (m_publiclyExecuteOnly) flags += "e";
    if (m_private) flags += "P";
    return flags;
}

void AdfsDirectoryEntry::show()
{
    // Pad the object name to 10 characters
    std::string paddedObjectName = m_objectName;
    while (paddedObjectName.size() < 10) {
        paddedObjectName += " ";
    }

    LOG_INFO("  {} {} ({:02d}) {} {} {} {}", paddedObjectName, flagString(), m_sequenceNumber,
        toString32bits(m_loadAddress), toString32bits(m_execAddress),
        toString32bits(m_byteLength), toString24bits(m_startSector));
}

AdfsDirectory::AdfsDirectory(const std::vector<uint8_t>& sectors) :
    m_isValid(false),
    m_broken(true),
    m_entriesTerminated(false),
    m_masterSequenceNumber(0),
    m_headerSequenceNumber(0),
    m_footerSequenceNumber(0),
    m_parentStartSector(0)
{
    // Expecting 5 sectors of data 5*256 bytes
    if (sectors.size() != 1280) {
        LOG_CRITICAL("AdfsDirectory::AdfsDirectory() - Incorrect number of bytes {}", sectors.size());
        return;
    }

    // Directories use five logical sectors and have up to 47 entries

    // 000	Directory Header
    // 005	First directory entry
    // 01F	Second directory entry
    // 039	Third directory entry
    // ...
    // 4B1	47th directory entry
    // 4CB	Small directory Footer

    // Directory header:
    // 000		Directory Master Sequence Number in BCD
    // 001-004 	Directory identifier "Hugo"

    // Get the Master Sequence Number
    m_headerSequenceNumber = static_cast<uint8_t>(sectors.at(0));

    // Convert the BCD to a number
    m_masterSequenceNumber = static_cast<uint8_t>(((m_headerSequenceNumber >> 4) * 10) +
                                                  (m_headerSequenceNumber & 0x0F));

    for (int i = 0; i < 4; ++i) m_headerIdentifier += static_cast<char>(sectors.at(0x001 + i));

    // Small directory footer:
    // 4CB       &00 - marks end of directory
    // 4CC-4D5   Directory name
    // 4D6-4D8   Start sector of parent directory
    // 4D9-4EB   Directory title - initially set to same as directory name.
    // 4EC-4F9   Reserved (set to zero)
    // 4FA       Directory Master Sequence Number in BCD
    // 4FB-4FE   Directory identifier - "Hugo" (or "Nick" in LargeDirs)
    // 4FF       &00 - This is used by 32-bit ADFS as a directory checksum,
    //             if it is zero it is ignored. 8-bit ADFS always ignores it,
    //             and writes it as a zero.

    m_directoryName = trimAcornString(sectors, 0x4CC, 10);
    m_parentStartSector = get24(sectors, 0x4D6);
    m_directoryTitle = trimAcornString(sectors, 0x4D9, 19);
    m_footerSequenceNumber = static_cast<uint8_t>(sectors.at(0x4FA));
    for (int i = 0; i < 4; ++i) m_footerIdentifier += static_cast<char>(sectors.at(0x4FB + i));

    // A directory is reported as 'Broken' if the Master Sequence Number and
    // "Hugo"/"Nick" strings do not match - bytes &000-&004 are compared with bytes
    // &4FA-&4FE.
    m_broken = (m_headerSequenceNumber != m_footerSequenceNumber) ||
               (m_headerIdentifier != m_footerIdentifier);

    // Read the directory entries. The final directory entry is followed by a
    // &00 byte; in a full directory this &00 byte is the byte at &4CB.
    LOG_INFO("Directory entries:");
    for (int i = 0; i < 47; ++i) {
        std::vector<uint8_t> entryData(sectors.begin() + 5 + (i * 26), sectors.begin() + 5 + ((i + 1) * 26));
        AdfsDirectoryEntry entry(entryData);

        // End of directory entries?
        if (entry.isEndOfDirectory()) {
            m_entriesTerminated = true;
            break;
        }

        entry.show();
        m_adfsDirectoryEntries.push_back(entry);
    }

    // A directory holding all 47 entries is terminated by the &00 at &4CB instead
    if (!m_entriesTerminated && sectors.at(0x4CB) == 0x00) {
        m_entriesTerminated = true;
    }

    // Strings in directories are terminated with &0D or &00 if shorter than ten
    // characters, they are not space padded as with most other filesystems.

    // The objects in a directory are always stored in case-insensitive sorted
    // order. If objects are not sorted, then mis-sorted entries will not be found
    // by filing system operations.

    m_isValid = true;
}

std::vector<AdfsDirectoryEntry> AdfsDirectory::entries() const
{
    return m_adfsDirectoryEntries;
}

void AdfsDirectory::show()
{
    LOG_DEBUG("AdfsDirectory::show() - Directory \"{}\" title \"{}\" master sequence number {} - {}",
        m_directoryName, m_directoryTitle, m_masterSequenceNumber,
        m_broken ? "BROKEN" : "consistent");
}
