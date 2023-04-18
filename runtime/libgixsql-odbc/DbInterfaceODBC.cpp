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

#include "DbInterfaceODBC.h"
#include "SqlVar.h"
#include "IConnection.h"
#include "Logger.h"
#include "utils.h"
#include "varlen_defs.h"
#include "cobol_var_flags.h"

#include <cstring>


SQLHANDLE DbInterfaceODBC::odbc_global_env_context = nullptr;
int DbInterfaceODBC::odbc_global_env_context_usage_count = 0;

static std::string __get_trimmed_hostref_or_literal(void* data, int l);
static std::string odbc_fixup_parameters(const std::string& sql);

DbInterfaceODBC::DbInterfaceODBC()
{
}


DbInterfaceODBC::~DbInterfaceODBC()
{
	// TODO: use shared_ptr with deleter
	odbc_global_env_context_usage_count--;
	if (odbc_global_env_context && odbc_global_env_context_usage_count == 0) {
		SQLFreeHandle(SQL_HANDLE_ENV, odbc_global_env_context);
		odbc_global_env_context = nullptr;
	}

	if (conn_handle)
		SQLFreeHandle(SQL_HANDLE_DBC, conn_handle);
}



int DbInterfaceODBC::init(const std::shared_ptr<spdlog::logger>& _logger)
{
	conn_handle = NULL;

	auto lib_sink = _logger->sinks().at(0);
	lib_logger = std::make_shared<spdlog::logger>("libgixsql-odbc", lib_sink);
	lib_logger->set_level(_logger->level());
	lib_logger->info("libgixsql-odbc logger started");

	if (!odbc_global_env_context) {
		SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &odbc_global_env_context);
		if (rc != SQL_SUCCESS) {
			lib_logger->debug(FMT_FILE_FUNC "FATAL ERROR: Can't allocate SQL Handle for the ODBC environment", __FILE__, __func__);
			lib_logger->error("FATAL ERROR: Can't allocate SQL Handle for the ODBC environment");
			odbc_global_env_context = NULL;
			return DBERR_OUT_OF_MEMORY;
		}

		// set ODBC3 version but ignore the error
		rc = SQLSetEnvAttr(odbc_global_env_context, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
		if (last_rc != SQL_SUCCESS) {
			lib_logger->debug(FMT_FILE_FUNC  "WARNING: Cannot set ODBC version", __FILE__, __func__);
			lib_logger->error("WARNING: Cannot set ODBC version");
		}

		// set NTS if possible to avoid extra moves
		rc = SQLSetEnvAttr(odbc_global_env_context, SQL_ATTR_OUTPUT_NTS, (SQLPOINTER)SQL_FALSE, 0);
		if (last_rc != SQL_SUCCESS) {
			lib_logger->debug(FMT_FILE_FUNC  "WARNING: Cannot set NTS", __FILE__, __func__);
			lib_logger->error("WARNING: Cannot set ODBC NTS");
		}
	}

	odbc_global_env_context_usage_count++;

	int rc = SQLAllocHandle(SQL_HANDLE_DBC, odbc_global_env_context, &conn_handle);
	if (odbcRetrieveError(rc, ErrorSource::Environmennt) != SQL_SUCCESS) {
		lib_logger->debug(FMT_FILE_FUNC  "FATAL ERROR: Can't allocate SQL Handle for the ODBC connection", __FILE__, __func__);
		lib_logger->error("FATAL ERROR: Can't allocate SQL Handle for the ODBC connection");
		conn_handle = NULL;
		return DBERR_OUT_OF_MEMORY;
	}

	return DBERR_NO_ERROR;
}

int DbInterfaceODBC::connect(std::shared_ptr<IDataSourceInfo> _conn_info, std::shared_ptr<IConnectionOptions> _conn_opts)
{
	int rc = 0;
	char dbms_name[256];
	std::string host = _conn_info->getHost();
	std::string user = _conn_info->getUsername();
	std::string pwd = _conn_info->getPassword();

	lib_logger->trace(FMT_FILE_FUNC  "ODBC: DB connect to DSN '{}' user = '{}'", __FILE__, __func__, host, user);

	// Connect
	if (!user.empty()) {
		rc = SQLConnect(conn_handle, (SQLCHAR*)host.c_str(), SQL_NTS, (SQLCHAR*)user.c_str(), SQL_NTS, (SQLCHAR*)pwd.c_str(), SQL_NTS);
	}
	else {
		rc = SQLConnect(conn_handle, (SQLCHAR*)host.c_str(), SQL_NTS, 0, 0, 0, 0);
	}

	if (odbcRetrieveError(rc, ErrorSource::Connection) != SQL_SUCCESS) {
		return DBERR_CONNECTION_FAILED;
	}

	if (_conn_opts->autocommit != AutoCommitMode::Native) {
		lib_logger->trace(FMT_FILE_FUNC "ODBC::setting autocommit to {}", __FILE__, __func__, (_conn_opts->autocommit == AutoCommitMode::On) ? "ON" : "OFF");
		auto autocommit_state = (_conn_opts->autocommit == AutoCommitMode::On) ? SQL_AUTOCOMMIT_ON : SQL_AUTOCOMMIT_OFF;
		rc = SQLSetConnectAttr(conn_handle, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)autocommit_state, 0);
		if (odbcRetrieveError(rc, ErrorSource::Connection) != SQL_SUCCESS) {
			lib_logger->error(FMT_FILE_FUNC  "ODBC: Can't set autocommit. Error = {} ({}): {}", __FILE__, __func__, last_rc, last_state, last_error);
			return DBERR_CONNECTION_FAILED;
		}
	}
	else {
		lib_logger->trace(FMT_FILE_FUNC "ODBC::setting autocommit to native (nothing to do)", __FILE__, __func__);
	}

	int rc_warning_only = SQLGetInfo(conn_handle, SQL_DBMS_NAME, (SQLPOINTER)dbms_name, sizeof(dbms_name), NULL);
	if (rc_warning_only != SQL_SUCCESS) {
		lib_logger->debug(FMT_FILE_FUNC  "WARNING: Cannot retrieve DBMS name", __FILE__, __func__);
		lib_logger->error("WARNING: Cannot retrieve DBMS name");
	}
	else {
		lib_logger->debug(FMT_FILE_FUNC "DBMS name is [{}]", __FILE__, __func__, dbms_name);
	}

	lib_logger->debug(FMT_FILE_FUNC  "ODBC: Connection registration successful", __FILE__, __func__);

	odbcClearError();

	this->connection_opts = _conn_opts;
	this->data_source_info = _conn_info;

	return DBERR_NO_ERROR;
}

