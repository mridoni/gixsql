/*
* Copyright (C) 2021 Marco Ridoni
* Copyright (C) 2013 Tokyo System House Co.,Ltd.
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

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstdbool>
#include <vector>
#include <map>
#include <string>
#include <cstring>
#include <memory>

#if (defined(_WIN32) || defined(_WIN64)) && !defined(__MINGW32__)
#include <io.h>
#else
#include <unistd.h>
#endif
#include <math.h>

#include "cobol_var_types.h"
#include "Connection.h"
#include "ConnectionManager.h"
#include "Cursor.h"
#include "CursorManager.h"
#include "DbInterfaceFactory.h"
#include "DataSourceInfo.h"
#include "SqlVar.h"
#include "SqlVarList.h"

#include "IDbInterface.h"
#include "IConnection.h"

#include "Logger.h"
#include "utils.h"
#include "gixsql.h"
#include "platform.h"
#include "Logger.h"

#include "spdlog/sinks/null_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"

#include "cobol_var_types.h"
#include "GlobalEnv.h"

#define FAIL_ON_ERROR(_rc, _st, _dbi, _err) if (_rc != DBERR_NO_ERROR) { \
										setStatus(_st, _dbi, _err); \
										return RESULT_FAILED; \
								   }

#define CHECK_LIB_INIT() if (!__lib_initialized) gixsql_initialize();

bool __lib_initialized = false;

struct query_info {
	char* pname;  // default
	char* query;
	int nParams;
};

static ConnectionManager connection_manager;
static CursorManager cursor_manager;


static void sqlca_initialize(struct sqlca_t*);
static int setStatus(struct sqlca_t* st, std::shared_ptr<IDbInterface> dbi, int err);
static AutoCommitMode get_autocommit(const std::shared_ptr<DataSourceInfo>& ds);
static bool get_fixup_params(const std::shared_ptr<DataSourceInfo>&);
static std::string get_client_encoding(const std::shared_ptr<DataSourceInfo>&);
static void init_sql_var_list(void);
static bool is_signed_numeric(CobolVarType t);

/* sql var list */
SqlVarList _current_sql_var_list;
SqlVarList _res_sql_var_list;

static int _gixsqlExec(const std::shared_ptr<IConnection>& conn, struct sqlca_t* st, char* _query);
static int _gixsqlExecParams(const std::shared_ptr<IConnection>& conn, struct sqlca_t* st, char* _query, unsigned int nParams);
static int _gixsqlCursorDeclare(struct sqlca_t* st, std::shared_ptr<IConnection> conn, std::string connection_name, std::string cursor_name, int with_hold, void* d_query, int query_tl, int nParams);
static int _gixsqlExecPrepared(sqlca_t* st, void* d_connection_id, int connection_id_tl, char* stmt_name, int nParams, std::shared_ptr<IDbInterface>& _dbi);
static int _gixsqlConnectReset(struct sqlca_t* st, const std::string& connection_id);

static std::string get_hostref_or_literal(void* data, int connection_id_tl);

static bool gixsql_initialize();

//int __norec_sqlcode = GIXSQL_DEFAULT_NO_REC_CODE;

GlobalEnv* __global_env;

static void
sqlca_initialize(struct sqlca_t* sqlca)
{
	memcpy((char*)sqlca, (char*)&sqlca_init, sizeof(struct sqlca_t));
}

// There are cases where the DB operation was successful
// but the data is invalid and cannot be used by GnuCOBOL.
// We ned to trap them and use the specific error code available
static bool is_data_error(int err)
{
	return (err == DBERR_NUM_OUT_OF_RANGE);
}

LIBGIXSQL_API int
GIXSQLConnect(struct sqlca_t* st, void* d_data_source, int data_source_tl, void* d_connection_id, int connection_id_tl,
	void* d_dbname, int dbname_tl, void* d_username, int username_tl, void* d_password, int password_tl)
{
	CHECK_LIB_INIT();

	spdlog::debug(FMT_FILE_FUNC "GIXSQLConnect start", __FILE__, __func__);

	std::string data_source_info;
	std::string connection_id;
	std::string dbname;
	std::string username;
	std::string password;

	// CONNECT ... AS
	connection_id = get_hostref_or_literal(d_connection_id, connection_id_tl);
	dbname = get_hostref_or_literal(d_dbname, dbname_tl);

	data_source_info = get_hostref_or_literal(d_data_source, data_source_tl);
	username = get_hostref_or_literal(d_username, username_tl);
	password = get_hostref_or_literal(d_password, password_tl);

	sqlca_initialize(st);

	trim(connection_id);

	if (!connection_id.empty() && connection_manager.exists(connection_id)) {
		spdlog::error("Connection already defined: {}", connection_id);
		setStatus(st, NULL, DBERR_NO_ERROR);
		return RESULT_FAILED;
	}

	std::shared_ptr<DataSourceInfo> data_source = std::make_shared<DataSourceInfo>();
	int rc = data_source->init(data_source_info, dbname, username, password);
	if (rc != 0) {
		spdlog::error("Cannot initialize connection parameters, aborting, data source is [{}]", data_source_info);
		setStatus(st, NULL, DBERR_CONN_INIT_ERROR);
		return RESULT_FAILED;
	}

	std::string dbtype = data_source->getDbType();
	std::shared_ptr<IDbInterface> dbi = DbInterfaceFactory::getInterface(dbtype, __global_env, gixsql_logger);
	if (dbi == NULL) {
		spdlog::error("Cannot initialize driver library, aborting, DB type is [{}]", dbtype);
		setStatus(st, NULL, DBERR_CONN_INVALID_DBTYPE);
		return RESULT_FAILED;
	}

	std::shared_ptr<IConnectionOptions> opts = std::make_shared<IConnectionOptions>();
	opts->autocommit = get_autocommit(data_source);;
	opts->fixup_parameters = get_fixup_params(data_source);
	opts->client_encoding = get_client_encoding(data_source);

	spdlog::trace(FMT_FILE_FUNC "Connection string : {}", __FILE__, __func__, data_source->get());
	spdlog::trace(FMT_FILE_FUNC "Data source info  : {}", __FILE__, __func__, data_source->dump());
	spdlog::trace(FMT_FILE_FUNC "Autocommit        : {}", __FILE__, __func__, (int)opts->autocommit);
	spdlog::trace(FMT_FILE_FUNC "Fix up parameters : {}", __FILE__, __func__, opts->fixup_parameters);
	spdlog::trace(FMT_FILE_FUNC "Client encoding   : {}", __FILE__, __func__, opts->client_encoding);

	rc = dbi->connect(data_source, opts);
	if (rc != DBERR_NO_ERROR) {
		setStatus(st, dbi, DBERR_CONNECTION_FAILED);
		//DbInterfaceFactory::removeInterface(dbi);		
		return RESULT_FAILED;
	}

	std::shared_ptr<Connection> c = connection_manager.create();
	c->setName(connection_id);	// it might still be empty, the connection manager will assign a default name
	c->setConnectionOptions(opts);	// Generic/global connection options, separate from driver-specific options that reside only in the data source info
	c->setConnectionInfo(data_source);
	c->setDbInterface(dbi);
	c->setOpened(true);
	connection_manager.add(c);

	spdlog::debug(FMT_FILE_FUNC "connection success. connection id# = {}, connection id = [{}]", __FILE__, __func__, c->getId(), connection_id);

	setStatus(st, NULL, DBERR_NO_ERROR);
	return RESULT_SUCCESS;
}

