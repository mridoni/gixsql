/*
This file is part of Gix-IDE, an IDE and platform for GnuCOBOL
Copyright (C) 2021,2022 Marco Ridoni

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

#include "TPESQLProcessor.h"
#include "TPESQLCommon.h"
#include "ESQLCall.h"
#include "gix_esql_driver.hh"
#include "MapFileWriter.h"
#include "libcpputils.h"
#include "limits.h"
#include "linq/linq.hpp"

#include "cobol_var_types.h"
#include "varlen_defs.h"

#if defined(_WIN32) && defined(_DEBUG)
#include <Windows.h>
#endif

#define BEGIN_DECLARE_SECTION			"HOST_BEGIN"
#define END_DECLARE_SECTION				"HOST_END"

#define AREA_A_PREFIX       "       " // 7 spaces
#define AREA_A_CPREFIX       "GIXSQL " // comment +  7 spaces
#define AREA_B_PREFIX       "           " // 11 spaces
#define AREA_B_CPREFIX      "GIXSQL     " // comment + 5 spaces
#define TAB					"    "		  // 4 spaces

#define PIC_ALPHABETIC 		0x01
#define PIC_NUMERIC 		0x02
#define PIC_NATIONAL		0x04
#define PIC_ALPHANUMERIC	(PIC_ALPHABETIC | PIC_NUMERIC)

#define CBL_FIELD_FLAG_NONE		(uint32_t)0x0
#define CBL_FIELD_FLAG_VARLEN	(uint32_t)0x80
#define CBL_FIELD_FLAG_BINARY	(uint32_t)0x100

#define CBL_FIELD_FLAG_AUTOTRIM	(uint32_t)0x200

#define MAP_FILE_FMT_VER ((uint16_t) 0x0100)
#define FLAG_M_BASE					0

#define ERR_NOTDEF_CONVERSION -1

#define DEFAULT_VARLEN_SUFFIX_DATA		"ARR"
#define DEFAULT_VARLEN_SUFFIX_LENGTH	"LEN"

#define SQL_QUERY_BLOCK_SIZE	8191

#define DEFAULT_NO_REC_CODE	100

#define HAS_INDICATOR(_V) (_V.find(":") != std::string::npos && _V.find(":") == std::string::npos)
#define ASSERT_NO_INDICATOR(_V,_F,_L) 		if (HAS_INDICATOR(_V)) { \
										raise_error("Invalid null indicator reference: " + _V, ERR_INVALID_NULLIND_REF, _F, _L); \
										return false; \
									}

static bool check_sql_type_compatibility(uint64_t type_info, cb_field_ptr var);

static std::map<std::string, ESQL_Command> ESQL_cmd_map{ { ESQL_CONNECT, ESQL_Command::Connect }, { ESQL_CONNECT_RESET, ESQL_Command::ConnectReset },
												 { ESQL_DISCONNECT, ESQL_Command::Disconnect }, { ESQL_CLOSE, ESQL_Command::Close },
												 { ESQL_COMMIT, ESQL_Command::Commit }, { ESQL_ROLLBACK, ESQL_Command::Rollback },
												 { ESQL_FETCH, ESQL_Command::Fetch }, { ESQL_DELETE, ESQL_Command::Delete },
												 { ESQL_INCFILE, ESQL_Command::Incfile }, { ESQL_INCSQLCA, ESQL_Command::IncSQLCA }, { ESQL_INSERT, ESQL_Command::Insert },
												 { ESQL_OPEN, ESQL_Command::Open }, { ESQL_SELECT, ESQL_Command::Select }, { ESQL_UPDATE, ESQL_Command::Update },
												 { ESQL_WORKING_BEGIN, ESQL_Command::WorkingBegin }, { ESQL_WORKING_END, ESQL_Command::WorkingEnd } ,
												 { ESQL_LINKAGE_BEGIN, ESQL_Command::LinkageBegin }, { ESQL_LINKAGE_END, ESQL_Command::WorkingEnd } ,
												 { ESQL_FILE_BEGIN, ESQL_Command::FileBegin }, { ESQL_FILE_END, ESQL_Command::FileEnd } ,
												 { ESQL_PROCEDURE_DIVISION, ESQL_Command::ProcedureDivision }, { ESQL_DECLARE_TABLE, ESQL_Command::DeclareTable },
												 { ESQL_PREPARE, ESQL_Command::PrepareStatement }, { ESQL_EXEC_PREPARED, ESQL_Command::ExecPrepared },
												 { ESQL_EXEC_IMMEDIATE, ESQL_Command::ExecImmediate}, { ESQL_WHENEVER, ESQL_Command::Whenever}, { ESQL_DECLARE_VAR, ESQL_Command::DeclareVar },
												 { BEGIN_DECLARE_SECTION, ESQL_Command::BeginDeclareSection}, { END_DECLARE_SECTION, ESQL_Command::EndDeclareSection },
												 { ESQL_COMMENT, ESQL_Command::Comment } , { ESQL_IGNORE, ESQL_Command::Ignore }, { ESQL_PASSTHRU, ESQL_Command::PassThru } };

#define CALL_PREFIX	"GIXSQL"
#define TAG_PREFIX	"GIXSQL"

struct esql_whenever_clause_handler_t {
	int action = WHENEVER_ACTION_CONTINUE;
	std::string host_label;
};

static struct esql_whenever_handler_t
{
	esql_whenever_clause_handler_t not_found;
	esql_whenever_clause_handler_t sqlwarning;
	esql_whenever_clause_handler_t sqlerror;
} esql_whenever_handler;

inline std::string TPESQLProcessor::get_call_id(const std::string s)
{
	return CALL_PREFIX + s;
}

TPESQLProcessor::TPESQLProcessor(GixPreProcessor* gpp) : ITransformationStep(gpp)
{
	this->owner = gpp;
	code_tag = TAG_PREFIX;

}

bool TPESQLProcessor::run(std::shared_ptr<ITransformationStep> prev_step)
{
	// opt_params_style and opt_preprocess_copy_files are set in the parser stage

	parser_data = this->getInput()->parserData();

	parser_data->job_params()->opt_emit_static_calls = std::get<bool>(owner->getOpt("emit_static_calls", false));
	parser_data->job_params()->opt_emit_debug_info = std::get<bool>(owner->getOpt("emit_debug_info", false));
	parser_data->job_params()->opt_emit_compat = std::get<bool>(owner->getOpt("emit_compat", false));
	parser_data->job_params()->opt_consolidated_map = std::get<bool>(owner->getOpt("consolidated_map", false));
	parser_data->job_params()->opt_no_output = std::get<bool>(owner->getOpt("no_output", false));
	parser_data->job_params()->opt_emit_map_file = std::get<bool>(owner->getOpt("emit_map_file", false));
	parser_data->job_params()->opt_emit_cobol85 = std::get<bool>(owner->getOpt("emit_cobol85", false));
	parser_data->job_params()->opt_picx_as_varchar = std::get<bool>(owner->getOpt("picx_as_varchar", false));
	parser_data->job_params()->opt_varying_len_sz_short = std::get<bool>(owner->getOpt("varying_len_sz_short", false));

	auto vsfxs = std::get<std::string>(owner->getOpt("varlen_suffixes", std::string()));
	if (vsfxs.empty()) {
		parser_data->job_params()->opt_varlen_suffix_len = DEFAULT_VARLEN_SUFFIX_LENGTH;
		parser_data->job_params()->opt_varlen_suffix_data = DEFAULT_VARLEN_SUFFIX_DATA;
	}
	else {
		int p = vsfxs.find(",");
		parser_data->job_params()->opt_varlen_suffix_len = vsfxs.substr(0, p);
		parser_data->job_params()->opt_varlen_suffix_data = vsfxs.substr(p + 1);
	}

	parser_data->job_params()->opt_norec_sqlcode = std::get<int>(owner->getOpt("no_rec_code", DEFAULT_NO_REC_CODE));

	output_line = 0;
	working_begin_line = 0;
	working_end_line = 0;
	current_input_line = 0;

	if (parser_data->job_params()->opt_params_style == ESQL_ParameterStyle::Unknown) {
		raise_error("Unsupported or invalid parameter style", ERR_PP_PARAM_ERROR);
		return false;
	}

	// *****************************

	int rc = outputESQL();
	if (rc == 0)
		add_preprocessed_blocks();

	owner->err_data.err_code = rc;

	return rc == 0;
}

TransformationStepDataType TPESQLProcessor::getInputType()
{
	return TransformationStepDataType::ESQLParserData;
}

TransformationStepDataType TPESQLProcessor::getOutputType()
{
	return TransformationStepDataType::Filename;
}

TransformationStepData *TPESQLProcessor::getOutput(std::shared_ptr<ITransformationStep> me)
{
	return output;
}

int TPESQLProcessor::outputESQL()
{
	working_begin_line = 0;
	working_end_line = 0;
	linkage_begin_line = 0;
	linkage_end_line = 0;

	input_file = this->parser_data->parsed_filename();
	output_file = owner->lastStep()->getOutput()->filename();

	if (output_file.empty()) {
		std::string f = filename_change_ext(input_file, ".cbsql");
		output_file = "#" + filename_get_name(f);
	}

	if (!starts_with(output_file, "#") && !file_is_writable(output_file)) {
		raise_error("File is not writable: " + output_file, ERR_FILE_NOT_FOUND);
		return -1;
	}

	input_file_stack.push(filename_clean_path(input_file));

	if (!find_working_storage(&working_begin_line, &working_end_line))
		return -1;


	// LINKAGE SECTION is optional, so we don't check for errors
	find_linkage_section(&linkage_begin_line, &linkage_end_line);

	startup_items = cpplinq::from(*(parser_data->exec_list())).where([](cb_exec_sql_stmt_ptr p) { return p->startup_item != 0; }).to_vector();
	process_sql_query_list();
	if (!fixup_declared_vars()) {
		return -1;
	}


#if defined(_WIN32) && defined(_DEBUG)
	std::vector<cb_exec_sql_stmt_ptr>* p = parser_data->exec_list();
	for (auto e : *p) {
		char bfr[1024];
		sprintf(bfr, "%04d-%04d : %s\n", e->startLine, e->endLine, e->commandName.c_str());
		OutputDebugStringA(bfr);
	}
#endif

	if (!processNextFile())
		return 1;

	// If we are using "smart" cursor initialization, the block containing the initialization code goes at the end of the program
	// otherwise it has already been output at the start of the PROCEDURE DIVISION
	input_file_stack.push(filename_clean_path(input_file));
	if (!put_cursor_declarations()) {
		raise_error("An error occurred while generating ESQL cursor declarations", ERR_CRSR_GEN);
		return 1;
	}
	input_file_stack.pop();


	bool b1 = parser_data->job_params()->opt_no_output ? true : file_write_all_lines(output_file, output_lines);
	bool b2;
	if (parser_data->job_params()->opt_no_output) {
		build_map_data();
		b2 = true;
	}
	else {
		if (parser_data->job_params()->opt_emit_map_file)
			b2 = write_map_file(output_file);
		else {
			build_map_data();
			b2 = true;
		}
	}

	return (b1 && b2) ? 0 : 1;
}


bool TPESQLProcessor::generate_consolidated_map()
{
	//output_line = 0;
	//working_begin_line = 0;
	//working_end_line = 0;
	//working_begin_line = 0;
	//working_end_line = 0;
	//current_input_line = 0;
	//in_to_out.clear();
	//out_to_in.clear();
	//input_file_stack = std::stack<std::string>();
	//input_file_stack.push(filename_clean_path(input_file));

	//parser_data->job_params()->opt_preprocess_copy_files = true;

	//int rc = main_module_driver.parse(owner, input_file);

	//return processNextFile();

	return false;
}


bool TPESQLProcessor::processNextFile()
{
	std::string the_file = input_file_stack.top();
	std::vector<std::string> input_lines = file_read_all_lines(the_file);

#if defined(_WIN32) && defined(_DEBUG) && defined(VERBOSE)
	char bfr[512];
	sprintf(bfr, "Processing file %s\n", the_file.c_str());
	OutputDebugStringA(bfr);
#endif

	if (!input_lines.size()) {
		input_file_stack.pop();
		if (input_file_stack.size() > 0)
			current_file = input_file_stack.top();

		return true;
	}

	std::string f1 = filename_absolute_path(the_file);
	for (int input_line = 1; input_line <= input_lines.size(); input_line++) {
		current_input_line = input_line;

		std::string cur_line = input_lines.at(input_line - 1);

		bool in_ws = (input_line >= working_begin_line) && (input_line <= working_end_line);

		cb_exec_sql_stmt_ptr exec_sql_stmt = find_exec_sql_stmt(f1, input_line);
		if (!exec_sql_stmt) {
			put_output_line(cur_line);
			continue;
		}

		std::string cmdname = exec_sql_stmt->commandName;
		ESQL_Command cmd = map_contains<std::string, ESQL_Command>(ESQL_cmd_map, cmdname) ? ESQL_cmd_map[cmdname] : ESQL_Command::Unknown;

		switch (cmd) {

		case ESQL_Command::WorkingBegin:
		case ESQL_Command::LinkageBegin:
		case ESQL_Command::LinkageEnd:
		case ESQL_Command::FileBegin:
		case ESQL_Command::FileEnd:
			put_output_line(input_lines.at(exec_sql_stmt->startLine - 1));
			break;

		case ESQL_Command::ProcedureDivision:

			// PROCEDURE DIVISION can be string_split across several lines if a USING clause is added
			for (int iline = exec_sql_stmt->startLine; iline <= exec_sql_stmt->endLine; iline++) {
				put_output_line(input_lines.at(iline - 1));
			}
			break;

		case ESQL_Command::Ignore:
		{
			std::vector<std::string> tmp_outlines;
			for (int iline = exec_sql_stmt->startLine; iline <= exec_sql_stmt->endLine; iline++) {
				tmp_outlines.push_back(input_lines.at(iline - 1));
			}
			if (!tmp_outlines.size())
				break;

			std::string txt = vector_join(tmp_outlines, "\n");
			txt = string_replace_regex(txt, "EXEC[\\ \\r\\n]+SQL[\\ \\r\\n]+IGNORE([\\ \\r\\n]+)?", "");
			txt = string_replace_regex(txt, "[\\ \\r\\n]+END-EXEC([\\ ]*\\.)", "");
			tmp_outlines = string_split(txt, "[\\n]");

			put_output_line(code_tag + "*   ESQL IGNORE");
			for (auto tl : tmp_outlines) {
				put_output_line(tl);
			}
			put_output_line(code_tag + "*   ESQL END IGNORE");
		}
		break;

		default:
			// Add original text, commented
			if (current_input_line != working_end_line && current_input_line != linkage_end_line) {
				for (int n = exec_sql_stmt->startLine; n <= exec_sql_stmt->endLine; n++) {
					put_output_line(comment_line("GIXSQL", input_lines.at(n - 1)));
					current_input_line++;
				}
			}
			else {
				put_output_line(input_lines.at(current_input_line - 1));

				current_input_line++;
			}

			// Add ESQL calls
			if (!handle_esql_stmt(cmd, exec_sql_stmt, in_ws)) {
				raise_error("Error in ESQL statement", ERR_ALREADY_SET, exec_sql_stmt->src_file, exec_sql_stmt->startLine);
				return false;
			}
		}

		// Special case
		if (exec_sql_stmt->endLine == working_end_line) {
			if (!handle_esql_stmt(ESQL_Command::WorkingEnd, find_esql_cmd(ESQL_WORKING_END, 0), 0)) {
				raise_error("Error in ESQL statement", ERR_ALREADY_SET, exec_sql_stmt->src_file, exec_sql_stmt->startLine);
				return false;
			}
		}

		// Update input line pointer
		input_line = exec_sql_stmt->endLine;
		current_input_line = input_line;
	}

	input_file_stack.pop();
	if (input_file_stack.size() > 0)
		current_file = input_file_stack.top();

	return true;
}

void TPESQLProcessor::put_start_exec_sql(bool with_period)
{
	ESQLCall start_exec_sql_call(get_call_id("StartSQL"), parser_data->job_params()->opt_emit_static_calls);
	put_call(start_exec_sql_call, with_period);
}

void TPESQLProcessor::put_end_exec_sql(bool with_period)
{
	ESQLCall start_exec_sql_call(get_call_id("EndSQL"), parser_data->job_params()->opt_emit_static_calls);
	put_call(start_exec_sql_call, with_period);
}

std::string take_max(std::string& s, int n)
{
	std::string res;
	if (s.length() > n) {
		res = s.substr(0, n);
		s = s.substr(n);
	}
	else {
		res = s;
		s = "";
	}
	return res;
}

static int str_count(const std::string& obj, const std::string& tgt)
{
	int occs = 0;
	std::string::size_type pos = 0;
	while ((pos = obj.find(tgt, pos)) != std::string::npos) {
		++occs;
		pos += tgt.length();
	}
	return occs;
}

bool TPESQLProcessor::put_query_defs()
{
	if (emitted_query_defs)
		return true;

	for (int i = 1; i <= ws_query_list.size(); i++) {
		std::string qry = ws_query_list.at(i - 1);
		int qry_len = qry.length();
		qry = string_replace(qry, "\"", "\"\"");

		put_output_line(code_tag + string_format(" 01  SQ%04d.", i));

		if (!parser_data->job_params()->opt_emit_cobol85) {

			int pos = 0;
			int max_sec_len = 30;

			int cur_out_char = 0;

			std::string s;

			int nblocks = qry.size() / SQL_QUERY_BLOCK_SIZE;
			int remainder = qry.size() % SQL_QUERY_BLOCK_SIZE;
			std::vector<std::string> sub_blocks;

			while (!qry.empty()) {
				std::string block = take_max(qry, SQL_QUERY_BLOCK_SIZE);
				int block_size = block.size();
				int block_size_a = block_size - str_count(block, "\"\"");

				std::string first_block = take_max(block, 29);
				put_output_line(code_tag + string_format("     02  FILLER PIC X(%04d) VALUE \"%s\"", block_size_a, first_block));

				while (!block.empty()) {
					std::string sub_block = take_max(block, 59);
					sub_blocks.push_back(sub_block);
				}

				if (sub_blocks.size() > 0 && sub_blocks.back().size() == 59) {
					std::string last_block = sub_blocks.back();
					std::string t = last_block.substr(58, 1);	// get last char
					last_block = last_block.substr(0, 58);		// cut last_block
					sub_blocks.pop_back();
					sub_blocks.push_back(last_block);
					sub_blocks.push_back(t);
				}

				for (auto sub_block : sub_blocks) {
					put_output_line(code_tag + string_format("  &  \"%s\"", sub_block));
				}
				output_lines.back() += ".";

				sub_blocks.clear();
			}
			put_output_line(code_tag + std::string("     02  FILLER PIC X(1) VALUE X\"00\"."));
		}
		else {
			int pos = 0;
			int max_sec_len = 30;

			std::string s;

			while (true) {
				std::string sub_block = take_max(qry, 256);
				int sb_size = sub_block.size();
				if (sub_block.empty())
					break;

				s = take_max(sub_block, 34);
				put_output_line(code_tag + string_format("  02  FILLER PIC X(%04d) VALUE \"%s", sb_size, s));

				while (true) {
					s = take_max(sub_block, 60);
					if (s.empty())
						break;

					put_output_line(code_tag + string_format("-    \"%s", s));
				}

				output_lines.back() += "\".";
			}

			put_output_line(code_tag + std::string("     02  FILLER PIC X(1) VALUE X\"00\"."));
		}
	}

	emitted_query_defs = true;
	return true;
}

void TPESQLProcessor::put_working_storage()
{
	put_output_line(code_tag + std::string(" WORKING-STORAGE SECTION."));
}

bool TPESQLProcessor::put_cursor_declarations()
{
	CobolVarType f_type;
	int f_size, f_scale;
	bool emit_static = parser_data->job_params()->opt_emit_static_calls;
	const char* _areab = AREA_B_CPREFIX;

	put_output_line(code_tag + "*");
	put_output_line(code_tag + "*   ESQL CURSOR DECLARATIONS (START)");

	put_output_line(std::string(_areab) + "GO TO GIX-SKIP-CRSR-INIT.");

	auto cursor_open_list = startup_items;
        auto other_crsrs_open = cpplinq::from(*(parser_data->exec_list())).where([](cb_exec_sql_stmt_ptr p) { return p->startup_item == 0 && p->commandName == ESQL_OPEN && !p->cursorName.empty(); }).to_vector();
        cursor_open_list.insert(cursor_open_list.end(), other_crsrs_open.begin(), other_crsrs_open.end());

	auto cursor_list = startup_items;
	auto other_crsrs = cpplinq::from(*(parser_data->exec_list())).where([](cb_exec_sql_stmt_ptr p) { return p->startup_item == 0 && p->commandName == ESQL_SELECT && !p->cursorName.empty(); }).to_vector();
	cursor_list.insert(cursor_list.end(), other_crsrs.begin(), other_crsrs.end());

	for (cb_exec_sql_stmt_ptr stmt : cursor_list) {
		if (stmt->host_list->size() <= 0 && stmt->cursor_from_prepared) {
			for (cb_exec_sql_stmt_ptr stmt_open : cursor_open_list) {
				if (stmt->cursorName == stmt_open->cursorName && stmt_open->commandName == ESQL_OPEN && stmt_open->host_list->size() > 0) {

					stmt->host_list = stmt_open->host_list;
				}
			}
		}
		bool has_params = stmt->host_list->size() > 0;

		//if (stmt->statementSource && !stmt->statementSource->is_literal) {
		//	raise_error("Cursors declared in WORKING-STORAGE cannot use a field as source: " + stmt->cursorName, ERR_CRSR_GEN, stmt->src_abs_path, stmt->startLine);
		//	return false;
		//}

		put_output_line(string_format(AREA_A_CPREFIX "GIXSQL-CI-P-%s.", string_replace(stmt->cursorName, "_", "-")));

		if (has_params) {
			put_start_exec_sql(false);

			if (!put_host_parameters(stmt))
				return false;

			ESQLCall cd_call(get_call_id("CursorDeclareParams"), emit_static);
			cd_call.addParameter("SQLCA", BY_REFERENCE);
			cd_call.addParameter(parser_data.get(), stmt->connectionId);
			cd_call.addParameter("\"" + stmt->cursorName + "\" & x\"00\"", BY_REFERENCE); //& x\"00\"
			cd_call.addParameter(std::to_string(stmt->cursor_hold ? 1 : 0), BY_VALUE);

			//cd_call.addParameter(stmt->sqlName, BY_REFERENCE); //& x\"00\"
			std::string sql_content = this->ws_query_list.at(stmt->sql_query_list_id - 1);
			if (sql_content.size() < 3 || !starts_with(sql_content, "@") || sql_content.at(1) != ':') {
				cd_call.addParameter(string_format("SQ%04d", stmt->sql_query_list_id), BY_REFERENCE);
				cd_call.addParameter(0, BY_VALUE);
			}
			else {
				std::string var_name = sql_content.substr(2);
				ASSERT_NO_INDICATOR(var_name, stmt->src_abs_path, stmt->startLine);
				if (!parser_data->field_exists(var_name)) {
					raise_error("Cannot find host variable: " + var_name, ERR_MISSING_HOSTVAR, stmt->src_abs_path, stmt->startLine);
					return false;
				}
				cd_call.addParameter(var_name, BY_REFERENCE);
				auto hr = parser_data->field_map(var_name);
				bool is_varlen = parser_data->get_actual_field_data(hr, &f_type, &f_size, &f_scale);
				cd_call.addParameter(f_size * (is_varlen ? -1 : 1), BY_VALUE);
			}

			cd_call.addParameter(std::to_string(stmt->host_list->size()), BY_VALUE);

			if (!put_call(cd_call, false))
				return false;

			put_end_exec_sql(false);

			put_whenever_handler(stmt->period);
		}
		else {
			ESQLCall cd_call(get_call_id("CursorDeclare"), emit_static);
			cd_call.addParameter("SQLCA", BY_REFERENCE);
			cd_call.addParameter(parser_data.get(), stmt->connectionId);
			cd_call.addParameter("\"" + stmt->cursorName + "\" & x\"00\"", BY_REFERENCE);
			cd_call.addParameter(stmt->cursor_hold, BY_VALUE);

			std::string sql_content = this->ws_query_list.at(stmt->sql_query_list_id - 1);
			if (sql_content.size() < 3 || !starts_with(sql_content, "@") || sql_content.at(1) != ':') {
				cd_call.addParameter(string_format("SQ%04d", stmt->sql_query_list_id), BY_REFERENCE);
				cd_call.addParameter(0, BY_VALUE);
			}
			else {
				std::string var_name = sql_content.substr(2);
				ASSERT_NO_INDICATOR(var_name, stmt->src_abs_path, stmt->startLine);
				if (!parser_data->field_exists(var_name)) {
					raise_error("Cannot find host variable: " + var_name, ERR_MISSING_HOSTVAR, stmt->src_abs_path, stmt->startLine);
					return false;
				}
				cd_call.addParameter(var_name, BY_REFERENCE);
				auto hr = parser_data->field_map(var_name);
				bool is_varlen = parser_data->get_actual_field_data(hr, &f_type, &f_size, &f_scale);
				cd_call.addParameter(f_size * (is_varlen ? -1 : 1), BY_VALUE);
			}

			if (!put_call(cd_call, false))
				return false;

			put_whenever_handler(stmt->period);
		}
	}

	put_output_line(AREA_A_CPREFIX "GIX-SKIP-CRSR-INIT.");

	put_output_line(code_tag + "*");
	put_output_line(code_tag + "*   ESQL CURSOR DECLARATIONS (END)");
	return true;
}

bool TPESQLProcessor::put_call(const ESQLCall& c, bool terminate_with_period, int indent_level)
{

	if (c.hasError()) {
		owner->err_data.err_messages.push_back(c.error());
		return false;
	}

	auto lines = c.format(indent_level);

	// The END-CALL statement was added by the format method, we just need (in case) to add the period
	if (terminate_with_period) {
		lines.back() = lines.back() + ".";
	}

	for (auto ln : lines) {
		put_output_line(ln);
	}

	return true;
}

cb_exec_sql_stmt_ptr TPESQLProcessor::find_exec_sql_stmt(const std::string f1, int i)
{
	std::vector<cb_exec_sql_stmt_ptr>* p = parser_data->exec_list();
	for (auto e : *p) {
		std::string f2 = e->src_abs_path;
		if (f1 == f2 && (e->startLine <= i && e->endLine >= i))
			return e;
	}

	return NULL;
}

cb_exec_sql_stmt_ptr TPESQLProcessor::find_esql_cmd(std::string cmd, int idx)
{
	int n = 0;
	std::vector<cb_exec_sql_stmt_ptr>* p = parser_data->exec_list();
	for (auto e : *p) {
		if (e->commandName == cmd) {
			if (n == idx)
				return e;
			else
				n++;
		}
	}

	return NULL;
}

void TPESQLProcessor::put_output_line(const std::string& line)
{
	output_line++;

	output_lines.push_back(line);

	std::string output_id = string_format("%d@%s", output_line, output_file);
	std::string input_id = string_format("%d@%s", current_input_line, input_file_stack.top());
#if defined(_WIN32) && defined(_DEBUG) && defined(_DEBUG_LOG_ON)
	OutputDebugStringA((input_id + "-> " + output_id + "\n").toLocal8Bit().constData());
#endif
	out_to_in[output_id] = input_id;
	in_to_out[input_id] = output_id;
}

bool TPESQLProcessor::handle_esql_stmt(const ESQL_Command cmd, const cb_exec_sql_stmt_ptr stmt, bool in_ws)
{
	CobolVarType f_type;
	int f_size, f_scale;
	bool emit_static = parser_data->job_params()->opt_emit_static_calls;

	if (stmt->startup_item)
		return true;

	int gen_block_start = output_line;

	switch (cmd) {

	case ESQL_Command::WorkingBegin:
		put_working_storage();
		break;

	case ESQL_Command::DeclareTable:
		// Do nothing
		break;

	case ESQL_Command::WorkingEnd:
		if (!put_query_defs())
			return false;

		// Cursor initialization flags (if requested)
		put_smart_cursor_init_flags();
		break;

	case ESQL_Command::Incfile:
	case ESQL_Command::IncSQLCA:
	{
		std::string copy_file;
		std::string inc_copy_name = (cmd == ESQL_Command::Incfile) ? stmt->incfileName : "SQLCA";
		if (parser_data->job_params()->opt_preprocess_copy_files) {
			// inline file
			if (!owner->getCopyResolver()->resolveCopyFile(inc_copy_name, copy_file)) {
				//owner->err_data.err_messages.push_back("Cannot resolve copybook: " + inc_copy_name);
				raise_error("Cannot resolve copybook: " + inc_copy_name, ERR_MISSING_COPYFILE);
				return false;
			}

			add_dependency(input_file_stack.top(), copy_file);

#if defined(_WIN32) && defined(_DEBUG) && defined(VERBOSE)
			char bfr[512];
			bool b = ends_with(copy_file, "DPCTP006.cpy");
			sprintf(bfr, "Preprocessing copy file %s\n", copy_file.c_str());
			OutputDebugStringA(bfr);
#endif
			input_file_stack.push(filename_clean_path(copy_file));
			if (!processNextFile())
				return false;
		}
		else {

			// we treat it as a standard copybook file, and let the compiler deal with any error
			// but we still try to resolve the copy to gather some metadata
			put_output_line(AREA_B_PREFIX + string_format("COPY %s.", inc_copy_name));

			if (owner->getCopyResolver()->resolveCopyFile(inc_copy_name, copy_file)) {
				add_dependency(input_file_stack.top(), copy_file);
			}
			else
				add_dependency(input_file_stack.top(), "*" + inc_copy_name);

		}

		_DBG_OUT("Copy resolved: %s -> %s\n", inc_copy_name.c_str(), copy_file.c_str());
	}
	break;

	case ESQL_Command::Connect:
	{
		ESQLCall connect_call(get_call_id("Connect"), emit_static);
		connect_call.addParameter("SQLCA", BY_REFERENCE);

		connect_call.addParameter(parser_data.get(), stmt->conninfo->data_source);
		connect_call.addParameter(parser_data.get(), stmt->conninfo->id);
		connect_call.addParameter(parser_data.get(), stmt->conninfo->dbname);
		connect_call.addParameter(parser_data.get(), stmt->conninfo->username);
		connect_call.addParameter(parser_data.get(), stmt->conninfo->password);

		if (!put_call(connect_call, false))
			return false;

		put_whenever_handler(stmt->period);
	}
	break;

	case ESQL_Command::ConnectReset:
	{
		ESQLCall connect_call(get_call_id("ConnectReset"), emit_static);
		connect_call.addParameter("SQLCA", BY_REFERENCE);
		connect_call.addParameter(parser_data.get(), stmt->connectionId);

		if (!put_call(connect_call, false))
			return false;

		put_whenever_handler(stmt->period);
	}
	break;

	case ESQL_Command::Disconnect:
	{
		ESQLCall connect_call(get_call_id("ConnectReset"), emit_static);
		connect_call.addParameter("SQLCA", BY_REFERENCE);
		connect_call.addParameter(parser_data.get(), stmt->connectionId);

		if (!put_call(connect_call, false))
			return false;

		put_whenever_handler(stmt->period);
	}
	break;


	case ESQL_Command::Select:
	{
		/*
			0   RESULT parameters -> CursorDeclare(Params) or Exec(Params).
									 (Params) depends on having input params
									 CursorDeclare/Exec depends on stmt->cursorName being in use
			>0 RESULT parameters  -> SelectInto
		*/

		if (stmt->cursorName.empty()) {
			int res_params_count = 0;

			put_start_exec_sql(false);

			if (!put_res_host_parameters(stmt, &res_params_count))
				return false;

			if (!put_host_parameters(stmt))
				return false;

			std::string call_id;
			if (!stmt->res_host_list->size()) {
				call_id = get_call_id("Exec");
				if (stmt->host_list->size())
					call_id += "Params";
			}
			else {
				call_id = get_call_id("ExecSelectIntoOne");
			}

			ESQLCall select_call(call_id, emit_static);
			select_call.addParameter("SQLCA", BY_REFERENCE);
			select_call.addParameter(parser_data.get(), stmt->connectionId);
			select_call.addParameter(string_format("SQ%04d", stmt->sql_query_list_id), BY_REFERENCE);

			if (ends_with(call_id, "Exec")) {
				// Nothing to add
			}
			else {
				if (ends_with(call_id, "ExecParams")) {
					select_call.addParameter(stmt->host_list->size(), BY_VALUE);
				}
				else {
					if (ends_with(call_id, "ExecSelectIntoOne")) {
						select_call.addParameter(stmt->host_list->size(), BY_VALUE);
						select_call.addParameter(res_params_count, BY_VALUE);
					}
				}
			}

			if (!put_call(select_call, false))
				return false;

			put_end_exec_sql(false);

			put_whenever_handler(stmt->period);
		}
		else {
			bool is_crsr_startup_item = cpplinq::from(startup_items).where([stmt](cb_exec_sql_stmt_ptr p) { return p->cursorName == stmt->cursorName; }).to_vector().size() > 0;
			if (!is_crsr_startup_item) {
				put_smart_cursor_init_check(stmt->cursorName, true);
			}
		}
	}
	break;

	case ESQL_Command::Open:
	{
		bool is_crsr_startup_item = cpplinq::from(startup_items).where([stmt](cb_exec_sql_stmt_ptr p) { return p->cursorName == stmt->cursorName; }).to_vector().size() > 0;

		// We need to add a check only if the cursor has been declared in the WORKING-STORAGE section
		std::string crsr_init_var = "GIXSQL-CI-F-" + string_replace(stmt->cursorName, "_", "-");
		put_smart_cursor_init_check(stmt->cursorName);
		put_output_line(string_format(AREA_B_CPREFIX "IF %s = 'X' THEN", crsr_init_var));


		std::string cursor_id = stmt->cursorName;
		ESQLCall open_call(get_call_id("CursorOpen"), emit_static);
		open_call.addParameter("SQLCA", BY_REFERENCE);
		open_call.addParameter("\"" + cursor_id + "\" & x\"00\"", BY_REFERENCE);

		if (!put_call(open_call, false, 1))
			return false;

		put_output_line(string_format(AREA_B_CPREFIX "END-IF"));

		put_whenever_handler(stmt->period);
	}
	break;

	case ESQL_Command::Close:
	{
		//if (opt_smart_crsr_init) {
		//	std::string crsr_init_var = "GIXSQL-CI-F-" + string_replace(stmt->cursorName, "_", "-");
		//	put_smart_cursor_init_check(stmt->cursorName);
		//	put_output_line(string_format(AREA_B_CPREFIX "IF %s = 'X' THEN", crsr_init_var));
		//}

		std::string cursor_id = stmt->cursorName;
		ESQLCall close_call(get_call_id("CursorClose"), emit_static);
		close_call.addParameter("SQLCA", BY_REFERENCE);
		close_call.addParameter("\"" + cursor_id + "\" & x\"00\"", BY_REFERENCE);

		if (!put_call(close_call, false))
			return false;

		//if (opt_smart_crsr_init) {
		//	put_output_line(string_format(AREA_B_CPREFIX "END-IF"));
		//}

		put_whenever_handler(stmt->period);
	}
	break;

	case ESQL_Command::Fetch:
	{
		put_start_exec_sql(false);

		int res_params_count = 0;

		for (cb_res_hostreference_ptr rp : *stmt->res_host_list) {

			std::string var_name, ind_name;
			bool has_indicator = decode_indicator(rp->hostreference.substr(1), var_name, ind_name);
			if (!parser_data->field_exists(var_name)) {
				raise_error("Cannot find host variable: " + var_name, ERR_MISSING_HOSTVAR, stmt->src_abs_path, rp->lineno);
				return false;
			}

			cb_field_ptr hr = parser_data->field_map(var_name);
			bool is_varlen = parser_data->get_actual_field_data(hr, &f_type, &f_size, &f_scale);

			// Support for group items used as host variables in SELECT statements
			// They are decomposed into their sub-elements
			if (f_type == CobolVarType::COBOL_TYPE_GROUP && !is_varlen) {

				if (has_indicator) {
					raise_error("Invalid null indicator reference: " + rp->hostreference.substr(1), ERR_INVALID_NULLIND_REF, stmt->src_abs_path, rp->lineno);
					return false;
				}

				if (hr->group_levels_count != 1) {
					raise_error("Nested levels not allowed in group variable: " + rp->hostreference.substr(1), ERR_MISSING_HOSTVAR, stmt->src_abs_path, rp->lineno);
					return false;
				}

				cb_field_ptr pp = hr->children;
				if (!pp) {
					raise_error("Inconsistent data for group field : " + hr->sname, ERR_MISSING_HOSTVAR, stmt->src_abs_path, rp->lineno);
					return false;
				}

				while (pp) {

					ESQLCall pp_call(get_call_id("SetResultParams"), emit_static);
					int pp_flags = (pp->usage == Usage::Binary) ? CBL_FIELD_FLAG_BINARY : CBL_FIELD_FLAG_NONE;

					CobolVarType pp_type = CobolVarType::UNKNOWN;
					int pp_size = 0, pp_scale = 0;
					bool pp_is_varlen = parser_data->get_actual_field_data(pp, &pp_type, &pp_size, &pp_scale);
					if (pp_is_varlen) {
						raise_error("Inconsistent data for group field member: " + pp->sname, ERR_MISSING_HOSTVAR, stmt->src_abs_path, rp->lineno);
						return false;
					}

					pp_call.addParameter(pp_type, BY_VALUE);
					pp_call.addParameter(pp_size, BY_VALUE);
					pp_call.addParameter(pp_scale > 0 ? -pp_scale : 0, BY_VALUE);
					pp_call.addParameter(pp_flags, BY_VALUE);
					pp_call.addParameter(pp->sname, BY_REFERENCE);
					pp_call.addParameter(0, BY_REFERENCE);

					if (!put_call(pp_call, false))
						return false;

					res_params_count++;

					pp = pp->sister;
				}

			}
			else {
				ESQLCall rp_call(get_call_id("SetResultParams"), emit_static);

				if (has_indicator && !parser_data->field_exists(ind_name)) {
					raise_error("Cannot find host variable: " + ind_name, ERR_MISSING_HOSTVAR, stmt->src_abs_path, rp->lineno);
					return false;
				}

				int flags = is_varlen ? CBL_FIELD_FLAG_VARLEN : CBL_FIELD_FLAG_NONE;
				flags |= (hr->usage == Usage::Binary) ? CBL_FIELD_FLAG_BINARY : CBL_FIELD_FLAG_NONE;

				rp_call.addParameter(f_type, BY_VALUE);
				rp_call.addParameter(f_size, BY_VALUE);
				rp_call.addParameter(f_scale > 0 ? -f_scale : 0, BY_VALUE);
				rp_call.addParameter(flags, BY_VALUE);
				rp_call.addParameter(var_name, BY_REFERENCE);
				if (has_indicator)
					rp_call.addParameter(ind_name, BY_REFERENCE);
				else
					rp_call.addParameter(0, BY_REFERENCE);

				if (!put_call(rp_call, false))
					return false;

				res_params_count++;

			}
		}

		std::string cursor_id = stmt->cursorName;
		ESQLCall fetch_call(get_call_id("CursorFetchOne"), emit_static);
		fetch_call.addParameter("SQLCA", BY_REFERENCE);
		fetch_call.addParameter("\"" + cursor_id + "\" & x\"00\"", BY_REFERENCE);

		if (!put_call(fetch_call, false))
			return false;

		put_end_exec_sql(false);

		//if (opt_smart_crsr_init) {
		//	put_output_line(string_format(AREA_B_CPREFIX "END-IF"));
		//}

		put_whenever_handler(stmt->period);
	}
	break;

	case ESQL_Command::Commit:
	{
		// Note: RELEASE not supported, in case check the stmt->transaction_release flag
		put_start_exec_sql(false);
		ESQLCall commit_call(get_call_id("Exec"), emit_static);
		commit_call.addParameter("SQLCA", BY_REFERENCE);
		commit_call.addParameter(parser_data.get(), stmt->connectionId);
		commit_call.addParameter("\"COMMIT\" & x\"00\"", BY_REFERENCE);

		if (!put_call(commit_call, false))
			return false;

		put_end_exec_sql(false);

		put_whenever_handler(stmt->period);
	}
	break;

	case ESQL_Command::Rollback:
	{
		// Note: RELEASE not supported, in case check the stmt->transaction_release flag
		put_start_exec_sql(false);
		ESQLCall rollback_call(get_call_id("Exec"), emit_static);
		rollback_call.addParameter("SQLCA", BY_REFERENCE);
		rollback_call.addParameter(parser_data.get(), stmt->connectionId);
		rollback_call.addParameter("\"ROLLBACK\" & x\"00\"", BY_REFERENCE);

		if (!put_call(rollback_call, false))
			return false;

		put_end_exec_sql(false);

		put_whenever_handler(stmt->period);
	}
	break;


	case ESQL_Command::Update:
	case ESQL_Command::Delete:
	case ESQL_Command::Insert:
	{
		put_start_exec_sql(false);

		int sql_params_count = 0;

		// Special case: we cannot use the put_host_parameters method because we need to handle group variables
		for (cb_hostreference_ptr p : *stmt->host_list) {

			std::string var_name, ind_name;
			bool has_indicator = decode_indicator(p->hostreference.substr(1), var_name, ind_name);
			if (!parser_data->field_exists(var_name)) {
				raise_error("Cannot find host variable: " + p->hostreference.substr(1), ERR_MISSING_HOSTVAR, stmt->src_abs_path, p->lineno);
				return false;
			}

			cb_field_ptr hr = parser_data->field_map(var_name);
			bool is_varlen = parser_data->get_actual_field_data(hr, &f_type, &f_size, &f_scale);

			// Support for group items used as host variables in INSERT statements
			// They are decomposed into their sub-elements
			if (cmd == ESQL_Command::Insert && f_type == CobolVarType::COBOL_TYPE_GROUP && !is_varlen) {

				if (has_indicator) {
					raise_error("Invalid null indicator reference: " + var_name, ERR_INVALID_NULLIND_REF, stmt->src_abs_path, p->lineno);
					return false;
				}

				if (hr->group_levels_count != 1) {
					raise_error("Nested levels not allowed in group variable: " + var_name, ERR_MISSING_HOSTVAR, stmt->src_abs_path, p->lineno);
					return false;
				}

				cb_field_ptr pp = hr->children;
				if (!pp) {
					raise_error("Inconsistent data for group field : " + hr->sname, ERR_MISSING_HOSTVAR, stmt->src_abs_path, p->lineno);
					return false;
				}

				while (pp) {

					ESQLCall pp_call(get_call_id("SetSQLParams"), emit_static);
					int pp_flags = (pp->usage == Usage::Binary) ? CBL_FIELD_FLAG_BINARY : CBL_FIELD_FLAG_NONE;

					uint32_t _type, _precision;
					uint16_t _scale;
					uint8_t _flags;
					decode_sql_type_info(hr->sql_type, &_type, &_precision, &_scale, &_flags);
					if (HAS_PICX_AS_VARCHAR(_flags) || parser_data->job_params()->opt_picx_as_varchar)
						pp_flags |= CBL_FIELD_FLAG_AUTOTRIM;

					CobolVarType pp_type = CobolVarType::UNKNOWN;
					int pp_size = 0, pp_scale = 0;
					bool pp_is_varlen = parser_data->get_actual_field_data(pp, &pp_type, &pp_size, &pp_scale);
					if (pp_is_varlen) {
						raise_error("Inconsistent data for group field member: " + pp->sname, ERR_MISSING_HOSTVAR, stmt->src_abs_path, p->lineno);
						return false;
					}

					pp_call.addParameter(pp_type, BY_VALUE);
					pp_call.addParameter(pp_size, BY_VALUE);
					pp_call.addParameter(pp_scale > 0 ? -pp_scale : 0, BY_VALUE);
					pp_call.addParameter(pp_flags, BY_VALUE);

					pp_call.addParameter(pp->sname, BY_REFERENCE);
					pp_call.addParameter(0, BY_VALUE);

					if (!put_call(pp_call, false))
						return false;

					sql_params_count++;

					pp = pp->sister;
				}

			}
			else {
				ESQLCall p_call(get_call_id("SetSQLParams"), emit_static);
				int flags = is_varlen ? CBL_FIELD_FLAG_VARLEN : CBL_FIELD_FLAG_NONE;
				flags |= (hr->usage == Usage::Binary) ? CBL_FIELD_FLAG_BINARY : CBL_FIELD_FLAG_NONE;

				uint32_t _type, _precision;
				uint16_t _scale;
				uint8_t _flags;
				decode_sql_type_info(hr->sql_type, &_type, &_precision, &_scale, &_flags);
				if (HAS_PICX_AS_VARCHAR(_flags) || parser_data->job_params()->opt_picx_as_varchar)
					flags |= CBL_FIELD_FLAG_AUTOTRIM;

				p_call.addParameter(f_type, BY_VALUE);
				p_call.addParameter(f_size, BY_VALUE);
				p_call.addParameter(f_scale > 0 ? -f_scale : 0, BY_VALUE);
				p_call.addParameter(flags, BY_VALUE);
				p_call.addParameter(var_name, BY_REFERENCE);
				if (has_indicator)
					p_call.addParameter(ind_name, BY_REFERENCE);
				else
					p_call.addParameter(0, BY_REFERENCE);

				if (!put_call(p_call, false))
					return false;

				sql_params_count++;
			}
		}

		std::string dml_call_id = get_call_id(stmt->host_list->size() == 0 ? "Exec" : "ExecParams");
		ESQLCall dml_call(dml_call_id, emit_static);
		dml_call.addParameter("SQLCA", BY_REFERENCE);
		dml_call.addParameter(parser_data.get(), stmt->connectionId);
		dml_call.addParameter(string_format("SQ%04d", stmt->sql_query_list_id), BY_REFERENCE);
		if (ends_with(dml_call_id, "Params"))
			dml_call.addParameter(sql_params_count, BY_VALUE);

		if (!put_call(dml_call, false))
			return false;

		put_end_exec_sql(false);

		put_whenever_handler(stmt->period);
	}
	break;

	case ESQL_Command::DeclareVar:
	{
		if (stmt->host_list->size() != 1) {
			raise_error("Cannot find host variable (invalid declaration)", ERR_MISSING_HOSTVAR, stmt->src_abs_path, stmt->startLine);
			return false;
		}

		std::string var_name = stmt->host_list->at(0)->hostreference;
		ASSERT_NO_INDICATOR(var_name, stmt->src_abs_path, stmt->startLine);
		if (!parser_data->field_exists(var_name)) {
			raise_error("Cannot find host variable: " + var_name, ERR_MISSING_HOSTVAR, stmt->src_abs_path, stmt->startLine);
			return false;
		}

		cb_field_ptr var = parser_data->field_map(var_name);
		if (var->level == 49 || var->level == 77) {
			raise_error(string_format("Invalid level for SQL variable (%02d)", var->level), ERR_INVALID_LEVEL, var->defined_at_source_file, var->defined_at_source_line);
			return false;
		}

		uint64_t type_info = var->sql_type;

		uint32_t sql_type, precision;
		uint16_t scale;
		uint8_t flags;
		decode_sql_type_info(type_info, &sql_type, &precision, &scale, &flags);

#ifdef USE_VARLEN_16
		if (precision > USHRT_MAX) {
			std::string msg = string_format("Unsupported field length (%d > %d)", precision, USHRT_MAX);
			raise_error(msg, ERR_INCOMPATIBLE_TYPES, stmt->src_abs_path, stmt->startLine);
			return false;
		}
#endif

		if (!check_sql_type_compatibility(type_info, var)) {
			std::string msg = string_format("SQL type definition for %s (%s) is not compatible with the COBOL one (%s)", var->sname, "N/A", "N/A");
			raise_error(msg, ERR_INCOMPATIBLE_TYPES, stmt->src_abs_path, stmt->startLine);
			return false;
		}

		switch (sql_type) {

		case TYPE_SQL_CHAR:
		case TYPE_SQL_VARCHAR:
		case TYPE_SQL_BINARY:
		case TYPE_SQL_VARBINARY:
		{
			int cbl_int_part_len = var->picnsize;

			if (cbl_int_part_len <= 0) {
				if (precision == 0) {
					raise_error(string_format("Missing length for field %s", var_name), ERR_MISSING_LENGTH, var->defined_at_source_file, var->defined_at_source_line);
					return false;
				}
				else {
					cbl_int_part_len = precision;
				}
			}

			if (HAS_FLAG_EMIT_VAR(flags)) {
				if (!var->is_varlen) {
					put_output_line(AREA_B_CPREFIX + string_format("%02d %s PIC X(%d).", var->level, var->sname, cbl_int_part_len));

					cb_field_ptr fdata = new cb_field_t();
					fdata->sql_type = sql_type;
					fdata->level = var->level;
					fdata->sname = var->sname;
					fdata->pictype = var->pictype != -1 ? var->pictype : (IS_NUMERIC(var->sql_type) ? PIC_NUMERIC : PIC_ALPHANUMERIC);
					fdata->usage = var->usage;
					fdata->picnsize = cbl_int_part_len;
					fdata->scale = 0;
					fdata->parent = nullptr;
					parser_data->add_to_field_map(fdata->sname, fdata);
				}
				else {
					std::string vlp = parser_data->job_params()->opt_varying_len_sz_short ? S_VARLEN_LENGTH_PIC : L_VARLEN_LENGTH_PIC;
					put_output_line(AREA_B_CPREFIX + string_format("%02d %s.", var->level, var->sname));
					put_output_line(AREA_B_CPREFIX + string_format("    49 %s-%s PIC %s.", var->sname, parser_data->job_params()->opt_varlen_suffix_len, vlp));
					put_output_line(AREA_B_CPREFIX + string_format("    49 %s-%s PIC X(%d).", var->sname, parser_data->job_params()->opt_varlen_suffix_data, cbl_int_part_len));

					cb_field_ptr flength = new cb_field_t();
					flength->level = 49;
					flength->sname = var->sname + "-" + parser_data->job_params()->opt_varlen_suffix_len;
					flength->pictype = PIC_NUMERIC;
					flength->usage = var->usage;
					flength->picnsize = parser_data->job_params()->opt_varying_len_sz_short ? S_VARLEN_PIC_SZ : L_VARLEN_PIC_SZ;
					flength->parent = var;
					var->children = flength;

					cb_field_ptr fdata = new cb_field_t();
					fdata->level = 49;
					fdata->sql_type = sql_type;
					fdata->sname = var->sname + "-" + parser_data->job_params()->opt_varlen_suffix_data;
					fdata->pictype = PIC_ALPHANUMERIC;
					fdata->usage = Usage::None;
					fdata->picnsize = cbl_int_part_len;
					fdata->scale = 0;
					fdata->parent = var;
					var->children->sister = fdata;

					parser_data->add_to_field_map(flength->sname, flength);
					parser_data->add_to_field_map(fdata->sname, fdata);
				}
			}
			else {
				var->pictype = PIC_ALPHANUMERIC;
			}
		}
		break;

		// TODO: should we use COMP-1/COMP-2 types for float?
		case TYPE_SQL_FLOAT:
		case TYPE_SQL_DECIMAL:
		{
			int cbl_int_part_len = var->picnsize - var->scale;
			int cbl_dec_part_len = var->scale;

			if (cbl_int_part_len <= 0) {
				if (precision == 0) {
					//owner->err_data.err_messages.push_back(string_format("Missing length for field %s", var_name));
					raise_error(string_format("Missing length for field %s", var_name), ERR_MISSING_LENGTH, var->defined_at_source_file, var->defined_at_source_line);
					return false;
				}
				else {
					cbl_int_part_len = precision;
					cbl_dec_part_len = scale;
				}
			}

			std::string pic = AREA_B_CPREFIX + string_format("01 %s PIC S", var->sname);	// SQL DECIMAL is always signed
			pic += string_format("9(%d)", cbl_int_part_len);
			if (cbl_dec_part_len)
				pic += string_format("V9(%d)", cbl_dec_part_len);

			pic += ".";

			if (HAS_FLAG_EMIT_VAR(flags)) {
				put_output_line(pic);
			}
		}
		break;

		case TYPE_SQL_INT:
		{
			int int_part_len = var->picnsize;
			std::string pic = AREA_B_CPREFIX + string_format("01 %s PIC S9(%d).", var->sname, var->picnsize);	// SQL INTEGER is always signed
			put_output_line(pic);
		}
		break;

		default:
			raise_error(string_format("Unsupported SQL type (ID: %d)", sql_type), ERR_INVALID_TYPE, var->defined_at_source_file, var->defined_at_source_line);
			return false;
		}
	}
	break;

	case ESQL_Command::Comment:
		/* Do nothing */
		break;

	case ESQL_Command::PrepareStatement:
	{
		ESQLCall ps_call(get_call_id("PrepareStatement"), emit_static);
		ps_call.addParameter("SQLCA", BY_REFERENCE);
		ps_call.addParameter(parser_data.get(), stmt->connectionId);
		ps_call.addParameter("\"" + stmt->statementName + "\" & x\"00\"", BY_REFERENCE);
		if (stmt->statementSource) {	// statement source is a variable, we must check its type
			auto sv_name = stmt->statementSource->name.substr(1);
			ASSERT_NO_INDICATOR(sv_name, stmt->src_abs_path, stmt->startLine);
			if (!parser_data->field_exists(sv_name)) {
				raise_error("Cannot find host variable: " + sv_name, ERR_MISSING_HOSTVAR, stmt->src_abs_path, stmt->startLine);
				return false;
			}
			cb_field_ptr sv = parser_data->field_map(sv_name);
			bool is_varlen = parser_data->get_actual_field_data(sv, &f_type, &f_size, &f_scale);
			// If is_varlen is true, we are pointing to a "variable length group", which is fine.
			// We pass the group and the runtime library will handle the "actual" data.
			if (!is_varlen) {
				if (sv->pictype != PIC_ALPHANUMERIC) {
					raise_error("Unsupported type for host variable: " + sv_name, ERR_INVALID_TYPE, stmt->src_abs_path, stmt->startLine);
					return false;
				}
			}

			int sz = !is_varlen ? 0 : f_size;
			ps_call.addParameter(parser_data.get(), stmt->statementSource, sz);
		}
		else {
			ps_call.addParameter(string_format("SQ%04d", stmt->sql_query_list_id), BY_REFERENCE);
			ps_call.addParameter(0, BY_VALUE);
		}

		if (!put_call(ps_call, false))
			return false;

		put_whenever_handler(stmt->period);
	}
	break;

	case ESQL_Command::ExecPrepared:
	{
		put_start_exec_sql(false);

		bool is_exec_into = stmt->res_host_list->size() > 0;
		int res_params_count = 0;

		if (!put_host_parameters(stmt))
			return false;

		if (!put_res_host_parameters(stmt, &res_params_count))
			return false;

		ESQLCall ep_call(get_call_id(!is_exec_into ? "ExecPrepared" : "ExecPreparedInto"), emit_static);
		ep_call.addParameter("SQLCA", BY_REFERENCE);
		ep_call.addParameter(parser_data.get(), stmt->connectionId);
		ep_call.addParameter("\"" + stmt->statementName + "\" & x\"00\"", BY_REFERENCE);
		ep_call.addParameter(stmt->host_list->size(), BY_VALUE);
		if (is_exec_into)
			ep_call.addParameter(res_params_count, BY_VALUE);

		if (!put_call(ep_call, false))
			return false;

		put_end_exec_sql(false);

		put_whenever_handler(stmt->period);
	}
	break;

	case ESQL_Command::ExecImmediate:
	{
		ESQLCall ei_call(get_call_id("ExecImmediate"), emit_static);
		ei_call.addParameter("SQLCA", BY_REFERENCE);
		ei_call.addParameter(parser_data.get(), stmt->connectionId);
		if (stmt->statementSource) {	// statement source is a variable, we must check its type
			auto sv_name = stmt->statementSource->name.substr(1);
			ASSERT_NO_INDICATOR(sv_name, stmt->src_abs_path, stmt->startLine);
			if (!parser_data->field_exists(sv_name)) {
				raise_error("Cannot find host variable: " + sv_name, ERR_MISSING_HOSTVAR, stmt->src_abs_path, stmt->startLine);
				return false;
			}
			cb_field_ptr sv = parser_data->field_map(sv_name);
			bool is_varlen = parser_data->get_actual_field_data(sv, &f_type, &f_size, &f_scale);
			// If is_varlen is true, we are pointing to a "variable length group", which is fine.
			// We pass the group and the runtime library will handle the "actual" data.
			if (!is_varlen) {
				if (sv->pictype != PIC_ALPHANUMERIC) {
					raise_error("Unsupported type for host variable: " + sv_name, ERR_INVALID_TYPE, stmt->src_abs_path, stmt->startLine);
					return false;
				}
			}

			int sz = !is_varlen ? 0 : f_size;
			ei_call.addParameter(parser_data.get(), stmt->statementSource, sz);
		}
		else {
			ei_call.addParameter(string_format("SQ%04d", stmt->sql_query_list_id), BY_REFERENCE);
			ei_call.addParameter(0, BY_VALUE);
		}

		if (!put_call(ei_call, false))
			return false;

		put_whenever_handler(stmt->period);
	}
	break;

	case ESQL_Command::BeginDeclareSection:
	case ESQL_Command::EndDeclareSection:
		/* Do nothing (for now) */
		break;

	case ESQL_Command::PassThru:
	{
		put_start_exec_sql(false);

		if (!put_host_parameters(stmt))
			return false;

		std::string dml_call_id = get_call_id(stmt->host_list->size() == 0 ? "Exec" : "ExecParams");
		ESQLCall dml_call(dml_call_id, emit_static);
		dml_call.addParameter("SQLCA", BY_REFERENCE);
		dml_call.addParameter(parser_data.get(), stmt->connectionId);
		dml_call.addParameter(string_format("SQ%04d", stmt->sql_query_list_id), BY_REFERENCE);
		if (ends_with(dml_call_id, "Params"))
			dml_call.addParameter(stmt->host_list->size(), BY_VALUE);

		if (!put_call(dml_call, false))
			return false;

		put_end_exec_sql(false);

		put_whenever_handler(stmt->period);
	}
	break;

	case ESQL_Command::Whenever:
	{
		if (!stmt->whenever_data) {
			raise_error("Inconsistent data for WHENEVER statement", ERR_INVALID_DATA, stmt->src_abs_path, stmt->startLine);
			return false;
		}

		esql_whenever_clause_handler_t* clause_handler = nullptr;

		switch (stmt->whenever_data->clause) {
		case WHENEVER_CLAUSE_NOT_FOUND:
			clause_handler = &esql_whenever_handler.not_found;
			break;

		case WHENEVER_CLAUSE_SQLWARNING:
			clause_handler = &esql_whenever_handler.sqlwarning;
			break;

		case WHENEVER_CLAUSE_SQLERROR:
			clause_handler = &esql_whenever_handler.sqlerror;
			break;
		}

		if (!clause_handler) {
			raise_error("Inconsistent data for WHENEVER statement", ERR_INVALID_DATA, stmt->src_abs_path, stmt->startLine);
			return false;
		}

		clause_handler->action = stmt->whenever_data->action;
		clause_handler->host_label = stmt->whenever_data->host_label;
	}
	break;

	// If we have intercepted all the cases this should never happen
	// In case it does, should we instead raise an error?
	default:
	{
		//owner->err_messages << "Invalid statement: " + stmt->commandName;
		//return false;
		ESQLCall exec_call(get_call_id("Exec"), emit_static);
		exec_call.addParameter("SQLCA", BY_REFERENCE);
		exec_call.addParameter(parser_data.get(), stmt->connectionId);
		exec_call.addParameter(string_format("SQ%04d", stmt->sql_query_list_id), BY_REFERENCE);

		if (!put_call(exec_call, stmt->period))
			return false;
	}
	break;
	}

	int gen_block_end = output_line;

	generated_blocks[stmt] = std::tuple(gen_block_start + 1, gen_block_end);

	return true;
}

