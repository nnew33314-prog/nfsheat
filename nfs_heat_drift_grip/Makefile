# Library metadata.

DEBUG_FLAGS = -D__FINAL__=1
LOG_TYPE = -D__USE_PRINTF__
BUILD_TYPE = _final

ifeq ($(DEBUG),1)
    DEBUG_FLAGS = -D__FINAL__=0
    BUILD_TYPE = _debug
endif

TYPE         := $(BUILD_TYPE)
FINAL        := $(DEBUG_FLAGS)
BUILD_FOLDER := $(shell pwd)/../../bin/plugins
OUTPUT_PRX   := $(shell basename $(CURDIR))
TARGET       := $(BUILD_FOLDER)/prx$(TYPE)/$(OUTPUT_PRX)
TARGET_ELF   := $(BUILD_FOLDER)/elf$(TYPE)/$(OUTPUT_PRX)
TARGETSTUB   := $(OUTPUT_PRX).so

# Libraries linked into the ELF.
LIBS := -lSceLibcInternal -lGoldHEN_Hook -lkernel -lSceSysmodule -lScePad -lSceUserService -lSceSystemService

EXTRAFLAGS := $(DEBUG_FLAGS) $(LOG_TYPE) -fcolor-diagnostics -Wall

# You likely won't need to touch anything below this point.
# Root vars
TOOLCHAIN     := $(OO_PS4_TOOLCHAIN)
GH_SDK        := $(GOLDHEN_SDK)

# Toolchain
CC      := clang
AR      := llvm-ar
LD      := ld.lld
OBJCOPY := llvm-objcopy

# GoldHEN SDK
GH_INC      := -I$(GH_SDK)/include -I$(GH_SDK)/include/GoldHEN
GH_LIB      := -L$(GH_SDK)/lib
GH_LINK     := $(GH_LIB) $(LIBS)

# Toolchain paths
SYSROOT := $(TOOLCHAIN)
SYSINC  := -isystem $(SYSROOT)/include -I$(SYSROOT)/include
SYSLD   := -L$(SYSROOT)/lib -L$(SYSROOT)/lib/gcc/x86_64-pc-freebsd12/18.1.0

# Compiler flags
CFLAGS  := -O2 -pipe -fno-builtin -fno-stack-protector -fno-exceptions -fno-rtti -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-math-errno -fPIC -funwind-tables -m64 -mno-sse -mno-avx -mcmodel=large -target x86_64-pc-freebsd12-elf -ffreestanding $(SYSINC) $(GH_INC) $(EXTRAFLAGS)
LDFLAGS := -Wl,--no-undefined -Wl,--gc-sections -Wl,--eh-frame-hdr -Wl,--hash-style=both -Wl,--build-id=sha1 -shared -nostdlib $(SYSLD) $(GH_LINK) -T $(SYSROOT)/link.x

OBJS := source/main.o

all: $(TARGET).prx

$(TARGET).elf: $(OBJS)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(SYSROOT)/lib/crtlib.o

$(TARGET).prx: $(TARGET).elf
	@mkdir -p $(dir $@)
	$(TOOLCHAIN)/bin/linux/create-fself -in=$< -out=$(TARGET).oelf --lib=$@ --paid=0x3800000000000011

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_FOLDER)/prx$(TYPE) $(BUILD_FOLDER)/elf$(TYPE) $(OBJS) $(TARGETSTUB)
