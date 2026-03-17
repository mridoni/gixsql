## Process this file with automake to generate a Makefile.in

lib_LTLIBRARIES = libgixsql-odbc.la 
libgixsql_odbc_la_SOURCES = DbInterfaceManagerODBC.cpp  DbInterfaceODBC.cpp  dblib.cpp  utils.cpp DbInterfaceODBC.h utils.h

if BUILD_WINDOWS
libgixsql_odbc_la_CXXFLAGS = -I$(top_srcdir)/runtime/libgixsql -I$(top_srcdir)/common -I$(top_srcdir)/libgixpp -std=c++17 -DSPDLOG_FMT_EXTERNAL -DNDEBUG
libgixsql_odbc_la_LIBADD = -lodbc32
else
libgixsql_odbc_la_CXXFLAGS = $(ODBC_CFLAGS) -I$(top_srcdir)/common -I$(top_srcdir)/runtime/libgixsql -I$(top_srcdir)/libgixpp -std=c++17 -DSPDLOG_FMT_EXTERNAL -DNDEBUG
libgixsql_odbc_la_LIBADD = $(ODBC_LIBS) 
endif

libgixsql_odbc_la_CXXFLAGS += @FMT_CFLAGS@

libgixsql_odbc_la_LIBADD += @FMT_LIBS@
libgixsql_odbc_la_LDFLAGS = -no-undefined -avoid-version