int DbInterfaceODBC::reset()
{
	lib_logger->trace(FMT_FILE_FUNC  "ODBC: connection reset invoked", __FILE__, __func__);

	int rc = terminate_connection();
	if (rc == DBERR_NO_ERROR)
		return DBERR_NO_ERROR;
	else
		return DBERR_CONN_RESET_FAILED;
}

int DbInterfaceODBC::terminate_connection()
{
	// we need to dispose these before closing the connection, 
	// or we will get a memory leak from the ODBC driver/subsystem
	// if later on we attempt to free the handle in ODBCStatementData
	if (current_statement_data) {
		current_statement_data.reset();
	}

	// same as above
	_prepared_stmts.clear();
	_declared_cursors.clear();

	lib_logger->trace(FMT_FILE_FUNC "ODBC: connection termination invoked", __FILE__, __func__);

	if (conn_handle) {
		int rc = SQLDisconnect(conn_handle);
		conn_handle = NULL;

		if (odbcRetrieveError(rc, ErrorSource::Connection) != SQL_SUCCESS)
			return DBERR_DISCONNECT_FAILED;
	}
	return DBERR_NO_ERROR;
}

const char* DbInterfaceODBC::get_error_message()
{
	return (char*)last_error.c_str();
}

int DbInterfaceODBC::get_error_code()
{
	return last_rc;
}

std::string DbInterfaceODBC::get_state()
{
	return last_state;
}

int DbInterfaceODBC::prepare(const std::string& _stmt_name, const std::string& query)
{
	std::string prepared_sql;
	std::shared_ptr<ODBCStatementData> res = std::make_shared<ODBCStatementData>(conn_handle);

	std::string stmt_name = to_lower(_stmt_name);

	lib_logger->trace(FMT_FILE_FUNC "ODPI::prepare ({}) - SQL: {}", __FILE__, __func__, stmt_name, query);

	if (this->_prepared_stmts.find(stmt_name) != _prepared_stmts.end()) {
		return DBERR_PREPARE_FAILED;
	}

	if (connection_opts->fixup_parameters) {
		prepared_sql = odbc_fixup_parameters(query);
		lib_logger->trace(FMT_FILE_FUNC "ODPI::fixup parameters is on", __FILE__, __func__);
		lib_logger->trace(FMT_FILE_FUNC "ODPI::prepare ({}) - SQL(P): {}", __FILE__, __func__, stmt_name, prepared_sql);
	}
	else {
		prepared_sql = query;
	}

	lib_logger->trace(FMT_FILE_FUNC "ODPI::prepare ({}) - SQL(P): {}", __FILE__, __func__, stmt_name, prepared_sql);

	int rc = SQLPrepare(res->statement, (SQLCHAR*)prepared_sql.c_str(), SQL_NTS);
	if (odbcRetrieveError(rc, ErrorSource::Statement, res->statement) < 0) {
		lib_logger->error(FMT_FILE_FUNC "ODPI::prepare ({} - res: ({}) {}", __FILE__, __func__, stmt_name, last_rc, last_error);
		return DBERR_PREPARE_FAILED;
	}

	odbcClearError();

	lib_logger->trace(FMT_FILE_FUNC "ODPI::prepare ({} - res: ({}) {}", __FILE__, __func__, stmt_name, last_rc, last_error);

	_prepared_stmts[stmt_name] = res;

	return DBERR_NO_ERROR;
}

