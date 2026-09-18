CC := clang
LD := /opt/homebrew/bin/ld.lld
OBJCOPY := /opt/homebrew/opt/llvm/bin/llvm-objcopy
OBJDUMP := /opt/homebrew/opt/llvm/bin/llvm-objdump

CFLAGS := -target aarch64-none-elf -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -fno-pic -O2 -Wall -I.
ASFLAGS := -target aarch64-none-elf -ffreestanding
LDFLAGS := -T linker.ld -nostdlib

OBJS := start.o main.o uart.o usb.o
TARGET := dumper
BOOTIMG := ../../artifacts/builds/xzs-log-dumper.img

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
	gzip -n -9 -c $(TARGET).bin > $(TARGET).bin.gz
	cat $(TARGET).bin.gz ../../artifacts/builds/twrp-extracted.dtb > dumper-gz-dtb.bin
	python3 ../../scripts/mkbootimg.py \
		--kernel dumper-gz-dtb.bin \
		--base 0x80000000 \
		--kernel_offset 0x00008000 \
		--cmdline "console=ttyMSM0,115200 earlycon=msm_serial_dm,0x075b0000" \
		--pagesize 4096 \
		-o $(BOOTIMG)
	cp $(BOOTIMG) ../../artifacts/builds/xzs-debug-dumper.img
	@echo "=== Generated $(BOOTIMG) ==="
	@ls -lh $(BOOTIMG)
	@echo "=== SHA256: $$(sha256sum $(BOOTIMG) | awk '{print $$1}') ==="


clean:
	rm -f *.o *.elf *.bin *.dis dumper-dtb.bin

.PHONY: all clean
