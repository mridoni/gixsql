/*
* This file is part of Gix-IDE, an IDE and platform for GnuCOBOL
* Copyright (C) 2021 Marco Ridoni
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public License
* as published by the Free Software Foundation; either version 3,
* or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with this library; see the file COPYING.LIB.  If
* not, write to the Free Software Foundation, 51 Franklin Street, Fifth Floor
* Boston, MA 02110-1301 USA
*/

#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "varlen_defs.h"
#include "cobol_var_types.h"

using std_binary_data = std::vector<unsigned char>;

class SqlVar
{
	friend class SqlVarList;

public:
	SqlVar(CobolVarType _type, int _length, int _power, uint32_t _flags, void *_addr);
	~SqlVar();

	SqlVar *copy();

	void createRealData();

	void* getAddr();
	const std_binary_data& getDbData();
	CobolVarType getType();
	unsigned long getLength();
	unsigned long getDbDataLength();
	uint32_t getFlags();

	bool isVarLen();
	bool isBinary();
	bool isAutoTrim();

	void createCobolData(char *retstr, int datalen, int* sqlcode);

	void createCobolDataLowValue();


private:
	CobolVarType type; 
	int length;								// includes the extra 2 bytes for variable length fields (level 49)
	int power; 
	void *addr = nullptr;					// address of variable (COBOL-side)
	std_binary_data db_data_buffer;			// realdata (i.e. displayable data) buffer
	unsigned long db_data_buffer_len = 0;	// length of db_data_buffer (actual length of allocated buffer is always realdata_len)
	int db_data_len = -1;			// actual length of data in db_data_buffer

	uint32_t flags = 0;
	
	// Variable length support
	bool is_variable_length = false;
	
	// Binary/VarBinary support
	bool is_binary = false;

	// auto-trim for CHAR(x) fields (i.e. gixpp with --picx-as=varchar)
	bool is_autotrim = false;


    static const char _decimal_point;

	void display_to_comp3(const char *data, int datalen, bool has_sign);	// , int total_len, int scale, int has_sign, uint8_t *addr
	void allocate_realdata_buffer();

};
