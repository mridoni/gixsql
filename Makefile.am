## Process this file with automake to generate Makefile.in
ACLOCAL_AMFLAGS = -I m4
SUBDIRS = libcpputils libgixpp gixpp runtime/libgixsql
EXTRA_DIST = copy/SQLCA.cpy misc/gixsql-wrapper README doc examples extra_files.mk
CLEANFILES = *~

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
#dist_examples_DATA = examples/TSQL001A.cbl

#dist-hook:
#	mkdir $(distdir)/doc && mkdir $(distdir)/examples
#	cp $(srcdir)/gixsql-tests-nunit/data/*.cbl $(srcdir)/gixsql-tests-nunit/data/*.cpy $(srcdir)/gixsql-tests-nunit/data/*.sql $(distdir)/examples
#	cp $(srcdir)/README $(distdir)/doc



