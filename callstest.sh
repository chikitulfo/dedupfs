#!/bin/bash

oldpwd="$PWD"
rutascript="$(dirname "$(readlink -f "$0")")"
if [ -z "$rutascript" ] 
then
rutascript="."
fi

mountpoint=testdir/mp
rootdir=testdir/rd

cd "$rutascript"
mkdir -p $rootdir $mountpoint
src/dedupfs $rootdir $mountpoint

cd $(dirname "$mountpoint")
cd mp
ls
touch nuevo
#echo hola >nuevo
#mkdir dir
#echo hola >dir/lala
#touch vacio
#ls
#cp nuevo nuevocopia
#cp -l nuevo nuevohardlink
#ln -s dir/lala lala
#cat nuevo
#cat lala
#ls -l
#rmdir dir #error
#rm -r dir
#rm nuevohardlink
#rm nuevocopia
#rm lala
#cat vacio
#rm *

sleep 1
cd "$rutascript"
fusermount -u $mountpoint
cd "$oldpwd"
