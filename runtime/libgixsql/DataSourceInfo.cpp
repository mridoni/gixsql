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

#include "DataSourceInfo.h"
#include "utils.h"
#include "default_driver.h"

#include <string>
#include <sstream>
#include <filesystem>
#include <stdlib.h>
#include <stdio.h>

#define CS_TYPE_GIXSQL_FULL			1
#define CS_TYPE_GIXSQL_DFLT_DRVR	2
#define CS_TYPE_OCESQL				3

DataSourceInfo::DataSourceInfo()
{
	conn_string.clear();
	port = 0;
}

DataSourceInfo::~DataSourceInfo()
{
}

static std::string rx_escape(const std::string s)
{
	if (s.size() == 1) {
		char c = s[0];
		if (c == '[' || c == ']' || c == '(' || c == ')' || c == '{' || c == '}' || c == '*' || c == '+' || c == '?' || c == '|' || c == '^' || c == '$' || c == '.' || c == '\\') {
			return "\\" + s;
		}
	}
	return s;
}

/*
(?:((?:gixsql|mysql|pgsql|odbc)\:))(\/\/[A-Za-z0-9\-_]+)(:[0-9]+)?(\/[A-Za-z0-9\-_]+)?
*/
int DataSourceInfo::init(const std::string& data_source, const std::string& dbname, const std::string& username, const std::string& password)
{
	// Format: type://user.password@host[:port][/database][?default_schema=schema&par1=val&par2=val]
	// e.g. pgsql://user.password@localhost:5432/postgres?default_schema=public&par1=val1&par2=val2

	/*
	(?:((?:gixsql|mysql|pgsql|odbc)\:))(\/\/(?:(([^:]+)\.([^:]+)@)?)[A-Za-z0-9\-_]+)(:[0-9]+)?(\/[A-Za-z0-9\-_]+)?
	*/

	// First we try the complete connection string
	std::string gixsql_usrpwd_sep = getenv("GIXSQL_USRPWD_SEP") ? getenv("GIXSQL_USRPWD_SEP") : "";
	if (!gixsql_usrpwd_sep.empty() && gixsql_usrpwd_sep.size() == 1) {
		this->usrpwd_sep = ".";
	}

	// Special case: if it is SQLite, we treat it as a filename
	bool uses_default_driver = false;
	if (is_sqlite(data_source, &uses_default_driver)) {

		if (!uses_default_driver && data_source.size() <= 10)
			return 1;

		std::string sqlite_path;
		std::string sqlite_opts;
		std::string path_opts = data_source.substr(uses_default_driver ? 0 : 9);

		if (path_opts.find('?') == std::string::npos) {
			sqlite_path = path_opts;
		}
		else {
			sqlite_path = path_opts.substr(0, path_opts.find('?'));
			retrieve_driver_options(path_opts);
		}

		this->dbtype = "sqlite";
		this->host = trim_copy(sqlite_path);
		this->dbname = std::filesystem::path(this->host).filename().string();
		return 0;
	}

	std::string connstring_rx_text_full = R"(^(?:((?:gixsql|mysql|pgsql|odbc|oracle|sqlite)\:))(\/\/(?:(([^:]+)##GIXSQL_USRPWD_SEP##([^:]+)@)?)[A-Za-z0-9\-_\.]+)(:[0-9]+)?(\/[A-Za-z0-9\-_]+)?\ *)";
	std::string connstring_rx_text_dflt_drvr = R"(^((?:(([^:]+)##GIXSQL_USRPWD_SEP##([^:]+)@)?)[A-Za-z0-9\-_\.]+)(:[0-9]+)?(\/[A-Za-z0-9\-_]+)?\ *)";
	std::string connstring_rx_text_ocesql = R"(^((?:(([^:]+)@)?)([A-Za-z0-9\-_\.]+))(:[0-9]+)?\ *)";
	std::string connstring_rx_text;

	connstring_rx_text = string_replace(connstring_rx_text_full, "##GIXSQL_USRPWD_SEP##", rx_escape(this->usrpwd_sep));

	std::regex rxConnString(connstring_rx_text);
	std::smatch cm;
	if (!regex_search(data_source, cm, rxConnString, std::regex_constants::match_default)) {

		connstring_rx_text = string_replace(connstring_rx_text_dflt_drvr, "##GIXSQL_USRPWD_SEP##", rx_escape(this->usrpwd_sep));
		rxConnString.assign(connstring_rx_text);

		if (!has_default_driver() || !regex_search(data_source, cm, rxConnString, std::regex_constants::match_default)) {
			return 1;
		}
		else {

			std::smatch ocematch;
			connstring_rx_text = connstring_rx_text_ocesql;
			rxConnString.assign(connstring_rx_text);

			// regex_match instead of regex_search is used because we need stricter matching here
			if (!regex_match(data_source, ocematch, rxConnString)) {
				conn_string_type = CS_TYPE_GIXSQL_DFLT_DRVR;
			}
			else {
				cm = ocematch;
				conn_string_type = CS_TYPE_OCESQL;
			}
		}

	}
	else {
		conn_string_type = CS_TYPE_GIXSQL_FULL;
		this->conn_string = data_source;
	}

#if defined(_DEBUG) && defined(VERBOSE)
	fprintf(stderr, "Data source: [%s] is of type [%d]\n", data_source.c_str(), this->conn_string_type);
#endif

	this->conn_string = data_source;

	switch (conn_string_type) {
	case CS_TYPE_GIXSQL_FULL:
		if (!retrieve_gixsql_full_params(cm))
			return 1;

		retrieve_driver_options(data_source);
		break;

	case CS_TYPE_GIXSQL_DFLT_DRVR:
		if (!retrieve_gixsql_dflt_drvr_params(cm))
			return 1;

		retrieve_driver_options(data_source);
		break;

	case CS_TYPE_OCESQL:
		if (!retrieve_ocesql_params(cm))
			return 1;

		retrieve_driver_options(data_source);
		break;
	}

	if (!username.empty() && !password.empty()) {
		this->username = username;
		this->password = password;
	}
	else {
		if (!username.empty() && password.empty()) {
			std::string up = username;
			int p = up.find(this->usrpwd_sep);
			if (p != std::string::npos) {
				this->username = up.substr(0, p);
				if (p < (up.size() - 1)) {
					this->password = up.substr(p + 1);
				}
			}
		}
	}

	if (!dbname.empty()) {
		this->dbname = dbname;
	}

	trim(this->host);
	trim(this->dbname);
	trim(this->username);
	trim(this->password);

	return 0;
	}

