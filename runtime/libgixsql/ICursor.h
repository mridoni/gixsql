/*
This file is part of Gix-IDE, an IDE and platform for GnuCOBOL
Copyright (C) 2021 Marco Ridoni

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or (at
your option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
USA.
*/

#pragma once

#include <string>
#include <vector>
#include <memory>

#include "IConnection.h"
#include "cobol_var_types.h"

using std_binary_data = std::vector<unsigned char>;

struct IPrivateStatementData {

	virtual ~IPrivateStatementData() {}
};

class ICursor
{

public:

	virtual void setConnection(std::shared_ptr<IConnection>) = 0;
	virtual void setConnectionName(std::string) = 0;
	virtual void setName(std::string) = 0;
	virtual void setQuery(std::string) = 0;
	virtual void setQuerySource(void *, int) = 0;
	virtual void setNumParams(int) = 0;

	virtual std::shared_ptr<IConnection> getConnection() = 0;
	virtual std::string getConnectionName() = 0;
	virtual std::string getName() = 0;
	virtual std::string getQuery() = 0;
	virtual void getQuerySource(void**, int*) = 0;
	virtual int getNumParams() = 0;
	virtual bool isWithHold() = 0;
	virtual bool isOpen() = 0;

	virtual std::vector<CobolVarType> getParameterTypes() = 0;
	virtual std::vector<std_binary_data> getParameterValues() = 0;
	virtual std::vector<unsigned long> getParameterLengths() = 0;
	virtual std::vector<uint32_t> getParameterFlags() = 0;

	virtual std::shared_ptr<IPrivateStatementData> getPrivateData() = 0;
	virtual void setPrivateData(std::shared_ptr<IPrivateStatementData>) = 0;
	virtual void clearPrivateData() = 0;

	virtual uint64_t getRowNum() = 0;
	virtual void increaseRowNum() = 0;
};

