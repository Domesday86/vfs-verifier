/************************************************************************

    adfs_directory.h

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

#ifndef ADFS_DIRECTORY_H
#define ADFS_DIRECTORY_H

#include <vector>
#include <string>
#include <cstdint>

#include "getbits.h"

class AdfsDirectoryEntry
{
public:
    AdfsDirectoryEntry(const std::vector<uint8_t>& data);

    // Public accessors
    std::string objectName() const { return m_objectName; }
    bool readable() const { return m_readable; }
    bool writable() const { return m_writable; }
    bool locked() const { return m_locked; }
    bool isDirectory() const { return m_directory; }
    bool executeOnly() const { return m_executeOnly; }
    bool publiclyReadable() const { return m_publiclyReadable; }
    bool publiclyWritable() const { return m_publiclyWritable; }
    bool publiclyExecuteOnly() const { return m_publiclyExecuteOnly; }
    bool isPrivate() const { return m_private; }
    uint32_t loadAddress() const { return m_loadAddress; }
    uint32_t execAddress() const { return m_execAddress; }
    uint32_t byteLength() const { return m_byteLength; }
    uint32_t startSector() const { return m_startSector; }
    uint8_t sequenceNumber() const { return m_sequenceNumber; }

    // A zero first byte of the object name marks the end of the directory
    bool isEndOfDirectory() const { return m_endOfDirectory; }

    // The number of whole ADFS sectors occupied by this object
    uint32_t sectorLength() const { return (m_byteLength + 255) / 256; }

    std::string flagString() const;
    void show();

private:
    std::string m_objectName;
    bool m_readable;
    bool m_writable;
    bool m_locked;
    bool m_directory;
    bool m_executeOnly;
    bool m_publiclyReadable;
    bool m_publiclyWritable;
    bool m_publiclyExecuteOnly;
    bool m_private;
    bool m_endOfDirectory;

    uint32_t m_loadAddress;
    uint32_t m_execAddress;
    uint32_t m_byteLength;
    uint32_t m_startSector;
    uint32_t m_sequenceNumber;
};

class AdfsDirectory
{
public:
    AdfsDirectory(const std::vector<uint8_t>& sectors);

    std::vector<AdfsDirectoryEntry> entries() const;

    bool isValid() const { return m_isValid; }

    // A directory is "broken" if the master sequence number and identifier in
    // the header do not match those in the footer
    bool isBroken() const { return m_broken; }

    uint8_t headerSequenceNumber() const { return m_headerSequenceNumber; }
    uint8_t footerSequenceNumber() const { return m_footerSequenceNumber; }
    std::string headerIdentifier() const { return m_headerIdentifier; }
    std::string footerIdentifier() const { return m_footerIdentifier; }
    std::string directoryName() const { return m_directoryName; }
    std::string directoryTitle() const { return m_directoryTitle; }
    uint32_t parentStartSector() const { return m_parentStartSector; }

    // True if the entries were correctly terminated within the directory
    bool entriesTerminated() const { return m_entriesTerminated; }

    void show();

private:
    std::vector<AdfsDirectoryEntry> m_adfsDirectoryEntries;

    bool m_isValid;
    bool m_broken;
    bool m_entriesTerminated;
    uint8_t m_masterSequenceNumber;
    uint8_t m_headerSequenceNumber;
    uint8_t m_footerSequenceNumber;
    std::string m_headerIdentifier;
    std::string m_footerIdentifier;
    std::string m_directoryName;
    std::string m_directoryTitle;
    uint32_t m_parentStartSector;
};

#endif // ADFS_DIRECTORY_H
