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
#include <map>

#if defined(_WIN32) || defined(_WIN64)

#if defined(__MINGW32__)
typedef unsigned char byte;
#endif

#endif

extern "C" {
#include <mysql.h>
}

#include "ICursor.h"
#include "IDbInterface.h"
#include "IDbManagerInterface.h"
#include "IDataSourceInfo.h"
#include "ISchemaManager.h"

struct MySQLStatementData
{

	MySQLStatementData();
	~MySQLStatementData();

	void resizeParams(int n);
	int resizeColumnData();

	int column_count = 0;
	std::vector<char*> data_buffers;
	std::vector<int> data_buffer_lengths;
	std::vector<unsigned long *> data_lengths;

	MYSQL_STMT* statement = nullptr;

private:
	void cleanup();

};


class DbInterfaceMySQL : public IDbInterface, public IDbManagerInterface
{
public:
	DbInterfaceMySQL();
	~DbInterfaceMySQL();

	virtual int init(const std::shared_ptr<spdlog::logger>& _logger) override;
	virtual int connect(IDataSourceInfo *, IConnectionOptions* opts) override;
	virtual int reset() override;
	virtual int terminate_connection() override;
	//virtual int begin_transaction() override;
	//virtual int end_transaction(string) override;
	virtual int exec(std::string) override;
	virtual int exec_params(std::string query, int nParams, const std::vector<int>& paramTypes, const std::vector<std::string>& paramValues, const std::vector<int>& paramLengths, const std::vector<int>& paramFormats) override;
	virtual int close_cursor(ICursor *) override;
	virtual int cursor_declare(ICursor *, bool, int) override;
	virtual int cursor_declare_with_params(ICursor *, char **, bool, int) override;
	virtual int cursor_open(ICursor *) override;
	virtual int fetch_one(ICursor *, int) override;
	virtual bool get_resultset_value(ResultSetContextType resultset_context_type, void* context, int row, int col, char* bfr, int bfrlen, int* value_len) override;
	virtual bool move_to_first_record(std::string stmt_name = "") override;
	virtual uint64_t get_native_features() override;
	virtual int get_num_rows(ICursor* crsr) override;
	virtual int get_num_fields(ICursor* crsr) override;
	virtual char *get_error_message() override;
	virtual int get_error_code() override;
	virtual std::string get_state() override;
	virtual void set_owner(IConnection *) override;
	virtual IConnection* get_owner() override;
	virtual int prepare(std::string stmt_name, std::string sql) override;
	virtual int exec_prepared(std::string stmt_name, std::vector<std::string> &paramValues, std::vector<int> paramLengths, std::vector<int> paramFormats) override;
	virtual DbPropertySetResult set_property(DbProperty p, std::variant<bool, int, std::string> v) override;

	virtual bool getSchemas(std::vector<SchemaInfo *>& res) override;
	virtual bool getTables(std::string table, std::vector<TableInfo*>& res) override;
	virtual bool getColumns(std::string schema, std::string table, std::vector<ColumnInfo*>& columns) override;
	virtual bool getIndexes(std::string schema, std::string tabl, std::vector<IndexInfo*> &idxs) override;

private:
	MYSQL *connaddr = nullptr;
	
	MySQLStatementData *current_statement_data = nullptr;

	IConnection *owner = nullptr;

	int last_rc;
	std::string last_error;
	std::string last_state;

	int mysqlRetrieveError(int rc);
	void mysqlClearError();
	void mysqlSetError(int err_code, std::string sqlstate, std::string err_msg);

	std::map<std::string, ICursor*> _declared_cursors;
	std::map<std::string, MySQLStatementData*> _prepared_stmts;

	int _mysql_exec_params(ICursor*, const std::string query, int nParams, const std::vector<int>& paramTypes, const std::vector<std::string>& paramValues, const std::vector<int>& paramLengths, const std::vector<int>& paramFormats, MySQLStatementData* prep_stmt_data = nullptr);
	int _mysql_exec(ICursor*, const std::string, MySQLStatementData* prep_stmt_data = nullptr);

	// Updatable cursor emulation
	bool updatable_cursors_emu = false;
	bool is_cursor_from_prepared_statement(ICursor* cursor);
	bool retrieve_prepared_statement(const std::string& prep_stmt_name, MySQLStatementData** prepared_stmt_data);
	bool has_unique_key(std::string table_name, ICursor* crsr, std::vector<std::string>& unique_key);
	bool prepare_updatable_cursor_query(const std::string& qry, ICursor* crsr, const std::vector<std::string>& unique_key, MYSQL_STMT** update_stmt, MYSQL_BIND** key_params, int* key_params_size);

};

