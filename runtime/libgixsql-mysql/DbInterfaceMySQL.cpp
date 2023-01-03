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

#include <cstring>

#include "DbInterfaceMySQL.h"
#include "IConnection.h"
#include "Logger.h"
#include "utils.h"
#include "varlen_defs.h"

#define CLIENT_SIDE_CURSOR_STORAGE
#define MYSQL_OK	0

static std::string mysql_fixup_parameters(const std::string& sql);
static std::string __get_trimmed_hostref_or_literal(void* data, int l);

DbInterfaceMySQL::DbInterfaceMySQL()
{
	connaddr = nullptr;
	last_rc = 0;
	owner = nullptr;
}


DbInterfaceMySQL::~DbInterfaceMySQL()
{
}

int DbInterfaceMySQL::init(const std::shared_ptr<spdlog::logger>& _logger)
{
	connaddr = NULL;
	//current_resultset.clear();
	owner = NULL;

	auto lib_sink = _logger->sinks().at(0);
	lib_logger = std::make_shared<spdlog::logger>("libgixsql-mysql", lib_sink);
	lib_logger->set_level(_logger->level());
	lib_logger->info("libgixsql-mysql logger started");

	return DBERR_NO_ERROR;
}

int DbInterfaceMySQL::connect(const std::shared_ptr<IDataSourceInfo>& conn_string, const std::shared_ptr<IConnectionOptions>& g_opts)
{
	int rc = 0;
	MYSQL* conn;
	std::string connstr;

	connaddr = NULL;

	lib_logger->trace(FMT_FILE_FUNC "connstring: {} - autocommit: {} - encoding: {}", __FILE__, __func__, conn_string->get(), (int)g_opts->autocommit, g_opts->client_encoding);

	unsigned int port = conn_string->getPort() > 0 ? conn_string->getPort() : 3306;
	conn = mysql_init(NULL);
	conn = mysql_real_connect(conn, conn_string->getHost().c_str(), conn_string->getUsername().c_str(),
		conn_string->getPassword().c_str(), conn_string->getDbName().c_str(),
		port, NULL, 0); // CLIENT_MULTI_STATEMENTS?

	if (conn == NULL) {
		return DBERR_CONNECTION_FAILED;
	}

	if (!g_opts->client_encoding.empty()) {
		std::string qenc = "SET NAMES " + g_opts->client_encoding;
		rc = mysql_real_query(conn, qenc.c_str(), qenc.size());
		if (mysqlRetrieveError(rc) != MYSQL_OK) {
			return DBERR_CONNECTION_FAILED;
		}
	}

	if (g_opts->autocommit != AutoCommitMode::Native) {
		lib_logger->trace(FMT_FILE_FUNC "MYSQL::setting autocommit to {}", __FILE__, __func__, g_opts->autocommit == AutoCommitMode::On ? "ON" : "OFF");
		std::string q("SET AUTOCOMMIT=");
		q += (g_opts->autocommit == AutoCommitMode::On) ? "1" : "0";
		rc = mysql_real_query(conn, q.c_str(), q.size());
		if (mysqlRetrieveError(rc) != MYSQL_OK) {
			return DBERR_CONNECTION_FAILED;
		}
	}
	else {
		lib_logger->trace(FMT_FILE_FUNC "MYSQL::setting autocommit to native (nothing to do)", __FILE__, __func__);
	}

	auto opts = conn_string->getOptions();
	if (opts.find("updatable_cursors") != opts.end()) {
		std::string opt_updatable_cursors = opts["updatable_cursors"];
		if (!opt_updatable_cursors.empty()) {
			if (opt_updatable_cursors == "on" || opt_updatable_cursors == "1" || opt_updatable_cursors == "true") {
				this->updatable_cursors_emu = true;
			}

			if (opt_updatable_cursors == "off" || opt_updatable_cursors == "0" || opt_updatable_cursors == "false") {
				this->updatable_cursors_emu = false;
			}
		}
	}

	if (this->updatable_cursors_emu)
		lib_logger->trace(FMT_FILE_FUNC "MYSQL::enabled updatable cursor support", __FILE__, __func__);
	else
		lib_logger->trace(FMT_FILE_FUNC "MYSQL::updatable cursor support is disabled", __FILE__, __func__);

	connaddr = conn;
	current_statement_data = nullptr;

	if (owner)
		owner->setOpened(true);

	return DBERR_NO_ERROR;
}

int DbInterfaceMySQL::reset()
{
	int rc = terminate_connection();
	if (rc == DBERR_NO_ERROR)
		return DBERR_NO_ERROR;
	else
		return DBERR_CONN_RESET_FAILED;
}

int DbInterfaceMySQL::terminate_connection()
{
	current_statement_data.reset();

	if (connaddr) {
		mysql_close(connaddr);
		connaddr = nullptr;
		// Apparently there isn't a way to check if an error 
		// occurred when attempting to close the connection
		// int rc = mysql_errno(connaddr);
		// if (rc != MYSQL_OK)
		// 	return DBERR_DISCONNECT_FAILED;
	}

	if (owner)
		owner->setOpened(false);

	return DBERR_NO_ERROR;

}

const char* DbInterfaceMySQL::get_error_message()
{
	return (char*)last_error.c_str();
}

