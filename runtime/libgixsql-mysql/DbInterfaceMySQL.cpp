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

// TODO: fix this
#define CLIENT_SIDE_CURSOR

DbInterfaceMySQL::DbInterfaceMySQL()
{
	connaddr = nullptr;
	last_rc = 0;
	owner = nullptr;
}


DbInterfaceMySQL::~DbInterfaceMySQL()
{}

int DbInterfaceMySQL::init(const std::shared_ptr<spdlog::logger>& _logger)
{
	connaddr = NULL;
	cur_crsr.clear();
	owner = NULL;

	auto lib_sink = _logger->sinks().at(0);
	lib_logger = std::make_shared<spdlog::logger>("libgixsql-mysql", lib_sink);
	lib_logger->set_level(_logger->level());
	lib_logger->info("libgixsql-mysql logger started");

	return DBERR_NO_ERROR;
}

int DbInterfaceMySQL::connect(IDataSourceInfo *conn_string, int autocommit, string encoding)
{
	MYSQL *conn;
	string connstr;

	connaddr = NULL;

	lib_logger->trace(FMT_FILE_FUNC "connstring: {} - autocommit: {} - encoding: {}", __FILE__, __func__,	conn_string->get(), autocommit, encoding);

	unsigned int port = conn_string->getPort() > 0 ? conn_string->getPort() : 3306;
	conn = mysql_init(NULL);
	conn = mysql_real_connect(conn, conn_string->getHost().c_str(), conn_string->getUsername().c_str(),
		conn_string->getPassword().c_str(), conn_string->getDbName().c_str(),
		port, NULL, 0); // CLIENT_MULTI_STATEMENTS?

	if (conn == NULL) {
		return DBERR_CONNECTION_FAILED;
	}

	if (!encoding.empty()) {
		string qenc = "SET NAMES " + encoding;
		mysql_real_query(conn, qenc.c_str(), qenc.size());
	}

	connaddr = conn;
	cur_crsr.clear();

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
	int rc = 0;
	if (cur_crsr.cursor_stmt) {
		mysql_stmt_close(cur_crsr.cursor_stmt);
		rc = mysql_errno(connaddr);
		cur_crsr.clear();
	}

	if (connaddr && !rc) {
		mysql_close(connaddr);
		rc = mysql_errno(connaddr);
		connaddr = NULL;
	}

	if (owner)
		owner->setOpened(false);

	return (rc == 0) ? DBERR_NO_ERROR : DBERR_DISCONNECT_FAILED;
}

int DbInterfaceMySQL::begin_transaction()
{
	int rc = exec("START TRANSACTION");
	return (rc == DBERR_NO_ERROR) ? DBERR_NO_ERROR : DBERR_BEGIN_TX_FAILED;
}

int DbInterfaceMySQL::end_transaction(string completion_type)
{
	if (completion_type != "COMMIT" && completion_type != "ROLLBACK")
		return DBERR_END_TX_FAILED;

	int rc = exec(completion_type);
	return (rc == DBERR_NO_ERROR) ? DBERR_NO_ERROR : DBERR_END_TX_FAILED;
}

char *DbInterfaceMySQL::get_error_message()
{
	return (char *)last_error.c_str();
}

int DbInterfaceMySQL::get_error_code()
{
	return last_rc;
}

std::string DbInterfaceMySQL::get_state()
{
	return last_state;
}

void DbInterfaceMySQL::set_owner(IConnection *conn)
{
	owner = conn;
}

IConnection *DbInterfaceMySQL::get_owner()
{
	return owner;
}

int DbInterfaceMySQL::prepare(std::string stmt_name, std::string sql)
{
	last_rc = DBERR_NOT_IMPL;
	last_error = "NOTIMPL";
	return DBERR_PREPARE_FAILED;
}

int DbInterfaceMySQL::exec_prepared(std::string stmt_name, std::vector<std::string> &paramValues, std::vector<int> paramLengths, std::vector<int> paramFormats)
{
	last_rc = DBERR_NOT_IMPL;
	last_error = "NOTIMPL";
	return DBERR_SQL_ERROR;
}

