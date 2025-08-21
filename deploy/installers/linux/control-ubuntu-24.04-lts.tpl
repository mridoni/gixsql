Package: gixsql
Section: devel
Version: #GIXSQLMAJ#.#GIXSQLMIN#.#GIXSQLREL#~#GIX_REVISION#-1
Priority: optional
Architecture: amd64
Depends: libmysqlclient21, libpq5, unixodbc, libspdlog1.12, libnng1
Installed-Size: 23552
Maintainer: Marco Ridoni <m.ridoni@mediumgray.info>
Description: GixSQL is an ESQL preprocessor and a series of runtime libraries to enable GnuCOBOL to access ODBC, MariaDB/MySQL, PostgreSQL, Oracle and SQLite databases
 .
 GixSQL comprises a preprocessor (a standalone executable or a library) 
 and a set of runtime libraries. libgixsql.so is the main library 
 and it is the one that will be either linked to your COBOL modules or
 needs to be pre-loaded by the COBOL runtime otherwise. 
 The other libraries (e.g. libgisql-odbc.so) will be dynamically 
 loaded at runtime depending on the DB you chose in your configuration. 
 It is possible, if so desired, to develop additional libraries 
 for specific DBMSs not covered in the standard install.
