#!/bin/sh
exec docker run --rm -it -v ./lab_1:/work -v ./rptree/:/work/rptree eda093:latest gdb --args ./build/lsh