void DataSourceInfo::retrieve_driver_options(const std::string& data_source)
{

	int p = data_source.find("?");
	if (p != std::string::npos && p < (data_source.size() - 1)) {
		std::string ps = data_source.substr(p + 1);
		trim(ps);
		auto params = string_split(ps, "&");
		for (auto pp : params) {
			auto ppi = string_split(pp, "=");
			if (ppi.size() == 2 && !ppi[0].empty() && !ppi[1].empty()) {
				this->options[ppi[0]] = ppi[1];
			}
		}
	}
}

std::string DataSourceInfo::get()
{
	return conn_string;
}

LIBGIXSQL_API std::string DataSourceInfo::getDbType()
{
	return dbtype;
}

std::string DataSourceInfo::getHost()
{
	return host;
}

int DataSourceInfo::getPort()
{
	return port;
}

std::string DataSourceInfo::getDbName()
{
	return dbname;
}

std::string DataSourceInfo::getUsername()
{
	return username;
}

std::string DataSourceInfo::getPassword()
{
	return password;
}


LIBGIXSQL_API std::string DataSourceInfo::getName()
{
	std::string conn_name = this->getDbType() + "://" + this->getUsername();

	if (this->getDbType() == "odbc")
		conn_name += ("@" + this->getHost());
	else
		conn_name += ("@" + this->getHost() + "/" + this->getDbName());

	return conn_name;
}

LIBGIXSQL_API void DataSourceInfo::setPassword(std::string pwd)
{
	this->password = pwd;
}


std::string DataSourceInfo::dump(bool with_password)
{
#ifdef _DEBUG
	std::string s;

	s += "{ conn_string: [" + conn_string + "], ";
	s += "dbtype: [" + dbtype + "], ";
	s += "host: [" + host + "], ";
	s += "port: [" + std::to_string(port) + "], ";
	s += "dbname: [" + dbname + "], ";
	s += "username: [" + username + "], ";
	s += "password: [" + password + "] }";

	int i = 0;
	for (auto it = options.begin(); it != options.end(); ++it) {
		s += "option " + std::to_string(i++) + ": " + it->first + " => " + it->second + ", ";
	}

	return s;
#else
	return "";
#endif
}