int DbInterfaceMySQL::_mysql_exec_params(ICursor *crsr, string query, int nParams, int *paramTypes, vector<string> &paramValues, int *paramLengths, int *paramFormats)
{
	string q = query;
	MySQLCursorData *exec_data = NULL;
	lib_logger->trace(FMT_FILE_FUNC "SQL: #{}#", __FILE__, __func__, q);

	if (!crsr) {
		if (cur_crsr.cursor_stmt) {
			mysql_stmt_close(cur_crsr.cursor_stmt);
			cur_crsr.clear();
		}
		exec_data = &cur_crsr;
		exec_data->cursor_stmt = mysql_stmt_init(connaddr);
	}
	else {
		exec_data = (MySQLCursorData *)crsr->getPrivateData();
	}

	/*
		Prepared statements are restricted to only a few category of statements.
		The type of queries that they work on is limited to DML (INSERT, REPLACE, UPDATE, and DELETE), CREATE TABLE, and SELECT queries.
		Support for additional query types will be added in further versions, to make the prepared statements API more general.
	*/

	if (is_dml_statement(q)) {

		if (!exec_data->cursor_stmt)
			return DBERR_OUT_OF_MEMORY;

		last_rc = mysql_stmt_prepare(exec_data->cursor_stmt, q.c_str(), q.size());
		if (last_rc) {
			last_error = retrieve_mysql_error_message(exec_data->cursor_stmt);
			return DBERR_SQL_ERROR;
		}



		MYSQL_BIND *bound_param_defs = (MYSQL_BIND *)calloc(sizeof(MYSQL_BIND), nParams);
		for (int i = 0; i < nParams; i++) {
			MYSQL_BIND *bound_param = &bound_param_defs[i];

			bound_param->buffer_type = MYSQL_TYPE_STRING;
			bound_param->buffer = (char *)paramValues.at(i).c_str();
			bound_param->buffer_length = paramValues.at(i).size();
		}
		mysql_stmt_bind_param(exec_data->cursor_stmt, bound_param_defs);

		free(bound_param_defs);

		last_rc = mysql_stmt_execute(exec_data->cursor_stmt);
		if (last_rc) {
			mysql_stmt_close(exec_data->cursor_stmt);
			last_error = retrieve_mysql_error_message(exec_data->cursor_stmt);
			exec_data->clear();
			lib_logger->error("MySQL: Error while executing query ({}): {}", last_rc, q);
			
			return DBERR_SQL_ERROR;
		}

		if (mysql_stmt_field_count(exec_data->cursor_stmt)) {

			// Set STMT_ATTR_UPDATE_MAX_LENGTH attribute
			bool aBool = 1;
			int rc = mysql_stmt_attr_set(exec_data->cursor_stmt, STMT_ATTR_UPDATE_MAX_LENGTH, &aBool);

#ifdef CLIENT_SIDE_CURSOR
			last_rc = mysql_stmt_store_result(exec_data->cursor_stmt);
			if (last_rc) {
				lib_logger->error("MySQL: Error while storing resultset ({})", last_rc);
				mysql_stmt_close(exec_data->cursor_stmt);
				last_error = retrieve_mysql_error_message(exec_data->cursor_stmt);
				exec_data->clear();
				return DBERR_SQL_ERROR;
			}
#endif
			if (!exec_data->init()) {
				lib_logger->error("MySQL: Error while initializing cursor buffers");
				if (exec_data->cursor_stmt) {
					mysql_stmt_close(exec_data->cursor_stmt);
					last_error = retrieve_mysql_error_message(exec_data->cursor_stmt);
				}
				else {
					last_error = "Cannot initialize cursor buffers";
				}
				last_rc = DBERR_INVALID_COLUMN_DATA;
				exec_data->clear();
				return DBERR_SQL_ERROR;
			}
		}
	}
	else {
		last_rc = mysql_real_query(connaddr, q.c_str(), q.size());
		if (last_rc) {
			last_error = retrieve_mysql_error_message(NULL);
			lib_logger->error("MySQL: Error while executing query [{} : {}] - {}", last_rc, get_error_message(), q);
			last_error = retrieve_mysql_error_message(exec_data->cursor_stmt);
			exec_data->clear();
			return DBERR_SQL_ERROR;
		}
	}

	return DBERR_NO_ERROR;
}

