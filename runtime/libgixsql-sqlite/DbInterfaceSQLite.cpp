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

#include "DbInterfaceSQLite.h"

#include <cstring>
#include "IConnection.h"
#include "Logger.h"
#include "utils.h"
#include "varlen_defs.h"

#define DEFAULT_CURSOR_ARRAYSIZE	100

static std::string __get_trimmed_hostref_or_literal(void* data, int l);
static std::string sqlite_fixup_parameters(const std::string& sql);

DbInterfaceSQLite::DbInterfaceSQLite()
{}


DbInterfaceSQLite::~DbInterfaceSQLite()
{

}

int DbInterfaceSQLite::init(const std::shared_ptr<spdlog::logger>& _logger)
{
	owner = NULL;
	connaddr = NULL;
	current_statement_data = NULL;
	last_rc = 0;

	auto lib_sink = _logger->sinks().at(0);
	lib_logger = std::make_shared<spdlog::logger>("libgixsql-sqlite", lib_sink);
	lib_logger->set_level(_logger->level());
	lib_logger->info("libgixsql-sqlite logger started");

	return DBERR_NO_ERROR;
}

int DbInterfaceSQLite::connect(const std::shared_ptr<IDataSourceInfo>& conn_info, const std::shared_ptr<IConnectionOptions>& g_opts)
{
	sqlite3* conn;
	std::string connstr;
	char* err_msg = 0;

	connaddr = nullptr;
	current_statement_data.reset();

	int sqlite_rc = sqlite3_open(conn_info->getHost().c_str(), &conn);

	if (sqlite_rc != SQLITE_OK || conn == NULL) {
		return DBERR_CONNECTION_FAILED;
	}

	if (!g_opts->client_encoding.empty()) {
		std::string sql = "PRAGMA encoding = '" + g_opts->client_encoding + "';";
		sqlite_rc = sqlite3_exec(conn, sql.c_str(), 0, 0, &err_msg);
		if (sqlite_rc != SQLITE_OK) {
			lib_logger->error("SQLite: cannot set encoding to {} transaction: {} ({}): {}", g_opts->client_encoding, last_rc, last_state, last_error);
			sqlite3_close(conn);
			return DBERR_CONNECTION_FAILED;
		}
	}

	// Autocommit is set to off. Since PostgreSQL is ALWAYS in autocommit mode 
	// we will optionally start a transaction
	if (g_opts->autocommit == AutoCommitMode::Off) {
		lib_logger->trace(FMT_FILE_FUNC "SQLite::connect: autocommit is off, starting initial transaction", __FILE__, __func__);
		auto rc = sqlite3_exec(conn, "BEGIN TRANSACTION", 0, 0, &err_msg);
		if (sqliteRetrieveError(rc) != SQLITE_OK) {
			lib_logger->error("SQLite: cannot start transaction: {} ({}): {}", last_rc, last_state, last_error);
			sqlite3_close(conn);
			return DBERR_CONNECTION_FAILED;
		}
	}

	auto opts = conn_info->getOptions();
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
		lib_logger->trace(FMT_FILE_FUNC "SQLite::enabled updatable cursor support", __FILE__, __func__);
	else
		lib_logger->trace(FMT_FILE_FUNC "SQLite::updatable cursor support is disabled", __FILE__, __func__);

	connaddr = conn;
	sqlite3_extended_result_codes(connaddr, 1);

	if (owner)
		owner->setOpened(true);

	return DBERR_NO_ERROR;
}

int DbInterfaceSQLite::reset()
{
	int rc = terminate_connection();
	if (rc == DBERR_NO_ERROR)
		return DBERR_NO_ERROR;
	else
		return DBERR_CONN_RESET_FAILED;
}

int DbInterfaceSQLite::terminate_connection()
{
	if (connaddr) {
		sqlite3_close(connaddr);
		connaddr = nullptr;
	}

	return DBERR_NO_ERROR;
}


char* DbInterfaceSQLite::get_error_message()
{
	return (char*)last_error.c_str();
}

int DbInterfaceSQLite::get_error_code()
{
	return last_rc;
}

std::string DbInterfaceSQLite::get_state()
{
	return last_state;
}

void DbInterfaceSQLite::set_owner(std::shared_ptr<IConnection> conn)
{
	owner = conn;
}

std::shared_ptr<IConnection> DbInterfaceSQLite::get_owner()
{
	return owner;
}

std::string vector_join(const std::vector<std::string>& v, char sep)
{
	std::string s;

	for (std::vector< std::string>::const_iterator p = v.begin();
		p != v.end(); ++p) {
		s += *p;
		if (p != v.end() - 1)
			s += sep;
	}
	return s;
}

