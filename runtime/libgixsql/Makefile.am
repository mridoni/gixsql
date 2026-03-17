## Process this file with automake to generate a Makefile.in

lib_LTLIBRARIES = libgixsql.la 
libgixsql_la_SOURCES = Connection.cpp  ConnectionManager.cpp  Cursor.cpp  CursorManager.cpp  DataSourceInfo.cpp  DbInterfaceFactory.cpp \
			dllmain.cpp  gixsql.cpp  Logger.cpp  platform.cpp  SqlVar.cpp  SqlVarList.cpp  utils.cpp \
			Connection.h Cursor.h DataSourceInfo.h gixsql.h ICursor.h IDbInterface.h IConnectionOptions.h Logger.h sqlca.h \
			SqlVarList.h ConnectionManager.h CursorManager.h DbInterfaceFactory.h IConnection.h IDataSourceInfo.h \
			IDbManagerInterface.h ISchemaManager.h platform.h SqlVar.h utils.h default_driver.h IResultSetContextData.h custom_formatters.h \
            $(top_srcdir)/common/cobol_var_types.h $(top_srcdir)/common/varlen_defs.h $(top_srcdir)/common/cobol_var_flags.h \
			GlobalEnv.h GlobalEnv.cpp

libgixsql_la_CXXFLAGS = -std=c++17 -DSPDLOG_FMT_EXTERNAL -DNDEBUG @FMT_CFLAGS@ @SPDLOG_CFLAGS@ -I$(top_srcdir)/libgixpp -I$(top_srcdir)/common 
libgixsql_la_LIBADD = @FMT_LIBS@ @SPDLOG_LIBS@
libgixsql_la_LDFLAGS = -no-undefined -avoid-version
