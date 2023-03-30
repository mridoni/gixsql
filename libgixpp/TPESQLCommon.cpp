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

bool ESQLParserData::field_exists(const std::string& f)
{
	return (_field_map.find(f) != _field_map.end());
}

std::map<std::string, cb_field_ptr>& ESQLParserData::get_field_map() const
{
	return const_cast<std::map<std::string, cb_field_ptr>&>(_field_map);
}
