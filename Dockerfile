FROM ubuntu:22.04

RUN apt-get update && apt-get -y upgrade
RUN apt-get -y install gcc g++ make neovim valgrind

COPY . /root

WORKDIR /root/grading
ENTRYPOINT ["make", "build-libs", "run"]