bool DataSourceInfo::has_default_driver()
{
	std::string default_driver = GIXSQL_DEFAULT_DRIVER;
	if (!default_driver.empty())
		return true;

	const char* c = getenv("GIXSQL_DEFAULT_DRIVER");
	if (c && !std::string(c).empty())
		return true;

	return false;
}

std::string DataSourceInfo::get_default_driver()
{
	std::string default_driver = GIXSQL_DEFAULT_DRIVER;
	if (!default_driver.empty())
		return default_driver;

	const char* c = getenv("GIXSQL_DEFAULT_DRIVER");
	if (c && !std::string(c).empty())
		return c;

	return "";
}

bool DataSourceInfo::retrieve_gixsql_full_params(const std::smatch& cm)
{
	for (int i = 1; i < cm.size(); i++) {
		std::string m = cm[i];

		if (ends_with(m, ":")) {
			this->dbtype = m.substr(0, m.size() - 1);
			continue;
		}

		if (starts_with(m, "//")) {
			int p_at = m.find("@");
			if (p_at == std::string::npos)
				this->host = m.substr(2);
			else
				if (p_at < (m.size() - 1))
					this->host = m.substr(p_at + 1);
			continue;
		}

		if (starts_with(m, "/")) {
			this->dbname = m.substr(1);
			continue;
		}

		if (starts_with(m, ":")) {
			this->port = atoi(m.substr(1).c_str());
			continue;
		}

		if (ends_with(m, "@")) {
			std::string up = m.substr(0, m.size() - 1);
			int p = up.find(this->usrpwd_sep);
			if (p != std::string::npos) {
				this->username = up.substr(0, p);
				if (p < (up.size() - 1)) {
					this->password = up.substr(p + 1);
				}
			}
			else {
				this->username = up;
			}
			continue;
		}
	}
	return true;
}

bool DataSourceInfo::retrieve_gixsql_dflt_drvr_params(const std::smatch& cm)
{
	this->dbtype = get_default_driver();
	if (this->dbtype.empty())
		return false;

	if (cm.size() > 1) {
		std::string h = cm[1].str();
		int p_at = h.find("@");
		if (p_at == std::string::npos)
			this->host = h;
		else
			if (p_at < (h.size() - 1))
				this->host = h.substr(p_at + 1);
	}

	for (int i = 1; i < cm.size(); i++) {
		std::string m = cm[i];

		if (starts_with(m, "/")) {
			this->dbname = m.substr(1);
			continue;
		}

		if (starts_with(m, ":")) {
			this->port = atoi(m.substr(1).c_str());
			continue;
		}

		if (ends_with(m, "@")) {
			std::string up = m.substr(0, m.size() - 1);
			int p = up.find(this->usrpwd_sep);
			if (p != std::string::npos) {
				this->username = up.substr(0, p);
				if (p < (up.size() - 1)) {
					this->password = up.substr(p + 1);
				}
			}
			else {
				this->username = up;
			}
			continue;
		}
	}
	return true;
}

bool DataSourceInfo::retrieve_ocesql_params(const std::smatch& cm)
{
	if (cm.size() < 6)
		return false;

	this->dbtype = get_default_driver();
	if (this->dbtype.empty())
		return false;

	if (cm[3].matched)
		this->dbname = cm[3].str();

	if (cm[4].matched)
		this->host = cm[4].str();

	if (cm[5].matched)
		this->port = atoi(cm[5].str().substr(1).c_str());

	return true;
}

LIBGIXSQL_API const std::map<std::string, std::string>& DataSourceInfo::getOptions()
{
	return options;
}


std::string DataSourceInfo::toConnectionString(bool use_pwd, std::string pwd)
{
	std::ostringstream res;
	res << dbtype << "://" << username << ":";
	if (use_pwd)
		res << ((!pwd.empty()) ? pwd : password);

	res << "@" << host;

	if (port > 0)
		res << ":" << std::to_string(port);

	if (!dbname.empty()) {
		res << "/" << dbname;

	}

	return res.str();
}

bool DataSourceInfo::is_sqlite(const std::string& ds, bool *uses_default_driver)
{
	if (starts_with(ds, "sqlite://")) {
		*uses_default_driver = false;
		return true;
	}

	if (get_default_driver() == "sqlite" && 
		!starts_with(ds, "pgsql://") &&
		!starts_with(ds, "mysql://") &&
		!starts_with(ds, "odbc://") &&
		!starts_with(ds, "oracle://")
	) {
		*uses_default_driver = true;
		return true;
	}

	return false;
}