int DbInterfaceMySQL::get_error_code()
{
	return last_rc;
}

std::string DbInterfaceMySQL::get_state()
{
	return last_state;
}

void DbInterfaceMySQL::set_owner(std::shared_ptr<IConnection> conn)
{
	owner = conn;
}

std::shared_ptr<IConnection> DbInterfaceMySQL::get_owner()
{
	return owner;
}

int DbInterfaceMySQL::prepare(const std::string& _stmt_name, const std::string& query)
{
	std::string prepared_sql;
	std::shared_ptr<MySQLStatementData> res = std::make_shared<MySQLStatementData>();
	res->statement = mysql_stmt_init(connaddr);

	std::string stmt_name = to_lower(_stmt_name);

	lib_logger->trace(FMT_FILE_FUNC "MySQL::prepare ({}) - SQL: {}", __FILE__, __func__, stmt_name, query);

	if (this->_prepared_stmts.find(stmt_name) != _prepared_stmts.end()) {
		return DBERR_PREPARE_FAILED;
	}

	if (this->owner->getConnectionOptions()->fixup_parameters) {
		prepared_sql = mysql_fixup_parameters(query);
		lib_logger->trace(FMT_FILE_FUNC "MySQL::fixup parameters is on", __FILE__, __func__);
		lib_logger->trace(FMT_FILE_FUNC "MySQL::prepare ({}) - SQL(P): {}", __FILE__, __func__, stmt_name, prepared_sql);
	}
	else {
		prepared_sql = query;
	}

	lib_logger->trace(FMT_FILE_FUNC "MySQL::prepare ({}) - SQL(P): {}", __FILE__, __func__, stmt_name, prepared_sql);


	int rc = mysql_stmt_prepare(res->statement, prepared_sql.c_str(), prepared_sql.size());

	if (mysqlRetrieveError(rc) != MYSQL_OK) {
		lib_logger->error(FMT_FILE_FUNC "MySQL::prepare ({} - res: ({}) {}", __FILE__, __func__, stmt_name, last_rc, last_error);
		return DBERR_PREPARE_FAILED;
	}

	mysqlClearError();

	lib_logger->trace(FMT_FILE_FUNC "MySQL::prepare ({} - res: ({}) {}", __FILE__, __func__, stmt_name, last_rc, last_error);

	_prepared_stmts[stmt_name] = res;

	return DBERR_NO_ERROR;
}

int DbInterfaceMySQL::exec_prepared(const std::string& _stmt_name, std::vector<CobolVarType> paramTypes, std::vector<std_binary_data>& paramValues, std::vector<unsigned long> paramLengths, const std::vector<uint32_t>& paramFlags)
{
	int rc = 0;
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

	std::shared_ptr<MySQLStatementData> wk_rs = _prepared_stmts[stmt_name];
	wk_rs->resizeParams(nParams);

	// *********************

	std::unique_ptr<MYSQL_BIND[]> bound_param_defs = std::make_unique<MYSQL_BIND[]>(nParams);
	for (int i = 0; i < nParams; i++) {
		MYSQL_BIND* bound_param = &bound_param_defs[i];

		bound_param->buffer_type = MYSQL_TYPE_STRING;
		bound_param->buffer = (char*)paramValues.at(i).data();
		bound_param->buffer_length = paramLengths.at(i);
	}

	rc = mysql_stmt_bind_param(wk_rs->statement, bound_param_defs.get());
	if (mysqlRetrieveError(rc) != MYSQL_OK) {
		lib_logger->error("MySQL: Error while binding paramenter definitions ({}): {}", last_rc, last_error);
		return DBERR_SQL_ERROR;
	}

	rc = mysql_stmt_execute(wk_rs->statement);
	if (mysqlRetrieveError(rc) != MYSQL_OK) {
		return DBERR_SQL_ERROR;
	}

	// Set STMT_ATTR_UPDATE_MAX_LENGTH attribute
	bool aBool = 1;
	rc = mysql_stmt_attr_set(wk_rs->statement, STMT_ATTR_UPDATE_MAX_LENGTH, &aBool);
	if (mysqlRetrieveError(rc) != MYSQL_OK) {
		lib_logger->error("MySQL: Error while initializing resultset buffers (1)");
		return DBERR_SQL_ERROR;
	}

#ifdef CLIENT_SIDE_CURSOR_STORAGE
	rc = mysql_stmt_store_result(wk_rs->statement);
	if (mysqlRetrieveError(rc) != MYSQL_OK) {
		lib_logger->error("MySQL: Error while storing resultset ({})", last_rc);
		return DBERR_SQL_ERROR;
	}
#endif
	rc = wk_rs->resizeColumnData();
	if (mysqlRetrieveError(rc) != MYSQL_OK) {
		lib_logger->error("MySQL: Error while initializing resultset buffers");
		return DBERR_SQL_ERROR;
	}

	if (wk_rs->column_count) {
		std::unique_ptr<MYSQL_BIND[]> bound_res_cols = std::make_unique<MYSQL_BIND[]>(wk_rs->column_count);
		for (int i = 0; i < wk_rs->column_count; i++) {
			MYSQL_BIND* bound_res_col = &bound_res_cols[i];
			bound_res_col->buffer_type = MYSQL_TYPE_STRING;
			bound_res_col->buffer = wk_rs->data_buffers.at(i);
			bound_res_col->buffer_length = wk_rs->data_buffer_lengths.at(i) + 1;
		}

		rc = mysql_stmt_bind_result(wk_rs->statement, bound_res_cols.get());
		if (mysqlRetrieveError(rc) != MYSQL_OK) {
			lib_logger->error("MySQL: Error while executing query ({} : {}) {}", last_rc, last_error, last_state);
			return DBERR_FETCH_ROW_FAILED;
		}
	}

	return DBERR_NO_ERROR;
}