int _gixsqlConnectReset(struct sqlca_t* st, const std::string& connection_id)
{
	std::shared_ptr<Connection> conn = connection_manager.get(connection_id);
	if (conn == NULL) {
		setStatus(st, NULL, DBERR_CONN_NOT_FOUND);
		return RESULT_FAILED;
	}

	cursor_manager.clearConnectionCursors(conn->getId(), true);

	std::shared_ptr<IDbInterface> dbi = conn->getDbInterface();
	int rc = dbi->reset();
	FAIL_ON_ERROR(rc, st, dbi, DBERR_CONN_RESET_FAILED)
		int dbi_uc = dbi.use_count();
	int cc = conn.use_count();
	conn->setDbInterface(nullptr);
	conn->setOpened(false);
	connection_manager.remove(conn);
	setStatus(st, NULL, DBERR_NO_ERROR);

	return RESULT_SUCCESS;
}

LIBGIXSQL_API int
GIXSQLConnectReset(struct sqlca_t* st, void* d_connection_id, int connection_id_tl)
{
	CHECK_LIB_INIT();

	spdlog::trace(FMT_FILE_FUNC "GIXSQLConnectReset start", __FILE__, __func__);

	std::string connection_id = get_hostref_or_literal(d_connection_id, connection_id_tl);

	if (connection_id != "*") {
		spdlog::trace(FMT_FILE_FUNC "GIXSQLConnectReset: Executing connect reset on {}", __FILE__, __func__, connection_id);
		return _gixsqlConnectReset(st, connection_id);
	}
	else {
		spdlog::trace(FMT_FILE_FUNC "GIXSQLConnectReset: resetting all connections", __FILE__, __func__);
		for (std::shared_ptr<Connection> c : connection_manager.list()) {
			spdlog::trace(FMT_FILE_FUNC "GIXSQLConnectReset: trying to reset: [{}]", __FILE__, __func__, c->getName());
			int rc = _gixsqlConnectReset(st, c->getName());
			if (rc != RESULT_SUCCESS)
				return rc;
		}

		setStatus(st, NULL, DBERR_NO_ERROR);
		return RESULT_SUCCESS;
	}
}

LIBGIXSQL_API int
GIXSQLExec(struct sqlca_t* st, void* d_connection_id, int connection_id_tl, char* _query)
{
	CHECK_LIB_INIT();

	spdlog::trace(FMT_FILE_FUNC "GIXSQLExec start", __FILE__, __func__);
	spdlog::trace(FMT_FILE_FUNC "GIXSQLExec SQL: {}", __FILE__, __func__, _query);

	std::string connection_id = get_hostref_or_literal(d_connection_id, connection_id_tl);
	std::shared_ptr<Connection> conn = connection_manager.get(connection_id);
	if (conn == NULL) {
		spdlog::error("Can't find a connection");
		setStatus(st, NULL, DBERR_CONN_NOT_FOUND);
		return RESULT_FAILED;
	}

	sqlca_initialize(st);

	if (_query == NULL || strlen(_query) == 0) {
		spdlog::error("Empty query");
		setStatus(st, NULL, DBERR_EMPTY_QUERY);
		return RESULT_FAILED;
	}

	return _gixsqlExec(conn, st, _query);
}

LIBGIXSQL_API int
GIXSQLExecImmediate(struct sqlca_t* st, void* d_connection_id, int connection_id_tl, void* d_query, int query_tl)
{
	CHECK_LIB_INIT();

	spdlog::trace(FMT_FILE_FUNC "GIXSQLExecImmediate start", __FILE__, __func__);

	std::string connection_id = get_hostref_or_literal(d_connection_id, connection_id_tl);
	std::shared_ptr<Connection> conn = connection_manager.get(connection_id);
	if (conn == NULL) {
		spdlog::error("Can't find a connection");
		setStatus(st, NULL, DBERR_CONN_NOT_FOUND);
		return RESULT_FAILED;
	}

	std::string query = get_hostref_or_literal(d_query, query_tl);

	spdlog::trace(FMT_FILE_FUNC "GIXSQLExecImmediate SQL: {}", __FILE__, __func__, query);

	sqlca_initialize(st);

	if (query.empty()) {
		spdlog::error("Empty query");
		setStatus(st, NULL, DBERR_EMPTY_QUERY);
		return RESULT_FAILED;
	}

	return _gixsqlExec(conn, st, (char*)query.c_str());
}

static int _gixsqlExec(const std::shared_ptr<IConnection>& conn, struct sqlca_t* st, char* _query)
{
	CHECK_LIB_INIT();

	std::string query = _query;
	int rc = 0;
	std::shared_ptr<IDbInterface> dbi = conn->getDbInterface();

	if (is_commit_or_rollback_statement(query)) {
		cursor_manager.closeConnectionCursors(conn->getId(), false);
	}

	rc = dbi->exec(query);
	FAIL_ON_ERROR(rc, st, dbi, DBERR_SQL_ERROR)


		setStatus(st, NULL, DBERR_NO_ERROR);
	return RESULT_SUCCESS;
}

LIBGIXSQL_API int
GIXSQLExecParams(struct sqlca_t* st, void* d_connection_id, int connection_id_tl, char* _query, int nParams)
{
	CHECK_LIB_INIT();

	spdlog::trace(FMT_FILE_FUNC "GIXSQLExecParams - SQL: {}", __FILE__, __func__, _query);

	std::string connection_id = get_hostref_or_literal(d_connection_id, connection_id_tl);
	std::shared_ptr<Connection> conn = connection_manager.get(connection_id);
	if (conn == NULL) {
		spdlog::error("Can't find a connection");
		setStatus(st, NULL, DBERR_CONN_NOT_FOUND);
		return RESULT_FAILED;
	}

	if (_query == NULL || strlen(_query) == 0) {
		spdlog::error("Empty query");
		setStatus(st, NULL, DBERR_EMPTY_QUERY);
		return RESULT_FAILED;
	}

	sqlca_initialize(st);

	if (_current_sql_var_list.size() > nParams) {
		setStatus(st, NULL, DBERR_TOO_MANY_ARGUMENTS);
		return RESULT_FAILED;
	}
	else if (_current_sql_var_list.size() < nParams) {
		setStatus(st, NULL, DBERR_TOO_FEW_ARGUMENTS);
		return RESULT_FAILED;
	}


	return _gixsqlExecParams(conn, st, _query, nParams);
}

static int _gixsqlExecParams(const std::shared_ptr<IConnection>& conn, struct sqlca_t* st, char* _query, unsigned int nParams)
{
	std::vector<std_binary_data> param_values;
	std::vector<CobolVarType> param_types;
	std::vector<unsigned long> param_lengths;
	std::vector<uint32_t> param_flags;
	std::vector<SqlVar*>::iterator it;

	// set parameters
	for (it = _current_sql_var_list.begin(); it != _current_sql_var_list.end(); it++) {
		SqlVar* v = *it;
		param_types.push_back(v->getType());
		param_values.push_back(v->getDbData());
		param_lengths.push_back(!v->isDbNull() ? v->getDisplayLength() : DB_NULL);
		param_flags.push_back(v->getFlags());
	}

	std::string query = _query;
	int rc = 0;
	std::shared_ptr<IDbInterface> dbi = conn->getDbInterface();
	if (!dbi) {
		FAIL_ON_ERROR(1, st, dbi, DBERR_SQL_ERROR);
	}

	if (is_commit_or_rollback_statement(query)) {
		cursor_manager.closeConnectionCursors(conn->getId(), false);
	}

	rc = dbi->exec_params(query, param_types, param_values, param_lengths, param_flags);
	FAIL_ON_ERROR(rc, st, dbi, DBERR_SQL_ERROR)

		setStatus(st, NULL, DBERR_NO_ERROR);
	return RESULT_SUCCESS;
}

