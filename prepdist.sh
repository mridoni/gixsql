#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

mkdir -p $SCRIPT_DIR/doc
mkdir -p $SCRIPT_DIR/examples

cp README.md README

cp README.md $SCRIPT_DIR/doc
cp README.md $SCRIPT_DIR/doc/README

cp TESTING.md $SCRIPT_DIR/doc
cp TESTING.md $SCRIPT_DIR/doc/TESTING

cp $SCRIPT_DIR/gixsql-tests-nunit/data/*.cbl $SCRIPT_DIR/examples
cp $SCRIPT_DIR/gixsql-tests-nunit/data/*.cpy $SCRIPT_DIR/examples
cp $SCRIPT_DIR/gixsql-tests-nunit/data/*.sql $SCRIPT_DIR/examples

cat << EOF > $SCRIPT_DIR/examples/README
Example files for GixSQL
(c) 2022 Marco Ridoni
License: GPL/LGPL 3.0
==========================

These example programs and SQL scripts are part of the GixSQL test suite
that is available for distribution at https://github.com/mridoni/gixsql
EOF

rm -f $SCRIPT_DIR/extra_files.mk && touch $SCRIPT_DIR/extra_files.mk

echo "DOC_FILES = doc/README" >> $SCRIPT_DIR/extra_files.mk
echo "" >> $SCRIPT_DIR/extra_files.mk
echo "EXAMPLES_FILES = \\"  >> $SCRIPT_DIR/extra_files.mk

for f in $SCRIPT_DIR/examples/* ; do
	echo -e "\texamples/$(basename $f) \\" >> $SCRIPT_DIR/extra_files.mk 
done

echo -e "\texamples/README" >> $SCRIPT_DIR/extra_files.mk


