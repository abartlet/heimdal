#!/bin/sh

hpath=svn://svn.h5l.se/heimdal/trunk/heimdal
spath=svn+ssh://svn.samba.org/data/svn/lorikeet/trunk/heimdal

rm -rf heimdal-import heimdal-lorikeet

l=`pwd`/log
echo "heimdal import `date`" > ${l}
svn checkout $hpath heimdal-import >> ${l} || exit 1
svn checkout $spath heimdal-lorikeet >> ${l} || exit 1
(cd heimdal-lorikeet && find . -type f -a ! -path '*/.svn/*' | xargs rm)
(cd heimdal-lorikeet && \
  svn update heimdal-lorikeet.diff HEIMDAL-LICENCE.txt IMPORT-HEIMDAL.sh)
cd heimdal-import || exit 1
hsvnrev=`svn info  | awk '/^Revision:/ { print $2; }'`
test "X$hsvnrev" = "X" && exit 1
(find . -name '.svn' -a -type d | xargs rm -r)
(tar cf - *) | (cd ../heimdal-lorikeet && tar xf - ) || exit 1
cd ../heimdal-lorikeet
svn status > status
grep '^\?' status | cut -b2- | xargs svn add >> ${l}
grep '^\!' status | cut -b2- | xargs svn rm >> ${l}
rm status
svn rm status
(cd lib/roken && perl -pi -e 's,"roken.h",\<roken.h\>,g' *.c)
perl -pi -e 's@AC_INIT\(\[[^\]]*\],\[([^\]]*)\],\[([^\)]*)\]\)@AC_INIT([Lorikeet-Heimdal, modified for Samba4],[\1-samba],[samba-technical\@samba.org])@' configure.in || exit 1


echo "now run:"
echo "cd heimdal-lorikeet"
echo "patch -p0 < heimdal-lorikeet.diff"
echo "and fix up the damage"
echo "svn commit -m \"Merged with Heimdal svn revision $hsvnrev\""

exit 0