int DbInterfaceODBC::exec_prepared(const std::string& _stmt_name, std::vector<CobolVarType> paramTypes, std::vector<std_binary_data>& paramValues, std::vector<unsigned long> paramLengths, const std::vector<uint32_t>& paramFlags)
{
	int rc = 0;
	SQLLEN null_data = SQL_NULL_DATA;

	lib_logger->trace(FMT_FILE_FUNC "statement name: {}", __FILE__, __func__, _stmt_name);

	if (paramTypes.size() != paramValues.size() || paramTypes.size() != paramFlags.size()) {
		lib_logger->error(FMT_FILE_FUNC "Internal error: parameter count mismatch", __FILE__, __func__);
		last_error = "Internal error: parameter count mismatch";
		last_rc = DBERR_INTERNAL_ERR;
		return DBERR_INTERNAL_ERR;
	}

	std::string stmt_name = to_lower(_stmt_name);

	if (_prepared_stmts.find(stmt_name) == _prepared_stmts.end()) {
		lib_logger->error("Statement not found: {}", stmt_name);
		return DBERR_SQL_ERROR;
	}

	int nParams = (int)paramValues.size();

	std::shared_ptr<ODBCStatementData> wk_rs = _prepared_stmts[stmt_name];
	wk_rs->resizeParams(nParams);

	std::vector<SQLLEN> lengths(paramLengths.size());

	for (int i = 0; i < nParams; i++) {
		int sql_type = cobol2odbctype(paramTypes[i], paramFlags[i]);
		int c_type = CBL_FIELD_IS_BINARY(paramFlags[i]) ? SQL_C_BINARY : SQL_C_CHAR;

		lengths[i] = paramLengths[i];
		
		if (paramLengths.at(i) != DB_NULL) {
			rc = SQLBindParameter(
				wk_rs->statement,
				i + 1,
				SQL_PARAM_INPUT,
				c_type,
				sql_type,
				10,
				0,
				(SQLPOINTER)paramValues.at(i).data(),
				(SQLLEN)paramLengths.at(i),
				&lengths[i]);
		}
		else
		{
			rc = SQLBindParameter(
				wk_rs->statement,
				i + 1,
				SQL_PARAM_INPUT,
				c_type,
				sql_type,
				10,
				0,
				nullptr,
				(SQLLEN)paramLengths.at(i),
				&null_data);
		}

		if (odbcRetrieveError(rc, ErrorSource::Statement, wk_rs->statement) != SQL_SUCCESS) {
			last_rc = rc;
			lib_logger->error("ODBC: Error while binding parameter {} in prepared statement ({}): {}", i + 1, last_rc, stmt_name);
			return DBERR_SQL_ERROR;
		}
	}

	rc = SQLExecute(wk_rs->statement);
	if (odbcRetrieveError(rc, ErrorSource::Statement, wk_rs->statement) != SQL_SUCCESS) {
		return DBERR_SQL_ERROR;
	}

	return DBERR_NO_ERROR;
}

DbPropertySetResult DbInterfaceODBC::set_property(DbProperty p, std::variant<bool, int, std::string> v)
{
	return DbPropertySetResult::Unsupported;
}


int DbInterfaceODBC::exec(std::string _query)
{
	return _odbc_exec(nullptr, _query);
}


int DbInterfaceODBC::_odbc_exec(std::shared_ptr<ICursor> crsr, const std::string& query, std::shared_ptr<ODBCStatementData> prep_stmt_data)
{
	int rc = 0;
	uint32_t nquery_cols = 0;
	std::string q = query;
	lib_logger->trace(FMT_FILE_FUNC "SQL: #{}#", __FILE__, __func__, q);

	std::shared_ptr<ODBCStatementData> wk_rs;

	if (!prep_stmt_data) {
		wk_rs = (crsr != nullptr) ? std::static_pointer_cast<ODBCStatementData>(crsr->getPrivateData()) : current_statement_data;

		if (wk_rs && wk_rs == current_statement_data) {
			current_statement_data.reset();
		}

		wk_rs = std::make_shared<ODBCStatementData>(conn_handle);
		if (!wk_rs->statement) {
			odbcRetrieveError(-1, ErrorSource::Connection, conn_handle);
			return DBERR_SQL_ERROR;
		}

		rc = SQLPrepare(wk_rs->statement, (SQLCHAR*)query.c_str(), SQL_NTS);
		if (odbcRetrieveError(rc, ErrorSource::Statement, wk_rs->statement) != SQL_SUCCESS) {
			return DBERR_SQL_ERROR;
		}
	}
	else {
		wk_rs = prep_stmt_data;	// Already prepared
	}

	rc = SQLExecute(wk_rs->statement);
	if (odbcRetrieveError(rc, ErrorSource::Statement, wk_rs->statement) != SQL_SUCCESS) {
		return DBERR_SQL_ERROR;
	}

	if (!prep_stmt_data) {
		q = trim_copy(q);
		if (starts_with(q, "delete ") || starts_with(q, "DELETE ") || starts_with(q, "update ") || starts_with(q, "UPDATE ")) {
			int nrows = get_affected_rows(wk_rs);
			if (nrows <= 0) {
				last_rc = NO_REC_CODE_DEFAULT;
				lib_logger->error("ODBC: cannot retrieve the number of affected rows. Reason: ({}): {}", last_rc, last_error);
				return DBERR_SQL_ERROR;
			}
		}
	}

	if (last_rc == SQL_SUCCESS) {
		if (crsr) {
			crsr->setPrivateData(wk_rs);
		}
		else
			current_statement_data = wk_rs;

		return DBERR_NO_ERROR;
	}
	else {
		last_rc = -(10000 + last_rc);
		return DBERR_SQL_ERROR;
	}

}

int DbInterfaceODBC::exec_params(const std::string& query, const std::vector<CobolVarType>& paramTypes, const std::vector<std_binary_data>& paramValues, const std::vector<unsigned long>& paramLengths, const std::vector<uint32_t>& paramFlags)
{
	return _odbc_exec_params(nullptr, query, paramTypes, paramValues, paramLengths, paramFlags);
}