int DbInterfaceMySQL::_mysql_exec(ICursor *crsr, const string query)
{
	string q = query;
	MySQLCursorData *exec_data = NULL;

	lib_logger->trace(FMT_FILE_FUNC "SQL: #{}#",  __FILE__, __func__, q);

	if (!crsr) {
		if (cur_crsr.cursor_stmt) {
			mysql_stmt_close(cur_crsr.cursor_stmt);
			cur_crsr.clear();
		}
		exec_data = &cur_crsr;
		exec_data->cursor_stmt = mysql_stmt_init(connaddr);
	}
	else {
		exec_data = (MySQLCursorData *)crsr->getPrivateData();
	}

	/*
		Prepared statements are restricted to only a few category of statements.
		The type of queries that they work on is limited to DML (INSERT, REPLACE, UPDATE, and DELETE), CREATE TABLE, and SELECT queries.
		Support for additional query types will be added in further versions, to make the prepared statements API more general.
	*/

	if (is_dml_statement(q)) {
		if (!exec_data->cursor_stmt)
			return DBERR_OUT_OF_MEMORY;

		last_rc = mysql_stmt_prepare(exec_data->cursor_stmt, q.c_str(), q.size());
		if (last_rc) {
			last_error = retrieve_mysql_error_message(exec_data->cursor_stmt);
			return DBERR_SQL_ERROR;
		}



		last_rc = mysql_stmt_execute(exec_data->cursor_stmt);
		if (last_rc) {
			last_error = retrieve_mysql_error_message(exec_data->cursor_stmt);
			lib_logger->error("MySQL: Error while executing query [{} : {}] - {}", last_rc, get_error_message(), q);
			mysql_stmt_close(exec_data->cursor_stmt);
			exec_data->clear();
			return DBERR_SQL_ERROR;
		}

		if (mysql_stmt_field_count(exec_data->cursor_stmt)) {

			// Set STMT_ATTR_UPDATE_MAX_LENGTH attribute
			bool aBool = 1;
			int rc = mysql_stmt_attr_set(exec_data->cursor_stmt, STMT_ATTR_UPDATE_MAX_LENGTH, &aBool);

#ifdef CLIENT_SIDE_CURSOR
			last_rc = mysql_stmt_store_result(exec_data->cursor_stmt);
			if (last_rc) {
				last_error = retrieve_mysql_error_message(exec_data->cursor_stmt);
				lib_logger->error("MySQL: Error while storing resultset ({}) - {}", last_rc, last_error);
				mysql_stmt_close(exec_data->cursor_stmt);
				exec_data->clear();
				return DBERR_SQL_ERROR;
			}
#endif
			if (!exec_data->init()) {
				last_error = retrieve_mysql_error_message(exec_data->cursor_stmt);
				lib_logger->error("MySQL: Error while initializing cursor buffers: {}", last_error);
				if (exec_data->cursor_stmt) {
					mysql_stmt_close(exec_data->cursor_stmt);

				}
				else {
					last_error = "Cannot initialize cursor buffers";
				}
				last_rc = DBERR_INVALID_COLUMN_DATA;
				exec_data->clear();
				return DBERR_SQL_ERROR;
			}
		}
	}
	else {
		last_rc = mysql_real_query(connaddr, q.c_str(), q.size());
		if (last_rc) {
			last_error = retrieve_mysql_error_message(NULL);
			lib_logger->error("MySQL: Error while executing query [{} : {}] - {}", last_rc, get_error_message(), q);
			exec_data->clear();
			return DBERR_SQL_ERROR;
		}
	}

	return DBERR_NO_ERROR;
}

const char *DbInterfaceMySQL::retrieve_mysql_error_message(MYSQL_STMT *stmt)
{
	const char *err = NULL;
	if (stmt != NULL)
		err = mysql_stmt_error(stmt);
	else
		if (connaddr != NULL)
			err = mysql_error(connaddr);

	return (!err) ? "Unknown error" : err;
}

