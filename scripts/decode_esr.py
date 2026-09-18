#!/usr/bin/env python3
"""
scripts/decode_esr.py - ARM64 ESR_EL1 Exception Syndrome Register Decoder
Decodes raw ESR_EL1 register values into human-readable diagnostic information.
"""

import sys

# Exception Class (EC) definitions according to ARM DDI 0487 (ARMv8 Architecture Reference Manual)
EC_TABLE = {
    0b000000: "Unknown / Unknown reason",
    0b000001: "Trapped WFI or WFE instruction",
    0b000011: "Trapped MCR or MRC access (CP15)",
    0b000100: "Trapped MCRR or MRRC access (CP15)",
    0b000101: "Trapped MCR or MRC access (CP14)",
    0b000110: "Trapped LDC or STC access",
    0b000111: "Trapped access to SVE/SIMD/FP registers",
    0b001100: "Trapped MRRC access (CP14)",
    0b001101: "Branch Target Exception",
    0b001110: "Illegal Execution state",
    0b010001: "SVC instruction in AArch32 state",
    0b010101: "SVC instruction in AArch64 state",
    0b011000: "Trapped MSR, MRS, or System instruction in AArch64",
    0b011001: "Trapped SVE access",
    0b011100: "Pointer Authentication failure",
    0b100000: "Instruction Abort from a lower Exception level",
    0b100001: "Instruction Abort taken without a change in Exception level",
    0b100010: "PC alignment fault",
    0b100100: "Data Abort from a lower Exception level",
    0b100101: "Data Abort taken without a change in Exception level",
    0b100110: "SP alignment fault",
    0b101000: "Trapped floating-point exception (AArch32)",
    0b101100: "Trapped floating-point exception (AArch64)",
    0b101111: "SError interrupt",
    0b110000: "Breakpoint exception from a lower Exception level",
    0b110001: "Breakpoint exception taken without a change in Exception level",
    0b110010: "Software Step exception from a lower Exception level",
    0b110011: "Software Step exception taken without a change in Exception level",
    0b110100: "Watchpoint exception from a lower Exception level",
    0b110101: "Watchpoint exception taken without a change in Exception level",
    0b111000: "BKPT instruction in AArch32",
    0b111100: "BRK instruction in AArch64",
}

DFSC_IFSC_TABLE = {
    0b000000: "Address size fault, level 0 of translation",
    0b000001: "Address size fault, level 1 of translation",
    0b000010: "Address size fault, level 2 of translation",
    0b000011: "Address size fault, level 3 of translation",
    0b000100: "Translation fault, level 0",
    0b000101: "Translation fault, level 1",
    0b000110: "Translation fault, level 2",
    0b000111: "Translation fault, level 3",
    0b001001: "Access flag fault, level 1",
    0b001010: "Access flag fault, level 2",
    0b001011: "Access flag fault, level 3",
    0b001101: "Permission fault, level 1",
    0b001110: "Permission fault, level 2",
    0b001111: "Permission fault, level 3",
    0b010000: "Synchronous External abort, not on translation table walk",
    0b010001: "Synchronous Tag Check Fault",
    0b010100: "Synchronous External abort on translation table walk, level 0",
    0b010101: "Synchronous External abort on translation table walk, level 1",
    0b010110: "Synchronous External abort on translation table walk, level 2",
    0b010111: "Synchronous External abort on translation table walk, level 3",
    0b011000: "Synchronous parity or ECC error on memory access",
    0b011100: "Synchronous parity or ECC error on translation table walk, level 0",
    0b011101: "Synchronous parity or ECC error on translation table walk, level 1",
    0b011110: "Synchronous parity or ECC error on translation table walk, level 2",
    0b011111: "Synchronous parity or ECC error on translation table walk, level 3",
    0b100001: "Alignment fault",
    0b100010: "Debug event",
    0b110000: "TLB conflict abort",
}

