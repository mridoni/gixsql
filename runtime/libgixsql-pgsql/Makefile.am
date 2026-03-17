## Process this file with automake to generate a Makefile.in

lib_LTLIBRARIES = libgixsql-pgsql.la 
libgixsql_pgsql_la_SOURCES = DbInterfaceManagerPGSQL.cpp  DbInterfacePGSQL.cpp  dblib.cpp  utils.cpp DbInterfacePGSQL.h utils.h

libgixsql_pgsql_la_CXXFLAGS = $(LIBPQ_CFLAGS) @FMT_CFLAGS@ -I$(top_srcdir)/common -I$(top_srcdir)/runtime/libgixsql -I$(top_srcdir)/common -std=c++17 -DSPDLOG_FMT_EXTERNAL -DNDEBUG
libgixsql_pgsql_la_LIBADD = $(LIBPQ_LIBS) @FMT_LIBS@
libgixsql_pgsql_la_LDFLAGS = -no-undefined -avoid-version


