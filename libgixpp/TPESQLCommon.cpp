#include "TPESQLCommon.h"

void ESQLParserData::add_to_field_map(std::string k, cb_field_ptr f)
{
#if defined(_WIN32) && defined(_DEBUG) && defined(VERBOSE)
	char bfr[512];
	sprintf(bfr, "Adding field to field_map: %s\n", k.c_str());
	OutputDebugStringA(bfr);
#endif
	_field_map[k] = f;
}

cb_field_ptr ESQLParserData::field_map(std::string k)
{
	return field_exists(k) ? _field_map[k] : nullptr;
}

std::tuple<uint64_t, int, int, std::string> ESQLParserData::field_sql_type_info(const std::string& n)
{
	return _field_sql_type_info[n];
}

std::map<std::string, sql_type_info_t> ESQLParserData::field_sql_type_info() const
{
	return _field_sql_type_info;
}

void ESQLParserData::field_sql_type_info_add(const std::string& n, sql_type_info_t t)
{
	_field_sql_type_info[n] = t;
}

std::shared_ptr< ESQLJobParams > ESQLParserData::job_params() const
{
	return _job_params;
}

std::vector<cb_exec_sql_stmt_ptr>* ESQLParserData::exec_list() const
{
	return _exec_list;
}

std::string ESQLParserData::program_id()
{
	return _program_id;
}

void ESQLParserData::set_program_id(std::string id)
{
	_program_id = id;
}

std::map<std::string, srcLocation>& ESQLParserData::paragraphs()
{
	return _paragraphs;
}

void ESQLParserData::paragraph_add(std::string n, srcLocation p)
{
	_paragraphs[n] = p;
}

ESQLParserData::ESQLParserData()
{
	_exec_list = new std::vector<cb_exec_sql_stmt_ptr>();
}

bool ESQLParserData::field_exists(const std::string& f)
{
	return (_field_map.find(f) != _field_map.end());
}

std::map<std::string, cb_field_ptr>& ESQLParserData::get_field_map() const
{
	return const_cast<std::map<std::string, cb_field_ptr>&>(_field_map);
}

bool ESQLParserData::get_actual_field_data(cb_field_ptr f, CobolVarType* type, int* size, int* scale)
{
	bool is_varlen = is_var_len_group(f);
	bool is_explicit_varlen = f->is_varlen;

	if (!is_varlen && !is_explicit_varlen) {
		gethostvarianttype(f, type);
		if (*type == CobolVarType::COBOL_TYPE_GROUP) {
			*size = 0;
			f->group_levels_count = 0;
			compute_group_size(f, f, size);
		}
		else {
			*size = f->picnsize;
		}
		*scale = f->scale;
	}
	else {
		std::string f_actual_name;
		if (is_varlen) {
			f_actual_name = f->children->sister->sname;

		}
		else {	// is_implicit_varlen
			f_actual_name = f->sname + "-" + this->job_params()->opt_varlen_suffix_data;
		}

		cb_field_ptr f_actual = this->field_map(f_actual_name);

		if (f_actual) {
			gethostvarianttype(f_actual, type);
			*size = f_actual->picnsize + VARLEN_LENGTH_SZ;
			*scale = f_actual->scale;
		}
		else {
			*type = CobolVarType::COBOL_TYPE_ALPHANUMERIC;
			*size = f->picnsize + VARLEN_LENGTH_SZ;
			*scale = f->scale;
		}

	}
	return is_varlen;
}