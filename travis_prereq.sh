#!/bin/bash

sudo add-apt-repository ppa:dosemu2/ppa -y
sudo add-apt-repository ppa:stsp-0/djgpp -y
sudo apt-get update -q
sudo apt-get install -y comcom32\
                        clang nasm libstdc++-5-dev\
                        git bash autoconf autotools-dev automake\
                        coreutils linuxdoc-tools bison flex gawk sed\
                        libx11-dev libxext-dev libslang2-dev xfonts-utils\
                        libgpm-dev libasound2-dev libsdl2-dev\
                        libsdl1.2-dev ladspa-sdk libfluidsynth-dev\
                        libao-dev libvdeplug-dev libreadline-dev\
                        binutils-dev pkg-config\
                        python-pexpect djgpp