def decode_esr(esr_val):
    ec = (esr_val >> 26) & 0x3f
    il = (esr_val >> 25) & 0x1
    iss = esr_val & 0x1ffffff

    ec_desc = EC_TABLE.get(ec, f"Reserved / Unknown EC (0x{ec:02x})")
    il_desc = "32-bit instruction" if il else "16-bit instruction"

    lines = []
    lines.append(f"ESR_EL1:          0x{esr_val:08x}")
    lines.append(f"  EC  (bits 31:26): 0x{ec:02x} ({ec_desc})")
    lines.append(f"  IL  (bit 25):     {il} ({il_desc})")
    lines.append(f"  ISS (bits 24:0):  0x{iss:07x}")

    # Decode Data Abort (0x24 / 0x25) or Instruction Abort (0x20 / 0x21)
    if ec in (0x24, 0x25, 0x20, 0x21):
        is_data = (ec in (0x24, 0x25))
        fsc = iss & 0x3f
        fsc_desc = DFSC_IFSC_TABLE.get(fsc, f"Unknown fault code (0x{fsc:02x})")
        
        fault_type = "Data Abort" if is_data else "Instruction Abort"
        stage = "same EL (EL1)" if (ec in (0x21, 0x25)) else "lower EL (EL0)"
        lines.append(f"\nFault Details:")
        lines.append(f"  Type:             {fault_type} from {stage}")
        lines.append(f"  Fault Status:     0x{fsc:02x} -> {fsc_desc}")

        if is_data:
            isv = (iss >> 24) & 1
            sas = (iss >> 22) & 3
            sse = (iss >> 21) & 1
            srt = (iss >> 16) & 0x1f
            sf  = (iss >> 15) & 1
            ar  = (iss >> 14) & 1
            ea  = (iss >> 9) & 1
            cm  = (iss >> 8) & 1
            s1ptw = (iss >> 7) & 1
            wnr = (iss >> 6) & 1

            size_map = {0: "Byte", 1: "Halfword", 2: "Word (32-bit)", 3: "Doubleword (64-bit)"}
            lines.append(f"  Write not Read:   {'Write (Store)' if wnr else 'Read (Load)'}")
            lines.append(f"  Stage 2 walk:     {'Fault on stage 2 walk' if s1ptw else 'No'}")
            lines.append(f"  Instruction Valid:{'Yes (Syndrome valid)' if isv else 'No'}")
            if isv:
                lines.append(f"    Access size:    {size_map.get(sas, 'Unknown')}")
                lines.append(f"    Target reg:     x{srt}")
                lines.append(f"    64-bit reg:     {'Yes' if sf else 'No (32-bit)'}")
                lines.append(f"    Acquire/Rel:    {'Yes' if ar else 'No'}")
        else:
            s1ptw = (iss >> 7) & 1
            lines.append(f"  Stage 2 walk:     {'Fault on stage 2 walk' if s1ptw else 'No'}")

        # Classify fault category
        if 0b000100 <= fsc <= 0b000111:
            level = fsc - 0b000100
            lines.append(f"  Category:         TRANSLATION FAULT (Level {level}) - Unmapped memory address (check FAR_EL1)")
        elif 0b001101 <= fsc <= 0b001111:
            level = fsc - 0b001101 + 1
            lines.append(f"  Category:         PERMISSION FAULT (Level {level}) - Protection/RWX violation (check page attributes)")
        elif fsc == 0b100001:
            lines.append(f"  Category:         ALIGNMENT FAULT - Misaligned memory access (check FAR_EL1 / SP)")
        elif 0b000000 <= fsc <= 0b000011:
            level = fsc
            lines.append(f"  Category:         ADDRESS SIZE FAULT (Level {level}) - Input address exceeds configured PA range")

    elif ec == 0x18: # Trapped System Register access
        op0 = (iss >> 20) & 0x3
        op2 = (iss >> 17) & 0x7
        op1 = (iss >> 14) & 0x7
        crn = (iss >> 10) & 0xf
        rt  = (iss >> 5) & 0x1f
        crm = (iss >> 1) & 0xf
        dir_read = iss & 1
        lines.append(f"\nTrapped System Register:")
        lines.append(f"  Direction:        {'MRS (Read)' if dir_read else 'MSR (Write)'}")
        lines.append(f"  Register:         s{op0}_{op1}_c{crn}_c{crm}_{op2} (Rt: x{rt})")

    elif ec == 0x26:
        lines.append(f"\nSP Alignment Fault:")
        lines.append(f"  Stack pointer SP was not 16-byte aligned during memory access.")

    elif ec == 0x22:
        lines.append(f"\nPC Alignment Fault:")
        lines.append(f"  Program counter PC was not 4-byte aligned.")

    elif ec == 0x2f:
        lines.append(f"\nSError Interrupt:")
        lines.append(f"  Asynchronous system or bus abort.")

    return "\n".join(lines)

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 scripts/decode_esr.py <ESR_EL1_HEX>")
        print("Example: python3 scripts/decode_esr.py 0x96000004")
        sys.exit(1)

    raw_str = sys.argv[1].strip()
    try:
        val = int(raw_str, 16) if raw_str.startswith(("0x", "0X")) else int(raw_str, 0)
    except ValueError:
        print(f"Error: Invalid hex value '{raw_str}'")
        sys.exit(1)

    print("============================================================")
    print("ARM64 ESR_EL1 Syndrome Decoder")
    print("============================================================")
    print(decode_esr(val))
    print("============================================================")

if __name__ == "__main__":
    main()
