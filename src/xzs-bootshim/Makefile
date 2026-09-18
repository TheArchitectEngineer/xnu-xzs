CC := clang
LD := /opt/homebrew/bin/ld.lld
OBJCOPY := /opt/homebrew/opt/llvm/bin/llvm-objcopy
OBJDUMP := /opt/homebrew/opt/llvm/bin/llvm-objdump

CFLAGS := -target aarch64-none-elf -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -fno-pic -mgeneral-regs-only -mstrict-align -O2 -Wall -I.
ASFLAGS := -target aarch64-none-elf -ffreestanding
LDFLAGS := -T linker.ld -nostdlib

OBJS := start.o main.o uart.o adt.o fb.o
TARGET := bootshim

all: $(TARGET).bin $(TARGET).dis

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

clean:
	rm -f *.o *.elf *.bin *.dis

.PHONY: all clean
