#
# Generic Makefile to automatically build shaders, procedural DSO's, etc.
# Only one source C file (or CPP) is allowed per test. The caller should
# cd into the data directory for that test:
#    cd data
#    make
#    cd ..
#

SHARED    = -shared
LIBEXT    = .so
ifeq ($(OSTYPE), darwin)
   SHARED = -dynamiclib
   LIBEXT = .so
  #LIBEXT = .dylib
endif

SOURCE    = $(wildcard *.c *.cpp)
MAKEFILE  = ../../common/Makefile
ARNOLDSDK = ${PROJDIR}/core
BASENAME  = $(basename $(SOURCE))
TARGET    = $(addsuffix $(LIBEXT), $(BASENAME))
OBJECTS   = $(addsuffix .o,  $(BASENAME))

LIBS      = -L$(LD_LIBRARY_PATH) -lai
CC        = gcc
CXX       = g++
CFLAGS    = -D_LINUX -I$(ARNOLDSDK)/include 
ifeq ($(MACHTYPE), x86_64)
   CFLAGS += -D_64BIT -fPIC
endif
CXXFLAGS  = $(CFLAGS)

default: $(TARGET)

all : $(TARGET)

$(TARGET) : $(OBJECTS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS) $(LIBS) $(SHARED)

debug : CFLAGS += -g -D_DEBUG
debug : $(TARGET)

.PHONY : clean
clean :
	@-rm $(TARGET) $(OBJECTS) core* 2> /dev/null

.PHONY : remake
remake :
	make -f $(MAKEFILE) clean ; make -f $(MAKEFILE)

