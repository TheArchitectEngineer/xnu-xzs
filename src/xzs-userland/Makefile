CLANG := /usr/bin/xcrun clang

TARGET := launchd
ROOTFS_TARGET := ../../rootfs/xzs-root/sbin/launchd

ASFLAGS := -target arm64-apple-macos11 -c
LDFLAGS := -target arm64-apple-macos11 -nostdlib \
	-Wl,-static \
	-Wl,-e,_start \
	-Wl,-pagezero_size,0x100000000 \
	-Wl,-segalign,0x4000

all: $(TARGET)

$(TARGET).o: $(TARGET).S
	$(CLANG) $(ASFLAGS) $< -o $@

$(TARGET): $(TARGET).o
	$(CLANG) $(LDFLAGS) $< -o $@

install: $(TARGET)
	cp $(TARGET) $(ROOTFS_TARGET)

clean:
	rm -f $(TARGET).o $(TARGET)

.PHONY: all install clean