DbPropertySetResult DbInterfaceMySQL::set_property(DbProperty p, std::variant<bool, int, std::string> v)
{
	return DbPropertySetResult::Unsupported;
}

int DbInterfaceMySQL::mysqlRetrieveError(int rc)
{
	if (rc == MYSQL_OK) {
		mysqlClearError();
		return rc;
	}

	last_rc = mysql_errno(connaddr);
	last_error = mysql_error(connaddr);
	last_state = mysql_sqlstate(connaddr);

	last_rc = last_rc > 0 ? -last_rc : last_rc;

	return rc;
}

void DbInterfaceMySQL::mysqlClearError()
{
	last_error = "";
	last_rc = DBERR_NO_ERROR;
	last_state = "00000";
}

void DbInterfaceMySQL::mysqlSetError(int err_code, std::string sqlstate, std::string err_msg)
{
	last_error = err_msg;
	last_rc = err_code;
	last_state = sqlstate;
}

int DbInterfaceMySQL::_mysql_exec_params(std::shared_ptr<ICursor> crsr, const std::string& query, const std::vector<CobolVarType>& paramTypes, const std::vector<std_binary_data>& paramValues, const std::vector<unsigned long>& paramLengths, const std::vector<uint32_t>& paramFlags, std::shared_ptr<MySQLStatementData> prep_stmt_data)
{
	int rc = 0;
	bool is_delete = false;
	std::string cursor_name, table_name, update_query;
	std::vector<std::string> unique_key;
	MYSQL_STMT* upd_del_statement = nullptr;
	MYSQL_BIND* key_params = nullptr;
	int key_params_size = 0;

	lib_logger->trace(FMT_FILE_FUNC "SQL: #{}#", __FILE__, __func__, query);

	std::shared_ptr<MySQLStatementData> wk_rs;

	if (paramTypes.size() != paramValues.size() || paramTypes.size() != paramFlags.size()) {
		lib_logger->error(FMT_FILE_FUNC "Internal error: parameter count mismatch", __FILE__, __func__);
		last_error = "Internal error: parameter count mismatch";
		last_rc = DBERR_INTERNAL_ERR;
		return DBERR_INTERNAL_ERR;
	}

	if (!prep_stmt_data) {

		wk_rs = (crsr != nullptr) ? std::static_pointer_cast<MySQLStatementData>(crsr->getPrivateData()) : current_statement_data;

		if (wk_rs && wk_rs == current_statement_data) {
			current_statement_data.reset();
		}

		wk_rs = std::make_shared<MySQLStatementData>();

		if (updatable_cursors_emu && is_update_or_delete_where_current_of(query, table_name, cursor_name, &is_delete)) {

			// No cursor was passed, we need to find it
			if (cursor_name.empty() || this->_declared_cursors.find(cursor_name) == this->_declared_cursors.end() || !this->_declared_cursors[cursor_name]) {
				spdlog::error("Cannot find updatable cursor {}", cursor_name);
				return DBERR_SQL_ERROR;
			}

			std::shared_ptr<ICursor> updatable_crsr = this->_declared_cursors[cursor_name];

			if (has_unique_key(table_name, updatable_crsr, unique_key)) {
				std::string new_query;

				if (!prepare_updatable_cursor_query(query, updatable_crsr, unique_key, &upd_del_statement, &key_params, &key_params_size) || !upd_del_statement) {
					spdlog::error("Cannot rewrite query for updatable cursor {}", updatable_crsr->getName());
					return DBERR_SQL_ERROR;
				}

				// Parameters are bound later

				wk_rs->statement = upd_del_statement;
			}
			else {
				spdlog::error("No unique key found on table while trying to update a cursor row (cursor: {}, table: {}", cursor_name, table_name);
				return DBERR_SQL_ERROR;
			}
		}
		else {
			wk_rs->statement = mysql_stmt_init(connaddr);
			rc = mysql_stmt_prepare(wk_rs->statement, query.c_str(), query.size());
		}

		if (mysqlRetrieveError(rc) != MYSQL_OK) {
			spdlog::error("Prepare error: {} ({}) - {}", last_rc, last_state, last_error);
			return DBERR_SQL_ERROR;
		}
	}
	else {
		wk_rs = prep_stmt_data;	// Already prepared
	}

	int nParams = paramValues.size();

	wk_rs->resizeParams(nParams + key_params_size);

	std::unique_ptr<MYSQL_BIND[]> bound_param_defs = std::make_unique<MYSQL_BIND[]>(nParams + key_params_size);

	for (int i = 0; i < nParams; i++) {
		MYSQL_BIND* bound_param = &bound_param_defs[i];

		bound_param->buffer_type = MYSQL_TYPE_STRING;
		bound_param->buffer = (char*)paramValues.at(i).data();
		bound_param->buffer_length = paramLengths.at(i);
	}

	// This will only be used if we are performing an update/delete on an aupdatable cursor
	for (int i = 0; i < key_params_size; i++) {
		MYSQL_BIND* bound_param = &bound_param_defs[nParams + i];

		bound_param->buffer_type = MYSQL_TYPE_STRING;
		bound_param->buffer = key_params[i].buffer;
		bound_param->buffer_length = key_params[i].buffer_length;
	}

	rc = mysql_stmt_bind_param(wk_rs->statement, bound_param_defs.get());

	if (mysqlRetrieveError(rc) != MYSQL_OK) {
		lib_logger->error("MySQL: Error while binding parameter definitions ({}): {}", last_rc, last_error);
		return DBERR_SQL_ERROR;
	}

	rc = mysql_stmt_execute(wk_rs->statement);
	if (mysqlRetrieveError(rc) != MYSQL_OK) {
		lib_logger->error("MySQL: Error while executing query ({}): {}", last_rc, query);
		return DBERR_SQL_ERROR;
	}

	// Set STMT_ATTR_UPDATE_MAX_LENGTH attribute
	bool aBool = 1;
	rc = mysql_stmt_attr_set(wk_rs->statement, STMT_ATTR_UPDATE_MAX_LENGTH, &aBool);
	if (mysqlRetrieveError(rc) != MYSQL_OK) {
		lib_logger->error("MySQL: Error while initializing resultset buffers (1)");
		return DBERR_SQL_ERROR;
	}

#ifdef CLIENT_SIDE_CURSOR_STORAGE
	rc = mysql_stmt_store_result(wk_rs->statement);
	if (mysqlRetrieveError(rc) != MYSQL_OK) {
		lib_logger->error("MySQL: Error while storing resultset ({})", last_rc);
		return DBERR_SQL_ERROR;
	}
#endif
	rc = wk_rs->resizeColumnData();
	if (mysqlRetrieveError(rc) != MYSQL_OK) {
		lib_logger->error("MySQL: Error while initializing resultset buffers");
		return DBERR_SQL_ERROR;
	}

	if (wk_rs->column_count) {
		std::unique_ptr<MYSQL_BIND[]> bound_res_cols = std::make_unique<MYSQL_BIND[]>(wk_rs->column_count);

		for (int i = 0; i < wk_rs->column_count; i++) {
			MYSQL_BIND* bound_res_col = &bound_res_cols[i];
			bound_res_col->buffer_type = MYSQL_TYPE_STRING;
			bound_res_col->buffer = wk_rs->data_buffers.at(i);
			bound_res_col->buffer_length = wk_rs->data_buffer_lengths.at(i) + 1;
		}

		rc = mysql_stmt_bind_result(wk_rs->statement, bound_res_cols.get());
		if (mysqlRetrieveError(rc) != MYSQL_OK) {
			lib_logger->error("MySQL: Error while executing query ({} : {}) {}", last_rc, last_error, last_state);
			return DBERR_FETCH_ROW_FAILED;
		}
	}

	if (!prep_stmt_data) {
		if (is_update_or_delete_statement(query)) {
			int nrows = mysql_stmt_affected_rows(wk_rs->statement);
			if (nrows <= 0) {
				last_rc = 100;
				return DBERR_SQL_ERROR;
			}
		}
	}

	if (last_rc != MYSQL_OK) {
		last_rc = -(10000 + last_rc);
		return DBERR_SQL_ERROR;
	}

	if (crsr) {
		crsr->setPrivateData(wk_rs);
	}
	else
		current_statement_data = wk_rs;

	return DBERR_NO_ERROR;
}