int DbInterfaceSQLite::prepare(std::string stmt_name, std::string sql)
{
	std::string prepared_sql;
	std::shared_ptr<SQLiteStatementData> res = std::make_shared<SQLiteStatementData>();

	stmt_name = to_lower(stmt_name);

	lib_logger->trace(FMT_FILE_FUNC "SQLite::prepare ({}) - SQL: {}", __FILE__, __func__, stmt_name, sql);

	if (this->_prepared_stmts.find(stmt_name) != _prepared_stmts.end()) {
		return DBERR_PREPARE_FAILED;
	}

	if (this->owner->getConnectionOptions()->fixup_parameters) {
		prepared_sql = sqlite_fixup_parameters(sql);
		lib_logger->trace(FMT_FILE_FUNC "SQLite::fixup parameters is on", __FILE__, __func__);
		lib_logger->trace(FMT_FILE_FUNC "SQLite::prepare ({}) - SQL(P): {}", __FILE__, __func__, stmt_name, prepared_sql);
	}
	else {
		prepared_sql = sql;
	}

	lib_logger->trace(FMT_FILE_FUNC "SQLite::prepare ({}) - SQL(P): {}", __FILE__, __func__, stmt_name, prepared_sql);

	int rc = sqlite3_prepare_v2(connaddr, sql.c_str(), sql.size(), &res->statement, nullptr);
	if (sqliteRetrieveError(rc) != SQLITE_OK) {
		lib_logger->error(FMT_FILE_FUNC "SQLite::prepare ({} - res: ({}) {}", __FILE__, __func__, stmt_name, last_rc, last_error);
		return DBERR_PREPARE_FAILED;
	}

	sqliteClearError();

	lib_logger->trace(FMT_FILE_FUNC "SQLite::prepare ({} - res: ({}) {}", __FILE__, __func__, stmt_name, last_rc, last_error);

	_prepared_stmts[stmt_name] = res;

	return DBERR_NO_ERROR;
}


int DbInterfaceSQLite::exec_prepared(std::string stmt_name, std::vector<std::string>& paramValues, std::vector<int> paramLengths, std::vector<int> paramFormats)
{

	lib_logger->trace(FMT_FILE_FUNC "statement name: {}", __FILE__, __func__, stmt_name);

	stmt_name = to_lower(stmt_name);

	if (_prepared_stmts.find(stmt_name) == _prepared_stmts.end()) {
		lib_logger->error("Statement not found: {}", stmt_name);
		return DBERR_SQL_ERROR;
	}

	int nParams = (int)paramValues.size();

	std::shared_ptr<SQLiteStatementData> wk_rs = _prepared_stmts[stmt_name];
	wk_rs->resizeParams(nParams);

	sqlite3_reset(wk_rs->statement);
	sqlite3_clear_bindings(wk_rs->statement);

	for (int i = 0; i < nParams; i++) {

		int rc = sqlite3_bind_text(wk_rs->statement, i + 1, paramValues.at(i).c_str(), paramValues.at(i).size(), SQLITE_TRANSIENT);
		if (sqliteRetrieveError(rc) != SQLITE_OK)
			return DBERR_SQL_ERROR;
	}

	int step_rc = sqlite3_step(wk_rs->statement);
	if (step_rc != SQLITE_DONE && step_rc != SQLITE_ROW) {
		int rc = sqliteRetrieveError(step_rc);
		return DBERR_SQL_ERROR;
	}

	return DBERR_NO_ERROR;
}

DbPropertySetResult DbInterfaceSQLite::set_property(DbProperty p, std::variant<bool, int, std::string> v)
{
	return DbPropertySetResult::Unsupported;
}

int DbInterfaceSQLite::exec(std::string query)
{
	return _sqlite_exec(nullptr, query);
}