int DbInterfaceODBC::_odbc_exec_params(std::shared_ptr<ICursor> crsr, const std::string& query, const std::vector<CobolVarType>& paramTypes, const std::vector<std_binary_data>& paramValues, const std::vector<unsigned long>& paramLengths, const std::vector<uint32_t>& paramFlags, std::shared_ptr<ODBCStatementData> prep_stmt_data)
{
	std::string q = query;
	int rc = 0;
	SQLLEN null_data = SQL_NULL_DATA;

	lib_logger->trace(FMT_FILE_FUNC "SQL: #{}#", __FILE__, __func__, q);

	std::shared_ptr<ODBCStatementData> wk_rs;

	if (paramTypes.size() != paramValues.size() || paramTypes.size() != paramFlags.size()) {
		lib_logger->error(FMT_FILE_FUNC "Internal error: parameter count mismatch", __FILE__, __func__);
		last_error = "Internal error: parameter count mismatch";
		last_rc = DBERR_INTERNAL_ERR;
		return DBERR_INTERNAL_ERR;
	}

	if (!prep_stmt_data) {
		wk_rs = (crsr != nullptr) ? std::static_pointer_cast<ODBCStatementData>(crsr->getPrivateData()) : current_statement_data;

		if (wk_rs && wk_rs == current_statement_data) {
			current_statement_data.reset();
		}

		wk_rs = std::make_shared<ODBCStatementData>(conn_handle);
		if (!wk_rs->statement) {
			return DBERR_SQL_ERROR;
		}

		rc = SQLPrepare(wk_rs->statement, (SQLCHAR*)query.c_str(), SQL_NTS);
		if (odbcRetrieveError(rc, ErrorSource::Statement, wk_rs->statement) != SQL_SUCCESS) {
			return DBERR_SQL_ERROR;
		}
	}
	else {
		wk_rs = prep_stmt_data;	// Already prepared
	}

	int nParams = paramValues.size();

	wk_rs->resizeParams(nParams);

	std::vector<SQLLEN> lengths(paramLengths.size());


	for (int i = 0; i < nParams; i++) {
		int sql_type = cobol2odbctype(paramTypes[i], paramFlags[i]);
		int c_type = CBL_FIELD_IS_BINARY(paramFlags[i]) ? SQL_C_BINARY : SQL_C_CHAR;
		unsigned long plen = paramLengths.at(i);

		lengths[i] = paramLengths[i];

		if (paramLengths.at(i) != DB_NULL) {
			rc = SQLBindParameter(
				wk_rs->statement,
				i + 1,
				SQL_PARAM_INPUT,
				c_type,
				sql_type,
				10,
				0,
				(SQLPOINTER)paramValues.at(i).data(),
				(SQLLEN)plen,
				&lengths[i]);
		}
		else
		{
			
			rc = SQLBindParameter(
				wk_rs->statement,
				i + 1,
				SQL_PARAM_INPUT,
				c_type,
				sql_type,
				10,
				0,
				nullptr,
				0,
				&null_data);
		}

		if (odbcRetrieveError(rc, ErrorSource::Statement, wk_rs->statement) != SQL_SUCCESS) {
			//free(pvals);
			last_rc = rc;
			lib_logger->error("ODBC: Error while binding parameter {} in statement ({}): {} - {}", i + 1, last_rc, last_error, query);
			return DBERR_SQL_ERROR;
		}
	}

	rc = SQLExecute(wk_rs->statement);
	if (odbcRetrieveError(rc, ErrorSource::Statement, wk_rs->statement) != SQL_SUCCESS) {
		lib_logger->error("ODBC: Error while executing statement ({}): {}", last_rc, last_error);
		return DBERR_SQL_ERROR;
	}

	if (!prep_stmt_data) {
		q = trim_copy(q);
		if (starts_with(q, "delete ") || starts_with(q, "DELETE ") || starts_with(q, "update ") || starts_with(q, "UPDATE ")) {
			int nrows = get_affected_rows(wk_rs);
			if (nrows <= 0) {
				last_rc = NO_REC_CODE_DEFAULT;
				lib_logger->error("ODBC: cannot retrieve the number of affected rows. Reason: ({}): {}", last_rc, last_error);
				return DBERR_SQL_ERROR;
			}
		}
	}

	if (last_rc == SQL_SUCCESS) {
		if (crsr) {
			crsr->setPrivateData(wk_rs);
		}
		else
			current_statement_data = wk_rs;

		return DBERR_NO_ERROR;
	}
	else {
		last_rc = -(10000 + last_rc);
		return DBERR_SQL_ERROR;
	}
}

bool DbInterfaceODBC::is_cursor_from_prepared_statement(const std::shared_ptr<ICursor>& cursor)
{
	std::string squery = cursor->getQuery();
	void* src_addr = nullptr;
	int src_len = 0;

	if (squery.size() == 0) {
		cursor->getQuerySource(&src_addr, &src_len);
		squery = __get_trimmed_hostref_or_literal(src_addr, src_len);
	}

	trim(squery);
	squery = to_lower(squery);

	return squery.size() > 1 && starts_with(squery, "@") && _prepared_stmts.find(squery.substr(1)) != _prepared_stmts.end();
}


