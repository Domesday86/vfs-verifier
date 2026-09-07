/************************************************************************

    image_reader.cpp

    vfs-tools - Acorn VFS (Domesday) image tools
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

#include "image_reader.h"
#include "logging.h"

bool FileImageReader::open(const std::string &filename)
{
    m_filename = filename;

    m_file.open(filename, std::ios::in | std::ios::binary);
    if (!m_file.is_open()) {
        return false;
    }

    m_file.seekg(0, std::ios::end);
    m_size = static_cast<uint64_t>(m_file.tellg());
    m_file.seekg(0, std::ios::beg);

    return true;
}

void FileImageReader::close()
{
    if (m_file.is_open()) {
        m_file.close();
    }
}

size_t FileImageReader::read(uint64_t offset, uint8_t *buffer, size_t length)
{
    if (!m_file.is_open()) {
        LOG_CRITICAL("FileImageReader::read() - File is not open");
        return 0;
    }

    // Clear any error state left by a previous read before seeking
    m_file.clear();
    m_file.seekg(static_cast<std::streamoff>(offset));
    if (!m_file.good()) {
        m_file.clear();
        return 0;
    }

    m_file.read(reinterpret_cast<char *>(buffer), static_cast<std::streamsize>(length));

    const size_t bytesRead = static_cast<size_t>(m_file.gcount());
    if (bytesRead != length) {
        // Leave the stream usable after a short read
        m_file.clear();
    }

    return bytesRead;
}
