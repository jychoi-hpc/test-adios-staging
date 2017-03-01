VT = n
CC = mpicc
CXX = mpicxx
CFLAGS = -g -DVERSION=$(shell git describe --abbrev=7 --dirty --always --tags) -fPIC
LDFLAGS =

ADIOS_INC = $(shell adios_config -c)
ADIOS_LIB = $(shell adios_config -l)

TCL_INCLUDE_SPEC ?= -I/opt/local/include
TCL_LIB_SPEC ?= -L/opt/local/lib -ltcl

INCS = $(ADIOS_INC)
LIBS = $(ADIOS_LIB)

ifneq (,${HOST})
  SYSTEMS := ${HOST}
else
  SYSTEMS := $(shell hostname)
endif

ifneq (,$(findstring titan, $(SYSTEMS)))
  CC = cc
  CXX = CC
endif

ifneq (,$(findstring cori, $(SYSTEMS)))
  CC = cc
  CXX = CC
  LDFLAGS += -zmuldefs
endif

ifneq (,$(findstring sith, $(SYSTEMS)))
  LDFLAGS += -Wl,--allow-multiple-definition
endif

VT_CC = ${CC}
VT_CXX = ${CXX}
EXE = adios_icee
ifeq ($(VT),y)
  VT_CC = scorep ${CC}
  VT_CXX = scorep ${CXX}
  EXE = adios_icee+sp
endif

.PHONE: all clean ggo

all: $(EXE)

.c.o:
	$(VT_CC) $(CFLAGS) $(INCS) -c $<

.cpp.o:
	$(VT_CXX) $(CFLAGS) $(INCS) -c $<

.cxx.o:
	$(VT_CXX) $(CFLAGS) $(INCS) -c $<

adios_write: adios_write.cpp
	$(VT_CXX) $(CFLAGS) $(INCS) -o $@ $^ $(LIBS)

adios_read: adios_read.cpp icee_cmdline.c
	$(VT_CXX) $(CFLAGS) $(INCS) -o $@ $^ $(LIBS)

$(EXE): adios_icee.o icee_cmdline.o filelock.o
	$(VT_CXX) $(LDFLAGS) -o $@ $^ $(LIBS)

lib: adios_icee.o filelock.o icee_cmdline.o
	swig -c++ adios_icee.i
	$(VT_CXX) -c -fPIC $(TCL_INCLUDE_SPEC) adios_icee_wrap.cxx
	$(VT_CXX) -shared -o libadios_icee.so adios_icee_wrap.o adios_icee.o filelock.o icee_cmdline.o $(TCL_LIB_SPEC) $(LIBS)

ggo:
	gengetopt --input=icee_cmdline.ggo --no-handle-version

clean:
	rm -f *.o *.*~ $(EXE)