int DbInterfaceODBC::cursor_close(const std::shared_ptr<ICursor>& cursor)
{
	if (!cursor) {
		lib_logger->error("Invalid cursor reference");
		return DBERR_CLOSE_CURSOR_FAILED;
	}

	// Prepared statements used for cursors will be disposed separately
	if (!is_cursor_from_prepared_statement(cursor)) {
		std::shared_ptr<ODBCStatementData> dp = std::dynamic_pointer_cast<ODBCStatementData>(cursor->getPrivateData());

		if (!dp || !dp->statement)
			return DBERR_CLOSE_CURSOR_FAILED;

		SQLHANDLE cursor_handle = dp->statement;

		int rc = SQLCloseCursor(cursor_handle);
		dp->statement = nullptr;
		if (odbcRetrieveError(rc, ErrorSource::Statement, cursor_handle) != SQL_SUCCESS) {
			lib_logger->error("ODBC: Error while closing cursor ({}) {}", last_rc, cursor->getName());
			return DBERR_CLOSE_CURSOR_FAILED;
		}

		cursor->setPrivateData(nullptr);

		if (rc != SQL_SUCCESS) {
			lib_logger->error("ODBC: Error while closing cursor ({}) {}", last_rc, cursor->getName());
			return DBERR_CLOSE_CURSOR_FAILED;
		}

	}
	else {
		cursor->setPrivateData(nullptr);
	}

	return DBERR_NO_ERROR;
}

int DbInterfaceODBC::cursor_declare(const std::shared_ptr<ICursor>& cursor)
{
	lib_logger->trace(FMT_FILE_FUNC "ODBC: cursor declare invoked", __FILE__, __func__);

	if (!cursor)
		return DBERR_DECLARE_CURSOR_FAILED;

	std::shared_ptr<ODBCStatementData> wk_rs = std::make_shared<ODBCStatementData>(conn_handle);
	if (!wk_rs->statement) {
		return DBERR_DECLARE_CURSOR_FAILED;
	}

	int rc = SQLSetCursorName(wk_rs->statement, (SQLCHAR*)cursor->getName().c_str(), SQL_NTS);
	lib_logger->debug(FMT_FILE_FUNC "ODBC: setting cursor name: [{}]", __FILE__, __func__, cursor->getName());
	if (odbcRetrieveError(rc, ErrorSource::Statement, wk_rs->statement) != SQL_SUCCESS) {
		lib_logger->error("ODBC: Error while setting cursor name ({}) {}", last_rc, cursor->getName());
		return DBERR_DECLARE_CURSOR_FAILED;
	}

	cursor->setPrivateData(wk_rs);

	std::map<std::string, std::shared_ptr<ICursor>>::iterator it = _declared_cursors.find(cursor->getName());
	if (it == _declared_cursors.end()) {
		_declared_cursors[cursor->getName()] = cursor;
	}

	return DBERR_NO_ERROR;
}

int DbInterfaceODBC::cursor_open(const std::shared_ptr<ICursor>& cursor)
{
	int rc = 0;

	if (!cursor)
		return DBERR_OPEN_CURSOR_FAILED;

	std::string sname = cursor->getName();
	std::string full_query;
	std::vector<int> empty;

	std::string squery = cursor->getQuery();
	void* src_addr = nullptr;
	int src_len = 0;

	if (squery.size() == 0) {
		cursor->getQuerySource(&src_addr, &src_len);
		squery = __get_trimmed_hostref_or_literal(src_addr, src_len);
	}

	std::shared_ptr<ODBCStatementData> prepared_stmt_data;
	if (starts_with(squery, "@")) {
		prepared_stmt_data = retrieve_prepared_statement(squery.substr(1));
		if (!prepared_stmt_data) {
			// last_error, etc. set by retrieve_prepared_statement_source
			return DBERR_OPEN_CURSOR_FAILED;
		}
	}

	if (squery.empty()) {
		last_rc = -1;
		last_error = "Empty query";
		return DBERR_OPEN_CURSOR_FAILED;
	}

	if (cursor->getNumParams() > 0) {
		std::vector<std_binary_data> param_values = cursor->getParameterValues();
		std::vector<CobolVarType> param_types = cursor->getParameterTypes();
		std::vector<unsigned long> param_lengths = cursor->getParameterLengths();
		std::vector<uint32_t> param_flags = cursor->getParameterFlags();
		rc = _odbc_exec_params(cursor, squery, param_types, param_values, param_lengths, param_flags, prepared_stmt_data);
	}
	else {
		rc = _odbc_exec(cursor, squery, prepared_stmt_data);
	}

	if (rc == SQL_SUCCESS) {
		return DBERR_NO_ERROR;
	}
	else {
		return DBERR_OPEN_CURSOR_FAILED;
	}
}

int DbInterfaceODBC::cursor_fetch_one(const std::shared_ptr<ICursor>& cursor, int)
{
	lib_logger->trace(FMT_FILE_FUNC "mode: {}", __FILE__, __func__, FETCH_NEXT_ROW);

	if (!cursor) {
		lib_logger->error("Invalid cursor reference");
		return DBERR_FETCH_ROW_FAILED;
	}

	lib_logger->trace(FMT_FILE_FUNC "owner id: {}, cursor name: {}, mode: {}", __FILE__, __func__, cursor->getConnectionName(), cursor->getName(), FETCH_NEXT_ROW);

	std::shared_ptr<ODBCStatementData> dp = std::dynamic_pointer_cast<ODBCStatementData>(cursor->getPrivateData());

	if (!dp || !dp->statement)
		return DBERR_FETCH_ROW_FAILED;

	int rc = SQLFetch(dp->statement);
	if (rc == SQL_NO_DATA)
		return DBERR_NO_DATA;

	if (odbcRetrieveError(rc, ErrorSource::Statement, dp->statement) != SQL_SUCCESS) {
		return DBERR_FETCH_ROW_FAILED;
	}

	return DBERR_NO_ERROR;
}

