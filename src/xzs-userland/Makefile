CLANG := /usr/bin/xcrun clang

TARGETS := launchd sh
ROOTFS_DIR := ../../rootfs/xzs-root

ASFLAGS := -target arm64-apple-macos11 -c
LDFLAGS := -target arm64-apple-macos11 -nostdlib \
	-Wl,-static \
	-Wl,-e,_start \
	-Wl,-pagezero_size,0x100000000 \
	-Wl,-segalign,0x4000

all: $(TARGETS)

%.o: %.S
	$(CLANG) $(ASFLAGS) $< -o $@

launchd: launchd.o
	$(CLANG) $(LDFLAGS) $< -o $@

sh: sh.o
	$(CLANG) $(LDFLAGS) $< -o $@

install-launchd: launchd
	cp launchd $(ROOTFS_DIR)/sbin/launchd

install-sh: sh
	cp sh $(ROOTFS_DIR)/bin/sh

install: install-launchd install-sh

clean:
	rm -f *.o $(TARGETS)

.PHONY: all install install-launchd install-sh clean
