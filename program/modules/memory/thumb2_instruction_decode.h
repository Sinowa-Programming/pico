#ifndef THUMB2_INSTRUCTION_DECODE_H
#define THUMB2_INSTRUCTION_DECODE_H

#include "pico/stdlib.h"


/// @brief Finds the base register of the given thumb2 instruction. It is only implemented for instructions that can cause a MPU fault.
/// @param instruction The instruction to find the base register of.
/// @return The base register idx. Ex: 0 = r0
uint8_t decode_instruction_base_register(uint32_t instruction);

#endif // THUMB2_INSTRUCTION_DECODE_H