FROM debian:trixie

RUN apt-get update -y \
  && apt-get install -y \
    build-essential \
    gdb \
    cmake \
    libreadline-dev \
    libncurses5-dev \
    libncursesw5-dev \
    tmux \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /work

