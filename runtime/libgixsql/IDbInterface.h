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
#include <variant>
#include <memory>

#include "ICursor.h"
#include "Logger.h"
#include "IConnection.h"
#include "IDataSourceInfo.h"
#include "IConnectionOptions.h"
#include "IDbManagerInterface.h"
#include "IResultSetContextData.h"
#include "cobol_var_types.h"

using std_binary_data = std::vector<unsigned char>;

#define USE_DEFAULT_CONNECTION		-998

#define DBERR_NO_ERROR				0
#define DBERR_CONNECTION_FAILED		-100
#define DBERR_BEGIN_TX_FAILED		-101
#define DBERR_END_TX_FAILED			-102
#define DBERR_CONN_NOT_FOUND		-103
#define DBERR_CONN_RESET_FAILED		-104
#define DBERR_EMPTY_QUERY			-105
#define DBERR_SQL_ERROR				-106
#define DBERR_TOO_MANY_ARGUMENTS	-107
#define DBERR_TOO_FEW_ARGUMENTS		-108
#define DBERR_NO_PARAMETERS			-109
#define DBERR_CURSOR_EXISTS			-110
#define DBERR_NO_SUCH_CURSOR		-111
#define DBERR_CLOSE_CURSOR_FAILED	-112
#define DBERR_DISCONNECT_FAILED		-113
#define DBERR_OUT_OF_MEMORY			-114
#define DBERR_DECLARE_CURSOR_FAILED	-115
#define DBERR_OPEN_CURSOR_FAILED	-116
#define DBERR_FETCH_ROW_FAILED		-117
#define DBERR_INVALID_COLUMN_DATA	-118
#define DBERR_CURSOR_CLOSED			-119
#define DBERR_MOVE_TO_FIRST_FAILED	-120
#define DBERR_FIELD_COUNT_MISMATCH	-121
#define DBERR_NO_DATA				-122
#define DBERR_TOO_MUCH_DATA			-123
#define DBERR_PREPARE_FAILED		-124

#define DBERR_CONN_INIT_ERROR		-201
#define DBERR_CONN_INVALID_DBTYPE	-202

#define DBERR_NUM_OUT_OF_RANGE		-410

#define DBERR_INTERNAL_ERR			-8880

#define DBERR_NOT_IMPL				-9990

#define FETCH_NEXT_ROW	1
#define FETCH_PREV_ROW	2
#define FETCH_CUR_ROW	3

#define NO_REC_CODE_DEFAULT	100

#define RS_CTX_CURRENT_RESULTSET	1
#define RS_CTX_PREPARED_STATEMENT	2
#define RS_CTX_CURSOR				3

class IDataSourceInfo;
class IConnection;
class ICursor;
struct IConnectionOptions;

enum class ResultSetContextType {
	CurrentResultSet	= 1,
	PreparedStatement	= 2,
	Cursor				= 3
};

enum class DbNativeFeature : uint64_t {
	// autocommit is supported by the DBMS, can be switched on/off
	AutoCommitSupport	= 1,

	// autocommit is enabled by default for new connections
	AutoCommitDefaultOn	= 1 << 1,

	// has native updatable cursors
	UpdatableCursors	= 1 << 2,

	// resultsets include row count 
	ResultSetRowCount	= 1 << 3
};

enum class DbProperty {
	AutoCommitMode = 1
};

enum class DbPropertySetResult {
	Success = 0,
	Failure = 1,
	Unsupported = 2
};

class IDbInterface
{
public:
	virtual ~IDbInterface() { }

	virtual int init(const std::shared_ptr<spdlog::logger>& _logger) = 0;
	virtual int connect(const std::shared_ptr<IDataSourceInfo>&, const std::shared_ptr<IConnectionOptions>&) = 0;
	virtual int reset() = 0;
	virtual int terminate_connection() = 0;
	virtual int exec(std::string) = 0;
	virtual int exec_params(const std::string& query, const std::vector<CobolVarType>& paramTypes, const std::vector<std_binary_data>& paramValues, const std::vector<unsigned long>& paramLengths, const std::vector<uint32_t>& paramFlags) = 0;
	virtual int cursor_declare(const std::shared_ptr<ICursor>& crsr) = 0;
	virtual int cursor_open(const std::shared_ptr<ICursor>& crsr) = 0;
	virtual int cursor_close(const std::shared_ptr<ICursor>& crsr) = 0;
	virtual int cursor_fetch_one(const std::shared_ptr<ICursor>& crsr, int) = 0;
	virtual bool get_resultset_value(ResultSetContextType resultset_context_type, const IResultSetContextData& context, int row, int col, char* bfr, uint64_t bfrlen, uint64_t* value_len) = 0;
	virtual bool move_to_first_record(const std::string& stmt_name = "") = 0;
	virtual uint64_t get_native_features() = 0;
	virtual int get_num_rows(const std::shared_ptr<ICursor>& crsr) = 0;
	virtual int get_num_fields(const std::shared_ptr<ICursor>& crsr) = 0;
	virtual const char *get_error_message() = 0;
	virtual int get_error_code() = 0;
	virtual std::string get_state() = 0;
	virtual void set_owner(std::shared_ptr<IConnection>) = 0;
	virtual std::shared_ptr<IConnection> get_owner() = 0;
	virtual int prepare(const std::string& stmt_name, const std::string& query) = 0;
	virtual int exec_prepared(const std::string& stmt_name, std::vector<CobolVarType> paramTypes, std::vector<std_binary_data> &paramValues, std::vector<unsigned long> paramLengths, const std::vector<uint32_t>& paramFlags) = 0;
	virtual DbPropertySetResult set_property(DbProperty p, std::variant<bool, int, std::string> v) = 0;

	IDbManagerInterface* manager()
	{
		return dynamic_cast<IDbManagerInterface*>(this);
	}

	bool has(DbNativeFeature f)
	{
		return get_native_features() & ((uint64_t)f);
	}

protected:

	std::shared_ptr<IConnection> owner;
	std::shared_ptr<spdlog::logger> lib_logger;
};