bool TPESQLProcessor::find_working_storage(int* working_begin_line, int* working_end_line)
{
	std::vector<cb_exec_sql_stmt_ptr>* p = parser_data->exec_list();

	*working_begin_line = 0;
	*working_end_line = 0;

	for (auto e : *p) {
		if (e->commandName == ESQL_WORKING_BEGIN)
			*working_begin_line = e->startLine;

		if (e->commandName == ESQL_WORKING_END)
			*working_end_line = e->startLine;
	}

	return (*working_begin_line | *working_end_line) > 0;
}

bool TPESQLProcessor::find_linkage_section(int* linkage_begin_line, int* linkage_end_line)
{
	std::vector<cb_exec_sql_stmt_ptr>* p = parser_data->exec_list();

	*linkage_begin_line = 0;
	*linkage_end_line = 0;

	for (auto e : *p) {
		if (e->commandName == ESQL_LINKAGE_BEGIN)
			*linkage_begin_line = e->startLine;

		if (e->commandName == ESQL_LINKAGE_END)
			*linkage_end_line = e->startLine;
	}

	return (*linkage_begin_line | *linkage_end_line) > 0;
}

std::string TPESQLProcessor::comment_line(const std::string& comment, const std::string& line)
{
	std::string ln = line;
	if (ln.size() < 7)
		ln = rpad(ln, 7);

	ln[6] = '*';

	int m = comment.size() > 6 ? 6 : comment.size();

	ln = comment + ln.substr(m);
	return ln;
}