int DbInterfaceSQLite::_sqlite_exec(std::shared_ptr<ICursor> crsr, const std::string query, std::shared_ptr<SQLiteStatementData> prep_stmt_data)
{
	int rc = 0;
	bool is_delete = false;
	bool is_updatable_crsr_stmt = false;
	std::string cursor_name, table_name, update_query;
	std::vector<std::string> unique_key;
	std::vector<std::string> key_params;
	sqlite3_stmt *upd_del_statement = nullptr;
	char* err_msg = 0;
	uint32_t nquery_cols = 0;
	lib_logger->trace(FMT_FILE_FUNC "SQL: #{}#", __FILE__, __func__, query);

	std::shared_ptr<SQLiteStatementData> wk_rs = std::make_shared<SQLiteStatementData>();

	if (!prep_stmt_data) {
		wk_rs = (crsr != nullptr) ? std::static_pointer_cast<SQLiteStatementData>(crsr->getPrivateData()) : current_statement_data;

		if (wk_rs) {
			if (wk_rs && wk_rs == current_statement_data) {
				current_statement_data.reset();
			}
		}

		wk_rs = std::make_shared<SQLiteStatementData>();

		if (updatable_cursors_emu && is_update_or_delete_where_current_of(query, table_name, cursor_name, &is_delete)) {
			
			is_updatable_crsr_stmt = true;
			
			// No cursor was passed, we need to find it
			if (cursor_name.empty() || this->_declared_cursors.find(cursor_name) == this->_declared_cursors.end() || !this->_declared_cursors[cursor_name]) {
				spdlog::error("Cannot find updatable cursor {}", cursor_name);
				return DBERR_SQL_ERROR;
			}

			std::shared_ptr<ICursor> updatable_crsr = this->_declared_cursors[cursor_name];

			if (has_unique_key(table_name, updatable_crsr, unique_key)) {
				std::string new_query;

				if (!prepare_updatable_cursor_query(query, updatable_crsr, unique_key, &upd_del_statement, key_params) || !upd_del_statement) {
					spdlog::error("Cannot rewrite query for updatable cursor {}", updatable_crsr->getName());
					return DBERR_SQL_ERROR;
				}

				for (int i = 0; i < key_params.size(); i++) {

					int rc = sqlite3_bind_text(upd_del_statement, i + 1, key_params.at(i).c_str(), key_params.at(i).size(), SQLITE_TRANSIENT);
					if (sqliteRetrieveError(rc) != SQLITE_OK) {
						lib_logger->error("SQLite: Error while binding parameter definitions (# {}): {} ({}): {}", i + 1, last_rc, last_state, last_error);
						return false;
					}
				}

				wk_rs->statement = upd_del_statement;
			}
			else {
				spdlog::error("No unique key found on table while trying to update a cursor row (cursor: {}, table: {}", cursor_name, table_name);
				return DBERR_SQL_ERROR;
			}
		}
		else {
			wk_rs = std::make_shared<SQLiteStatementData>();
			rc = sqlite3_prepare_v2(connaddr, query.c_str(), query.size(), &wk_rs->statement, nullptr);
		}

		if (sqliteRetrieveError(rc) != SQLITE_OK) {
			spdlog::error("Prepare error: {} ({}) - {}", last_rc, last_state, last_error);
			return DBERR_SQL_ERROR;
		}
	}
	else {
		wk_rs = prep_stmt_data;	// Already prepared
	}

	int step_rc = sqlite3_step(wk_rs->statement);

	// we trap COMMIT/ROLLBACK
	if (owner->getConnectionOptions()->autocommit == AutoCommitMode::Off && is_tx_termination_statement(query)) {

		// we clean up: if the COMMIT/ROLLBACK failed this is probably useless anyway
		if (current_statement_data) {
			current_statement_data = nullptr;
		}

		if (sqliteRetrieveError(step_rc) == SQLITE_OK) {	// COMMIT/ROLLBACK succeeded, we try to start a new transaction
			lib_logger->trace(FMT_FILE_FUNC "autocommit mode is disabled, trying to start a new transaction", __FILE__, __func__);
			step_rc = sqlite3_exec(connaddr, "START TRANSACTION", 0, 0, &err_msg);
			sqliteRetrieveError(step_rc);
			lib_logger->trace(FMT_FILE_FUNC "transaction start result: {} ({})", __FILE__, __func__, last_error, last_state);
			return (step_rc != SQLITE_OK) ? DBERR_SQL_ERROR : DBERR_NO_ERROR;
		}

		// if COMMIT/ROLLBACK failed, the error code/state is already set, it will be handled below
	}

	if (step_rc != SQLITE_DONE && step_rc != SQLITE_ROW) {
		rc = sqliteRetrieveError(step_rc);
		return DBERR_SQL_ERROR;
	}

	if (is_updatable_crsr_stmt) {
		int affected_rows = sqlite3_changes(connaddr);
		if (affected_rows != 1) {
			// Something went wrong, we should have updated/deleted exactly one row
			spdlog::error("UPDATE/DELETE on updatable cursor affected {} rows (expected: 1)", affected_rows);
			last_error =  "UPDATE/DELETE on updatable cursor affected " + std::to_string(affected_rows) + " rows (expected: 1)";
			last_rc = -1;
			last_state = "22000";
			return DBERR_SQL_ERROR;
		}
	}

	if (crsr) {
		crsr->setPrivateData(wk_rs);
	}
	else {
		current_statement_data = wk_rs;
	}

	return DBERR_NO_ERROR;
}