bool DbInterfaceODBC::get_resultset_value(ResultSetContextType resultset_context_type, const IResultSetContextData& context, int row, int col, char* bfr, uint64_t bfrlen, uint64_t* value_len, bool
                                          * is_db_null)
{
	int rc = 0;
	std::shared_ptr<ODBCStatementData> wk_rs;

	switch (resultset_context_type) {
	case ResultSetContextType::CurrentResultSet:
		wk_rs = current_statement_data;
		break;

	case ResultSetContextType::PreparedStatement:
	{
		PreparedStatementContextData& p = (PreparedStatementContextData&)context;

		std::string stmt_name = to_lower(p.prepared_statement_name);
		if (_prepared_stmts.find(stmt_name) == _prepared_stmts.end()) {
			lib_logger->error("Invalid prepared statement name: {}", stmt_name);
			return false;
		}

		wk_rs = _prepared_stmts[stmt_name];
	}
	break;

	case ResultSetContextType::Cursor:
	{
		CursorContextData& p = (CursorContextData&)context;
		std::shared_ptr <ICursor> c = p.cursor;
		if (!c) {
			lib_logger->error("Invalid cursor reference");
			return false;
		}
		wk_rs = std::dynamic_pointer_cast<ODBCStatementData>(c->getPrivateData());
	}
	break;
	}

	if (!wk_rs) {
		lib_logger->error("Invalid resultset");
		return false;
	}

	//SQL_C_BINARY

	SQLLEN reslen = 0;

	bool is_binary = false;
	if (!column_is_binary(wk_rs->statement, col + 1, &is_binary))
		return false;

	short target_type = is_binary ? SQL_C_BINARY : SQL_C_CHAR;

	rc = SQLGetData(wk_rs->statement, col + 1, target_type, bfr, (SQLLEN) bfrlen, &reslen);
	if (odbcRetrieveError(rc, ErrorSource::Statement, wk_rs->statement) != SQL_SUCCESS) {
		return false;
	}

	if (reslen == SQL_NULL_DATA) {
		*is_db_null = true;
		*value_len = 0;
		bfr[0] = 0;
		return true;
	}

	// TODO: fix this, with proper null handling
	if (reslen != SQL_NULL_DATA)
		*value_len = reslen;
	else {
		bfr[0] = '\0';
		*value_len = 0;
	}

#ifdef VERBOSE
	lib_logger->trace(FMT_FILE_FUNC "col: {}, data: {}", __FILE__, __func__, col, std::string(c, l));
	std::string s = fmt::format("col: {}, data: {}", col, std::string(c, l));
	fprintf(stderr, "%s\n", s.c_str());
#endif

	return true;
}

bool DbInterfaceODBC::move_to_first_record(const std::string& _stmt_name)
{
	std::shared_ptr<ODBCStatementData> dp;

	lib_logger->trace(FMT_FILE_FUNC "ODBC: moving to first row in resultset", __FILE__, __func__);

	std::string stmt_name = to_lower(_stmt_name);

	if (stmt_name.empty()) {
		if (!current_statement_data) {
			odbcSetError(DBERR_MOVE_TO_FIRST_FAILED, "HY000", "Invalid statement reference");
			return false;
		}

		dp = current_statement_data;
	}
	else {
		if (_prepared_stmts.find(stmt_name) == _prepared_stmts.end()) {
			odbcSetError(DBERR_MOVE_TO_FIRST_FAILED, "HY000", "Invalid statement reference");
			return false;
		}
		dp = _prepared_stmts[stmt_name];
	}

	if (!dp || !dp->statement) {
		odbcSetError(DBERR_MOVE_TO_FIRST_FAILED, "HY000", "Invalid statement reference");
		return false;
	}

	int rc = SQLFetch(dp->statement);
	if (rc == SQL_NO_DATA) {
		odbcSetError(DBERR_NO_DATA, "02000", "No data");
		return false;
	}

	if (odbcRetrieveError(rc, ErrorSource::Statement, dp->statement) != SQL_SUCCESS) {
		lib_logger->trace(FMT_FILE_FUNC "ODBC: Error while moving to first row in resultset", __FILE__, __func__);
		odbcSetError(DBERR_MOVE_TO_FIRST_FAILED, "HY000", "Invalid statement reference");
		return false;
	}

	return true;
}

uint64_t DbInterfaceODBC::get_native_features()
{
	return (uint64_t)0;
}


int DbInterfaceODBC::get_num_rows(const std::shared_ptr<ICursor>& crsr)
{
	return -1;
}

int DbInterfaceODBC::get_num_fields(const std::shared_ptr<ICursor>& crsr)
{
	SQLHANDLE wk_rs = nullptr;

	if (crsr) {
		std::shared_ptr<ODBCStatementData> p = std::dynamic_pointer_cast<ODBCStatementData>(crsr->getPrivateData());
		wk_rs = p->statement;
	}
	else {
		if (!current_statement_data)
			return -1;

		wk_rs = current_statement_data->statement;
	}

	if (wk_rs) {

		SQLSMALLINT NumCols = 0;
		int rc = SQLNumResultCols(wk_rs, &NumCols);
		if (odbcRetrieveError(rc, ErrorSource::Statement, wk_rs) != SQL_SUCCESS) {
			return SQL_ERROR;
		}
		return NumCols;
	}

	return -1;
}

