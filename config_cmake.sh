#!/bin/sh
docker run --rm -t -v ./lab_1:/work eda093:latest cmake -S code -B build
