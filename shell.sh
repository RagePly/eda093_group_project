#!/bin/sh
exec docker run --rm -it --cap-add=SYS_PTRACE -v ./lab_1:/work -v ./rptree:/work/rptree eda093:latest tmux