std::string TPESQLProcessor::process_sql_query_item(const std::vector<std::string>& input_sql_list)
{
	bool in_single_quoted_string = false;
	bool in_double_quoted_string = false;

	std::string sql = "";
	std::vector <std::string> items;

	// We need to handle placeholders for group items passed as host variables
	for (std::vector< std::string>::const_iterator it = input_sql_list.begin(); it != input_sql_list.end(); ++it) {
		std::string item = *it;

		if (starts_with(item, "@[") && ends_with(item, "]")) {
			item = item.substr(2);
			item = item.substr(0, item.length() - 1);
		}

		items.push_back(item);
	}

	for (std::vector<std::string>::const_iterator it = items.begin(); it != items.end(); ++it) {
		std::string item = *it;

		for (auto itc = item.begin(); itc != item.end(); ++itc) {
			char c = *itc;

			if (c == '"')
				in_double_quoted_string = !in_double_quoted_string;

			if (c == '\'')
				in_single_quoted_string = !in_single_quoted_string;

			sql += c;
		}

		if (!in_single_quoted_string && !in_double_quoted_string)
			sql += ' ';
	}
	trim(sql);
	return sql;
}

void TPESQLProcessor::process_sql_query_list()
{
	for (cb_exec_sql_stmt_ptr p : *(parser_data->exec_list())) {
		if (p->sql_list->size()) {
			std::string sql = process_sql_query_item(*p->sql_list);
			// We account for EOL differencrs between platforms, so the preprocessor behaves the same everywhere
			sql = string_replace_regex(sql, "[\\r]", "");
			sql = string_replace_regex(sql, "[\\n\\t]", " ");
			ws_query_list.push_back(sql);
		}
	}
}