int DbInterfaceMySQL::_mysql_exec(std::shared_ptr<ICursor> crsr, const std::string& query, std::shared_ptr<MySQLStatementData> prep_stmt_data)
{
	int rc = 0;
	bool is_delete = false;
	std::string cursor_name, table_name, update_query;
	std::vector<std::string> unique_key;
	MYSQL_STMT* upd_del_statement = nullptr;
	MYSQL_BIND* key_params = nullptr;
	int key_params_size = 0;

	lib_logger->trace(FMT_FILE_FUNC "SQL: #{}#", __FILE__, __func__, query);

	std::shared_ptr<MySQLStatementData> wk_rs;

	if (!prep_stmt_data) {
		wk_rs = (crsr != nullptr) ? std::static_pointer_cast<MySQLStatementData>(crsr->getPrivateData()) : current_statement_data;

		if (wk_rs && wk_rs == current_statement_data) {
			current_statement_data.reset();
		}

		wk_rs = std::make_shared<MySQLStatementData>();

		if (updatable_cursors_emu && is_update_or_delete_where_current_of(query, table_name, cursor_name, &is_delete)) {

			// No cursor was passed, we need to find it
			if (cursor_name.empty() || this->_declared_cursors.find(cursor_name) == this->_declared_cursors.end() || !this->_declared_cursors[cursor_name]) {
				spdlog::error("Cannot find updatable cursor {}", cursor_name);
				return DBERR_SQL_ERROR;
			}

			std::shared_ptr<ICursor> updatable_crsr = this->_declared_cursors[cursor_name];

			if (has_unique_key(table_name, updatable_crsr, unique_key)) {
				std::string new_query;

				if (!prepare_updatable_cursor_query(query, updatable_crsr, unique_key, &upd_del_statement, &key_params, &key_params_size) || !upd_del_statement) {
					spdlog::error("Cannot rewrite query for updatable cursor {}", updatable_crsr->getName());
					return DBERR_SQL_ERROR;
				}

				rc = mysql_stmt_bind_param(upd_del_statement, key_params);
				free(key_params);
				if (mysqlRetrieveError(rc) != MYSQL_OK) {
					lib_logger->error("MySQL: Error while binding parameter definitions ({}): {}", last_rc, last_error);
					return DBERR_SQL_ERROR;
				}

				wk_rs->statement = upd_del_statement;
			}
			else {
				spdlog::error("No unique key found on table while trying to update a cursor row (cursor: {}, table: {}", cursor_name, table_name);
				return DBERR_SQL_ERROR;
			}
		}
		else {
			wk_rs->statement = mysql_stmt_init(connaddr);
			rc = mysql_stmt_prepare(wk_rs->statement, query.c_str(), query.size());
		}

		if (mysqlRetrieveError(rc) != MYSQL_OK) {
			spdlog::error("Prepare error: {} ({}) - {}", last_rc, last_state, last_error);
			return DBERR_SQL_ERROR;
		}
	}
	else {
		wk_rs = prep_stmt_data;	// Already prepared
	}

	wk_rs->resizeParams(0);

	/*
		Prepared statements are restricted to only a few category of statements.
		The type of queries that they work on is limited to DML (INSERT, REPLACE, UPDATE, and DELETE), CREATE TABLE, and SELECT queries.
		Support for additional query types will be added in further versions, to make the prepared statements API more general.
	*/

	rc = mysql_stmt_execute(wk_rs->statement);
	if (mysqlRetrieveError(rc) != MYSQL_OK) {
		lib_logger->error("MySQL: Error while executing query [{} : {}] - {}", last_rc, get_error_message(), query);
		return DBERR_SQL_ERROR;
	}

	// Set STMT_ATTR_UPDATE_MAX_LENGTH attribute
	bool aBool = 1;
	rc = mysql_stmt_attr_set(wk_rs->statement, STMT_ATTR_UPDATE_MAX_LENGTH, &aBool);
	if (mysqlRetrieveError(rc) != MYSQL_OK) {
		lib_logger->error("MySQL: Error while initializing resultset buffers (1)");
		return DBERR_SQL_ERROR;
	}

#ifdef CLIENT_SIDE_CURSOR_STORAGE
	rc = mysql_stmt_store_result(wk_rs->statement);
	if (mysqlRetrieveError(rc) != MYSQL_OK) {
		lib_logger->error("MySQL: Error while storing resultset ({})", last_rc);
		return DBERR_SQL_ERROR;
	}
#endif
	rc = wk_rs->resizeColumnData();
	if (mysqlRetrieveError(rc) != MYSQL_OK) {
		lib_logger->error("MySQL: Error while initializing resultset buffers");
		return DBERR_SQL_ERROR;
	}

	if (wk_rs->column_count) {
		std::unique_ptr<MYSQL_BIND[]> bound_res_cols = std::make_unique<MYSQL_BIND[]>(wk_rs->column_count);

		for (int i = 0; i < wk_rs->column_count; i++) {
			MYSQL_BIND* bound_res_col = &bound_res_cols[i];
			
			bound_res_col->buffer_type = MYSQL_TYPE_STRING;
			bound_res_col->buffer = wk_rs->data_buffers.at(i);
			bound_res_col->buffer_length = wk_rs->data_buffer_lengths.at(i) + 1;
		}

		rc = mysql_stmt_bind_result(wk_rs->statement, bound_res_cols.get());
		if (mysqlRetrieveError(rc) != MYSQL_OK) {
			lib_logger->error("MySQL: Error while executing query ({} : {}) {}", last_rc, last_error, last_state);
			return DBERR_FETCH_ROW_FAILED;
		}
	}

	if (!prep_stmt_data) {
		if (is_update_or_delete_statement(query)) {
			int nrows = mysql_stmt_affected_rows(wk_rs->statement);
			if (nrows <= 0) {
				last_rc = 100;
				return DBERR_SQL_ERROR;
			}
		}
	}

	if (last_rc != MYSQL_OK) {
		last_rc = -(10000 + last_rc);
		return DBERR_SQL_ERROR;
	}

	if (crsr) {
		crsr->setPrivateData(wk_rs);
	}
	else
		current_statement_data = wk_rs;

	return DBERR_NO_ERROR;
}

