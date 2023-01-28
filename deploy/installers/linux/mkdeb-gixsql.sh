#!/bin/bash

export PKGDEBDIR=$WORKSPACE/pkg
export PKGNAME=gixsql-${DIST}-${HOST_PLATFORM}-${GIXSQLMAJ}.${GIXSQLMIN}.${GIXSQLREL}-${GIX_REVISION}
export PKGFILE=${PKGNAME}.deb

echo "WORKSPACE: $WORKSPACE"
echo "PKGDEBDIR: $PKGDEBDIR"
echo "PKGNAME  : $PKGNAME"
echo "PKGFILE  : $PKGFILE"
echo "DIST     : $DIST"

#rm -fr $PKGDEBDIR

if [ ! -f "$WORKSPACE/deploy/installers/linux/control-${DIST}.tpl" ] ; then
	echo "ERROR: invalid distribution id (DIST: $DIST)"
	exit 1
fi

#mkdir -p $PKGDEBDIR/DEBIAN
#if [ "$?" != "0" ] ; then echo "Cannot create package directory" ; exit 1 ; fi

# debian-binary
echo "2.0" > $PKGDEBDIR/DEBIAN/debian-binary

# control.tar.gz
cat $WORKSPACE/deploy/installers/linux/control-${DIST}.tpl | \
	sed "s/#GIXSQLMAJ#/$GIXSQLMAJ/g" | \
	sed "s/#GIXSQLMIN#/$GIXSQLMIN/g" | \
	sed "s/#GIXSQLREL#/$GIXSQLREL/g" | \
	sed "s/#GIX_REVISION#/$GIX_REVISION/g" > $PKGDEBDIR/DEBIAN/control
	
cat <<EOF > $PKGDEBDIR/DEBIAN/postinst
#!/bin/sh

exit 0
EOF

cd $WORKSPACE
find $PKGDEBDIR -type d | xargs chmod 755 
chmod 755 $PKGDEBDIR/DEBIAN/postinst

echo "dpkg-deb --build $PKGDEBDIR $PKGFILE"
dpkg-deb --build $PKGDEBDIR $PKGFILE
if [ "$?" -ne "0" ] ; then
	echo "Error while building .deb package"
    exit 1
fi


exit 0
