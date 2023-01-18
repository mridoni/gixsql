#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

# If /E is specified on the command line we take the following variables from the current environment
if [ "$1" != "/E" ] ; then

	# Check these variables and in case adjust them depending on your environment
	HOST_PLATFORM=x64
	# Check these variables (end)
	
	# These indicates the current version of GixSQL included in Gix-IDE
	GIXSQLMAJ=1
	GIXSQLMIN=0
	GIXSQLREL=20dev1
	# These indicates the current version of GixSQL included in Gix-IDE (end)

fi

echo "Configuring version ($GIXSQLMAJ.$GIXSQLMIN.$GIXSQLREL) in header file for GixSQL"
echo "#define VERSION \"$GIXSQLMAJ.$GIXSQLMIN.$GIXSQLREL\"" > $SCRIPT_DIR/config.h