int DbInterfaceMySQL::exec(std::string query)
{
	return _mysql_exec(NULL, query);
}


int DbInterfaceMySQL::exec_params(const std::string& query, const std::vector<CobolVarType>& paramTypes, const std::vector<std_binary_data>& paramValues, const std::vector<unsigned long>& paramLengths, const std::vector<uint32_t>& paramFlags)
{
	return _mysql_exec_params(NULL, query, paramTypes, paramValues, paramLengths, paramFlags);
}


int DbInterfaceMySQL::cursor_close(const std::shared_ptr<ICursor>& cursor)
{
	if (!cursor) {
		lib_logger->error("Invalid cursor reference");
		return DBERR_CLOSE_CURSOR_FAILED;
	}

	// Prepared statements used for cursors will be disposed separately
	if (!is_cursor_from_prepared_statement(cursor)) {
		std::shared_ptr<MySQLStatementData> dp = std::static_pointer_cast<MySQLStatementData>(cursor->getPrivateData());

		if (!dp || !dp->statement)
			return DBERR_CLOSE_CURSOR_FAILED;


		int rc = mysql_stmt_free_result(dp->statement);
		if (mysqlRetrieveError(rc) != MYSQL_OK) {
			return DBERR_CLOSE_CURSOR_FAILED;
		}

		rc = mysql_stmt_close(dp->statement);
		dp->statement = nullptr;
		if (mysqlRetrieveError(rc) != MYSQL_OK) {
			return DBERR_CLOSE_CURSOR_FAILED;
		}
	}

	cursor->setPrivateData(nullptr);

	return DBERR_NO_ERROR;
}