int _gixsqlExecPrepared(sqlca_t* st, void* d_connection_id, int connection_id_tl, char* stmt_name, int nParams, std::shared_ptr<IDbInterface>& r_dbi)
{
	CHECK_LIB_INIT();

	//*r_dbi = nullptr;

	std::string connection_id = get_hostref_or_literal(d_connection_id, connection_id_tl);
	std::shared_ptr<Connection> conn = connection_manager.get(connection_id);
	if (conn == NULL) {
		spdlog::error("Can't find a connection");
		setStatus(st, NULL, DBERR_CONN_NOT_FOUND);
		return RESULT_FAILED;
	}

	if (stmt_name == NULL || strlen(stmt_name) == 0) {
		spdlog::error("Empty statement name");
		setStatus(st, NULL, DBERR_EMPTY_QUERY);
		return RESULT_FAILED;
	}

	sqlca_initialize(st);

	// TODO: check number of parameters
	if (_current_sql_var_list.size() > nParams) {
		setStatus(st, NULL, DBERR_TOO_MANY_ARGUMENTS);
		return RESULT_FAILED;
	}
	else if (_current_sql_var_list.size() < nParams) {
		setStatus(st, NULL, DBERR_TOO_FEW_ARGUMENTS);
		return RESULT_FAILED;
	}

	std::vector<std_binary_data> param_values;
	std::vector<unsigned long> param_lengths;
	std::vector<CobolVarType> param_types;
	std::vector<uint32_t> param_flags;
	std::vector<SqlVar*>::iterator it;

	// set parameters
	for (it = _current_sql_var_list.begin(); it != _current_sql_var_list.end(); it++) {
		param_values.push_back((*it)->getDbData());
		param_types.push_back((*it)->getType());
		param_lengths.push_back((*it)->getDisplayLength());
		param_flags.push_back((*it)->getFlags());
	}

	int rc = 0;
	std::shared_ptr<IDbInterface> dbi = conn->getDbInterface();
	if (!dbi)
		FAIL_ON_ERROR(1, st, dbi, DBERR_SQL_ERROR)

		rc = dbi->exec_prepared(stmt_name, param_types, param_values, param_lengths, param_flags);
	FAIL_ON_ERROR(rc, st, dbi, DBERR_SQL_ERROR)

		setStatus(st, NULL, DBERR_NO_ERROR);

	r_dbi = dbi;
	return RESULT_SUCCESS;
}

LIBGIXSQL_API int GIXSQLExecPrepared(sqlca_t* st, void* d_connection_id, int connection_id_tl, char* stmt_name, int nParams)
{
	CHECK_LIB_INIT();

	std::shared_ptr<IDbInterface> dbi;	// not used but we need it for the call to the worker function
	spdlog::trace(FMT_FILE_FUNC "GIXSQLExecPrepared start", __FILE__, __func__);
	return _gixsqlExecPrepared(st, d_connection_id, connection_id_tl, stmt_name, nParams, dbi);
}

LIBGIXSQL_API int GIXSQLExecPreparedInto(sqlca_t* st, void* d_connection_id, int connection_id_tl, char* stmt_name, int nParams, int nResParams)
{
	CHECK_LIB_INIT();

	std::shared_ptr<IDbInterface> dbi;
	spdlog::trace(FMT_FILE_FUNC "GIXSQLExecPreparedInto start", __FILE__, __func__);

	int rc = _gixsqlExecPrepared(st, d_connection_id, connection_id_tl, stmt_name, nParams, dbi);
	if (rc != RESULT_SUCCESS)
		return rc;

	if (!dbi->move_to_first_record(stmt_name)) {
		spdlog::error("move_to_first_record failed: {} - {}:", dbi->get_error_code(), dbi->get_state(), dbi->get_error_message());
		setStatus(st, dbi, dbi->get_error_code());
		return RESULT_FAILED;
	}

	spdlog::trace(FMT_FILE_FUNC "move_to_first_record successful", __FILE__, __func__);

	int nfields = dbi->get_num_fields(nullptr);
	if (nfields != nResParams) {
		spdlog::error("ResParams({}) and fields({}) are different", nResParams, nfields);
		setStatus(st, NULL, DBERR_FIELD_COUNT_MISMATCH);
		return RESULT_FAILED;
	}

	// Some drivers (notably DB2, both natevely and under ODBC, do not support SQLNumRows, so we have to check here
	if (dbi->has(DbNativeFeature::ResultSetRowCount)) {

		// check numtuples
		if (dbi->get_num_rows(nullptr) < 1) {
			spdlog::trace("No data");
			setStatus(st, NULL, DBERR_NO_DATA);
			return RESULT_FAILED;
		}

		if (dbi->get_num_rows(nullptr) > 1) {
			spdlog::trace("Too much data");
			setStatus(st, NULL, DBERR_TOO_MUCH_DATA);
			return RESULT_FAILED;
		}
	}

	// set result params
	uint64_t datalen = 0;
	int sqlcode = 0;
	bool has_invalid_column_data = false;
	uint64_t bsize = _res_sql_var_list.getMaxLength() + __global_env->varlen_length_sz() + 1;

	std::unique_ptr<char[]> buffer = std::make_unique<char[]>(bsize);
	for (int i = 0; i < _res_sql_var_list.size(); i++) {
		SqlVar* v = _res_sql_var_list.at(i);
		bool is_null = false;
		if (!dbi->get_resultset_value(ResultSetContextType::PreparedStatement, PreparedStatementContextData(stmt_name), 0, i, buffer.get(), bsize, &datalen, &is_null)) {
			setStatus(st, dbi, DBERR_INVALID_COLUMN_DATA);
			sqlcode = DBERR_INVALID_COLUMN_DATA;
			continue;
		}

		int sql_code_local = DBERR_NO_ERROR;
		v->createCobolData(buffer.get(), datalen, &sql_code_local);
		if (sql_code_local) {
			setStatus(st, dbi, sql_code_local);
			sqlcode = sql_code_local;
			continue;
		}

		spdlog::trace(FMT_FILE_FUNC "result parameter {} - addr: {}", __FILE__, __func__, i + 1, (void*)v->getAddr());
	}

	if (sqlcode != 0) {
		return RESULT_FAILED;
	}

	setStatus(st, NULL, DBERR_NO_ERROR);
	return RESULT_SUCCESS;
}

LIBGIXSQL_API int
GIXSQLCursorDeclareParams(struct sqlca_t* st, void* d_connection_id, int connection_id_tl, char* cursor_name, int with_hold, void* d_query, int query_tl, int nParams)
{
	CHECK_LIB_INIT();

	spdlog::trace(FMT_FILE_FUNC "GIXSQLCursorDeclareParams start for cursor [{}]", __FILE__, __func__, cursor_name);

	bool is_literal = (query_tl == 0);
	std::string query_or_stmt_name = get_hostref_or_literal(d_query, query_tl);

	spdlog::trace(FMT_FILE_FUNC "SQL: #{}#", __FILE__, __func__, is_literal ? query_or_stmt_name : "(from field at runtime)");

	std::string connection_id = get_hostref_or_literal(d_connection_id, connection_id_tl);
	std::shared_ptr<Connection> conn = connection_manager.get(connection_id);
	if (conn == NULL) {
		spdlog::trace(FMT_FILE_FUNC "connection id is not found, cursor initialization will be performed later", __FILE__, __func__);
	}

	// check argument
	if ((is_literal && query_or_stmt_name.empty()) ||
		cursor_name == NULL || strlen(cursor_name) == 0) {
		spdlog::error("Empty query/cursor");
		setStatus(st, NULL, DBERR_EMPTY_QUERY);
		return RESULT_FAILED;
	}

	// check argument
	if (nParams == 0) {
		setStatus(st, NULL, DBERR_NO_PARAMETERS);
		return RESULT_FAILED;
	}

	if (_current_sql_var_list.size() > nParams) {
		setStatus(st, NULL, DBERR_TOO_MANY_ARGUMENTS);
		return RESULT_FAILED;
	}
	else if (_current_sql_var_list.size() < nParams) {
		setStatus(st, NULL, DBERR_TOO_FEW_ARGUMENTS);
		return RESULT_FAILED;
	}

	int rc = _gixsqlCursorDeclare(st, conn, connection_id, cursor_name, with_hold, d_query, query_tl, nParams);

	if (!rc && !conn && d_connection_id && connection_id_tl) {
		// This means we can try to resolve it later (possibly on open), so we save the COBOL field reference
		std::shared_ptr<Cursor> c = cursor_manager.get(cursor_name);
		if (c) {
			c->setConnectionReference(d_connection_id, connection_id_tl);
		}

	}

	return rc;
}


