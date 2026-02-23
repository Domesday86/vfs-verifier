/************************************************************************

    adfs_directory.h

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

    void show();

private:
    std::vector<AdfsDirectoryEntry> m_adfsDirectoryEntries;

    int8_t m_masterSequenceNumber;

};

#endif // ADFS_DIRECTORY_H