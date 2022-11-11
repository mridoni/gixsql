#pragma once

#include <string>

enum class AutoCommitMode {
	On = 1,
	Off = 2,
	Native = 3
};
struct IConnectionOptions
{
	AutoCommitMode autocommit = AutoCommitMode::Native;
	bool fixup_parameters = false;
	std::string client_encoding;
};