LIBGIXSQL_API int
GIXSQLCursorDeclare(struct sqlca_t* st, void* d_connection_id, int connection_id_tl, char* cursor_name, int with_hold, void* d_query, int query_tl)
{
	CHECK_LIB_INIT();

	spdlog::trace(FMT_FILE_FUNC "GIXSQLCursorDeclare start for cursor [{}]", __FILE__, __func__, cursor_name);

	bool is_literal = (query_tl == 0);
	std::string query_or_stmt_name = get_hostref_or_literal(d_query, query_tl);

	spdlog::trace(FMT_FILE_FUNC "SQL: #{}#", __FILE__, __func__, is_literal ? query_or_stmt_name : "(from field at runtime)");

	std::string connection_id = get_hostref_or_literal(d_connection_id, connection_id_tl);
	std::shared_ptr<Connection> conn = connection_manager.get(connection_id);
	if (conn == NULL) {
		spdlog::trace(FMT_FILE_FUNC "connection id is not found, cursor initialization will be performed later", __FILE__, __func__);
	}

	// check argument
	if ((is_literal && query_or_stmt_name.empty()) ||
		cursor_name == NULL || strlen(cursor_name) == 0) {
		spdlog::error("Empty query/cursor");
		setStatus(st, NULL, DBERR_EMPTY_QUERY);
		return RESULT_FAILED;
	}

	sqlca_initialize(st);

	int rc = _gixsqlCursorDeclare(st, conn, connection_id, cursor_name, with_hold, d_query, query_tl, 0);

	if (!rc && !conn && d_connection_id && connection_id_tl) {
		// This means we can try to resolve it later (possibly on open), so we save the COBOL field reference
		std::shared_ptr<Cursor> c = cursor_manager.get(cursor_name);
		if (c) {
			c->setConnectionReference(d_connection_id, connection_id_tl);
		}

	}
	return rc;
}

static int  _gixsqlCursorDeclare(struct sqlca_t* st, std::shared_ptr<IConnection> conn, std::string connection_name, std::string cursor_name, int with_hold, void* d_query, int query_tl, int nParams)
{
	if (cursor_manager.exists(cursor_name)) {
		spdlog::error("Cursor exists: {}", cursor_name);
		setStatus(st, NULL, DBERR_CURSOR_EXISTS);
		return RESULT_FAILED;
	}

	std::shared_ptr<Cursor> c = cursor_manager.create();
	c->setConnection(conn);
	c->setConnectionName(connection_name);
	c->setName(std::string(cursor_name));

	if (!query_tl)
		c->setQuery(get_hostref_or_literal(d_query, query_tl));
	else
		c->setQuerySource(d_query, query_tl);

	c->setNumParams(nParams);
	c->setWithHold(with_hold);

	if (nParams > 0) {
		c->setParameters(_current_sql_var_list);
	}

	if (conn) {
		std::shared_ptr<IDbInterface> dbi = conn->getDbInterface();
		if (dbi->cursor_declare(c)) {
			spdlog::error("Invalid cursor data for cursor [{}]", cursor_name);
			setStatus(st, NULL, DBERR_DECLARE_CURSOR_FAILED);
			return RESULT_FAILED;
		}
	}

	cursor_manager.add(c);

	setStatus(st, NULL, DBERR_NO_ERROR);
	return RESULT_SUCCESS;
}

LIBGIXSQL_API int
GIXSQLCursorOpen(struct sqlca_t* st, char* cname)
{
	CHECK_LIB_INIT();

	int rc = 0;

	spdlog::trace(FMT_FILE_FUNC "GIXSQLCursorOpen start for cursor [{}]", __FILE__, __func__, cname);

	sqlca_initialize(st);

	// check argument
	if (cname == NULL || strlen(cname) == 0) {
		spdlog::error("Empty query/cursor");
		setStatus(st, NULL, DBERR_EMPTY_QUERY);
		return RESULT_FAILED;
	}

	// search cursor
	std::shared_ptr<Cursor> cursor = cursor_manager.get(std::string(cname));
	if (cursor == NULL) {
		spdlog::error("cursor {} not registered", cname);
		setStatus(st, NULL, DBERR_NO_SUCH_CURSOR);
		return RESULT_FAILED;
	}

	void* crsr_src_addr = nullptr;
	int crsr_src_len = 0;
	if (cursor->getQuery().empty()) {
		cursor->getQuerySource(&crsr_src_addr, &crsr_src_len);
		if (!crsr_src_addr) {
			spdlog::error("cursor {} has an empty query", cname);
			setStatus(st, NULL, DBERR_EMPTY_QUERY);
			return RESULT_FAILED;
		}
	}

	// This was not properly declared, we have to set it up
	if (!cursor->getConnection()) {
		std::string connection_id = cursor->getConnectionName();
		std::shared_ptr<IConnection> cc = connection_manager.get(connection_id);
		if (cc)
			cursor->setConnection(cc);
		else {
			// try to resolve the connection id through a reference to a COBOL variable
			connection_id = cursor->getConnectionNameFromReference();
			cc = connection_manager.get(connection_id);
			if (cc)
				cursor->setConnection(cc);
		}

		if (cursor->getConnection()) {
			std::shared_ptr<IDbInterface> cdbi = cursor->getConnection()->getDbInterface();
			if (!cdbi || cdbi->cursor_declare(cursor)) {
				spdlog::error("Invalid cursor data: {}", cname);
				setStatus(st, NULL, DBERR_DECLARE_CURSOR_FAILED);
				return RESULT_FAILED;
			}
		}
	}

	if (!cursor->getConnection()) {
		spdlog::error("Cannot find an active connection for cursor {}", cname);
		setStatus(st, NULL, DBERR_CONN_NOT_FOUND);
		return RESULT_FAILED;
	}

	std::shared_ptr<IConnection> c = cursor->getConnection();
	std::shared_ptr<IDbInterface> dbi = c->getDbInterface();

	if (cursor->isOpen()) {
		spdlog::error("cursor {} is alredy open", cname);
		rc = dbi->cursor_close(cursor);
		cursor->setOpened(false);
		FAIL_ON_ERROR(rc, st, dbi, DBERR_CLOSE_CURSOR_FAILED)
	}

	rc = dbi->cursor_open(cursor);
	cursor->setOpened(rc == DBERR_NO_ERROR);
	FAIL_ON_ERROR(rc, st, dbi, DBERR_OPEN_CURSOR_FAILED)

		setStatus(st, NULL, DBERR_NO_ERROR);
	return RESULT_SUCCESS;
}

