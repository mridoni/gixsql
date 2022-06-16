## Process this file with automake to generate a Makefile.in

lib_LTLIBRARIES = libgixsql-pgsql.la 
libgixsql_pgsql_la_SOURCES = DbInterfaceManagerPGSQL.cpp  DbInterfacePGSQL.cpp  dblib.cpp  utils.cpp DbInterfacePGSQL.h utils.h

libgixsql_pgsql_la_CXXFLAGS = $(LIBPQ_CFLAGS) -I$(top_srcdir)/runtime/libgixsql -std=c++17 -I $(top_srcdir)/spdlog/include
libgixsql_pgsql_la_LIBADD = $(LIBPQ_LIBS)
libgixsql_pgsql_la_LDFLAGS = -no-undefined -avoid-version