int DbInterfaceSQLite::exec_params(std::string query, int nParams, const std::vector<int>& paramTypes, const std::vector<std::string>& paramValues, const std::vector<int>& paramLengths, const std::vector<int>& paramFormats)
{
	return _sqlite_exec_params(nullptr, query, nParams, paramTypes, paramValues, paramLengths, paramFormats);
}

int DbInterfaceSQLite::_sqlite_exec_params(std::shared_ptr<ICursor> crsr, const std::string query, int nParams, const std::vector<int>& paramTypes, const std::vector<std::string>& paramValues, const std::vector<int>& paramLengths, const std::vector<int>& paramFormats, std::shared_ptr<SQLiteStatementData> prep_stmt_data)
{
	int rc = 0;
	bool is_delete = false;
	bool is_updatable_crsr_stmt = false;
	std::string cursor_name, table_name, update_query;
	std::vector<std::string> unique_key;
	std::vector<std::string> key_params;
	sqlite3_stmt* upd_del_statement = nullptr;
	char* err_msg = 0;
	uint32_t nquery_cols = 0;
	lib_logger->trace(FMT_FILE_FUNC "SQL: #{}#", __FILE__, __func__, query);

	std::shared_ptr<SQLiteStatementData> wk_rs;

	if (!prep_stmt_data) {
		wk_rs = (crsr != nullptr) ? std::static_pointer_cast<SQLiteStatementData>(crsr->getPrivateData()) : current_statement_data;

		if (wk_rs) {
			if (wk_rs && wk_rs == current_statement_data) {
				current_statement_data.reset();
			}
		}

		wk_rs = std::make_shared<SQLiteStatementData>();

		if (updatable_cursors_emu && is_update_or_delete_where_current_of(query, table_name, cursor_name, &is_delete)) {

			is_updatable_crsr_stmt = true;

			// No cursor was passed, we need to find it
			if (cursor_name.empty() || this->_declared_cursors.find(cursor_name) == this->_declared_cursors.end() || !this->_declared_cursors[cursor_name]) {
				spdlog::error("Cannot find updatable cursor {}", cursor_name);
				return DBERR_SQL_ERROR;
			}

			std::shared_ptr<ICursor> updatable_crsr = this->_declared_cursors[cursor_name];

			if (has_unique_key(table_name, updatable_crsr, unique_key)) {
				std::string new_query;

				if (!prepare_updatable_cursor_query(query, updatable_crsr, unique_key, &upd_del_statement, key_params) || !upd_del_statement) {
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
			wk_rs = std::make_shared<SQLiteStatementData>();
			rc = sqlite3_prepare_v2(connaddr, query.c_str(), query.size(), &wk_rs->statement, nullptr);
		}

		if (sqliteRetrieveError(rc) != SQLITE_OK) {
			spdlog::error("Prepare error: {} ({}) - {}", last_rc, last_state, last_error);
			return DBERR_SQL_ERROR;
		}
	}
	else {
		wk_rs = prep_stmt_data;	// Already prepared
	}

	for (int i = 0; i < nParams; i++) {

		int rc = sqlite3_bind_text(wk_rs->statement, i + 1, paramValues.at(i).c_str(), paramValues.at(i).size(), SQLITE_TRANSIENT);
		if (sqliteRetrieveError(rc))
			return DBERR_SQL_ERROR;
	}

	// This will only be used if we are performing an update/delete on an updatable cursor
	for (int i = 0; i < key_params.size(); i++) {

		int rc = sqlite3_bind_text(wk_rs->statement, nParams + i + 1, key_params.at(i).c_str(), key_params.at(i).size(), SQLITE_TRANSIENT);
		if (sqliteRetrieveError(rc) != SQLITE_OK) {
			lib_logger->error("SQLite: Error while binding parameter definitions (# {}): {} ({}): {}", i + 1, last_rc, last_state, last_error);
			return false;
		}
	}

	int step_rc = sqlite3_step(wk_rs->statement);

	// we trap COMMIT/ROLLBACK
	if (owner->getConnectionOptions()->autocommit == AutoCommitMode::Off && is_tx_termination_statement(query)) {

		// we clean up: if the COMMIT/ROLLBACK failed this is probably useless anyway
		if (current_statement_data) {
			current_statement_data.reset();
		}

		if (sqliteRetrieveError(step_rc) == SQLITE_OK) {	// COMMIT/ROLLBACK succeeded, we try to start a new transaction
			lib_logger->trace(FMT_FILE_FUNC "autocommit mode is disabled, trying to start a new transaction", __FILE__, __func__);
			step_rc = sqlite3_exec(connaddr, "START TRANSACTION", 0, 0, &err_msg);
			sqliteRetrieveError(step_rc);
			lib_logger->trace(FMT_FILE_FUNC "transaction start result: {} ({})", __FILE__, __func__, last_error, last_state);
			return (step_rc != SQLITE_OK) ? DBERR_SQL_ERROR : DBERR_NO_ERROR;
		}

		// if COMMIT/ROLLBACK failed, the error code/state is already set, it will be handled below
	}

	if (step_rc != SQLITE_DONE && step_rc != SQLITE_ROW) {
		rc = sqliteRetrieveError(step_rc);
		return DBERR_SQL_ERROR;
	}

	if (is_updatable_crsr_stmt) {
		int affected_rows = sqlite3_changes(connaddr);
		if (affected_rows != 1) {
			// Something went wrong, we should have updated/deleted exactly one row
			spdlog::error("UPDATE/DELETE on updatable cursor affected {} rows (expected: 1)", affected_rows);
			last_error = "UPDATE/DELETE on updatable cursor affected " + std::to_string(affected_rows) + " rows (expected: 1)";
			last_rc = -1;
			last_state = "22000";
			return DBERR_SQL_ERROR;
		}
	}


	if (crsr) {
		crsr->setPrivateData(wk_rs);
	}
	else {
		current_statement_data = wk_rs;

	}

	return DBERR_NO_ERROR;
}

bool DbInterfaceSQLite::is_cursor_from_prepared_statement(ICursor* cursor)
{
	//std::string squery = cursor->getQuery();
	//void* src_addr = nullptr;
	//int src_len = 0;

	//if (squery.size() == 0) {
	//	cursor->getQuerySource(&src_addr, &src_len);
	//	squery = __get_trimmed_hostref_or_literal(src_addr, src_len);
	//}

	//trim(squery);
	//squery = to_lower(squery);

	//return squery.size() > 1 && starts_with(squery, "@") && _prepared_stmts.find(squery.substr(1)) != _prepared_stmts.end();
	return false;
}

#define CHECK_UNIQUE_KEY_ERR(_C) 	if (sqliteRetrieveError(rc) != _C) { \
		lib_logger->error("Could not extract unique key data for table {} ({})", table_name, err_idx++); \
			return false; \
		}

bool DbInterfaceSQLite::has_unique_key(std::string table_name, const std::shared_ptr<ICursor>& crsr, std::vector<std::string>& unique_key)
{
	int err_idx = 1;
	std::string q1 = "SELECT name, case when il.\"origin\" = 'pk' then 0 else 1 end a FROM pragma_index_list(?) il where \"unique\"=1 order by a";

	sqlite3_stmt* idxs_stmt = nullptr, *key_stmt = nullptr;

	int rc = sqlite3_prepare_v2(connaddr, q1.c_str(), q1.size(), &idxs_stmt, 0);
	CHECK_UNIQUE_KEY_ERR(SQLITE_OK)

	rc = sqlite3_bind_text(idxs_stmt, 1, table_name.c_str(), table_name.size(), SQLITE_TRANSIENT);
	CHECK_UNIQUE_KEY_ERR(SQLITE_OK)

	rc = sqlite3_step(idxs_stmt);
	CHECK_UNIQUE_KEY_ERR(SQLITE_ROW)

	if (sqlite3_data_count(idxs_stmt) <= 0) {
		lib_logger->error("Could not extract unique key data for table {} ({})", table_name, err_idx++);
		return false;
	}

	std::string index_name = (char *)sqlite3_column_text(idxs_stmt, 0);

	rc = sqlite3_finalize(idxs_stmt);
	CHECK_UNIQUE_KEY_ERR(SQLITE_OK)

	// we got an index, now we need its columns

	q1 = "SELECT name FROM pragma_index_info(?) order by seqno";

	rc = sqlite3_prepare_v2(connaddr, q1.c_str(), q1.size(), &key_stmt, 0);
	CHECK_UNIQUE_KEY_ERR(SQLITE_OK)

	rc = sqlite3_bind_text(key_stmt, 1, index_name.c_str(), index_name.size(), SQLITE_TRANSIENT);
	CHECK_UNIQUE_KEY_ERR(SQLITE_OK)

	rc = sqlite3_step(key_stmt);
	CHECK_UNIQUE_KEY_ERR(SQLITE_ROW)

	if (sqlite3_data_count(key_stmt) <= 0) {
		lib_logger->error("Could not extract unique key data for table {} ({})", table_name, err_idx++);
		return false;
	}

	do {
		std::string col = (char*)sqlite3_column_text(key_stmt, 0);
		unique_key.push_back(to_upper(col));
		rc = sqlite3_step(key_stmt);
	} while (rc == SQLITE_ROW);

	rc = sqlite3_finalize(key_stmt);
	CHECK_UNIQUE_KEY_ERR(SQLITE_OK)

	return true;
}

bool DbInterfaceSQLite::prepare_updatable_cursor_query(const std::string& qry, const std::shared_ptr<ICursor>& crsr, const std::vector<std::string>& unique_key, sqlite3_stmt** update_stmt, std::vector<std::string>& key_params)
{
	if (!crsr || !crsr->getQuery().size())
		return false;

	auto rs = std::dynamic_pointer_cast<SQLiteStatementData>(crsr->getPrivateData());
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
		where_clause += " AND " + unique_key.at(i) + " = $" + std::to_string(i + 1);
	}

	tqry += where_clause;

	sqlite3_stmt* upd_stmt = nullptr;

	lib_logger->trace("Preparing updatable cursor query for {}: {}", crsr->getName(), tqry);

	int rc = sqlite3_prepare_v2(connaddr, tqry.c_str(), tqry.size(), &upd_stmt, nullptr);
	if (sqliteRetrieveError(rc) != SQLITE_OK) {
		lib_logger->error("Could not prepare updatable cursor query for {}", crsr->getName());
		return false;
	}

	key_params.clear();
	for (int i = 0; i < unique_key.size(); i++) {

		std::vector<std::string>::iterator itr = std::find(crsr_cols.begin(), crsr_cols.end(), unique_key.at(i));
		if (itr == crsr_cols.cend()) {
			return false;
		}

		int col_idx = std::distance(crsr_cols.begin(), itr);

		std::string col_data = (char*)sqlite3_column_text(rs->statement, col_idx);
		key_params.push_back(col_data);
	}

	*update_stmt = upd_stmt;

	return true;
}

