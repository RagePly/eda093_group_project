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
    python3 \
    python3-pip \
  && rm -rf /var/lib/apt/lists/*

COPY ./lab_1/tests/requirements.txt /tmp/requirements.txt
RUN pip install --break-system-packages -r /tmp/requirements.txt

WORKDIR /work

