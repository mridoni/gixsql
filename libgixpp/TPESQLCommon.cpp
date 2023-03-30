#include "TPESQLCommon.h"
#include "varlen_defs.h"

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


bool is_var_len_group(cb_field_ptr f)
{
	if (f->level == 49)
		return false;

	cb_field_ptr f1 = f->children;
	if (!f1 || f1->level != 49)
		return false;

	cb_field_ptr f2 = f1->sister;
	if (!f2 || f2->level != 49 || f2->pictype != PIC_ALPHANUMERIC)
		return false;

	return true;
}

bool gethostvarianttype(cb_field_ptr p, CobolVarType* type)
{
	CobolVarType tmp_type = CobolVarType::UNKNOWN;

	if (p->pictype != 0) {
		switch (p->pictype) {
		case PIC_ALPHANUMERIC:
			tmp_type = CobolVarType::COBOL_TYPE_ALPHANUMERIC;
			break;
		case PIC_NATIONAL:
			tmp_type = CobolVarType::COBOL_TYPE_NATIONAL;
			break;
		case PIC_NUMERIC:
			if (p->have_sign) {
				if (p->usage != Usage::None) {
					switch (p->usage) {
					case Usage::Packed:
						tmp_type = CobolVarType::COBOL_TYPE_SIGNED_NUMBER_PD;
						break;
					case Usage::Binary:
					case Usage::NativeBinary:
						tmp_type = CobolVarType::COBOL_TYPE_SIGNED_BINARY;
						break;
					default:
						return false;
					}
				}
				else if (p->sign_leading) {
					if (p->separate) {
						tmp_type = CobolVarType::COBOL_TYPE_SIGNED_NUMBER_LS;
					}
					else {
						tmp_type = CobolVarType::COBOL_TYPE_SIGNED_NUMBER_LC;
					}
				}
				else {
					if (p->separate) {
						tmp_type = CobolVarType::COBOL_TYPE_SIGNED_NUMBER_TS;
					}
					else {
						tmp_type = CobolVarType::COBOL_TYPE_SIGNED_NUMBER_TC;
					}
				}
			}
			else {
				if (p->usage != Usage::None) {
					switch (p->usage) {
					case Usage::Packed:
						tmp_type = CobolVarType::COBOL_TYPE_UNSIGNED_NUMBER_PD;
						break;
					case Usage::Binary:
					case Usage::NativeBinary:
						tmp_type = CobolVarType::COBOL_TYPE_UNSIGNED_BINARY;
						break;
					default:
						return false;
					}
				}
				else {
					tmp_type = CobolVarType::COBOL_TYPE_UNSIGNED_NUMBER;
				}
			}
			break;
		default:
			break;
		}
		*type = tmp_type;
		return true;
	}

	if (p->usage != Usage::None) {
		switch (p->usage) {
		case Usage::Float:
			tmp_type = CobolVarType::COBOL_TYPE_FLOAT;
			break;
		case Usage::Double:
			tmp_type = CobolVarType::COBOL_TYPE_DOUBLE;
			break;
		default:
			return false;
		}
		*type = tmp_type;
		return true;
	}

	if (p->children) {
		*type = CobolVarType::COBOL_TYPE_GROUP;
		return true;
	}

	return false;
}

unsigned int compute_field_size(cb_field_ptr f)
{
	if (!f || !f->pictype)
		return 0;

	switch (f->usage) {
	case Usage::None:
		return f->picnsize;

	case Usage::Packed:
		return (f->picnsize / 2) + 1;

	case Usage::Float:
		return sizeof(float);

	case Usage::Double:
		return sizeof(double);

	case Usage::NativeBinary:
	case Usage::Binary:
		if (f->picnsize == 1 || f->picnsize == 2) {	// 1 byte
			return 1;
		}
		else {

			if (f->picnsize == 3 || f->picnsize == 4) {	// 2 bytes
				return 2;
			}
			else {
				if (f->picnsize >= 5 || f->picnsize <= 9) {	// 4 bytes
					return 4;
				}
				else {
					if (f->picnsize >= 10 || f->picnsize <= 18) {	// 8 bytes
						return 8;
					}
					else {
						// Should never happen
						return 0;
					}
				}

			}
		}
		break;

	}
	return 0;
}

void compute_group_size(cb_field_ptr root, cb_field_ptr f, int* size)
{
	if (!f)
		return;

	if (f->pictype != 0) {
		*size += compute_field_size(f);
	}

	if (f->parent) {	// only if we are not a top-level group
		cb_field_ptr s = f->sister;

		if (s) {
			compute_group_size(root, s, size);
		}
	}

	cb_field_ptr c = f->children;
	if (c)
		root->group_levels_count++;

	while (c) {
		compute_group_size(root, c, size);
		c = c->children;
	}

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