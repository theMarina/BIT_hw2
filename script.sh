#!/bin/bash

#rm  obj-intel64/hw1.so
mkdir obj-intel64
#rm rtn-output.txt
make obj-intel64/ex2.so
#cp given_files/clean/input.txt given_files/
#rm given_files/input.txt.bz2
../../../pin -t obj-intel64/ex2.so --  given_files/bzip2 -k -f given_files/input.txt
