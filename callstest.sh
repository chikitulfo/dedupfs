#!/bin/bash

oldpwd="$PWD"
PATH=$(echo $PATH | cut -f5- -d:)

mountpoint=/tmp/callstest/mp
rootdir=/tmp/callstest/rd

mkdir -p $rootdir $mountpoint
src/bbfs $rootdir $mountpoint

cd $(dirname $mountpoint)
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

cd "$oldpwd"
fusermount -u $mountpoint