std::vector<std::string> DbInterfaceSQLite::get_resultset_column_names(sqlite3_stmt* stmt)
{
	std::vector<std::string> crsr_cols;
	for (int i = 0; i < sqlite3_column_count(stmt); i++) {
		const char* c = sqlite3_column_name(stmt, i);
		crsr_cols.push_back(to_upper(c));
	}
	return crsr_cols;
}

int DbInterfaceSQLite::close_cursor(const std::shared_ptr<ICursor>& crsr)
{
	//if (!cursor) {
	//	lib_logger->error("Invalid cursor reference");
	//	return DBERR_CLOSE_CURSOR_FAILED;
	//}

	//// Prepared statements used for cursors will be disposed separately
	//if (!is_cursor_from_prepared_statement(cursor)) {
	//	SQLiteStatementData* dp = (SQLiteStatementData*)cursor->getPrivateData();

	//	if (!dp || !dp->statement)
	//		return DBERR_CLOSE_CURSOR_FAILED;

	//	int rc = dpiStmt_release(dp->statement);
	//	dp->statement = nullptr;
	//	if (dpiRetrieveError(rc) != DPI_SUCCESS) {
	//		return DBERR_CLOSE_CURSOR_FAILED;
	//	}

	//	delete dp;
	//}

	//cursor->setPrivateData(nullptr);
	//cursor->setOpened(false);

	return DBERR_NO_ERROR;
}

