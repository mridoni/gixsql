#!/bin/bash

export PKGFILE=$PKGNAME.deb


# debian-binary
echo "2.0" > $PKGDEBDIR/DEBIAN/debian-binary

# control.tar.gz
cat $WORKSPACE/deploy/installers/linux/control-gixsql.tpl | \
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

dpkg-deb --build $PKGDEBDIR $PKGFILE
if [ "$?" -ne "0" ] ; then
	echo "Error while building .deb package"
    exit 1
fi


exit 0
