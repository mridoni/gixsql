#pragma once

#include <string>
#include <map>
#include <memory>

#include "ESQLDefinitions.h"
#include "GixEsqlLexer.hh"
#include "cobol_var_types.h"

#define ESQL_CONNECT					"CONNECT"
#define ESQL_CONNECT_RESET				"CONNECT_RESET"
#define ESQL_DISCONNECT					"DISCONNECT"
#define ESQL_CLOSE						"CLOSE"
#define ESQL_COMMIT						"COMMIT"
#define ESQL_ROLLBACK					"ROLLBACK"
#define ESQL_FETCH						"FETCH"
#define ESQL_INCFILE					"INCFILE"
#define ESQL_INCSQLCA					"INCSQLCA"
#define ESQL_INSERT						"INSERT"
#define ESQL_OPEN						"OPEN"
#define ESQL_SELECT						"SELECT"
#define ESQL_UPDATE						"UPDATE"
#define ESQL_DELETE						"DELETE"
#define ESQL_WORKING_BEGIN				"WORKING_BEGIN"
#define ESQL_WORKING_END				"WORKING_END"
#define ESQL_FILE_BEGIN					"FILE_BEGIN"
#define ESQL_FILE_END					"FILE_END"
#define ESQL_LINKAGE_BEGIN				"LINKAGE_BEGIN"
#define ESQL_LINKAGE_END				"LINKAGE_END"
#define ESQL_PROCEDURE_DIVISION			"PROCEDURE_DIVISION"
#define ESQL_DECLARE_TABLE				"DECLARE_TABLE"
#define ESQL_DECLARE_VAR				"DECLARE_VAR"
#define ESQL_COMMENT					"COMMENT"
#define ESQL_IGNORE						"IGNORE"
#define ESQL_PREPARE					"PREPARE_STATEMENT"
#define ESQL_EXEC_PREPARED				"EXECUTE_PREPARED"
#define ESQL_EXEC_IMMEDIATE				"EXECUTE_IMMEDIATE"
#define ESQL_PASSTHRU					"PASSTHRU"
#define ESQL_WHENEVER					"WHENEVER"

using sql_type_info_t = std::tuple<uint64_t, int, int, std::string>;

enum class ESQL_Command
{
	Connect,
	ConnectReset,
	Disconnect,
	Close,
	Commit,
	Rollback,
	Fetch,
	Incfile,
	IncSQLCA,
	Insert,
	Open,
	Select,
	Update,
	Delete,
	WorkingBegin,
	WorkingEnd,
	FileBegin,
	FileEnd,
	LinkageBegin,
	LinkageEnd,
	ProcedureDivision,
	DeclareTable,
	PrepareStatement,
	ExecPrepared,
	ExecImmediate,
	Whenever,

	// Helpers
	StartSQL,
	EndSQL,

	BeginDeclareSection,
	EndDeclareSection,

	// Fields declaration
	DeclareVar,

	// Comment code ranges (also comment everything between EXEC SQL IGNORE and END-EXEC)
	Comment,

	// Comment code ranges (leave anything between EXEC SQL IGNORE and END-EXEC uncommented)
	Ignore,

	// ESQL is passed directly to the database driver (unknow statements, DBMS-specific syntax, etc.)
	PassThru,

	Unknown
};


enum class ESQL_ParameterStyle {
	DollarPrefix,
	ColonPrefix,
	Anonymous,
	Unknown
};

struct ESQLJobParams
{
	ESQL_ParameterStyle opt_params_style;
	bool opt_preprocess_copy_files;
	bool opt_emit_static_calls;
	bool opt_emit_debug_info;
	bool opt_emit_compat;
	bool opt_consolidated_map;
	bool opt_no_output;
	bool opt_emit_map_file;
	bool opt_emit_cobol85;
	bool opt_picx_as_varchar;
	int opt_norec_sqlcode = 100;
	std::string opt_varlen_suffix_len;
	std::string opt_varlen_suffix_data;
};

class ESQLParserData
{
public:

	ESQLParserData();

	bool field_exists(const std::string& f);
	std::map<std::string, cb_field_ptr>& get_field_map() const;
	void add_to_field_map(std::string k, cb_field_ptr f);
	cb_field_ptr field_map(std::string k);

	sql_type_info_t field_sql_type_info(const std::string& n);
	std::map<std::string, sql_type_info_t> field_sql_type_info() const;

	void field_sql_type_info_add(const std::string& n, sql_type_info_t t);

	std::shared_ptr<ESQLJobParams> job_params() const;
	std::vector<cb_exec_sql_stmt_ptr>* exec_list() const;

	std::string program_id();
	void set_program_id(std::string id);

	std::map<std::string, srcLocation>& paragraphs();
	void paragraph_add(std::string, srcLocation p);

	bool get_actual_field_data(cb_field_ptr f, CobolVarType* type, int* size, int* scale);

private:
	std::map<std::string, std::tuple<uint64_t, int, int, std::string>> _field_sql_type_info;

	std::map<std::string, srcLocation> _paragraphs;

	std::string _program_id;
	std::vector<cb_exec_sql_stmt_ptr>* _exec_list;
	std::map<std::string, cb_field_ptr> _field_map;

	std::shared_ptr<ESQLJobParams> _job_params;

};

