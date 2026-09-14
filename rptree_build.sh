#!/bin/sh
docker run --rm -t -v ./rptree:/work/rptree eda093:latest make -C rptree
