## Process this file with automake to generate Makefile.in
ACLOCAL_AMFLAGS = -I m4
SUBDIRS = libcpputils libgixpp gixpp runtime/libgixsql
EXTRA_DIST = copy/SQLCA.cpy misc/gixsql-wrapper misc/gixsql.cmd \
             README TESTING.md doc examples extra_files.mk
CLEANFILES = *~

install-exec-hook:
	install -c -m 755 $(top_srcdir)/misc/gixsql-wrapper $(DESTDIR)$(bindir)/gixsql

uninstall-hook:
	rm -f $(DESTDIR)$(bindir)/gixsql

if ENABLE_MYSQL
SUBDIRS += runtime/libgixsql-mysql
endif

if ENABLE_ODBC
SUBDIRS += runtime/libgixsql-odbc
endif

if ENABLE_PGSQL
SUBDIRS += runtime/libgixsql-pgsql
endif

if ENABLE_ORACLE
SUBDIRS += runtime/libgixsql-oracle
endif

if ENABLE_SQLITE
SUBDIRS += runtime/libgixsql-sqlite
endif

copydir = $(prefix)/share/gixsql/copy
copy_DATA = copy/SQLCA.cpy

include extra_files.mk

docdir = $(prefix)/share/gixsql/doc
dist_doc_DATA = $(DOC_FILES)

examplesdir = $(prefix)/share/gixsql/examples
dist_examples_DATA = $(EXAMPLES_FILES)