bool TPESQLProcessor::fixup_declared_vars()
{
	int n = 99999;
	for (auto it = parser_data->field_sql_type_info().begin(); it != parser_data->field_sql_type_info().end(); ++it) {
		cb_field_ptr var = nullptr;
		std::string var_name = it->first;
		std::tuple<uint64_t, int, int, std::string> d = it->second;
		uint64_t type_info = std::get<0>(d);
		int orig_start_line = std::get<1>(d);
		int orig_end_line = std::get<2>(d);
		std::string orig_src_file = std::get<3>(d);

		uint32_t sql_type, precision;
		uint16_t scale;
		uint8_t flags;
		decode_sql_type_info(type_info, &sql_type, &precision, &scale, &flags);

		if (HAS_FLAG_EMIT_VAR(flags)) {
			ASSERT_NO_INDICATOR(var_name, orig_src_file, orig_start_line);
			if (!parser_data->field_exists(var_name)) {
				if (precision == 0) {
					raise_error("Cannot find host variable: " + var_name, ERR_MISSING_HOSTVAR, orig_src_file, orig_start_line);
					return false;
				}
				else {
					var = new cb_field_t;
					var->level = 1;
					var->usage = Usage::None;
					var->sname = var_name;
					var->sql_type = type_info;
					var->pictype = -1;
					var->defined_at_source_line = orig_start_line;
					var->defined_at_source_file = orig_src_file;
					parser_data->add_to_field_map(var_name, var);
				}
			}
		}

		ASSERT_NO_INDICATOR(var_name, orig_src_file, orig_start_line);
		if (!parser_data->field_exists(var_name)) {
			raise_error("Cannot find host variable: " + var_name, ERR_MISSING_HOSTVAR, orig_src_file, orig_start_line);
			return false;
		}

		var = parser_data->field_map(var_name);
		if (var->level != 1) {
			std::string msg = string_format("Host variable %s has level %02d, should be 01", var_name, var->level);
			//owner->err_data.err_messages.push_back(msg);
			raise_error(msg, ERR_INVALID_LEVEL);
			return false;
		}

		if (!HAS_FLAG_EMIT_VAR(flags) && IS_VARLEN(sql_type) && !IS_BINARY(sql_type)) {
			var->sql_type = encode_sql_type_info(sql_type, precision, scale, flags | FLAG_PICX_AS_VARCHAR);
		}
		else {
			var->sql_type = type_info;
		}
		var->is_varlen = HAS_FLAG_EMIT_VAR(flags);
		var->usage = IS_BINARY(sql_type) ? Usage::Binary : Usage::None;
		var->picnsize = precision;
		var->scale = scale;

		cb_exec_sql_stmt_ptr stmt = nullptr;
		if (HAS_FLAG_EMIT_VAR(flags)) {
			stmt = new cb_exec_sql_stmt_t();
			stmt->commandName = ESQL_DECLARE_VAR;
			stmt->src_file = filename_clean_path(var->defined_at_source_file);
			stmt->src_abs_path = filename_absolute_path(var->defined_at_source_file);
			stmt->startLine = var->defined_at_source_line;
			stmt->endLine = var->defined_at_source_line;

			cb_hostreference_ptr p = new cb_hostreference_t();
			p->hostno = n++;
			p->hostreference = var_name;
			p->lineno = var->defined_at_source_line;

			stmt->host_list->push_back(p);
			parser_data->exec_list()->push_back(stmt);
		}
		stmt = new cb_exec_sql_stmt_t();
		stmt->commandName = ESQL_COMMENT;
		stmt->src_file = filename_clean_path(var->defined_at_source_file);
		stmt->src_abs_path = filename_absolute_path(var->defined_at_source_file);
		stmt->startLine = orig_start_line;
		stmt->endLine = orig_end_line;

		parser_data->exec_list()->push_back(stmt);

	}

	return true;
}

