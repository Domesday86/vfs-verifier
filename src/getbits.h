/************************************************************************

    getbits.h

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

#ifndef GETBITS_H
#define GETBITS_H

#include <vector>
#include <string>
#include <cstdint>

uint32_t get32(const std::vector<uint8_t> &data, int byteOffset);
uint32_t get24(const std::vector<uint8_t> &data, int byteOffset);
uint16_t get16(const std::vector<uint8_t> &data, int byteOffset);
uint8_t get8(const std::vector<uint8_t> &data, int byteOffset);

std::string toString32bits(uint32_t value);
std::string toString24bits(uint32_t value);
std::string toString16bits(uint16_t value);
std::string toString8bits(uint8_t value);

#endif // GETBITS_H