int DbInterfaceMySQL::cursor_declare(const std::shared_ptr<ICursor>& cursor)
{
	lib_logger->trace(FMT_FILE_FUNC "MySQL: cursor declare invoked", __FILE__, __func__);
	if (!cursor)
		return DBERR_DECLARE_CURSOR_FAILED;

	std::map<std::string, std::shared_ptr<ICursor>>::iterator it = _declared_cursors.find(cursor->getName());
	if (it == _declared_cursors.end()) {
		_declared_cursors[cursor->getName()] = cursor;
	}

	return DBERR_NO_ERROR;
}

int DbInterfaceMySQL::cursor_open(const std::shared_ptr<ICursor>& cursor)
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

	std::shared_ptr<MySQLStatementData> prepared_stmt_data;
	if (starts_with(squery, "@")) {
		prepared_stmt_data = retrieve_prepared_statement(squery.substr(1));
		if (!prepared_stmt_data) {
			// last_error, etc. set by retrieve_prepared_statement_source?
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
		rc = _mysql_exec_params(cursor, squery, param_types, param_values, param_lengths, param_flags, prepared_stmt_data);
	}
	else {
		rc = _mysql_exec(cursor, squery, prepared_stmt_data);
	}

	if (mysqlRetrieveError(rc) == MYSQL_OK) {
		return DBERR_NO_ERROR;
	}
	else {
		return DBERR_OPEN_CURSOR_FAILED;
	}
}

int DbInterfaceMySQL::cursor_fetch_one(const std::shared_ptr<ICursor>& cursor, int)
{
	lib_logger->trace(FMT_FILE_FUNC "MySQL: fetch from cursor invoked", __FILE__, __func__);

	if (!owner) {
		lib_logger->error("Invalid connection reference");
		return DBERR_CONN_NOT_FOUND;
	}

	if (!cursor) {
		lib_logger->error("Invalid cursor reference");
		return DBERR_FETCH_ROW_FAILED;
	}

	lib_logger->trace(FMT_FILE_FUNC "owner id: {}, cursor name: {}, mode: {}", __FILE__, __func__, owner->getId(), cursor->getName(), FETCH_NEXT_ROW);

	std::shared_ptr<MySQLStatementData> dp = std::static_pointer_cast<MySQLStatementData>(cursor->getPrivateData());

	if (!dp || !dp->statement)
		return DBERR_FETCH_ROW_FAILED;

	int rc = mysql_stmt_fetch(dp->statement);
	if (rc == MYSQL_NO_DATA) {
		return DBERR_NO_DATA;
	}

	if (mysqlRetrieveError(rc) != MYSQL_OK)
		return DBERR_FETCH_ROW_FAILED;

	return DBERR_NO_ERROR;
}

bool DbInterfaceMySQL::get_resultset_value(ResultSetContextType resultset_context_type, const IResultSetContextData& context, int row, int col, char* bfr, uint64_t bfrlen, uint64_t* value_len)
{
	*value_len = 0;

	int rc = 0;
	std::shared_ptr<MySQLStatementData> wk_rs;

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
			return DBERR_SQL_ERROR;
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
			return DBERR_SQL_ERROR;
		}
		wk_rs = std::dynamic_pointer_cast<MySQLStatementData>(c->getPrivateData());
	}
	break;
	}

	if (!wk_rs) {
		lib_logger->error("Invalid resultset");
		return DBERR_SQL_ERROR;
	}

	if (col < wk_rs->data_buffers.size()) {
		char* data = wk_rs->data_buffers.at(col);
		unsigned long datalen = *(wk_rs->data_lengths.at(col));
		
		if (datalen > bfrlen) {
			lib_logger->error("MySQL: ERROR: data truncated: needed {} bytes, {} allocated", datalen, bfrlen);	// was just a warning
			return false;
		}

		strcpy(bfr, wk_rs->data_buffers.at(col));
		*value_len = strlen(bfr);

		return true;
	}
	else {
		lib_logger->error("MySQL: invalid column index: {}, max: {}", col, wk_rs->data_buffers.size() - 1);
		return false;
	}
}