int DbInterfaceMySQL::retrieve_mysql_error_code(MYSQL_STMT *stmt)
{
	if (stmt != NULL)
		return mysql_stmt_errno(stmt);
	else
		if (connaddr != NULL)
			return mysql_errno(connaddr);
		else
			return -1;
}

int DbInterfaceMySQL::exec(string query)
{
	return _mysql_exec(NULL, query);
}


int DbInterfaceMySQL::exec_params(string query, int nParams, int *paramTypes, vector<string> &paramValues, int *paramLengths, int *paramFormats)
{
	return _mysql_exec_params(NULL, query, nParams, paramTypes, paramValues, paramLengths, paramFormats);
}


int DbInterfaceMySQL::close_cursor(ICursor *cursor)
{
	lib_logger->trace(FMT_FILE_FUNC "MySQL: close cursor invoked", __FILE__, __func__);

	if (!cursor) {
		lib_logger->error("MySQL: ERROR: invalid cursor ({})", cursor->getName());
		return DBERR_CLOSE_CURSOR_FAILED;
	}

	MySQLCursorData *cursor_data = (MySQLCursorData *)cursor->getPrivateData();
	if (!cursor_data) {
		lib_logger->error("MySQL: ERROR: closing uninitialized cursor ({})", cursor->getName());
		return DBERR_CLOSE_CURSOR_FAILED;
	}

	int rc = mysql_stmt_close(cursor_data->cursor_stmt);

	delete cursor_data;
	cursor->setPrivateData(NULL);

	if (rc) {
		lib_logger->error("MySQL: Error while closing cursor ({}) {}", rc, cursor->getName());
		return DBERR_CLOSE_CURSOR_FAILED;
	}
	return DBERR_NO_ERROR;
}

int DbInterfaceMySQL::cursor_declare(ICursor *cursor, bool with_hold, int res_type)
{
	lib_logger->trace(FMT_FILE_FUNC "MySQL: cursor declare invoked", __FILE__, __func__);

	if (!cursor)
		return DBERR_DECLARE_CURSOR_FAILED;

	MYSQL_STMT *cursor_stmt = mysql_stmt_init(connaddr);
	if (!cursor_stmt) {
		last_rc = retrieve_mysql_error_code(NULL);
		last_error = retrieve_mysql_error_message(NULL);
		return DBERR_DECLARE_CURSOR_FAILED;
	}

	MySQLCursorData *cursor_data = new MySQLCursorData();
	cursor_data->cursor_stmt = cursor_stmt;

	std::map<string, ICursor *>::iterator it = _declared_cursors.find(cursor->getName());
	if (it == _declared_cursors.end()) {
		_declared_cursors[cursor->getName()] = cursor;
		cursor->setPrivateData(cursor_data);
	}

	// Nothing to do for MySQL
	return DBERR_NO_ERROR;
}

int DbInterfaceMySQL::cursor_declare_with_params(ICursor *cursor, char **param_values, bool with_hold, int res_type)
{
	lib_logger->trace(FMT_FILE_FUNC "MySQL: cursor declare invoked", __FILE__, __func__);
	if (!cursor)
		return DBERR_DECLARE_CURSOR_FAILED;

	MYSQL_STMT *cursor_stmt = mysql_stmt_init(connaddr);
	if (!cursor_stmt) {
		last_rc = retrieve_mysql_error_code(NULL);
		last_error = retrieve_mysql_error_message(NULL);
		return DBERR_DECLARE_CURSOR_FAILED;
	}

	MySQLCursorData *cursor_data = new MySQLCursorData();
	cursor_data->cursor_stmt = cursor_stmt;

	std::map<string, ICursor *>::iterator it = _declared_cursors.find(cursor->getName());
	if (it == _declared_cursors.end()) {
		_declared_cursors[cursor->getName()] = cursor;
		cursor->setPrivateData(cursor_data);
	}

	return DBERR_NO_ERROR;
}