std::map<uint64_t, uint64_t>& TPESQLProcessor::getBinarySrcLineMap() const
{
	return const_cast<std::map<uint64_t, uint64_t>&>(b_in_to_out);
}

std::map<uint64_t, uint64_t>& TPESQLProcessor::getBinarySrcLineMapReverse() const
{
	return const_cast<std::map<uint64_t, uint64_t>&>(b_out_to_in);
}

bool TPESQLProcessor::build_map_data()
{
	map_collect_files(filemap);

	std::string file;
	int line_in, line_out;
	//uint64_t k, v;

	// in to out map
	for (std::map<std::string, std::string>::const_iterator it = in_to_out.begin(); it != in_to_out.end(); ++it) {
		splitLineEntry(it->first, file, &line_in);
		int fin_id = filemap[file];

		splitLineEntry(it->second, file, &line_out);
		int fout_id = filemap[file];

		uint64_t k = ((uint64_t)fin_id << 32) + line_in;
		uint64_t v = ((uint64_t)fout_id << 32) + line_out;

		b_in_to_out[k] = v;
	}

	// out to in map
	for (std::map<std::string, std::string>::const_iterator it = out_to_in.begin(); it != out_to_in.end(); ++it) {

		splitLineEntry(it->first, file, &line_out);
		int fout_id = filemap[file];

		splitLineEntry(it->second, file, &line_in);
		int fin_id = filemap[file];

		uint64_t k = ((uint64_t)fout_id << 32) + line_out;
		uint64_t v = ((uint64_t)fin_id << 32) + line_in;

		b_out_to_in[k] = v;

	}

	// Variable declaraton source location info

	//mw.addSection("field_map");
	//mw.appendToSectionContents("field_map", main_module_driver.field_map.size());
	//for (auto it = main_module_driver.field_map.begin(); it != main_module_driver.field_map.end(); ++it) {
	//	std::string path = "";
	//	std::string var = it.key();
	//	cb_field_ptr fld = it.value();

	//	cb_field_ptr p = fld;
	//	do {
	//		path = p->sname + ":" + path;
	//		p = p->parent;
	//	} while (p);

	//	if (path.length() > 0)
	//		path = path.left(path.length() - 1);

	//	path = "WS:" + path;

	//	mw.appendToSectionContents("field_map", std::string("%1/%2@%3:%4").arg(fld->sname).arg(path).arg(fld->defined_at_source_file).arg(fld->defined_at_source_line));
	//}

	return true;
}