int DbInterfaceSQLite::cursor_declare(const std::shared_ptr<ICursor>& cursor, bool, int)
{
	if (!cursor)
		return DBERR_DECLARE_CURSOR_FAILED;

	std::map<std::string, std::shared_ptr<ICursor>>::iterator it = _declared_cursors.find(cursor->getName());
	if (it == _declared_cursors.end()) {
		_declared_cursors[cursor->getName()] = cursor;
	}

	return DBERR_NO_ERROR;
}

int DbInterfaceSQLite::cursor_declare_with_params(const std::shared_ptr<ICursor>& cursor, char**, bool, int)
{
	if (!cursor)
		return DBERR_DECLARE_CURSOR_FAILED;

	std::map<std::string, std::shared_ptr<ICursor>>::iterator it = _declared_cursors.find(cursor->getName());
	if (it == _declared_cursors.end()) {
		_declared_cursors[cursor->getName()] = cursor;
	}

	return DBERR_NO_ERROR;
}

int DbInterfaceSQLite::cursor_open(const std::shared_ptr<ICursor>& cursor)
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

	std::shared_ptr<SQLiteStatementData> prepared_stmt_data;
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

	// If updatable cursion emulation is enabled and this is an updatable cursor, we transform the query
	if (updatable_cursors_emu) {
		squery = string_replace(squery, "\n", " ");
		squery = string_replace(squery, "\r", " ");
		squery = string_replace(squery, "\t", " ");
		trim(squery);
		if (ends_with(to_upper(squery), "FOR UPDATE")) {
			squery = squery.substr(0, squery.size() - 10);
		}
	}
	
	if (cursor->getNumParams() > 0) {
		std::vector<std::string> params = cursor->getParameterValues();
		std::vector<int> param_types = cursor->getParameterTypes();
		std::vector<int> param_lengths = cursor->getParameterLengths();
		rc = _sqlite_exec_params(cursor, squery, cursor->getNumParams(), param_types, params, param_lengths, param_types, prepared_stmt_data);
	}
	else {
		rc = _sqlite_exec(cursor, squery, prepared_stmt_data);
	}

	if (sqliteRetrieveError(rc) == SQLITE_OK) {

		// SQLite automatically positions it on the first row, so we need a mechanism to avoid fetching (and skipping) it
		std::shared_ptr<SQLiteStatementData> wk_rs = std::dynamic_pointer_cast<SQLiteStatementData>(cursor->getPrivateData());
		wk_rs->_on_first_row = true;

		return DBERR_NO_ERROR;
	}
	else {
		return DBERR_OPEN_CURSOR_FAILED;
	}
}