int DbInterfaceMySQL::cursor_open(ICursor *cursor)
{
	lib_logger->trace(FMT_FILE_FUNC "MySQL: open cursor invoked", __FILE__, __func__);
	if (!cursor) {
		lib_logger->error("Invalid cursor");
		return DBERR_DECLARE_CURSOR_FAILED;
	}

	string query = cursor->getQuery();

	if (cursor->getNumParams() > 0) {
		vector<string> params = cursor->getParameterValues();

		int rc = _mysql_exec_params(cursor, string(query), cursor->getNumParams(), NULL, params, NULL, NULL);
		return (rc == DBERR_NO_ERROR) ? DBERR_NO_ERROR : DBERR_OPEN_CURSOR_FAILED;
	}
	else {
		int rc = _mysql_exec(cursor, string(query));
		return (rc == DBERR_NO_ERROR) ? DBERR_NO_ERROR : DBERR_OPEN_CURSOR_FAILED;
	}
}

int DbInterfaceMySQL::fetch_one(ICursor *cursor, int fetchmode)
{
	lib_logger->trace(FMT_FILE_FUNC "MySQL: fetch from cursor invoked", __FILE__, __func__);

	if (!cursor) {
		lib_logger->error("MySQL: ERROR: invalid cursor ({})", cursor->getName());
		return DBERR_FETCH_ROW_FAILED;
	}

	MySQLCursorData *cursor_data = (MySQLCursorData *)cursor->getPrivateData();;
	if (!cursor_data) {
		lib_logger->error("MySQL: ERROR: invalid cursor data ({})", cursor->getName());
		return DBERR_FETCH_ROW_FAILED;
	}

	if (!cursor_data->cursor_stmt) {
		lib_logger->error("MySQL: ERROR: invalid cursor resultset ({})", cursor->getName());
		return DBERR_FETCH_ROW_FAILED;
	}

	int ncols = cursor_data->getColumnCount();
	if (ncols <= 0) {
		lib_logger->error("MySQL: ERROR: invalid column count for cursor ({})", cursor->getName());
		return DBERR_FETCH_ROW_FAILED;
	}

	MYSQL_BIND *bound_res_cols = (MYSQL_BIND *)calloc(sizeof(MYSQL_BIND), ncols);
	for (int i = 0; i < ncols; i++) {
		MYSQL_BIND *bound_res_col = &bound_res_cols[i];
		bound_res_col->buffer_type = MYSQL_TYPE_STRING;
		bound_res_col->buffer = cursor_data->data_buffers.at(i);
		bound_res_col->buffer_length = cursor_data->data_buffer_lengths.at(i) + 1;
	}

	int rc = mysql_stmt_bind_result(cursor_data->cursor_stmt, bound_res_cols);
	if (rc) {
		last_error = retrieve_mysql_error_message(cursor_data->cursor_stmt);
		//LOG_ERROR("MySQL: Error while fetching row from cursor (%d : %s) %s", get_error_code(), get_error_message(), cursor->getName().c_str());
		return DBERR_FETCH_ROW_FAILED;
	}

	last_rc = mysql_stmt_fetch(cursor_data->cursor_stmt);
	if (last_rc) {
		last_error = (last_rc == MYSQL_DATA_TRUNCATED) ? "Data truncated" : retrieve_mysql_error_message(cursor_data->cursor_stmt);
		//LOG_ERROR("MySQL: Error while fetching row from cursor (%d : %s) %s", get_error_code(), get_error_message(), cursor->getName().c_str());
		return DBERR_FETCH_ROW_FAILED;
	}

	return DBERR_NO_ERROR;
}

bool DbInterfaceMySQL::get_resultset_value(ICursor *cursor, int row, int col, char *bfr, int bfrlen, int *value_len)
{
	*value_len = 0;

	MySQLCursorData *cursor_data = (MySQLCursorData *)((cursor != NULL) ? cursor->getPrivateData() : &cur_crsr);
	if (col < cursor_data->data_buffers.size()) {
		char *data = cursor_data->data_buffers.at(col);
		unsigned long datalen = *(cursor_data->data_lengths.at(col));
		if (datalen > bfrlen) {
			lib_logger->error("MySQL: ERROR: data truncated: needed {} bytes, {} allocated", datalen, bfrlen);	// was just a warning
			return false;
		}

		strcpy(bfr, cursor_data->data_buffers.at(col));
		*value_len = strlen(bfr);

		return true;
	}
	else {
		lib_logger->error("MySQL: invalid column index: {}, max: {}", col, cursor_data->data_buffers.size() - 1);
		return false;
	}
}

