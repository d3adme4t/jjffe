#ifndef JJFFE_BYTECODE_H
#define JJFFE_BYTECODE_H

#include <cstdint>

namespace jjffe {

// Expression evaluator core (FUNC_000031 in core.cpp)
uint32_t FUNC_000031(intptr_t* ip, intptr_t context);

// Bytecode interpreter core (FUNC_000041 in core.cpp)
void FUNC_000041(intptr_t* ip, int32_t* context);

struct GameObj;

// Bytecode interpreter state management
void BC_Init();
void BC_Push(uint32_t value);
uint32_t BC_Pop();
uint32_t BC_Peek();

// Read 32-bit value from bytecode stream
uint32_t BC_ReadValue(int** ip);

// Read expression value from bytecode stream — thin wrapper over FUNC_000031
uint32_t BC_ReadExpression(int** ip, intptr_t context);

// Main bytecode interpreter — thin wrapper over FUNC_000041
void RunBytecode(int* ip, GameObj* context);

} // namespace jjffe

#endif // JJFFE_BYTECODE_H
