#pragma once

#include <memory>

class ICursor;

struct IResultSetContextData {

	virtual ~IResultSetContextData() {}
};

struct CurrentResultSetContextData : public IResultSetContextData {
	// Nothing here, just a marker
};

struct PreparedStatementContextData : public IResultSetContextData {
	PreparedStatementContextData(std::string n) { prepared_statement_name = n; }

	std::string prepared_statement_name;
};

struct CursorContextData : public IResultSetContextData {
	CursorContextData(std::shared_ptr<ICursor> c) { cursor = c; }

	std::shared_ptr<ICursor> cursor;
};