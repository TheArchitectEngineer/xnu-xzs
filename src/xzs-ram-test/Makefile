CC := clang
LD := /opt/homebrew/bin/ld.lld
OBJCOPY := /opt/homebrew/opt/llvm/bin/llvm-objcopy
OBJDUMP := /opt/homebrew/opt/llvm/bin/llvm-objdump

CFLAGS := -target aarch64-none-elf -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -fno-pic -O2 -Wall -I.
ASFLAGS := -target aarch64-none-elf -ffreestanding
LDFLAGS := -T linker.ld -nostdlib

OBJS := start.o main.o
TARGET := ram-writer
BOOTIMG := ../../artifacts/builds/xzs-ram-test-writer.img

all: $(BOOTIMG)

%.o: %.S
	$(CC) $(ASFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(TARGET).dis: $(TARGET).elf
	$(OBJDUMP) -d $< > $@

$(BOOTIMG): $(TARGET).bin $(TARGET).dis
	mkdir -p ../../artifacts/builds
	python3 -c "k = open('$(TARGET).bin', 'rb').read(); assert len(k) <= 0x4000, f'ram-writer.bin too large ({len(k)} > 0x4000)'; dtb = open('../../device/reference/minimal-keyaki.dtb', 'rb').read(); open('ram-writer-dtb.bin', 'wb').write(k.ljust(0x4000, b'\x00') + dtb)"
	python3 ../../scripts/mkbootimg.py \
		--kernel ram-writer-dtb.bin \
		--dtb ../../device/reference/minimal-keyaki.dtb \
		--base 0x80000000 \
		--kernel_offset 0x00080000 \
		--cmdline "console=ttyMSM0,115200 earlycon=msm_serial_dm,0x075b0000" \
		--pagesize 4096 \
		-o $(BOOTIMG)
	@echo "=== Generated $(BOOTIMG) ==="
	@ls -lh $(BOOTIMG)
	@echo "=== SHA256: $$(sha256sum $(BOOTIMG) | awk '{print $$1}') ==="

clean:
	rm -f *.o *.elf *.bin *.dis ram-writer-dtb.bin

.PHONY: all clean
