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

#include <vector>
#include <string>
#include <map>
#include <memory>

#include "Cursor.h"

class CursorManager
{
public:
	CursorManager();
	~CursorManager();

	std::shared_ptr<Cursor> create();
	void clearConnectionCursors(int, bool);
	void closeConnectionCursors(int, bool);
	
	//static Cursor *current();
	int add(std::shared_ptr<Cursor>);
	void remove(std::shared_ptr<Cursor>);
	bool exists(std::string cname);
	std::shared_ptr<Cursor> get(std::string cname);
	void clear();

private:
	std::vector<std::shared_ptr<Cursor>> _cursor_list;
	std::map<std::string, std::shared_ptr<Cursor>> _cursor_map;
};

