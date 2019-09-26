include $(SRCDIR)/../ert.mk

# BSP archive is created from SDK generated bsp
# Extract to build dir in includes target
# Also update the MicroBlaze linker script when it changes
SRC := $(SRCDIR)/scheduler.cpp
OBJ := $(BLDDIR)/scheduler.o
ELF := $(BLDDIR)/sched.elf
BIN := $(BLDDIR)/sched.bin
BSP := $(BLDDIR)/bsp
RTS := $(SRCDIR)/../..

ifndef SCHED_VERSION
 export SCHED_VERSION := 0x$(shell git rev-list -1 HEAD $(SRC) | cut -c1-8)
endif

MYCFLAGS := -I$(BSP)/include -I$(RTS) $(DEFINES) -DERT_VERSION=$(SCHED_VERSION) -DERT_SVERSION=\"$(SCHED_VERSION)\"
MYLFLAGS :=  -Wl,-T,$(BLDDIR)/lscript.ld

$(OBJ): $(SRC) $(BSP).extracted $(RTS)/core/include/ert.h
	$(CPP) $(MYCFLAGS) -c -o $@ $<

$(ELF): $(OBJ)
	$(LINK) $(MYLFLAGS) -o $@ $< -L$(BSP)/lib -lxil

$(BIN): $(ELF)
	$(MB_HOME)/bin/mb-objcopy -I elf32-microblaze -O binary $< $@

.PHONY: ert
ert: $(BIN)