bool TPESQLProcessor::write_map_file(const std::string& preprocd_file)
{
	map_collect_files(filemap);

	if (parser_data->job_params()->opt_no_output)
		return true;

	MapFileWriter mw;
	std::string outfile = filename_change_ext(preprocd_file, ".cbsql.map");

	uint32_t nflags = FLAG_M_BASE;

	// global data
	mw.addSection("map");
	mw.appendToSectionContents("map", MAP_FILE_FMT_VER);
	mw.appendToSectionContents("map", nflags);
	mw.appendToSectionContents("map", input_file);
	mw.appendToSectionContents("map", output_file);
	mw.appendToSectionContents("map", filemap[input_file]);
	mw.appendToSectionContents("map", filemap[output_file]);

	// file map
	mw.addSection("filemap");
	mw.appendToSectionContents("filemap", filemap.size());
	for (std::map<std::string, int>::const_iterator it = filemap.begin(); it != filemap.end(); ++it) {
		mw.appendToSectionContents("filemap", string_format("#%d:%s", it->second, it->first));
	}

	std::string file;
	int line_in, line_out;

	// in to out map
	mw.addSection("in_to_out_map");
	mw.appendToSectionContents("in_to_out_map", in_to_out.size());

	for (std::map<std::string, std::string>::const_iterator it = in_to_out.begin(); it != in_to_out.end(); ++it) {
		splitLineEntry(it->first, file, &line_in);
		int fin_id = filemap[file];

		splitLineEntry(it->second, file, &line_out);
		int fout_id = filemap[file];

		mw.appendToSectionContents("in_to_out_map", string_format("%d@%d:%d@%d", line_in, fin_id, line_out, fout_id));
	}

	// out to in map
	mw.addSection("out_to_in_map");
	mw.appendToSectionContents("out_to_in_map", out_to_in.size());

	for (std::map<std::string, std::string>::const_iterator it = out_to_in.begin(); it != out_to_in.end(); ++it) {

		splitLineEntry(it->first, file, &line_out);
		int fout_id = filemap[file];

		splitLineEntry(it->second, file, &line_in);
		int fin_id = filemap[file];

		mw.appendToSectionContents("out_to_in_map", string_format("%d@%d:%d@%d", line_out, fout_id, line_in, fin_id));
	}

	// Variable declaraton source location info

	mw.addSection("field_map");
	auto const fmap = parser_data->get_field_map();
	mw.appendToSectionContents("field_map", fmap.size());
	for (std::map<std::string, cb_field_ptr>::const_iterator it = fmap.begin(); it != fmap.end(); ++it) {
		std::string path;
		std::string var = it->first;
		cb_field_ptr fld = it->second;

		cb_field_ptr p = fld;
		do {
			path = p->sname + ":" + path;
			p = p->parent;
		} while (p);

		if (path.length() > 0)
			path = path.substr(0, path.length() - 1);

		path = "WS:" + path;

		mw.appendToSectionContents("field_map", string_format("%s/%s@%s:%d", fld->sname, path, fld->defined_at_source_file, fld->defined_at_source_line));
	}

	return mw.writeToFile(outfile);
}

