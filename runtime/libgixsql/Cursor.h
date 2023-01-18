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

#include <string>
#include <vector>
#include <memory>

#include "ICursor.h"
#include "IConnection.h"
#include "SqlVar.h"
#include "SqlVarList.h"

class Cursor : public ICursor
{

public:
	Cursor();
	~Cursor();

	// ICursor
	void setConnection(std::shared_ptr<IConnection>) override;
	void setConnectionName(std::string) override;
	void setName(std::string) override;
	void setQuery(std::string) override;
	void setQuerySource(void*, int) override;
	void setNumParams(int) override;
	std::shared_ptr<IConnection> getConnection() override;
	std::string getConnectionName() override;
	std::string getName() override;
	std::string getQuery() override;
	void getQuerySource(void**, int*) override;
	int getNumParams() override;
	bool isOpen() override;
	bool isWithHold() override;
	
	virtual std::vector<CobolVarType> getParameterTypes() override;
	virtual std::vector<std_binary_data> getParameterValues() override;
	virtual std::vector<unsigned long> getParameterLengths() override;
	virtual std::vector<uint32_t> getParameterFlags() override;

	void setOpened(bool);

	// For private DbInterfaceData
	std::shared_ptr<IPrivateStatementData> getPrivateData() override;
	void setPrivateData(std::shared_ptr<IPrivateStatementData>) override;
	virtual void clearPrivateData() override;
	
	void setParameters(SqlVarList&);
	SqlVarList& getParameters();
	void createRealDataforParameters();
	void setWithHold(bool);

	uint64_t getRowNum() override;
	void increaseRowNum() override;

	void setConnectionReference(void *d, int l);
	std::string getConnectionNameFromReference();

private:

	std::shared_ptr<IConnection> connection;
	std::string connection_name;
	std::string name;
	std::string query;
	void* query_source_addr = nullptr;
	int query_source_len = 0;
	int nParams = 0;
	bool is_opened = false;
	bool is_with_hold = false;
	int tuples = 0;

	SqlVarList parameter_list; // parameter list

	std::shared_ptr<IPrivateStatementData> dbi_data;

	uint64_t rownum = 0;

	void *connref_data = nullptr;
	int connref_datalen = 0;
};

