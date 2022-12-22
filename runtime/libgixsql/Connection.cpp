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

#include "Connection.h"
#include "DbInterfaceFactory.h"


Connection::Connection()
{	
	// ext_conninfo = false;
	is_opened = false;
}


Connection::~Connection()
{
	if (dbi != NULL) {
		DbInterfaceFactory::removeInterface(dbi);
	}
}

int Connection::getId()
{
	return id;
}

bool Connection::isOpen()
{
	return is_opened;
}

void Connection::setName(std::string name)
{
	this->name = name;
}

std::string Connection::getName()
{
	return name;
}

std::shared_ptr<IConnectionOptions> Connection::getConnectionOptions() const
{
	return options;
}

void Connection::setConnectionOptions(std::shared_ptr<IConnectionOptions> p)
{
	options = p;
}

void Connection::setConnectionInfo(std::shared_ptr<IDataSourceInfo> conn_string)
{
	conninfo = conn_string;
	// if (conn_string)
	// 	ext_conninfo = true;
}

void Connection::setOpened(bool i)
{
	is_opened = i;
}

void Connection::setDbInterface(std::shared_ptr<IDbInterface> _dbi)
{
	dbi = _dbi;
}

bool Connection::test(IDataSourceInfo*)
{
	return false;
}

std::shared_ptr<IDataSourceInfo> Connection::getConnectionInfo()
{
	return conninfo;
}

std::shared_ptr<IDbInterface> Connection::getDbInterface()
{
	return dbi;
}
