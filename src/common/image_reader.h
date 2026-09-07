/************************************************************************

    image_reader.h

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

#ifndef IMAGE_READER_H
#define IMAGE_READER_H

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>

// Random access to the bytes of an image. The verifier reads an image from a
// file; the stacker reads the merge it is about to produce, so that the same
// filesystem analysis can be run on a stacked image before it exists on disc
class ImageReader
{
public:
    virtual ~ImageReader() = default;

    virtual uint64_t size() const = 0;

    // Read up to length bytes from offset into buffer, returning the number of
    // bytes that were actually available. A short read is not an error
    virtual size_t read(uint64_t offset, uint8_t *buffer, size_t length) = 0;

    // How to name this image in log messages
    virtual std::string description() const = 0;
};

// An image held in a file
class FileImageReader : public ImageReader
{
public:
    bool open(const std::string &filename);
    void close();

    uint64_t size() const override { return m_size; }
    size_t read(uint64_t offset, uint8_t *buffer, size_t length) override;
    std::string description() const override { return m_filename; }

private:
    std::ifstream m_file;
    std::string m_filename;
    uint64_t m_size = 0;
};

#endif // IMAGE_READER_H