LIBGIXSQL_API int GIXSQLCursorFetchOne(struct sqlca_t* st, char* cname)
{
	CHECK_LIB_INIT();

	spdlog::trace(FMT_FILE_FUNC "GIXSQLCursorFetchOne start", __FILE__, __func__);

	sqlca_initialize(st);

	// check argument
	if (cname == NULL || strlen(cname) == 0) {
		setStatus(st, NULL, DBERR_NO_SUCH_CURSOR);
		return RESULT_FAILED;
	}

	spdlog::trace(FMT_FILE_FUNC "GIXSQLCursorFetchOne - cursor name: {}", __FILE__, __func__, cname);

	std::shared_ptr<Cursor> cursor = cursor_manager.get(cname);
	if (cursor == NULL) {
		spdlog::error("cursor {} not registered", cname);
		setStatus(st, NULL, DBERR_NO_SUCH_CURSOR);
		return RESULT_FAILED;
	}

	if (!cursor->isOpen()) {
		spdlog::error("cursor {} closed", cname);
		setStatus(st, NULL, DBERR_CURSOR_CLOSED);
		return RESULT_FAILED;
	}

	std::shared_ptr<IDbInterface> dbi = cursor->getConnection()->getDbInterface();
	int rc = dbi->cursor_fetch_one(cursor, FETCH_NEXT_ROW);
	if (rc == DBERR_NO_DATA) {
		setStatus(st, dbi, DBERR_NO_DATA);
		return DBERR_FETCH_ROW_FAILED;
	}
	FAIL_ON_ERROR(rc, st, dbi, DBERR_FETCH_ROW_FAILED)

		int nResParams = _res_sql_var_list.size();
	int nfields = dbi->get_num_fields(cursor);
	if (nfields != nResParams) {
		spdlog::error("ResParams({}) and fields({}) are different", nResParams, nfields);
		setStatus(st, dbi, DBERR_FIELD_COUNT_MISMATCH);
		return RESULT_FAILED;
	}

	uint64_t bsize = _res_sql_var_list.getMaxLength() + __global_env->varlen_length_sz() + 1;
	std::unique_ptr<char[]> buffer = std::make_unique<char[]>(bsize);
	std::vector<SqlVar*>::iterator it;
	int i = 0;
	uint64_t datalen = 0;
	int sqlcode = 0;
	for (it = _res_sql_var_list.begin(); it != _res_sql_var_list.end(); it++) {
		bool is_null = false;
		if (!dbi->get_resultset_value(ResultSetContextType::Cursor, CursorContextData(cursor), 0, i++, buffer.get(), bsize, &datalen, &is_null)) {
			setStatus(st, dbi, DBERR_INVALID_COLUMN_DATA);
			sqlcode = DBERR_INVALID_COLUMN_DATA;
			continue;
		}

		int sql_code_local = DBERR_NO_ERROR;
		char* _data_bfr = is_null ? nullptr : buffer.get();
		uint64_t _data_len = is_null ? 0 : datalen;
		(*it)->createCobolData(_data_bfr, _data_len, &sql_code_local);
		if (sql_code_local) {
			setStatus(st, dbi, sql_code_local);
			sqlcode = sql_code_local;
			continue;
		}

		spdlog::trace(FMT_FILE_FUNC "result parameter {} - addr: {}", __FILE__, __func__, i + 1, (*it)->getAddr());

	}

	if (sqlcode != 0) {
		return RESULT_FAILED;
	}

	setStatus(st, NULL, DBERR_NO_ERROR);
	return RESULT_SUCCESS;

}

LIBGIXSQL_API int
GIXSQLCursorClose(struct sqlca_t* st, char* cname)
{
	CHECK_LIB_INIT();

	spdlog::trace(FMT_FILE_FUNC "GIXSQLCursorClose start", __FILE__, __func__);

	sqlca_initialize(st);

	std::shared_ptr<Cursor> cursor = cursor_manager.get(cname);
	if (cursor == NULL) {
		spdlog::error("cursor {} not registered", cname);
		setStatus(st, NULL, DBERR_NO_SUCH_CURSOR);
		return RESULT_FAILED;
	}

	if (!cursor->isOpen()) {
		spdlog::error("cursor {} already closed", cname);
		setStatus(st, NULL, DBERR_NO_ERROR);
		return RESULT_SUCCESS;
	}

	std::shared_ptr<IConnection> conn = cursor->getConnection();
	if (conn == NULL) {
		spdlog::error("No connection assigned, invalid cursor: {}", cname);	// warning
		setStatus(st, NULL, DBERR_NO_ERROR);
		return RESULT_SUCCESS;
	}

	std::shared_ptr<IDbInterface> dbi = cursor->getConnection()->getDbInterface();
	int rc = dbi->cursor_close(cursor);

	// when closing a cursor we always mark its logical state as closed, 
	// even if an error occurred
	cursor->setOpened(false);

	FAIL_ON_ERROR(rc, st, dbi, DBERR_CLOSE_CURSOR_FAILED)

		setStatus(st, NULL, DBERR_NO_ERROR);
	return RESULT_SUCCESS;
}

LIBGIXSQL_API int GIXSQLPrepareStatement(sqlca_t* st, void* d_connection_id, int connection_id_tl, char* stmt_name, void* d_statement_src, int statement_src_tl)
{
	CHECK_LIB_INIT();

	spdlog::trace(FMT_FILE_FUNC "GIXSQLPrepareStatement start", __FILE__, __func__);
	spdlog::trace(FMT_FILE_FUNC "Statement name: {}", __FILE__, __func__, stmt_name);

	std::string connection_id = get_hostref_or_literal(d_connection_id, connection_id_tl);
	std::shared_ptr<Connection> conn = connection_manager.get(connection_id);
	if (conn == NULL) {
		spdlog::error("Can't find a connection");
		setStatus(st, NULL, DBERR_CONN_NOT_FOUND);
		return RESULT_FAILED;
	}

	std::string statement_src = get_hostref_or_literal(d_statement_src, statement_src_tl);
	spdlog::trace(FMT_FILE_FUNC "Statement source: {}", __FILE__, __func__, statement_src);

	// check argument
	if (stmt_name == NULL || strlen(stmt_name) == 0) {
		spdlog::error("Empty statement name");
		setStatus(st, NULL, DBERR_EMPTY_QUERY);
		return RESULT_FAILED;
	}

	std::shared_ptr<IDbInterface> dbi = conn->getDbInterface();

	if (dbi->prepare(stmt_name, statement_src)) {
		spdlog::error("Cannot prepare statement (2)");
		setStatus(st, dbi, DBERR_SQL_ERROR);
		return RESULT_FAILED;
	}

	return RESULT_SUCCESS;
}

