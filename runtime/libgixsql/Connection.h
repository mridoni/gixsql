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

#ifndef LIBGIXSQL_API
#if defined(_WIN32) || defined(_WIN64)
#define LIBGIXSQL_API __declspec(dllexport)   
#else  
#define LIBGIXSQL_API
#endif  
#endif

#include <string>
#include <map>
#include <vector>
#include <memory>

#include "IConnection.h"
#include "IDbInterface.h"
#include "IDataSourceInfo.h"
#include "IConnectionOptions.h"

class DbInterface;

class Connection : public IConnection 
{
	friend class ConnectionManager;
	friend class IDbInterface;

public:
	LIBGIXSQL_API Connection();
	LIBGIXSQL_API ~Connection();

	LIBGIXSQL_API std::shared_ptr<IDataSourceInfo> getConnectionInfo() override;
	LIBGIXSQL_API void setConnectionInfo(std::shared_ptr<IDataSourceInfo>) override;
	LIBGIXSQL_API std::shared_ptr<IDbInterface> getDbInterface() override;
	LIBGIXSQL_API void setDbInterface(std::shared_ptr<IDbInterface>) override;

	LIBGIXSQL_API static bool test(IDataSourceInfo*);

	int getId() override;
	bool isOpen() override;
	void setName(std::string) override;
	
	void setOpened(bool) override;
	
	std::string getName();

	std::shared_ptr<IConnectionOptions> getConnectionOptions() const override;
	void setConnectionOptions(std::shared_ptr<IConnectionOptions>) override;

private:

	int id;
	std::string name;
	std::shared_ptr<IDataSourceInfo> conninfo;
	bool is_opened = false;
	std::shared_ptr<IConnectionOptions> options;
	std::shared_ptr<IDbInterface> dbi;
};