bool DbInterfaceMySQL::move_to_first_record(const std::string& _stmt_name)
{
	lib_logger->trace(FMT_FILE_FUNC  "MySQL: moving to first row in resultset", __FILE__, __func__);

	std::shared_ptr<MySQLStatementData> dp;
	std::string stmt_name = to_lower(_stmt_name);;

	if (stmt_name.empty()) {
		if (!current_statement_data) {
			mysqlSetError(DBERR_MOVE_TO_FIRST_FAILED, "HY000", "Invalid statement reference");
			return false;
		}

		dp = current_statement_data;
	}
	else {
		if (_prepared_stmts.find(stmt_name) == _prepared_stmts.end()) {
			mysqlSetError(DBERR_MOVE_TO_FIRST_FAILED, "HY000", "Invalid statement reference");
			return false;
		}
		dp = _prepared_stmts[stmt_name];
	}

	if (!dp || !dp->statement) {
		mysqlSetError(DBERR_MOVE_TO_FIRST_FAILED, "HY000", "Invalid statement reference");
		return false;
	}


	int rc = mysql_stmt_fetch(dp->statement);
	if (rc == MYSQL_NO_DATA) {
		mysqlSetError(DBERR_NO_DATA, "02000", "No data");
		return false;
	}

	if (mysqlRetrieveError(rc)) {
		lib_logger->error("MySQL: Error while moving to first row of current resultset ({} - {}) : {}", last_rc, last_error, last_state);
		return false;
	}

	return true;
}

uint64_t DbInterfaceMySQL::get_native_features()
{
	return (uint64_t)DbNativeFeature::ResultSetRowCount;
}


int DbInterfaceMySQL::get_num_rows(const std::shared_ptr<ICursor>& crsr)
{
	std::shared_ptr<MySQLStatementData> wk_rs = nullptr;

	if (crsr) {
		wk_rs = std::dynamic_pointer_cast<MySQLStatementData>(crsr->getPrivateData());
	}
	else {
		wk_rs = current_statement_data;
	}

	if (!wk_rs || !wk_rs->statement) {
		return -1;
	}

	return mysql_stmt_num_rows(wk_rs->statement);
}

int DbInterfaceMySQL::get_num_fields(const std::shared_ptr<ICursor>& crsr)
{
	std::shared_ptr<MySQLStatementData> wk_rs = nullptr;

	if (crsr) {
		wk_rs = std::dynamic_pointer_cast<MySQLStatementData>(crsr->getPrivateData());
	}
	else {
		wk_rs = current_statement_data;
	}

	if (!wk_rs || !wk_rs->statement) {
		return -1;
	}

	return wk_rs->column_count;
}

MySQLStatementData::MySQLStatementData()
{
}

MySQLStatementData::~MySQLStatementData()
{
	cleanup();

	if (this->statement) {
		mysql_stmt_free_result(this->statement);
		mysql_stmt_close(this->statement);
	}
}

void MySQLStatementData::resizeParams(int n)
{
}

int MySQLStatementData::resizeColumnData()
{
	if (!this->statement)
		return false;

	MYSQL_RES* metadata = mysql_stmt_result_metadata(this->statement);

	cleanup();

	if (!metadata) {
		return false;
	}

	this->column_count = mysql_stmt_field_count(this->statement);

	for (unsigned int i = 0; i < this->column_count; i++) {
		MYSQL_FIELD* f = &metadata->fields[i];
		int len = f->max_length + 1;
		char* bfr = (char*)calloc(len, 1);
		data_buffers.push_back(bfr);
		data_buffer_lengths.push_back(len);

		unsigned long* dl = (unsigned long*)calloc(1, sizeof(unsigned long*));
		data_lengths.push_back(dl);
	}

	mysql_free_result(metadata);

	return MYSQL_OK;
}

void MySQLStatementData::cleanup()
{
	for (int i = 0; i < data_buffers.size(); i++) {
		if (data_buffers.at(i))
			free(data_buffers.at(i));

		if (data_lengths.at(i))
			free(data_lengths.at(i));
	}
	data_buffers.clear();
	data_buffer_lengths.clear();
	data_lengths.clear();
}


static std::string mysql_fixup_parameters(const std::string& sql)
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
		case '@':
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

bool DbInterfaceMySQL::is_cursor_from_prepared_statement(std::shared_ptr<ICursor> cursor)
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

