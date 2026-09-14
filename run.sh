#!/bin/sh
exec docker run --rm -it -v ./lab_1:/work eda093:latest ./build/lsh