int DbInterfaceSQLite::fetch_one(const std::shared_ptr<ICursor>& cursor, int)
{
	lib_logger->trace(FMT_FILE_FUNC "mode: {}", __FILE__, __func__, FETCH_NEXT_ROW);

	if (!owner) {
		lib_logger->error("Invalid connection reference");
		return DBERR_CONN_NOT_FOUND;
	}

	if (!cursor) {
		lib_logger->error("Invalid cursor reference");
		return DBERR_FETCH_ROW_FAILED;
	}

	lib_logger->trace(FMT_FILE_FUNC "owner id: {}, cursor name: {}, mode: {}", __FILE__, __func__, owner->getId(), cursor->getName(), FETCH_NEXT_ROW);

	std::shared_ptr<SQLiteStatementData> dp = std::dynamic_pointer_cast<SQLiteStatementData>(cursor->getPrivateData());

	if (!dp || !dp->statement)
		return DBERR_FETCH_ROW_FAILED;

	// SQLite automatically positions it on the first row, so we need a mechanism to avoid fetching (and skipping) it
	if (dp->_on_first_row) {
		dp->_on_first_row = false;
	}
	else {
		int step_rc = sqlite3_step(dp->statement);
		if (step_rc != SQLITE_DONE && step_rc != SQLITE_ROW) {
			sqliteRetrieveError(step_rc);
			return DBERR_SQL_ERROR;
		}

		if (step_rc == SQLITE_DONE)
			return DBERR_NO_DATA;
	}

	return DBERR_NO_ERROR;
}

bool DbInterfaceSQLite::get_resultset_value(ResultSetContextType resultset_context_type, const IResultSetContextData& context, int row, int col, char* bfr, int bfrlen, int* value_len)
{
	int rc = 0;
	std::shared_ptr<SQLiteStatementData> wk_rs;

	switch (resultset_context_type) {
	case ResultSetContextType::CurrentResultSet:
		wk_rs = current_statement_data;
		break;

	case ResultSetContextType::PreparedStatement:
	{
		PreparedStatementContextData& p = (PreparedStatementContextData&)context;

		std::string stmt_name = p.prepared_statement_name;
		stmt_name = to_lower(stmt_name);
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
		wk_rs = std::dynamic_pointer_cast<SQLiteStatementData>(c->getPrivateData());
	}
	break;
	}

	if (!wk_rs) {
		lib_logger->error("Invalid resultset");
		return false;
	}

	const unsigned char* c = sqlite3_column_text(wk_rs->statement, col);
	if (c) {
		int l = strlen((const char*)c);

		if (l > bfrlen) {
			return false;
		}

		*value_len = l;
		memcpy(bfr, c, l);
		bfr[l] = '\0';
	}
	else
		bfr[0] = '\0';

	return true;
}

