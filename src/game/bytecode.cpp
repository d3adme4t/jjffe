#include "jjffe/bytecode.h"
#include <cstring>

namespace jjffe {

// Bytecode interpreter state
static struct {
    uint32_t stack[256];
    int stackTop;
    uint32_t context;
    uint32_t missionData;
} g_bcState;

// Initialize bytecode state
void BC_Init()
{
    memset(&g_bcState, 0, sizeof(g_bcState));
    g_bcState.stackTop = 0;
}

// Push value to stack
void BC_Push(uint32_t value)
{
    if (g_bcState.stackTop < 256) {
        g_bcState.stack[g_bcState.stackTop++] = value;
    }
}

// Pop value from stack
uint32_t BC_Pop()
{
    if (g_bcState.stackTop > 0) {
        return g_bcState.stack[--g_bcState.stackTop];
    }
    return 0;
}

// Peek at top of stack
uint32_t BC_Peek()
{
    if (g_bcState.stackTop > 0) {
        return g_bcState.stack[g_bcState.stackTop - 1];
    }
    return 0;
}

// Read 32-bit value from bytecode stream (little-endian, byte-by-byte)
uint32_t BC_ReadValue(int** ip)
{
    uint8_t* bytePtr = (uint8_t*)*ip;
    uint32_t value = bytePtr[0] | (bytePtr[1] << 8) | (bytePtr[2] << 16) | (bytePtr[3] << 24);
    *ip = (int*)(bytePtr + 4);
    return value;
}

// Read expression value from bytecode stream — thin wrapper over FUNC_000031
uint32_t BC_ReadExpression(int** ip, intptr_t context)
{
    if (!ip || !*ip) return 0;
    intptr_t cursor = reinterpret_cast<intptr_t>(*ip);
    uint32_t r = FUNC_000031(&cursor, context);
    *ip = reinterpret_cast<int*>(cursor);
    return r;
}

// RunBytecode — thin wrapper over FUNC_000041 (core.cpp)
void RunBytecode(int* ip, GameObj* context)
{
    if (!ip) return;
    BC_Init();
    intptr_t cursor = reinterpret_cast<intptr_t>(ip);
    // context is the console/mission slot pointer (historically misnamed GameObj*)
    FUNC_000041(&cursor, reinterpret_cast<int32_t*>(context));
}

} // namespace jjffe
