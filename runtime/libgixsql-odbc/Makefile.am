## Process this file with automake to generate a Makefile.in

lib_LTLIBRARIES = libgixsql-odbc.la 
libgixsql_odbc_la_SOURCES = DbInterfaceManagerODBC.cpp  DbInterfaceODBC.cpp  dblib.cpp  utils.cpp DbInterfaceODBC.h utils.h

if BUILD_WINDOWS
libgixsql_odbc_la_CXXFLAGS = -I$(top_srcdir)/runtime/libgixsql -std=c++17 -I $(top_srcdir)/spdlog/include
libgixsql_odbc_la_LIBADD = $(ODBC_LIBS) 
libgixsql_odbc_la_LDFLAGS = -no-undefined -avoid-version
else
libgixsql_odbc_la_CXXFLAGS = $(ODBC_CFLAGS) -I$(top_srcdir)/runtime/libgixsql -std=c++17 -I $(top_srcdir)/spdlog/include
libgixsql_odbc_la_LIBADD = $(ODBC_LIBS) 
libgixsql_odbc_la_LDFLAGS = -no-undefined -avoid-version
endif