void TPESQLProcessor::add_dependency(const std::string& parent, const std::string& dep_path)
{
	std::vector<std::string> deps = (map_contains< std::string, std::vector<std::string>>(file_dependencies, parent) ? file_dependencies.at(parent) : std::vector<std::string>());
	deps.push_back(dep_path);
	file_dependencies[parent] = deps;
}

bool TPESQLProcessor::is_current_file_included()
{
	return input_file_stack.size() > 1;
}

void TPESQLProcessor::put_whenever_clause_handler(esql_whenever_clause_handler_t* ch)
{
	const char* lp = AREA_B_CPREFIX;

	switch (ch->action) {
	case WHENEVER_ACTION_CONTINUE:
		put_output_line(lp + string_format("    CONTINUE"));
		break;

	case WHENEVER_ACTION_GOTO:
		put_output_line(lp + string_format("    GO TO %s", ch->host_label));
		break;

	case WHENEVER_ACTION_PERFORM:
		put_output_line(lp + string_format("    PERFORM %s", ch->host_label));
		break;
	}
}

void TPESQLProcessor::put_smart_cursor_init_flags()
{
	const char* lp = AREA_B_CPREFIX;

	if (emitted_smart_cursor_init_flags)
		return;

	put_output_line(code_tag + "*   ESQL CURSOR INIT FLAGS (START)");
	// First we generate the flags for cursors declared in WORKING-STORAGE
	for (cb_exec_sql_stmt_ptr stmt : startup_items) {
		std::string cname = string_replace(stmt->cursorName, "_", "-");
		put_output_line(code_tag + string_format(" 01  GIXSQL-CI-F-%s PIC X.", cname));
	}
	// Then the other cursors
	auto other_crsrs = cpplinq::from(*(parser_data->exec_list())).where([](cb_exec_sql_stmt_ptr p) { return p->startup_item == 0 && p->commandName == ESQL_SELECT && !p->cursorName.empty(); }).to_vector();
	for (cb_exec_sql_stmt_ptr stmt : other_crsrs) {
		std::string cname = string_replace(stmt->cursorName, "_", "-");
		put_output_line(code_tag + string_format(" 01  GIXSQL-CI-F-%s PIC X.", cname));
	}
	put_output_line(code_tag + "*   ESQL CURSOR INIT FLAGS (END)");

	emitted_smart_cursor_init_flags = true;
}

void TPESQLProcessor::put_smart_cursor_init_check(const std::string& crsr_name, bool reset_sqlcode)
{
	std::string cname = string_replace(crsr_name, "_", "-");
	std::string crsr_init_var = "GIXSQL-CI-F-" + cname;
	std::string crsr_init_st = "GIXSQL-CI-P-" + cname;

	put_output_line(string_format(AREA_B_CPREFIX "IF %s = ' ' THEN", crsr_init_var));
	put_output_line(string_format(AREA_B_CPREFIX "    PERFORM %s", crsr_init_st));
	put_output_line(string_format(AREA_B_CPREFIX "    IF SQLCODE = 0"));
	put_output_line(string_format(AREA_B_CPREFIX "      MOVE 'X' TO %s", crsr_init_var));
	put_output_line(string_format(AREA_B_CPREFIX "    END-IF"));
	if (reset_sqlcode) {
		put_output_line(string_format(AREA_B_CPREFIX "ELSE"));
		put_output_line(string_format(AREA_B_CPREFIX "    MOVE 0 TO SQLCODE"));
	}
	put_output_line(string_format(AREA_B_CPREFIX "END-IF"));
}