LIBGIXSQL_API int
GIXSQLExecSelectIntoOne(struct sqlca_t* st, void* d_connection_id, int connection_id_tl, char* _query, int nParams, int nResParams)
{
	CHECK_LIB_INIT();

	spdlog::trace(FMT_FILE_FUNC "GIXSQLExecSelectIntoOne start", __FILE__, __func__);
	spdlog::trace(FMT_FILE_FUNC "SQL: #{}#", __FILE__, __func__, _query);

	std::string connection_id = get_hostref_or_literal(d_connection_id, connection_id_tl);
	std::shared_ptr<Connection> conn = connection_manager.get(connection_id);
	if (conn == NULL) {
		spdlog::error("Can't find a connection");
		setStatus(st, NULL, DBERR_CONN_NOT_FOUND);
		return RESULT_FAILED;
	}

	// check argument
	if (_query == NULL || strlen(_query) == 0) {
		spdlog::error("Empty query/cursor");
		setStatus(st, NULL, DBERR_EMPTY_QUERY);
		return RESULT_FAILED;
	}

	std::shared_ptr<IDbInterface> dbi = conn->getDbInterface();

	if (nParams > 0) {
		if (_gixsqlExecParams(conn, st, _query, nParams) != RESULT_SUCCESS)
			return RESULT_FAILED;
	}
	else {
		if (_gixsqlExec(conn, st, _query) != RESULT_SUCCESS)
			return RESULT_FAILED;
	}

	if (!dbi->move_to_first_record()) {
		spdlog::error("move_to_first_record failed: {} - {}: {}", dbi->get_error_code(), dbi->get_state(), dbi->get_error_message());
		setStatus(st, dbi, dbi->get_error_code());
		return RESULT_FAILED;
	}

	spdlog::trace(FMT_FILE_FUNC "move_to_first_record successful", __FILE__, __func__);

	int nfields = dbi->get_num_fields(nullptr);
	if (nfields != nResParams) {
		spdlog::error("ResParams({}) and fields({}) are different", nResParams, nfields);
		setStatus(st, NULL, DBERR_FIELD_COUNT_MISMATCH);
		return RESULT_FAILED;
	}

	// Some drivers (notably DB2, both natevely and under ODBC, do not support SQLNumRows, so we have to check here
	if (dbi->has(DbNativeFeature::ResultSetRowCount)) {

		// check numtuples
		if (dbi->get_num_rows(nullptr) < 1) {
			spdlog::trace("No data");
			setStatus(st, dbi, DBERR_NO_DATA);
			return RESULT_FAILED;
		}

		if (dbi->get_num_rows(nullptr) > 1) {
			spdlog::trace("Too much data");
			setStatus(st, dbi, DBERR_TOO_MUCH_DATA);
			return RESULT_FAILED;
		}
	}

	// set result params
	uint64_t datalen = 0;
	int sqlcode = 0;
	bool has_invalid_column_data = false;
	uint64_t bsize = _res_sql_var_list.getMaxLength() + __global_env->varlen_length_sz() + 1;
	std::unique_ptr<char[]> buffer = std::make_unique<char[]>(bsize);
	for (int i = 0; i < _res_sql_var_list.size(); i++) {
		SqlVar* v = _res_sql_var_list.at(i);
		bool is_null = false;
		if (!dbi->get_resultset_value(ResultSetContextType::CurrentResultSet, CurrentResultSetContextData(), 0, i, buffer.get(), bsize, &datalen, &is_null)) {
			setStatus(st, dbi, DBERR_INVALID_COLUMN_DATA);
			sqlcode = DBERR_INVALID_COLUMN_DATA;
			continue;
		}

		int sql_code_local = DBERR_NO_ERROR;
		char* _data_bfr = is_null ? nullptr : buffer.get();
		uint64_t _data_len = is_null ? 0 : datalen;
		v->createCobolData(_data_bfr, _data_len, &sql_code_local);
		if (sql_code_local) {
			setStatus(st, dbi, sql_code_local);
			sqlcode = sql_code_local;
			continue;
		}

		spdlog::trace(FMT_FILE_FUNC "result parameter {} - addr: {}", __FILE__, __func__, i + 1, (void*)v->getAddr());
	}

	if (sqlcode != 0) {
		return RESULT_FAILED;
	}

	setStatus(st, NULL, DBERR_NO_ERROR);
	return RESULT_SUCCESS;
}

LIBGIXSQL_API int
GIXSQLDisconnect(struct sqlca_t* st, void* d_connection_id, int connection_id_tl)
{
	CHECK_LIB_INIT();

	spdlog::trace(FMT_FILE_FUNC "GIXSQLDisconnect start", __FILE__, __func__);

	std::string connection_id = get_hostref_or_literal(d_connection_id, connection_id_tl);
	std::shared_ptr<Connection> conn = connection_manager.get(connection_id);
	if (conn == NULL) {
		spdlog::error("connection is not found");
		return RESULT_FAILED;
	}

	cursor_manager.clearConnectionCursors(conn->getId(), true);

	std::shared_ptr<IDbInterface> dbi = conn->getDbInterface();
	int rc = dbi->terminate_connection();
	conn->setOpened(false);

	FAIL_ON_ERROR(rc, st, dbi, DBERR_DISCONNECT_FAILED)

		setStatus(st, NULL, DBERR_NO_ERROR);
	return RESULT_SUCCESS;
}

LIBGIXSQL_API int
GIXSQLStartSQL(void)
{
	CHECK_LIB_INIT();

	spdlog::trace(FMT_FILE_FUNC "#begin SQL fragment", __FILE__, __func__);
	init_sql_var_list();
	spdlog::trace(FMT_FILE_FUNC "#end SQL fragment", __FILE__, __func__);
	return 0;
}

LIBGIXSQL_API int
GIXSQLSetSQLParams(int type, int length, int scale, uint32_t flags, void* var_addr, void* ind_addr)
{
	CHECK_LIB_INIT();

	if (type < COBOL_TYPE_MIN || type > COBOL_TYPE_MAX) {
		spdlog::error("invalid argument 'type': {}", type);
		return RESULT_FAILED;
	}

	if (length < 0) {
		spdlog::error("invalid argument 'length': {}", length);
		return RESULT_FAILED;
	}

	if (!var_addr) {
		spdlog::error("invalid argument addr is NULL");
		return RESULT_FAILED;
	}

	if (_current_sql_var_list.AddVar(static_cast<CobolVarType>(type), length, scale, flags, var_addr, ind_addr) == NULL) {
		spdlog::error("fail to add SQLVARLIST");
		return RESULT_FAILED;
	}

	return RESULT_SUCCESS;
}

LIBGIXSQL_API int GIXSQLSetResultParams(int type, int length, int scale, uint32_t flags, void* var_addr, void* ind_addr)
{
	CHECK_LIB_INIT();

	if (type < COBOL_TYPE_MIN || type > COBOL_TYPE_MAX) {
		spdlog::error("invalid argument 'type' for variable: {}", type);
		return RESULT_FAILED;
	}

	if (length < 0) {
		spdlog::error("invalid argument 'length': {}", length);
		return RESULT_FAILED;
	}

	if (!var_addr) {
		spdlog::error("invalid argument addr: NULL");
		return RESULT_FAILED;
	}

	if (_res_sql_var_list.AddVar(static_cast<CobolVarType>(type), length, scale, flags, var_addr, ind_addr) == NULL) {
		spdlog::error("fail to add SQLVARLIST");
		return RESULT_FAILED;
	}
	return RESULT_SUCCESS;
}

LIBGIXSQL_API int GIXSQLEndSQL(void)
{
	CHECK_LIB_INIT();

	spdlog::trace(FMT_FILE_FUNC "#debug start dump var_list", __FILE__, __func__);
	_current_sql_var_list.dump();
	spdlog::trace(FMT_FILE_FUNC "#debug start dump res_list", __FILE__, __func__);
	_res_sql_var_list.dump();
	spdlog::trace(FMT_FILE_FUNC "#debug start dump list", __FILE__, __func__);

	_current_sql_var_list.clear();
	_res_sql_var_list.clear();

	return RESULT_SUCCESS;
}

static void init_sql_var_list(void)
{
	_current_sql_var_list.clear();
	_res_sql_var_list.clear();
}

static void set_sqlerrm(struct sqlca_t* st, const char* m)
{
	if (strlen(m) <= 70)
		strcpy(st->sqlerrm.sqlerrmc, m);
	else {
		strncpy(st->sqlerrm.sqlerrmc, m, 69);
		st->sqlerrm.sqlerrmc[69] = 0;
	}
	st->sqlerrm.sqlerrml = strlen(st->sqlerrm.sqlerrmc);
}