int DbInterfaceMySQL::move_to_first_record()
{
	lib_logger->trace(FMT_FILE_FUNC  "MySQL: moving to first row in resultset", __FILE__, __func__);

	MySQLCursorData *cursor_data = &cur_crsr;
	if (!cursor_data) {
		lib_logger->error("MySQL: ERROR: invalid statement data");
		return DBERR_MOVE_TO_FIRST_FAILED;
	}

	if (!cursor_data->cursor_stmt) {
		lib_logger->error("MySQL: ERROR: invalid statement resultset");
		return DBERR_MOVE_TO_FIRST_FAILED;
	}

	int ncols = cursor_data->getColumnCount();
	if (ncols <= 0) {
		lib_logger->error("MySQL: ERROR: invalid column count for statement");
		return DBERR_MOVE_TO_FIRST_FAILED;
	}

	MYSQL_BIND *bound_res_cols = (MYSQL_BIND *)calloc(sizeof(MYSQL_BIND), ncols);
	for (int i = 0; i < ncols; i++) {
		MYSQL_BIND *bound_res_col = &bound_res_cols[i];
		bound_res_col->buffer_type = MYSQL_TYPE_STRING;
		bound_res_col->buffer = cursor_data->data_buffers.at(i);
		bound_res_col->buffer_length = cursor_data->data_buffer_lengths.at(i) + 1;
		bound_res_col->length = cursor_data->data_lengths.at(i);
	}

	int rc = mysql_stmt_bind_result(cursor_data->cursor_stmt, bound_res_cols);
	if (rc) {
		lib_logger->error("MySQL: Error while binding resultset ({}) : {}", get_error_code(), get_error_message());
		return DBERR_MOVE_TO_FIRST_FAILED;
	}

	last_rc = mysql_stmt_fetch(cursor_data->cursor_stmt);
	if (last_rc) {
		last_error = retrieve_mysql_error_message(cursor_data->cursor_stmt);
		lib_logger->error("MySQL: Error while moving to first row of current resultset ({}) : {}", get_error_code(), get_error_message());
		return DBERR_MOVE_TO_FIRST_FAILED;
	}

	return DBERR_NO_ERROR;
}

int DbInterfaceMySQL::supports_num_rows()
{
	return 1;
}

int DbInterfaceMySQL::get_num_rows()
{
	if (cur_crsr.cursor_stmt)
		return  (int)mysql_stmt_num_rows(cur_crsr.cursor_stmt);
	else
		return -1;
}

int DbInterfaceMySQL::get_num_fields()
{
	if (cur_crsr.cursor_stmt)
		return  mysql_stmt_field_count(cur_crsr.cursor_stmt);
	else
		return -1;
}

MySQLCursorData::MySQLCursorData()
{
	cursor_stmt = nullptr;
}

MySQLCursorData::~MySQLCursorData()
{
	clear();
}

bool MySQLCursorData::init()
{
	if (!cursor_stmt)
		return false;

	MYSQL_RES *metadata = mysql_stmt_result_metadata(cursor_stmt);

	clear_buffers();

	if (!metadata) {
		return false;
	}

	unsigned int field_count = mysql_stmt_field_count(cursor_stmt);

	for (unsigned int i = 0; i < field_count; i++) {
		MYSQL_FIELD *f = &metadata->fields[i];
		int len = f->max_length + 1;
		char *bfr = (char *)calloc(len, 1);
		data_buffers.push_back(bfr);
		data_buffer_lengths.push_back(len);

		unsigned long *dl = (unsigned long *)calloc(1, sizeof(int));
		data_lengths.push_back(dl);
	}

	mysql_free_result(metadata);

	return true;
}

void MySQLCursorData::clear()
{
	this->cursor_stmt = NULL;
	clear_buffers();
}

int MySQLCursorData::getColumnCount()
{
	return data_buffers.size();
}

void MySQLCursorData::clear_buffers()
{
	for (int i = 0; i < data_buffers.size(); i++) {
		if (data_buffers.at(i))
			free(data_buffers.at(i));
	}
	data_buffers.clear();
	data_buffer_lengths.clear();
}
