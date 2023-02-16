#pragma once

#include <climits>

enum class CobolVarType : int {
	UNKNOWN = 0,
	COBOL_TYPE_UNSIGNED_NUMBER = 1,
	COBOL_TYPE_SIGNED_NUMBER_TS = 2,        // (trailing separate)
	COBOL_TYPE_SIGNED_NUMBER_TC = 3,        // (trailing combined)
	COBOL_TYPE_SIGNED_NUMBER_LS = 4,        // (leading separate)
	COBOL_TYPE_SIGNED_NUMBER_LC = 5,        // (leading combined)
	COBOL_TYPE_UNSIGNED_NUMBER_PD = 8,		 // packed decimal
	COBOL_TYPE_SIGNED_NUMBER_PD = 9,		 // packed decimal
	COBOL_TYPE_ALPHANUMERIC = 16,
	COBOL_TYPE_UNSIGNED_BINARY = 22,
	COBOL_TYPE_SIGNED_BINARY = 23,
	COBOL_TYPE_JAPANESE = 24,
	COBOL_TYPE_GROUP = 25,
	COBOL_TYPE_FLOAT = 26,
	COBOL_TYPE_DOUBLE = 27,
	COBOL_TYPE_NATIONAL = 28,
};

// Only to be used in comparisons
#define COBOL_TYPE_MIN					((int) CobolVarType::COBOL_TYPE_UNSIGNED_NUMBER)
#define COBOL_TYPE_MAX					((int) CobolVarType::COBOL_TYPE_NATIONAL)

#define COBOL_TYPE_IS_NUMERIC(T)	(T == CobolVarType::COBOL_TYPE_UNSIGNED_NUMBER || T == CobolVarType::COBOL_TYPE_SIGNED_NUMBER_TS || \
									T == CobolVarType::COBOL_TYPE_SIGNED_NUMBER_TC || T == CobolVarType::COBOL_TYPE_SIGNED_NUMBER_LS || \
									T == CobolVarType::COBOL_TYPE_SIGNED_NUMBER_LC || T == CobolVarType::COBOL_TYPE_UNSIGNED_NUMBER_PD || \
									T == CobolVarType::COBOL_TYPE_SIGNED_NUMBER_PD || T == CobolVarType::COBOL_TYPE_UNSIGNED_BINARY || \
									T == CobolVarType::COBOL_TYPE_SIGNED_BINARY || T == CobolVarType::COBOL_TYPE_FLOAT || T ==CobolVarType::COBOL_TYPE_DOUBLE)

#define DB_NULL ULONG_MAX