static int setStatus(struct sqlca_t* st, std::shared_ptr<IDbInterface> dbi, int err)
{
	sqlca_initialize(st);

	switch (err) {
	case DBERR_NO_ERROR:
		memcpy(st->sqlstate, "00000", 5);
		st->sqlcode = 0;
		memset(st->sqlerrm.sqlerrmc, ' ', 69);
		st->sqlerrm.sqlerrmc[69] = 0;
		st->sqlerrm.sqlerrml = 69;
		return RESULT_SUCCESS;

	case DBERR_CONNECTION_FAILED:
		memcpy(st->sqlstate, "08001", 5);
		set_sqlerrm(st, "Connection failed");
		break;

	case DBERR_BEGIN_TX_FAILED:
		memcpy(st->sqlstate, "3B001", 5);
		set_sqlerrm(st, "Start transaction failed");
		break;

	case DBERR_END_TX_FAILED:
		memcpy(st->sqlstate, "2D521", 5);
		set_sqlerrm(st, "End transaction failed");
		break;

	case DBERR_CONN_NOT_FOUND:
		memcpy(st->sqlstate, "08003", 5);
		set_sqlerrm(st, "Connection ID not found");
		break;

	case DBERR_CONN_RESET_FAILED:
		memcpy(st->sqlstate, "08005", 5);
		set_sqlerrm(st, "Connection reset failed");
		break;

	case DBERR_EMPTY_QUERY:
		memcpy(st->sqlstate, "42617", 5);
		set_sqlerrm(st, "Empty query");
		break;

	case DBERR_SQL_ERROR:
		memcpy(st->sqlstate, "42617", 5);
		set_sqlerrm(st, "SQL error");
		break;

	case DBERR_TOO_MANY_ARGUMENTS:
		memcpy(st->sqlstate, "07001", 5);
		set_sqlerrm(st, "Too many arguments");
		break;

	case DBERR_TOO_FEW_ARGUMENTS:
		memcpy(st->sqlstate, "07001", 5);
		set_sqlerrm(st, "Too few arguments");
		break;

	case DBERR_NO_PARAMETERS:
		memcpy(st->sqlstate, "07002", 5);
		set_sqlerrm(st, "No parameters found");
		break;

	case DBERR_CURSOR_EXISTS:
		memcpy(st->sqlstate, "24502", 5);
		set_sqlerrm(st, "Cursor exists");
		break;

	case DBERR_NO_SUCH_CURSOR:
		memcpy(st->sqlstate, "24518", 5);
		set_sqlerrm(st, "No such cursor");
		break;

	case DBERR_CLOSE_CURSOR_FAILED:
		memcpy(st->sqlstate, "42887", 5);
		set_sqlerrm(st, "Close cursor failed");
		break;

	case DBERR_DISCONNECT_FAILED:
		memcpy(st->sqlstate, "08006", 5);
		set_sqlerrm(st, "Disconnect failed");
		break;

	case DBERR_OUT_OF_MEMORY:
		memcpy(st->sqlstate, "58900", 5);
		set_sqlerrm(st, "Out of memory");
		break;

	case DBERR_DECLARE_CURSOR_FAILED:
		memcpy(st->sqlstate, "34001", 5);
		set_sqlerrm(st, "Declare cursor failed");
		break;

	case DBERR_OPEN_CURSOR_FAILED:
		memcpy(st->sqlstate, "245F0", 5);
		set_sqlerrm(st, "Open cursor failed");
		break;

	case DBERR_FETCH_ROW_FAILED:
		memcpy(st->sqlstate, "24591", 5);
		set_sqlerrm(st, "Fetch row failed");
		break;

	case DBERR_INVALID_COLUMN_DATA:
		memcpy(st->sqlstate, "225FF", 5);
		set_sqlerrm(st, "Invalid column data");
		break;

	case DBERR_CURSOR_CLOSED:
		memcpy(st->sqlstate, "24501", 5);
		set_sqlerrm(st, "Cursor is closed");
		break;

	case DBERR_MOVE_TO_FIRST_FAILED:
		memcpy(st->sqlstate, "020F0", 5);
		set_sqlerrm(st, "Move to first row failed");
		break;

	case DBERR_FIELD_COUNT_MISMATCH:
		memcpy(st->sqlstate, "42886", 5);
		set_sqlerrm(st, "Field count mismatch");
		break;

	case DBERR_NO_DATA:
		memcpy(st->sqlstate, "02000", 5);
		set_sqlerrm(st, "No data");
		break;

	case DBERR_TOO_MUCH_DATA:
		memcpy(st->sqlstate, "22537", 5);
		set_sqlerrm(st, "Too much data");
		break;

	case DBERR_CONN_INIT_ERROR:
		memcpy(st->sqlstate, "IM002", 5);
		set_sqlerrm(st, "Invalid datasource definition");
		break;

	case DBERR_CONN_INVALID_DBTYPE:
		memcpy(st->sqlstate, "IM003", 5);
		set_sqlerrm(st, "Invalid DB type/driver requested");
		break;

	case DBERR_NUM_OUT_OF_RANGE:
		memcpy(st->sqlstate, "22003", 5);
		set_sqlerrm(st, "Numeric value is out of range");
		break;

	default:
		memcpy(st->sqlstate, "HV000", 5);
		set_sqlerrm(st, "General GixSQL error");
	}

	if (dbi) {
		const char* m = dbi->get_error_message();
		if (m != NULL && strlen(m) > 0) {
			set_sqlerrm(st, m);
		}

		st->sqlcode = dbi->get_error_code();
		if (st->sqlcode == 0 || is_data_error(err))
			st->sqlcode = err;

		// if the driver provides an sqlstate we use it instead of the generic one above
		std::string sqlstate = dbi->get_state();
		if (!sqlstate.empty() && sqlstate != "00000") {
			if (sqlstate.size() > 5)
				sqlstate = sqlstate.substr(0, 5);

			memset(st->sqlstate, '0', 5);
			memcpy(st->sqlstate, sqlstate.c_str(), sqlstate.size());
		}
		else {	// SQLSTATE might be empty due to the "&&" in the "if" above, we check for the specific set of conditions
			if (err != DBERR_NO_ERROR && sqlstate == "00000") {
				spdlog::warn("internally set sqlstate {} while driver sets 00000", st->sqlstate);
			}
		}
	}
	else {
		st->sqlcode = err;
# if 0 /* currently we always set sqlerrm in the switch above */
		char bfr[128];
		if (st->sqlerrm.sqlerrmc[0] == ' ') {
			sprintf(bfr, "%d : %s", err, err != 0 ? "Generic GIXSQL error" : "No error");
			set_sqlerrm(st, bfr);
		}
#endif
	}

	if (err == DBERR_NO_DATA)
		st->sqlcode = __global_env->norec_sqlcode();

	return RESULT_SUCCESS;
}

static AutoCommitMode get_autocommit(const std::shared_ptr<DataSourceInfo>& ds)
{
	std::map<std::string, std::string> options = ds->getOptions();
	if (options.find("autocommit") != options.end()) {
		std::string o = to_lower(options["autocommit"]);
		if (o == "on" || o == "1")
			return AutoCommitMode::On;
		else
			if (o == "off" || o == "0")
				return AutoCommitMode::Off;
			else
				if (o == "native")
					return AutoCommitMode::Native;
	}

	char* v = getenv("GIXSQL_AUTOCOMMIT");
	if (v) {
		std::string o = to_lower(v);
		if (o == "on" || o == "1")
			return AutoCommitMode::On;
		else
			if (o == "off" || o == "0")
				return AutoCommitMode::Off;
			else
				if (o == "native")
					return AutoCommitMode::Native;
	}

	return GIXSQL_AUTOCOMMIT_DEFAULT;
}


static bool get_fixup_params(const std::shared_ptr<DataSourceInfo>& ds)
{
	std::map<std::string, std::string> options = ds->getOptions();
	if (options.find("fixup_params") != options.end()) {
		std::string o = options["fixup_params"];
		return (o == "on" || o == "1") ? GIXSQL_FIXUP_PARAMS_ON : GIXSQL_FIXUP_PARAMS_OFF;
	}

	char* v = getenv("GIXSQL_FIXUP_PARAMS");
	if (v) {
		if (strcmp(v, "1") == 0 || strcasecmp(v, "ON") == 0)
			return GIXSQL_FIXUP_PARAMS_ON;

		if (strcmp(v, "0") == 0 || strcasecmp(v, "OFF") == 0)
			return GIXSQL_FIXUP_PARAMS_OFF;
	}

	return GIXSQL_FIXUP_PARAMS_DEFAULT;
}


