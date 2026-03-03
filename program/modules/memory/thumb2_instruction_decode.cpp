#include "thumb2_instruction_decode.h"

// Decoded based on the ARMv8m architecture manual.
// This is a pain in the butt.
/**
 * Decodes the base register (Rn) from the load/store instruction at the faulted PC.
 * * @param pc The address of the faulting instruction (from the execution frame).
 * @return The index of the base register (0-15), or 0xFF if not recognized.
 */
uint8_t decode_instruction_base_register(uint32_t pc) {
    // Thumb instructions are minimally 2-byte aligned.
    // Read the first halfword (hw1) of the instruction.
    uint16_t hw1 = *(uint16_t*)pc;

    // Check if it's a 32-bit T32 instruction.
    // 32-bit instructions have their top 5 bits as 0b11101, 0b11110, or 0b11111.
    if ((hw1 & 0xE000) == 0xE000 && (hw1 & 0x1800) != 0x0000) {
        // For ALL 32-bit T32 memory load/store instructions (LDR, STR, LDM, STM,
        // LDRD, STRD, VLDR, VSTR, PLD, etc.), the base register Rn is universally
        // encoded in bits [3:0] of the first halfword.
        return hw1 & 0x000F;
    }

    // 16-bit T32 instruction decoding.
    // We can isolate the major encoding groups by switching on the top 4 bits.
    switch (hw1 >> 12) {
        case 0x4:
            // LDR (literal) -> 0100 1xxx xxxx xxxx
            if ((hw1 & 0xF800) == 0x4800) {
                return 15; // PC is implicitly the base register
            }
            break;

        case 0x5:
            // LDR / STR (register offset) -> 0101 xxxx xxxx xxxx
            // Rn is located in bits [5:3]
            return (hw1 >> 3) & 0x0007;

        case 0x6:
        case 0x7:
            // LDR / STR (immediate offset) -> 011x xxxx xxxx xxxx
            // Rn is located in bits [5:3]
            return (hw1 >> 3) & 0x0007;

        case 0x8:
            // LDRH / STRH (immediate offset) -> 1000 xxxx xxxx xxxx
            // Rn is located in bits [5:3]
            return (hw1 >> 3) & 0x0007;

        case 0x9:
            // LDR / STR (SP-relative) -> 1001 xxxx xxxx xxxx
            return 13; // SP is implicitly the base register

        case 0xB:
            // PUSH / POP -> 1011 010x (PUSH) or 1011 110x (POP)
            // The mask 0xF600 cleanly matches both 0xB400 (PUSH) and 0xBC00 (POP)
            if ((hw1 & 0xF600) == 0xB400) {
                return 13; // SP is implicitly the base register
            }
            break;

        case 0xC:
            // LDM / STM (Load/Store Multiple) -> 1100 xxxx xxxx xxxx
            // Rn is located in bits [10:8]
            return (hw1 >> 8) & 0x0007;

        default:
            break; // Not a load/store memory instruction
    }

    // Return 0xFF if the instruction isn't a recognized memory access
    return 0xFF;
}