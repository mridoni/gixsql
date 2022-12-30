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

#define WIN32_LEAN_AND_MEAN

#include <string>
#include <vector>
#include <map>
#include <memory>

#if defined(_WIN32) || defined(_WIN64)

#if defined(__MINGW32__)
typedef unsigned char byte;
#endif

#include <windows.h>
#endif

#include <sql.h>
#include <sqlext.h>


#include "ICursor.h"
#include "IDbInterface.h"
#include "IDbManagerInterface.h"
#include "IDataSourceInfo.h"
#include "IConnectionOptions.h"
#include "ISchemaManager.h"

struct IConnectionOptions;

enum class ErrorSource {
	Environmennt = 1,
	Connection = 2,
	Statement = 3
};

struct ODBCStatementData : public IPrivateStatementData {

	ODBCStatementData(SQLHANDLE conn);
	~ODBCStatementData();

	void resizeParams(int n);
	void resizeColumnData(int n);

	SQLHANDLE statement = nullptr;
};

class DbInterfaceODBC : public IDbInterface, public IDbManagerInterface
{
public:
	DbInterfaceODBC();
	~DbInterfaceODBC();

	virtual int init(const std::shared_ptr<spdlog::logger>& _logger) override;
	virtual int connect(const std::shared_ptr<IDataSourceInfo>& conn_string, const std::shared_ptr<IConnectionOptions>& g_opts) override;
	virtual int reset() override;
	virtual int terminate_connection() override;
	virtual int exec(std::string) override;
	virtual int exec_params(std::string query, int nParams, const std::vector<int>& paramTypes, const std::vector<std::string>& paramValues, const std::vector<int>& paramLengths, const std::vector<int>& paramFormats) override;
	virtual int close_cursor(const std::shared_ptr<ICursor>& crsr) override;
	virtual int cursor_declare(const std::shared_ptr<ICursor>& crsr, bool, int) override;
	virtual int cursor_declare_with_params(const std::shared_ptr<ICursor>& crsr, char**, bool, int) override;
	virtual int cursor_open(const std::shared_ptr<ICursor>& cursor);
	virtual int fetch_one(const std::shared_ptr<ICursor>& crsr, int) override;
	virtual bool get_resultset_value(ResultSetContextType resultset_context_type, const IResultSetContextData& context, int row, int col, char* bfr, int bfrlen, int* value_len);
	virtual bool move_to_first_record(std::string stmt_name = "") override;
	virtual uint64_t get_native_features() override;
	virtual int get_num_rows(const std::shared_ptr<ICursor>& crsr) override;
	virtual int get_num_fields(const std::shared_ptr<ICursor>& crsr) override;
	virtual char* get_error_message() override;
	virtual int get_error_code() override;
	virtual std::string get_state() override;
	virtual void set_owner(std::shared_ptr<IConnection>) override;
	virtual std::shared_ptr<IConnection> get_owner() override;
	virtual int prepare(std::string stmt_name, std::string sql) override;
	virtual int exec_prepared(std::string stmt_name, std::vector<std::string>& paramValues, std::vector<int> paramLengths, std::vector<int> paramFormats) override;
	virtual DbPropertySetResult set_property(DbProperty p, std::variant<bool, int, std::string> v) override;

	virtual bool getSchemas(std::vector<SchemaInfo*>& res) override;
	virtual bool getTables(std::string table, std::vector<TableInfo*>& res) override;
	virtual bool getColumns(std::string schema, std::string table, std::vector<ColumnInfo*>& columns) override;
	virtual bool getIndexes(std::string schema, std::string tabl, std::vector<IndexInfo*>& idxs) override;

private:

	int cobol2odbctype(int);
	int cobol2ctype(int);
	int get_data_len(SQLHANDLE hStmt, int cnum);

	static SQLHANDLE odbc_global_env_context;
	static int odbc_global_env_context_usage_count;

	SQLHANDLE conn_handle = nullptr;

	std::shared_ptr<ODBCStatementData> current_statement_data;
	std::shared_ptr<IConnection> owner;

	int last_rc = 0;
	std::string last_state;
	std::string last_error;

	std::map<std::string, std::shared_ptr<ICursor>> _declared_cursors;
	std::map<std::string, std::shared_ptr<ODBCStatementData>> _prepared_stmts;

	int odbcRetrieveError(int rc, ErrorSource err_src, SQLHANDLE h = 0);
	void odbcClearError();
	void odbcSetError(int err_code, std::string sqlstate, std::string err_msg);

	int _odbc_exec_params(std::shared_ptr<ICursor>, std::string query, int nParams, const std::vector<int>& paramTypes, const std::vector<std::string>& paramValues, const std::vector<int>& paramLengths, const std::vector<int>& paramFormats, std::shared_ptr<ODBCStatementData> prep_stmt = nullptr);
	int _odbc_exec(std::shared_ptr<ICursor>, const std::string, std::shared_ptr<ODBCStatementData> prep_stmt = nullptr);

	int get_affected_rows(std::shared_ptr<ODBCStatementData> d);
	bool is_cursor_from_prepared_statement(const std::shared_ptr<ICursor>& cursor);
	std::shared_ptr<ODBCStatementData> retrieve_prepared_statement(const std::string& prep_stmt_name);
	bool column_is_binary(SQLHANDLE stmt, int col_index, bool* is_binary);
};

