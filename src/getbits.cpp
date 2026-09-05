/************************************************************************

    getbits.cpp

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

#include "getbits.h"
#include <fmt/format.h>

uint32_t get32(const std::vector<uint8_t> &data, int byteOffset)
{
    uint8_t b0 = static_cast<uint8_t>(data.at(byteOffset));
    uint8_t b1 = static_cast<uint8_t>(data.at(byteOffset+1));
    uint8_t b2 = static_cast<uint8_t>(data.at(byteOffset+2));
    uint8_t b3 = static_cast<uint8_t>(data.at(byteOffset+3));

    return static_cast<uint32_t>(b0 | (b1 << 8) | (b2 << 16) | (b3 << 24));
}

uint32_t get24(const std::vector<uint8_t> &data, int byteOffset)
{
    uint8_t b0 = static_cast<uint8_t>(data.at(byteOffset));
    uint8_t b1 = static_cast<uint8_t>(data.at(byteOffset+1));
    uint8_t b2 = static_cast<uint8_t>(data.at(byteOffset+2));

    return static_cast<uint32_t>(b0 | (b1 << 8) | (b2 << 16));
}

uint16_t get16(const std::vector<uint8_t> &data, int byteOffset)
{
    return static_cast<uint16_t>(static_cast<uint8_t>(data.at(byteOffset)) | (static_cast<uint8_t>(data.at(byteOffset+1)) << 8));
}

uint8_t get8(const std::vector<uint8_t> &data, int byteOffset)
{
    return static_cast<uint8_t>(data.at(byteOffset));
}

std::string toString32bits(uint32_t value)
{
    return fmt::format("0x{:08X}", value);
}

std::string toString24bits(uint32_t value)
{
    return fmt::format("0x{:06X}", value);
}

std::string toString16bits(uint16_t value)
{
    return fmt::format("0x{:04X}", value);
}

std::string toString8bits(uint8_t value)
{
    return fmt::format("0x{:02X}", value);
}