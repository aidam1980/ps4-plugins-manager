# Package metadata.
TITLE       := Plugin Manager
TITLE_ID    := PLUG00001
VERSION := 1.00
CONTENT_ID  := IV0000-PLUG00001_00-PLUGINEDITOR0001

# Libraries linked into the ELF.
LIBS := -L$(CURDIR)/libjbc -ljbc -lc -lkernel -lc++ -lSceVideoOut -lSceSysmodule -lSceFreeType -lScePad -lSceUserService -lSceImeDialog

# Additional compile flags.
EXTRAFLAGS := -DGRAPHICS_USES_FONT

# Asset and module directories.
ASSETS      := $(shell find assets -type f -print)
LIBMODULES  := $(wildcard sce_module/*)

# You likely won't need to touch anything below this point.

# Root vars
TOOLCHAIN   := $(OO_PS4_TOOLCHAIN)
PROJDIR     := PS4PluginEditor
# Shared graphics source is part of this repository; only SDK headers/libs
# and linker/runtime files are taken from the external OpenOrbis toolchain.
COMMONDIR   := $(CURDIR)/_common
COMMONFILES := $(COMMONDIR)/graphics.cpp
INTDIR      := $(PROJDIR)/x64/Debug

# Define objects to build
CFILES      := $(wildcard $(PROJDIR)/*.c)
CPPFILES    := $(wildcard $(PROJDIR)/*.cpp)
OBJS := $(patsubst $(PROJDIR)/%.c,$(INTDIR)/%.o,$(CFILES)) $(patsubst $(PROJDIR)/%.cpp,$(INTDIR)/%.o,$(CPPFILES)) $(patsubst $(COMMONDIR)/%.cpp,$(INTDIR)/common_%.o,$(COMMONFILES))
LIBJBC_ARCHIVE := $(CURDIR)/libjbc/libjbc.a

# Define final C/C++ flags
CFLAGS      := -I$(COMMONDIR)  --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -c $(EXTRAFLAGS) -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include
CXXFLAGS    := $(CFLAGS) -isystem $(TOOLCHAIN)/include/c++/v1 -I$(CURDIR)/libjbc
LDFLAGS     := -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x --eh-frame-hdr -L$(TOOLCHAIN)/lib $(LIBS) $(TOOLCHAIN)/lib/crt1.o

# Create the intermediate directory incase it doesn't already exist.
_unused     := $(shell mkdir -p $(INTDIR))

# Select host tools supplied by OpenOrbis.
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
	CC := clang
	CCX := clang++
	LD := ld.lld
	CDIR := linux
	PKGTOOL := $(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core
	CREATE_GP4 := $(TOOLCHAIN)/bin/$(CDIR)/create-gp4
	CREATE_FSELF := $(TOOLCHAIN)/bin/$(CDIR)/create-fself
else ifeq ($(UNAME_S),Darwin)
	CC := /usr/local/opt/llvm/bin/clang
	CCX := /usr/local/opt/llvm/bin/clang++
	LD := /usr/local/opt/llvm/bin/ld.lld
	CDIR := macos
	PKGTOOL := $(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core
	CREATE_GP4 := $(TOOLCHAIN)/bin/$(CDIR)/create-gp4
	CREATE_FSELF := $(TOOLCHAIN)/bin/$(CDIR)/create-fself
else ifneq (,$(filter MINGW% MSYS% CYGWIN%,$(UNAME_S)))
	CDIR := windows
	CC := $(TOOLCHAIN)/bin/$(CDIR)/clang.exe
	CCX := $(TOOLCHAIN)/bin/$(CDIR)/clang++.exe
	LD := $(TOOLCHAIN)/bin/$(CDIR)/ld.lld.exe
	PKGTOOL := $(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core.exe
	CREATE_GP4 := $(TOOLCHAIN)/bin/$(CDIR)/create-gp4.exe
	CREATE_FSELF := $(TOOLCHAIN)/bin/$(CDIR)/create-fself.exe
else
	$(error Unsupported host OS '$(UNAME_S)'. Use Linux, macOS, or Windows with MSYS2/Git Bash.)
endif

all: $(CONTENT_ID).pkg

$(CONTENT_ID).pkg: pkg.gp4
	$(PKGTOOL) pkg_build $< .

pkg.gp4: FORCE Makefile eboot.bin sce_sys/about/right.sprx sce_sys/param.sfo sce_sys/icon0.png $(LIBMODULES) $(ASSETS)
	$(CREATE_GP4) -out $@ --content-id=$(CONTENT_ID) --files "$(filter-out Makefile FORCE,$^)"
	@grep -q '<dir targ_name="buttons"' $@ || sed -i '/<dir targ_name="audio" \/>/a\            <dir targ_name="buttons" />' $@


# ATTRIBUTE 0x20 (32) makes Common Dialogs use the PS4 System Software button assignment.
sce_sys/param.sfo:	Makefile
	$(PKGTOOL) sfo_new $@
	$(PKGTOOL) sfo_setentry $@ APP_TYPE --type Integer --maxsize 4 --value 1 
	$(PKGTOOL) sfo_setentry $@ APP_VER --type Utf8 --maxsize 8 --value '$(VERSION)'
	$(PKGTOOL) sfo_setentry $@ ATTRIBUTE --type Integer --maxsize 4 --value 32
	$(PKGTOOL) sfo_setentry $@ CATEGORY --type Utf8 --maxsize 4 --value 'gd'  
	$(PKGTOOL) sfo_setentry $@ CONTENT_ID --type Utf8 --maxsize 48 --value '$(CONTENT_ID)'
	$(PKGTOOL) sfo_setentry $@ DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0 
	$(PKGTOOL) sfo_setentry $@ SYSTEM_VER --type Integer --maxsize 4 --value 0  
	$(PKGTOOL) sfo_setentry $@ TITLE --type Utf8 --maxsize 128 --value '$(TITLE)'
	$(PKGTOOL) sfo_setentry $@ TITLE_ID --type Utf8 --maxsize 12 --value '$(TITLE_ID)'
	$(PKGTOOL) sfo_setentry $@ VERSION --type Utf8 --maxsize 8 --value '$(VERSION)'

$(LIBJBC_ARCHIVE): libjbc/build-libjbc.sh
	@chmod +x libjbc/build-libjbc.sh
	@./libjbc/build-libjbc.sh "$(TOOLCHAIN)" "$(CDIR)"

eboot.bin: $(INTDIR) $(OBJS) $(LIBJBC_ARCHIVE)
	$(LD) $(OBJS) -o $(INTDIR)/$(PROJDIR).elf $(LDFLAGS)
	$(CREATE_FSELF) -in=$(INTDIR)/$(PROJDIR).elf -out=$(INTDIR)/$(PROJDIR).oelf --eboot "eboot.bin" --paid 0x3800000000000011


$(INTDIR)/common_%.o: $(COMMONDIR)/%.cpp
	$(CCX) $(CXXFLAGS) -o $@ $<

$(INTDIR)/%.o: $(PROJDIR)/%.c
	$(CC) $(CFLAGS) -o $@ $<

$(INTDIR)/%.o: $(PROJDIR)/%.cpp
	$(CCX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f $(CONTENT_ID).pkg pkg.gp4 sce_sys/param.sfo eboot.bin \
		$(INTDIR)/$(PROJDIR).elf $(INTDIR)/$(PROJDIR).oelf $(OBJS)

.PHONY: FORCE
FORCE:
