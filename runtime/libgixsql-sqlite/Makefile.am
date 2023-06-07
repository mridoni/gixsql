## Process this file with automake to generate a Makefile.in

lib_LTLIBRARIES = libgixsql-sqlite.la
libgixsql_sqlite_la_SOURCES = DbInterfaceManagerSQLite.cpp  DbInterfaceSQLite.cpp  dblib.cpp  utils.cpp DbInterfaceSQLite.h utils.h \
							 sqlite3.c sqlite3.h

libgixsql_sqlite_la_CXXFLAGS = -I$(top_srcdir)/common -I$(top_srcdir)/runtime/libgixsql -std=c++17 -DSPDLOG_FMT_EXTERNAL -DNDEBUG
libgixsql_sqlite_la_LIBADD = 
libgixsql_sqlite_la_LDFLAGS = -lfmt -no-undefined -avoid-version