SQLSMALLINT DbInterfaceODBC::cobol2odbctype(CobolVarType t, uint32_t flags)
{
	switch (t) {
	case CobolVarType::COBOL_TYPE_UNSIGNED_NUMBER:
	case CobolVarType::COBOL_TYPE_SIGNED_NUMBER_TC:
	case CobolVarType::COBOL_TYPE_SIGNED_NUMBER_TS:
	case CobolVarType::COBOL_TYPE_SIGNED_NUMBER_LC:
	case CobolVarType::COBOL_TYPE_SIGNED_NUMBER_LS:
	case CobolVarType::COBOL_TYPE_UNSIGNED_NUMBER_PD:
	case CobolVarType::COBOL_TYPE_SIGNED_NUMBER_PD:
	case CobolVarType::COBOL_TYPE_UNSIGNED_BINARY:
	case CobolVarType::COBOL_TYPE_SIGNED_BINARY:
		return SQL_NUMERIC;

	case CobolVarType::COBOL_TYPE_ALPHANUMERIC:
	case CobolVarType::COBOL_TYPE_JAPANESE:
		return CBL_FIELD_IS_BINARY(flags) ? SQL_BINARY : SQL_VARCHAR;

	default:
		return SQL_VARCHAR;
	}
}

SQLSMALLINT DbInterfaceODBC::cobol2ctype(CobolVarType t, uint32_t flags)
{
	switch (t) {
	case CobolVarType::COBOL_TYPE_UNSIGNED_NUMBER:
	case CobolVarType::COBOL_TYPE_SIGNED_NUMBER_TC:
	case CobolVarType::COBOL_TYPE_SIGNED_NUMBER_TS:
	case CobolVarType::COBOL_TYPE_SIGNED_NUMBER_LC:
	case CobolVarType::COBOL_TYPE_SIGNED_NUMBER_LS:
	case CobolVarType::COBOL_TYPE_UNSIGNED_NUMBER_PD:
	case CobolVarType::COBOL_TYPE_SIGNED_NUMBER_PD:
	case CobolVarType::COBOL_TYPE_UNSIGNED_BINARY:
	case CobolVarType::COBOL_TYPE_SIGNED_BINARY:
		return SQL_C_NUMERIC;

	case CobolVarType::COBOL_TYPE_ALPHANUMERIC:
	case CobolVarType::COBOL_TYPE_JAPANESE:
		return CBL_FIELD_IS_BINARY(flags) ? SQL_C_BINARY : SQL_C_CHAR;

	default:
		return SQL_C_CHAR;
	}
}

int DbInterfaceODBC::get_data_len(SQLHANDLE hStmt, int cnum)
{
	SQLRETURN	   rc;
	SQLCHAR		   ColumnName[256];
	SQLSMALLINT    ColumnNameLen;
	SQLSMALLINT    ColumnDataType;
	SQLULEN        ColumnDataSize;
	SQLSMALLINT    ColumnDataDigits;
	SQLSMALLINT    ColumnDataNullable;
	//SQLCHAR*	   ColumnData;
	//SQLLEN         ColumnDataLen;

	rc = SQLDescribeCol(
		hStmt,                    // Select Statement (Prepared)
		cnum + 1,                      // Columnn Number
		ColumnName,            // Column Name (returned)
		256,         // size of Column Name buffer
		&ColumnNameLen,        // Actual size of column name
		&ColumnDataType,       // SQL Data type of column
		&ColumnDataSize,       // Data size of column in table
		&ColumnDataDigits,     // Number of decimal digits
		&ColumnDataNullable);  // Whether column nullable

	return ColumnDataSize;
}

std::shared_ptr<ODBCStatementData> DbInterfaceODBC::retrieve_prepared_statement(const std::string& prep_stmt_name)
{
	std::string stmt_name = to_lower(prep_stmt_name);
	if (_prepared_stmts.find(stmt_name) == _prepared_stmts.end() || _prepared_stmts[stmt_name] == nullptr || _prepared_stmts[stmt_name]->statement == nullptr)
		return nullptr;

	return _prepared_stmts[stmt_name];
}

int DbInterfaceODBC::odbcRetrieveError(int rc, ErrorSource err_src, SQLHANDLE h)
{
	SQLHANDLE err_handle = nullptr;
	SQLSMALLINT handle_type = 0;
	std::vector<std::string> odbc_errors;
	SQLINTEGER i = 0;
	SQLINTEGER NativeError = 0;
	SQLCHAR SQLState[7];
	SQLCHAR MessageText[1024];
	char bfr[2000];
	SQLSMALLINT TextLength;
	SQLRETURN ret = 0;

	SQLINTEGER main_NativeError = 0;
	SQLCHAR main_SQLState[7];

	if (rc != SQL_SUCCESS) {

		// *************
		switch (err_src) {
		case ErrorSource::Environmennt:
			err_handle = this->odbc_global_env_context;
			handle_type = SQL_HANDLE_ENV;
			break;

		case ErrorSource::Connection:
			err_handle = conn_handle;
			handle_type = SQL_HANDLE_DBC;
			break;

		case ErrorSource::Statement:
			handle_type = SQL_HANDLE_STMT;
			if (h != 0)
				err_handle = h;
			else {
				if (current_statement_data && current_statement_data->statement)
					err_handle = current_statement_data->statement;
			}
			break;

		default:
			lib_logger->error("Invalid error source specified");
			return -1;
		}

		if (err_handle == nullptr || handle_type == 0) {
			last_rc = rc;
			last_error = "Unknown error";
			last_state = "HY000";
			return rc;
		}

		do {
			i++;
			ret = SQLGetDiagRec(handle_type, err_handle, i, SQLState, &NativeError,
				MessageText, sizeof(MessageText), &TextLength);

			if (SQL_SUCCEEDED(ret)) {
				sprintf(bfr, "%s (%d): %s", SQLState, NativeError, MessageText);
				odbc_errors.push_back(std::string(bfr));

#if _DEBUG
				lib_logger->trace("ODBC error code: {}", bfr);
#endif
			}

			if (i == 1) {
				main_NativeError = NativeError;
				strcpy((char*)&main_SQLState, (char*)&SQLState);
			}
		} while (ret == SQL_SUCCESS);

		last_error = "";

		for (std::vector< std::string>::const_iterator p = odbc_errors.begin();
			p != odbc_errors.end(); ++p) {
			last_error += *p;
			if (p != odbc_errors.end() - 1)
				last_error += ',';
		}

		if (rc == SQL_SUCCESS_WITH_INFO) {
			last_rc = abs(main_NativeError);
		}
		else {
			last_rc = main_NativeError > 0 ? -main_NativeError : main_NativeError;
		}

		last_state = (char*)&main_SQLState;
	}
	else {
		odbcClearError();
	}

	return rc;
}

