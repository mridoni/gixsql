#pragma once

#include <vector>
#include <ios>

#include "fmt/format.h"

using std_binary_data = std::vector<unsigned char>;

template <> struct fmt::formatter<CobolVarType> {

	constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
		return ctx.end();
	}

	template <typename FormatContext>
	auto format(const CobolVarType& p, FormatContext& ctx) const -> decltype(ctx.out()) {
		return fmt::format_to(ctx.out(), "{}", (int)p);
	}

};

#if 0
template <> struct fmt::formatter<std_binary_data> {

	constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
		return ctx.end();
	}

	template <typename FormatContext>
	auto format(const unsigned char *p, FormatContext& ctx) const -> decltype(ctx.out()) {
		if (!p.isBinary())
			return fmt::format_to(ctx.out(), "{}", (char *) p);
		else {
			std::stringstream ss;
			unsigned char* data = p.getDbData().data();
			int datalen = p.getRealDataLength() <= 256 ? p.getRealDataLength() : 256;
			for (int i = 0; i < datalen; ++i)
				ss << std::hex << (int)data[i];

			return fmt::format_to(ctx.out(), "{}", ss.str());
		}
	}

};
#endif