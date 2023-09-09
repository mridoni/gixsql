#include "GlobalEnv.h"

#include "Logger.h"
#include "utils.h"

std::string f_get_trimmed_hostref_or_literal(void* data, int l);
int f_norec_sqlcode();
int f_varlen_length_sz();
bool f_varlen_length_sz_short();

static std::shared_ptr<GlobalEnv> genv;;

GlobalEnv::GlobalEnv()
{
	genv = std::shared_ptr<GlobalEnv>(this);

	setup_no_rec_code();
	setup_varying_length_size();

	get_trimmed_hostref_or_literal = f_get_trimmed_hostref_or_literal;
	norec_sqlcode = f_norec_sqlcode;
	varlen_length_sz = f_varlen_length_sz;
	varlen_length_sz_short = f_varlen_length_sz_short;
}

void GlobalEnv::setup_no_rec_code()
{
	char* c = getenv("GIXSQL_NOREC_CODE");
	if (c) {
		int i = atoi(c);
		if (i != 0 && i >= -999999999 && i <= 999999999) {
			__norec_sqlcode = i;
		}
	}
	spdlog::info("GixSQL: \"no record found\" code set to {})", __norec_sqlcode);
}

void GlobalEnv::setup_varying_length_size()
{
	char* c = getenv("GIXSQL_VARYING_LEN_SZ_SHORT");
	if (c) {
		int i = atoi(c);
		if (i == 1) {
			__varying_len_sz_short = true;
		}
	}

	spdlog::info("GixSQL: length indicator for VARYING fields set to {} ({} bits)", (__varying_len_sz_short ? 2 : 4), (__varying_len_sz_short ? 16 : 32));
}

int f_norec_sqlcode()
{
	return genv->__norec_sqlcode;
}

int f_varlen_length_sz()
{
	return genv->__varying_len_sz_short ? 2 : 4;
}


std::string f_get_trimmed_hostref_or_literal(void* data, int l)
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
	int actual_len = 0;
	void* actual_data = (char*)data + genv->varlen_length_sz();
	if (genv->__varying_len_sz_short) {
		actual_len =  *((uint16_t*)data);
	}
	else {
		actual_len = *((uint32_t*)data);
	}

	// Should we check the actual length against the defined length?
	//...

	std::string t = std::string((char*)actual_data, (-l) - genv->varlen_length_sz());
	return trim_copy(t);
}

bool f_varlen_length_sz_short()
{
	return genv->__varying_len_sz_short;
}