std::shared_ptr<MySQLStatementData> DbInterfaceMySQL::retrieve_prepared_statement(const std::string& prep_stmt_name)
{
	std::string stmt_name = to_lower(prep_stmt_name);
	if (_prepared_stmts.find(stmt_name) == _prepared_stmts.end() || _prepared_stmts[stmt_name] == nullptr || _prepared_stmts[stmt_name]->statement == nullptr)
		return nullptr;

	return _prepared_stmts[stmt_name];
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

std::vector<std::string> DbInterfaceMySQL::get_resultset_column_names(MYSQL_STMT* stmt)
{
	MYSQL_RES* rsdata = mysql_stmt_result_metadata(stmt);
	std::vector<std::string> crsr_cols;
	for (int i = 0; i < rsdata->field_count; i++) {
		crsr_cols.push_back(to_upper(rsdata->fields[i].name));
	}
	mysql_free_result(rsdata);
	return crsr_cols;
}

bool DbInterfaceMySQL::has_unique_key(std::string table_name, std::shared_ptr<ICursor> crsr, std::vector<std::string>& unique_key)
{
	std::vector<std::string> key;
	std::vector<std::string> constraints;

	if (!connaddr || table_name.empty() || !crsr || !crsr->getPrivateData()) {
		return false;
	}

	auto rs = std::dynamic_pointer_cast<MySQLStatementData>(crsr->getPrivateData());

	if (!rs->statement)
		return false;

	int n = table_name.find(".");
	if (n != std::string::npos)
		table_name = table_name.substr(n + 1);

	int rc = mysql_query(connaddr, ("SHOW KEYS FROM `" + table_name + "`").c_str());
	if (mysqlRetrieveError(rc) != MYSQL_OK) {
		return false;
	}

	MYSQL_RES* result = mysql_store_result(connaddr);
	if (!result) {
		return false;
	}

	bool key_found = false;
	int num_fields = mysql_num_fields(result);
	std::vector<std::string> crsr_cols = get_resultset_column_names(rs->statement);

	MYSQL_ROW r = nullptr;
	std::string cur_key_name;
	while (true) {

		r = mysql_fetch_row(result);
		if (!r) {
			if (key.size() > 0 && (key_found = vector_contains_all(crsr_cols, key))) {
				unique_key = key;
			}
			break;
		}

		bool is_unique = r[1][0] == '0';
		int seq_in_index = atoi(r[3]);
		std::string column_name = r[4];
		std::string key_name = r[2];

		if (cur_key_name.empty()) {
			cur_key_name = key_name;
			key.clear();
			key.push_back(to_upper(column_name));
			continue;
		}

		if (key_name != cur_key_name) {
			// check and close key
			key_found = vector_contains_all(crsr_cols, key);
			if (key_found) {
				unique_key = key;
				break;
			}
			else {
				key.clear();
				cur_key_name = key_name;
				key.push_back(to_upper(column_name));
			}
		}
		else {
			key.push_back(to_upper(column_name));
			continue;
		}

	};

	if (!key_found) {
		lib_logger->trace("unique key not found for updatable cursor (cursor: {}, table: {})", crsr->getName(), table_name);
	}
	else {
		lib_logger->trace("unique key found for updatable cursor (cursor: {}, table: {}): {}", crsr->getName(), table_name, vector_join(unique_key, ','));
	}

	if (result)
		mysql_free_result(result);

	return key_found;
}

bool DbInterfaceMySQL::prepare_updatable_cursor_query(const std::string& qry, std::shared_ptr<ICursor> crsr, const std::vector<std::string>& unique_key, MYSQL_STMT** update_stmt, MYSQL_BIND** key_params, int *key_params_size)
{
	*update_stmt = nullptr;

	if (!crsr || !crsr->getQuery().size())
		return false;

	auto rs = std::dynamic_pointer_cast<MySQLStatementData>(crsr->getPrivateData());

	if (!rs->statement)
		return false;

	std::string tqry = to_upper(qry);
	tqry = string_replace(tqry, "\r", " ");
	tqry = string_replace(tqry, "\n", " ");
	tqry = string_replace(tqry, "\t", " ");

	int np = tqry.find("WHERE CURRENT OF");
	if (np == std::string::npos)
		return false;

	tqry = qry;	// now we preserve lower/uppercase
	tqry = string_replace(tqry, "\r", " ");
	tqry = string_replace(tqry, "\n", " ");
	tqry = string_replace(tqry, "\t", " ");

	tqry = tqry.substr(0, np);

	std::vector<std::string> crsr_cols = get_resultset_column_names(rs->statement);
	if (!crsr_cols.size())
		return false;

	std::string where_clause = " WHERE 1=1";

	for (int i = 0; i < unique_key.size(); i++) {
		where_clause += " AND " + unique_key.at(i) + " = ?";
	}

	tqry += where_clause;

	MYSQL_STMT* upd_stmt = mysql_stmt_init(connaddr);

	lib_logger->trace("Preparing updatable cursor query for {}: {}", crsr->getName(), tqry);

	int rc = mysql_stmt_prepare(upd_stmt, tqry.c_str(), tqry.size());
	if (mysqlRetrieveError(rc) != MYSQL_OK) {
		lib_logger->error("Could not prepare updatable cursor query for {}", crsr->getName());
		return false;
	}

	// we pass parameters that need to be bound
	MYSQL_BIND* bound_param_defs = (MYSQL_BIND*)calloc(sizeof(MYSQL_BIND), unique_key.size());
	for (int i = 0; i < unique_key.size(); i++) {
		MYSQL_BIND* bound_param = &bound_param_defs[i];

		std::vector<std::string>::iterator itr = std::find(crsr_cols.begin(), crsr_cols.end(), unique_key.at(i));

		if (itr == crsr_cols.cend()) {
			lib_logger->error("Could not bind parameters for updatable cursor query ({})/[{}]", crsr->getName(), tqry);
			return false;
		}

		int col_idx = std::distance(crsr_cols.begin(), itr);
		bound_param->buffer_type = MYSQL_TYPE_STRING;
		bound_param->buffer = rs->data_buffers[col_idx];
		bound_param->buffer_length = strlen(rs->data_buffers[col_idx]);
	}

	*key_params = bound_param_defs;
	*key_params_size = unique_key.size();
	*update_stmt = upd_stmt;

	return true;
}