void DbInterfaceODBC::odbcClearError()
{
	last_error = "";
	last_rc = DBERR_NO_ERROR;
	last_state = "00000";
}

void DbInterfaceODBC::odbcSetError(int err_code, std::string sqlstate, std::string err_msg)
{
	last_error = err_msg;
	last_rc = err_code;
	last_state = sqlstate;
}

int DbInterfaceODBC::get_affected_rows(std::shared_ptr<ODBCStatementData> d)
{
	if (!d || !d->statement)
		return DBERR_SQL_ERROR;

	lib_logger->trace(FMT_FILE_FUNC "ODBC: getting number of rows affected by last statement", __FILE__, __func__);

	SQLLEN NumRows = 0;
	int rc = SQLRowCount(d->statement, &NumRows);
	if (odbcRetrieveError(rc, ErrorSource::Statement, d->statement) != SQL_SUCCESS) {
		lib_logger->error("ODBC: Error while getting row count: {}", last_error);
		return DBERR_SQL_ERROR;
	}

	lib_logger->debug(FMT_FILE_FUNC  "ODBC: affected row count: {}", __FILE__, __func__, (int)NumRows);

	return (int)NumRows;
}

ODBCStatementData::ODBCStatementData(SQLHANDLE conn_handle)
{
	if (conn_handle) {
		int rc = SQLAllocHandle(SQL_HANDLE_STMT, conn_handle, &this->statement);
		if (rc != SQL_SUCCESS) {
			this->statement = nullptr;
		}
	}
}

ODBCStatementData::~ODBCStatementData()
{
	int rc = 0;
	if (this->statement) {
		rc = SQLFreeStmt(this->statement, SQL_CLOSE);
		rc = SQLFreeStmt(this->statement, SQL_UNBIND);  
   		rc = SQLFreeStmt(this->statement, SQL_RESET_PARAMS); 
		SQLFreeHandle(SQL_HANDLE_STMT, this->statement);
		this->statement = nullptr;	// Not actually needed, since we are in a destructor, but...
	}
}

void ODBCStatementData::resizeParams(int n)
{
}

void ODBCStatementData::resizeColumnData(int n)
{
}

bool DbInterfaceODBC::column_is_binary(SQLHANDLE stmt, int col_index, bool* is_binary)
{
	SQLLEN sql_type = 0;
	SQLRETURN rc = SQLColAttribute(
		stmt,
		col_index,
		SQL_DESC_TYPE,
		nullptr,
		0,
		nullptr,
		&sql_type);

	if (odbcRetrieveError(rc, ErrorSource::Statement, stmt) != SQL_SUCCESS)
		return false;

	*is_binary = (sql_type == SQL_BINARY || sql_type == SQL_VARBINARY || sql_type == SQL_LONGVARBINARY);
	return true;
}


static std::string __get_trimmed_hostref_or_literal(void* data, int l)
{
	if (!data)
		return std::string();

	if (!l)
		return std::string((char*)data);

	if (l > 0) {
		std::string s = std::string((char*)data, l);
		return trim_copy(s);
	}

	// variable-length fields (negative length)
	void* actual_data = (char*)data + VARLEN_LENGTH_SZ;
	VARLEN_LENGTH_T* len_addr = (VARLEN_LENGTH_T*)data;
	int actual_len = (*len_addr);

	// Should we check the actual length against the defined length?
	//...

	std::string t = std::string((char*)actual_data, (-l) - VARLEN_LENGTH_SZ);
	return trim_copy(t);
}

static std::string odbc_fixup_parameters(const std::string& sql)
{
	int n = 1;
	bool in_single_quoted_string = false;
	bool in_double_quoted_string = false;
	bool in_param_id = false;
	std::string out_sql;

	for (auto itc = sql.begin(); itc != sql.end(); ++itc) {
		char c = *itc;

		if (in_param_id && isalnum(c))
			continue;
		else {
			in_param_id = false;
		}

		switch (c) {
		case '"':
			out_sql += c;
			in_double_quoted_string = !in_double_quoted_string;
			continue;

		case '\'':
			out_sql += c;
			in_single_quoted_string = !in_single_quoted_string;
			continue;

		case '$':
		case ':':
			if (!in_single_quoted_string && !in_double_quoted_string) {
				out_sql += '?';
				in_param_id = true;
			}
			else
				out_sql += c;
			continue;

		default:
			out_sql += c;

		}
	}

	return out_sql;
}
