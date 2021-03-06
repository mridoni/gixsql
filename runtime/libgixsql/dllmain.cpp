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

#include <stdlib.h>

#include "Logger.h"

#include "spdlog/sinks/null_sink.h"

class lib_load_handler
{
public:
    lib_load_handler()
    {
        spdlog::sink_ptr gixsql_std_sink;

        spdlog::level::level_enum level = get_debug_log_level();
        if (level == spdlog::level::off) {
            gixsql_std_sink = std::make_shared<spdlog::sinks::null_sink_st>();
        }
        else {
            std::string filename = get_debug_log_file();
            gixsql_std_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(filename);
        }

        gixsql_logger = std::make_shared<spdlog::logger>("libgixsql", gixsql_std_sink);
        spdlog::set_default_logger(gixsql_logger);
        spdlog::set_level(level);
        spdlog::info("GixSQL logger started");
    }
    ~lib_load_handler()
    {
        gixsql_logger->flush();
        spdlog::shutdown();
    }

private:

    std::string get_debug_log_file() {
        char* c = getenv("GIXSQL_DEBUG_LOG_FILE");
        if (c) {
            return c;
        }
        return DEFAULT_GIXSQL_LOG_FILE;
    }

    spdlog::level::level_enum get_debug_log_level() {
        char* c = getenv("GIXSQL_DEBUG_LOG_LEVEL");
        if (!c) {
            return DEFAULT_GIXSQL_LOG_LEVEL;
        }

        std::string s = c;
        if (s == "trace") {
            return spdlog::level::trace;
        }
        else
            if (s == "debug") {
                return spdlog::level::debug;
            }
            else
                if (s == "info") {
                    return spdlog::level::info;
                }
                else
                    if (s == "warn") {
                        return spdlog::level::warn;
                    }
                    else
                        if (s == "error") {
                            return spdlog::level::err;
                        }
                        else
                            if (s == "critical") {
                                return spdlog::level::critical;
                            }
                            else
                                if (s == "off") {
                                    return spdlog::level::off;
                                }
                                else
                                    return DEFAULT_GIXSQL_LOG_LEVEL;
    }


} lib_load_handler_hook;