bool TPESQLProcessor::put_res_host_parameters(const cb_exec_sql_stmt_ptr stmt, int* res_params_count)
{
	int rp_count = 0;
	CobolVarType f_type;
	int f_size, f_scale;
	bool emit_static = parser_data->job_params()->opt_emit_static_calls;

	for (cb_res_hostreference_ptr rp : *stmt->res_host_list) {

		std::string var_name, ind_name;
		bool has_indicator = decode_indicator(rp->hostreference.substr(1), var_name, ind_name);

		if (!parser_data->field_exists(var_name)) {
			raise_error("Cannot find host variable: " + var_name, ERR_MISSING_HOSTVAR, stmt->src_abs_path, rp->lineno);
			return false;
		}

		cb_field_ptr hr = parser_data->field_map(var_name);
		bool is_varlen = parser_data->get_actual_field_data(hr, &f_type, &f_size, &f_scale);

		// Support for group items used as host variables in SELECT statements
		// They are decomposed into their sub-elements
		if (f_type == CobolVarType::COBOL_TYPE_GROUP && !is_varlen) {

			if (has_indicator) {
				raise_error("Invalid null indicator reference: " + rp->hostreference.substr(1), ERR_INVALID_NULLIND_REF, stmt->src_abs_path, rp->lineno);
				return false;
			}

			if (hr->group_levels_count != 1) {
				raise_error("Nested levels not allowed in group variable: " + var_name, ERR_MISSING_HOSTVAR, stmt->src_abs_path, rp->lineno);
				return false;
			}

			cb_field_ptr pp = hr->children;
			if (!pp) {
				raise_error("Inconsistent data for group field : " + hr->sname, ERR_MISSING_HOSTVAR, stmt->src_abs_path, rp->lineno);
				return false;
			}

			while (pp) {

				ESQLCall pp_call(get_call_id("SetResultParams"), emit_static);
				int pp_flags = (pp->usage == Usage::Binary) ? CBL_FIELD_FLAG_BINARY : CBL_FIELD_FLAG_NONE;

				CobolVarType pp_type = CobolVarType::UNKNOWN;
				int pp_size = 0, pp_scale = 0;
				bool pp_is_varlen = parser_data->get_actual_field_data(pp, &pp_type, &pp_size, &pp_scale);
				if (pp_is_varlen) {
					raise_error("Inconsistent data for group field member: " + pp->sname, ERR_MISSING_HOSTVAR, stmt->src_abs_path, rp->lineno);
					return false;
				}

				pp_call.addParameter(pp_type, BY_VALUE);
				pp_call.addParameter(pp_size, BY_VALUE);
				pp_call.addParameter(pp_scale > 0 ? -pp_scale : 0, BY_VALUE);
				pp_call.addParameter(pp_flags, BY_VALUE);
				pp_call.addParameter(pp->sname, BY_REFERENCE);
				pp_call.addParameter(0, BY_REFERENCE);

				if (!put_call(pp_call, false))
					return false;

				rp_count++;

				pp = pp->sister;
			}

		}
		else {
			ESQLCall rp_call(get_call_id("SetResultParams"), emit_static);

			int flags = is_varlen ? CBL_FIELD_FLAG_VARLEN : CBL_FIELD_FLAG_NONE;
			flags |= (hr->usage == Usage::Binary) ? CBL_FIELD_FLAG_BINARY : CBL_FIELD_FLAG_NONE;

			rp_call.addParameter(f_type, BY_VALUE);
			rp_call.addParameter(f_size, BY_VALUE);
			rp_call.addParameter(f_scale > 0 ? -f_scale : 0, BY_VALUE);
			rp_call.addParameter(flags, BY_VALUE);
			rp_call.addParameter(var_name, BY_REFERENCE);

			if (has_indicator)
				rp_call.addParameter(ind_name, BY_REFERENCE);
			else
				rp_call.addParameter(0, BY_REFERENCE);

			if (!put_call(rp_call, false))
				return false;

			rp_count++;
		}
	}

	*res_params_count = rp_count;
	return true;
}

bool TPESQLProcessor::put_host_parameters(const cb_exec_sql_stmt_ptr stmt)
{
	CobolVarType f_type;
	int f_size, f_scale;
	bool emit_static = parser_data->job_params()->opt_emit_static_calls;

	for (cb_hostreference_ptr p : *stmt->host_list) {
		ESQLCall p_call(get_call_id("SetSQLParams"), emit_static);

		std::string var_name, ind_name;
		bool has_indicator = decode_indicator(p->hostreference.substr(1), var_name, ind_name);

		if (!parser_data->field_exists(var_name)) {
			raise_error("Cannot find host variable: " + var_name, ERR_MISSING_HOSTVAR, stmt->src_abs_path, p->lineno);
			return false;
		}

		cb_field_ptr hr = parser_data->field_map(var_name);
		bool is_varlen = parser_data->get_actual_field_data(hr, &f_type, &f_size, &f_scale);

		int flags = is_varlen ? CBL_FIELD_FLAG_VARLEN : CBL_FIELD_FLAG_NONE;
		flags |= (hr->usage == Usage::Binary) ? CBL_FIELD_FLAG_BINARY : CBL_FIELD_FLAG_NONE;

		uint32_t hr_type, hr_precision;
		uint16_t hr_scale;
		uint8_t hr_flags;
		decode_sql_type_info(hr->sql_type, &hr_type, &hr_precision, &hr_scale, &hr_flags);
		if (HAS_PICX_AS_VARCHAR(hr_flags) || parser_data->job_params()->opt_picx_as_varchar)
			flags |= CBL_FIELD_FLAG_AUTOTRIM;

		p_call.addParameter(f_type, BY_VALUE);
		p_call.addParameter(f_size, BY_VALUE);
		p_call.addParameter(f_scale > 0 ? -f_scale : 0, BY_VALUE);
		p_call.addParameter(flags, BY_VALUE);
		p_call.addParameter(var_name, BY_REFERENCE);

		if (has_indicator)
			p_call.addParameter(ind_name, BY_REFERENCE);
		else
			p_call.addParameter(0, BY_REFERENCE);

		if (!put_call(p_call, false))
			return false;
	}
	return true;
}

void TPESQLProcessor::add_preprocessed_blocks()
{
	std::vector<cb_exec_sql_stmt_ptr>* p = parser_data->exec_list();
	parser_data->_preprocessed_blocks.clear();
	for (auto e : *p) {
		std::shared_ptr<PreprocessedBlockInfo> bi = std::make_shared<PreprocessedBlockInfo>();
		bi->type = PreprocessedBlockType::ESQL;
		bi->command = e->commandName;

		bi->orig_source_file = e->src_file;
		bi->orig_start_line = e->startLine;
		bi->orig_end_line = e->endLine;

		auto in_out_start_key = std::to_string(bi->orig_start_line) + "@" + bi->orig_source_file;
		auto in_out_end_key = std::to_string(bi->orig_end_line) + "@" + bi->orig_source_file;
		if (in_to_out.find(in_out_start_key) == in_to_out.end() || in_to_out.find(in_out_end_key) == in_to_out.end()) {
			continue;
		}

		if (generated_blocks.find(e) == generated_blocks.end()) {
			continue;
		}

		auto in_out_start_val = in_to_out[in_out_start_key];
		auto in_out_end_val = in_to_out[in_out_end_key];

		bi->pp_source_file = in_out_start_val.substr(in_out_start_val.find("@") + 1);
		bi->pp_start_line = atoi(in_out_start_val.substr(0, in_out_start_val.find("@")).c_str());

		bi->pp_end_line = atoi(in_out_end_val.substr(0, in_out_end_val.find("@")).c_str());

		bi->module_name = parser_data->program_id();

		auto gb = generated_blocks[e];
		bi->pp_gen_start_line = std::get<0>(gb);
		bi->pp_gen_end_line = std::get<1>(gb);

		parser_data->_preprocessed_blocks.push_back(bi);
	}

}

bool TPESQLProcessor::decode_indicator(const std::string& orig_name, std::string& var_name, std::string& ind_name)
{
	bool res = false;
	const auto pos = orig_name.find(":");
	const auto pos2 = orig_name.find("::");	// This is to work around a problem with casts in PostgreSQL
	if (pos != std::string::npos && pos2 == std::string::npos) {
		res = true;
		var_name = orig_name.substr(0, pos);
		ind_name = orig_name.substr(pos + 1);
	}
	else {
		res = false;
		var_name = orig_name;
		ind_name = "";
	}
	return res;
}

void TPESQLProcessor::raise_error(const std::string& m, int err_code, std::string filename, int line)
{
	if (filename.empty())
		filename = current_file;

	if (line == -1)
		line = 1;

	std::string msg;
	if (!filename.empty())
		msg = string_format("%s(%d): error: %s", filename, line, m);
	else
		msg = string_format("unknown(0): error: %s", m);

	this->owner->err_data.err_messages.push_back(msg);
	if (!this->owner->err_data.err_code || err_code != ERR_ALREADY_SET)
		this->owner->err_data.err_code = err_code;
}

void TPESQLProcessor::put_whenever_handler(bool terminate_with_period)
{
	const char* lp = AREA_B_CPREFIX;

	// Optimization (sort of) if we have no handlers defined, we treat it as a special case
	if (esql_whenever_handler.not_found.action == WHENEVER_ACTION_CONTINUE &&
		esql_whenever_handler.sqlwarning.action == WHENEVER_ACTION_CONTINUE &&
		esql_whenever_handler.sqlerror.action == WHENEVER_ACTION_CONTINUE) {
		// We always need to output something to handle statement-terminating periods
		// and expect the COBOL compiler to optimize this out
		//put_output_line(lp + string_format("CONTINUE"));
		if (terminate_with_period) {
			output_lines.back() += ".";
		}
		return;
	}

	put_output_line(lp + std::string("EVALUATE TRUE"));

	// Not found
	put_output_line(lp + std::string("WHEN SQLCODE = ") + std::to_string(parser_data->job_params()->opt_norec_sqlcode));
	put_whenever_clause_handler(&esql_whenever_handler.not_found);

	// SQLWARNING
	put_output_line(lp + std::string("WHEN SQLCODE = 1"));
	put_whenever_clause_handler(&esql_whenever_handler.sqlwarning);

	// SQLERROR
	put_output_line(lp + std::string("WHEN SQLCODE < 0"));
	put_whenever_clause_handler(&esql_whenever_handler.sqlerror);

	put_output_line(lp + std::string("END-EVALUATE"));

	if (terminate_with_period) {
		output_lines.back() += ".";
	}
}

std::string TPESQLProcessor::getModuleName()
{
	return parser_data->program_id();
}

void TPESQLProcessor::splitLineEntry(const std::string& k, std::string& s, int* i)
{
	int p = k.find("@");
	s = k.substr(p + 1);
	*i = std::stoi(k.substr(0, p));
}

void TPESQLProcessor::map_collect_files(std::map<std::string, int>& filemap)
{
	int l;
	int n = 1;
	std::string f;

	if (!in_to_out.size())
		return;

	//std::string s = in_to_out[in_to_out.keys().at(0)];
	std::string s = in_to_out.begin()->second;

	splitLineEntry(s, f, &l);
	filemap[f] = n++;

	std::map<std::string, std::string>::const_iterator it = in_to_out.begin();
	auto end = in_to_out.end();
	while (it != end) {
		splitLineEntry(it->first, f, &l);
		if (!map_contains<std::string, int>(filemap, f))
			filemap[f] = n++;

		splitLineEntry(it->second, f, &l);
		if (!map_contains<std::string, int>(filemap, f))
			filemap[f] = n++;

		++it;
	}
}

std::map<std::string, std::string>& TPESQLProcessor::getSrcLineMap() const
{
	return const_cast<std::map<std::string, std::string>&>(in_to_out);
}

std::map<std::string, std::string>& TPESQLProcessor::getSrcLineMapReverse() const
{
	return const_cast<std::map<std::string, std::string>&>(out_to_in);
}

std::map<std::string, int>& TPESQLProcessor::getFileMap() const
{
	return const_cast<std::map<std::string, int>&>(filemap);
}

std::map<int, std::string> TPESQLProcessor::getReverseFileMap()
{
	std::map<int, std::string> rm;
	for (const std::string& k : map_get_keys(filemap)) {
		int v = filemap[k];
		rm[v] = k;
	}
	return rm;
}

std::map<std::string, cb_field_ptr>& TPESQLProcessor::getVariableDeclarationInfoMap() const
{
	return parser_data->get_field_map();
}

std::map<std::string, srcLocation> TPESQLProcessor::getParagraphs()
{
	return parser_data->paragraphs();
}

std::map<std::string, std::vector<std::string>> TPESQLProcessor::getFileDependencies()
{
	return file_dependencies;
}

//std::vector<PreprocessedBlockInfo*> TPESQLProcessor::getPreprocessedBlocks()
//{
//	return preprocessed_blocks;
//}

bool check_sql_type_compatibility(uint64_t type_info, cb_field_ptr var)
{
	// TODO: implement this
	return true;
}