bool DbInterfaceSQLite::move_to_first_record(std::string stmt_name)
{
	std::shared_ptr<SQLiteStatementData> dp = nullptr;

	if (stmt_name.empty()) {
		if (!current_statement_data) {
			sqliteSetError(DBERR_MOVE_TO_FIRST_FAILED, "HY000", "Invalid statement reference");
			return DBERR_MOVE_TO_FIRST_FAILED;
		}

		dp = current_statement_data;
	}
	else {
		stmt_name = to_lower(stmt_name);
		if (_prepared_stmts.find(stmt_name) == _prepared_stmts.end()) {
			sqliteSetError(DBERR_MOVE_TO_FIRST_FAILED, "HY000", "Invalid statement reference");
			return false;
		}
		dp = _prepared_stmts[stmt_name];
	}

	if (!dp || !dp->statement) {
		sqliteSetError(DBERR_MOVE_TO_FIRST_FAILED, "HY000", "Invalid statement reference");
		return false;
	}

	int nrows = sqlite3_data_count(dp->statement);
	if (nrows <= 0) {
		sqliteSetError(DBERR_NO_DATA, "02000", "No data");
		return false;
	}

	return true;
}

uint64_t DbInterfaceSQLite::get_native_features()
{
	return (uint64_t)0;
}


int DbInterfaceSQLite::get_num_rows(const std::shared_ptr<ICursor>& crsr)
{
	return -1;
}

int DbInterfaceSQLite::get_num_fields(const std::shared_ptr<ICursor>& crsr)
{
	sqlite3_stmt* wk_rs = nullptr;

	if (crsr) {
		std::shared_ptr<SQLiteStatementData> p = std::dynamic_pointer_cast<SQLiteStatementData>(crsr->getPrivateData());
		wk_rs = p->statement;
	}
	else {
		if (!current_statement_data)
			return -1;

		wk_rs = current_statement_data->statement;
	}

	if (wk_rs) {
		return sqlite3_column_count(wk_rs);
	}
	else
		return -1;
}

int DbInterfaceSQLite::_sqlite_get_num_rows(sqlite3_stmt* r)
{
	return -1;
}

std::shared_ptr<SQLiteStatementData>  DbInterfaceSQLite::retrieve_prepared_statement(const std::string& prep_stmt_name)
{
	std::string stmt_name = to_lower(prep_stmt_name);
	if (_prepared_stmts.find(stmt_name) == _prepared_stmts.end() || _prepared_stmts[stmt_name] == nullptr || _prepared_stmts[stmt_name]->statement == nullptr)
		return false;

	return _prepared_stmts[stmt_name];
}

int DbInterfaceSQLite::sqliteRetrieveError(int rc)
{
	char bfr[1024];

	if (rc != SQLITE_OK) {
		int ext_err = sqlite3_extended_errcode(connaddr);
		const char* err_msg = sqlite3_errmsg(connaddr);
		sprintf(bfr, "ERROR: %s", err_msg);

		last_error = bfr;
		last_rc = (rc > 0) ? -rc : rc;

		last_state = "HY000";	// TODO: fix this
	}
	else {
		sqliteClearError();
	}

	return rc;
}

void DbInterfaceSQLite::sqliteClearError()
{
	last_error = "";
	last_rc = DBERR_NO_ERROR;
	last_state = "00000";
}

void DbInterfaceSQLite::sqliteSetError(int err_code, std::string sqlstate, std::string err_msg)
{
	last_error = err_msg;
	last_rc = err_code;
	last_state = sqlstate;
}

SQLiteStatementData::SQLiteStatementData()
{
	this->params_count = 0;
	this->coldata_count = 0;
}

SQLiteStatementData::~SQLiteStatementData()
{
	cleanup();
	if (statement) {
		sqlite3_finalize(statement);
		statement = nullptr;
	}
}

void SQLiteStatementData::resizeParams(int n)
{
	cleanup();

	params_count = n;
}

void SQLiteStatementData::resizeColumnData(int n)
{
	coldata_count = n;
}

void SQLiteStatementData::cleanup()
{
	params_count = 0;
	coldata_count = 0;
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

static std::string sqlite_fixup_parameters(const std::string& sql)
{
#if 0
	int n = 1;
	bool in_single_quoted_string = false;
	bool in_double_quoted_string = false;
	std::string out_sql;

	for (auto itc = sql.begin(); itc != sql.end(); ++itc) {
		char c = *itc;

		switch (c) {
		case '"':
			out_sql += c;
			in_double_quoted_string = !in_double_quoted_string;
			continue;

		case '\'':
			out_sql += c;
			in_single_quoted_string = !in_single_quoted_string;
			continue;

		case '$':	// :1 is valid in SQLite, so we just change the prefix
			out_sql += ':';
			continue;

		case '?':
			if (!in_single_quoted_string && !in_double_quoted_string)
				out_sql += (":" + std::to_string(n++));
			else
				out_sql += c;
			continue;

		default:
			out_sql += c;

}
	}

	return out_sql;
#else
	return sql;
#endif
}
