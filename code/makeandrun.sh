#!/bin/sh
rm DISKFILE
./mount.sh
make clean
make
cd benchmark
make clean
make
./test_case
cd ..
./unmount.sh