static std::string get_client_encoding(const std::shared_ptr<DataSourceInfo>& ds)
{
	std::map<std::string, std::string> options = ds->getOptions();
	if (options.find("client_encoding") != options.end()) {
		return options["client_encoding"];
	}

	char* v = getenv("GIXSQL_CLIENT_ENCODING");
	if (v) {
		return std::string(v);
	}

	return GIXSQL_CLIENT_ENCODING_DEFAULT;
}

std::string get_hostref_or_literal(void* data, int l)
{
	if (!data)
		return std::string();

	if (!l)
		return std::string((char*)data);

	if (l > 0) {
		std::string s = std::string((char*)data, l);
		return s;
	}

	// variable-length fields (negative length)
	void* actual_data = (char*)data + __global_env->varlen_length_sz();
	//VARLEN_LENGTH_T* len_addr = (VARLEN_LENGTH_T*)data;
	//int actual_len = (*len_addr);
	int actual_len = __global_env->varlen_length_sz_short() ? (*((uint16_t*)data)) : (*((uint32_t*)data));

	// Should we check the actual length against the defined length?
	//...

	std::string t = std::string((char*)actual_data, (-l) - __global_env->varlen_length_sz());
	return t;
}

static std::string get_debug_log_file() {
	char* c = getenv("GIXSQL_LOG_FILE");
	if (c) {
		return c;
	}
	return DEFAULT_GIXSQL_LOG_FILE;
}

static spdlog::level::level_enum get_log_level() {
	char* c = getenv("GIXSQL_LOG_LEVEL");
	if (!c) {
		return DEFAULT_GIXSQL_LOG_LEVEL;
	}

	std::string s = c;
	if (s == "trace") {
		return spdlog::level::trace;
	}
	else
		if (s == "debug") {
			return spdlog::level::debug;
		}
		else
			if (s == "info") {
				return spdlog::level::info;
			}
			else
				if (s == "warn") {
					return spdlog::level::warn;
				}
				else
					if (s == "error") {
						return spdlog::level::err;
					}
					else
						if (s == "critical") {
							return spdlog::level::critical;
						}
						else
							if (s == "off") {
								return spdlog::level::off;
							}
							else
								return DEFAULT_GIXSQL_LOG_LEVEL;
}

bool get_log_truncation_flag() {
	char* c = getenv("GIXSQL_LOG_TRUNCATE");
	if (!c) {
		return DEFAULT_GIXSQL_LOG_TRUNC;
	}

	std::string s = to_lower(c);
	if (s == "on" || s == "1") {
		return true;
	}
	else
		if (s == "off" || s == "0") {
			return false;
		}
		else
			return DEFAULT_GIXSQL_LOG_TRUNC;
}

bool get_log_rotation_flag() {
	char* c = getenv("GIXSQL_LOG_ROTATE");
	if (!c) {
		return DEFAULT_GIXSQL_LOG_ROTATE;
	}

	std::string s = to_lower(c);
	if (s == "on" || s == "1") {
		return true;
	}
	else
		if (s == "off" || s == "0") {
			return false;
		}
		else
			return DEFAULT_GIXSQL_LOG_ROTATE;
}

void get_log_rotation_parameters(unsigned long* max_size, int* max_files, bool *rotate_on_open)
{
	char* c = getenv("GIXSQL_LOG_ROTATE_MAX_SIZE");
	if (!c) {
		*max_size = DEFAULT_GIXSQL_LOG_ROTATE_MAX_SIZE;
	}
	else {
		char suffix = DEFAULT_GIXSQL_LOG_SIZE_SUFFIX;
		if (strlen(c) > 1 && (c[strlen(c) - 1] == 'B' || c[strlen(c) - 1] == 'K' || c[strlen(c) - 1] == 'M' || c[strlen(c) - 1] == 'G')) {
			suffix = c[strlen(c) - 1];
			c[strlen(c) - 1] = '\0';
		}

		int multiplier = 1024 * 1024;
		switch (suffix)
		{
			case 'B':
				multiplier = 1;
				break;

			case 'K':
				multiplier = 1024;
				break;

			case 'M':
				multiplier = 1024 * 1024;
				break;

			case 'G':
				multiplier = 1024 * 1024 * 1024;
				break;
		}

		*max_size = atoi(c) ? atoi(c) * multiplier : DEFAULT_GIXSQL_LOG_ROTATE_MAX_SIZE;
	}

	// ***

	c = getenv("GIXSQL_LOG_ROTATE_MAX_FILES");
	if (!c) {
		*max_files = DEFAULT_GIXSQL_LOG_ROTATE_MAX_FILES;
	}
	else {
		*max_files = atoi(c) ? atoi(c) : DEFAULT_GIXSQL_LOG_ROTATE_MAX_FILES;
	}

	// ***

	c = getenv("GIXSQL_LOG_ROTATE_ON_OPEN");
	if (!c) {
		*rotate_on_open = DEFAULT_GIXSQL_LOG_ROTATE_ON_OPEN;
	}

	std::string s = to_lower(c);
	if (s == "on" || s == "1") {
		*rotate_on_open = true;
	}
	else
		if (s == "off" || s == "0") {
			*rotate_on_open = false;
		}
		else
			*rotate_on_open = DEFAULT_GIXSQL_LOG_ROTATE_ON_OPEN;

}

static bool is_signed_numeric(CobolVarType t)
{
	return t == CobolVarType::COBOL_TYPE_SIGNED_NUMBER_TS ||
		t == CobolVarType::COBOL_TYPE_SIGNED_NUMBER_TC ||
		t == CobolVarType::COBOL_TYPE_SIGNED_NUMBER_LS ||
		t == CobolVarType::COBOL_TYPE_SIGNED_NUMBER_LC ||
		t == CobolVarType::COBOL_TYPE_SIGNED_BINARY ||
		t == CobolVarType::COBOL_TYPE_SIGNED_NUMBER_PD;
}

static bool gixsql_initialize()
{
	int pid = getpid();

	spdlog::sink_ptr gixsql_std_sink;

	bool truncate_log = get_log_truncation_flag();
	bool rotate_log = get_log_rotation_flag();

	spdlog::level::level_enum level = get_log_level();
	if (level == spdlog::level::off) {
		gixsql_std_sink = std::make_shared<spdlog::sinks::null_sink_st>();
	}
	else {
		std::string filename = get_debug_log_file();
		if (filename.find("$$") != std::string::npos) {
			filename = string_replace(filename, "$$", std::to_string(pid));
		}

		if (!rotate_log) {
			gixsql_std_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(filename, truncate_log);
		}
		else
		{
			unsigned long rotate_max_size;
			int rotate_max_files;
			bool rotate_on_open;	// Not available on spdlog < 1.4.0
			get_log_rotation_parameters(&rotate_max_size, &rotate_max_files, &rotate_on_open);

#if SPDLOG_VERSION >= 10400
			gixsql_std_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(filename, rotate_max_size, rotate_max_files, rotate_on_open);
#else
			gixsql_std_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(filename, rotate_max_size, rotate_max_files);
#endif
		}
	}

	gixsql_logger = std::shared_ptr<spdlog::logger>(new spdlog::logger("libgixsql", gixsql_std_sink), [](spdlog::logger* p) {
		if (p != nullptr) {
			p->info("Terminating logger");
			p->flush();
			delete p;
		}
		});

#ifdef _DEBUG
	gixsql_logger->flush_on(spdlog::level::trace);
#endif
	spdlog::set_default_logger(gixsql_logger);
	spdlog::set_level(level);
	spdlog::info("GixSQL logger started (PID: {})", pid);

	__global_env = new GlobalEnv();

	__lib_initialized = true;

	return true;
}

#if 1
void gixsql_shutdown()
{
	//connection_manager.clear();
	//cursor_manager.clear();
	//DbInterfaceFactory::clear();
}
#endif
