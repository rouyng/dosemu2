top_builddir=../../..
include $(top_builddir)/Makefile.conf

CFILES=cpu.c do_vm86.c cputime.c kvm.c coopth_vm86.c


SFILES=kvmmon.S

include $(REALTOPDIR)/src/Makefile.common

all: lib

install: all
