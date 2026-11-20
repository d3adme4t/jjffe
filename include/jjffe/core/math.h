#ifndef JJFFE_CORE_MATH_H
#define JJFFE_CORE_MATH_H

#include <cstdint>
#include <cstddef>

namespace jjffe {

// Fixed-point core
int32_t MulFixed32High(int32_t a, int32_t b);

// Vector/matrix
void Vec64Transform(int32_t obj1, int32_t obj2, int32_t* out1, int32_t* out2, int32_t param5);

// Physics math (pure, no object types)
void PhysicsDotProduct(int32_t* vec);
int32_t PhysicsFindMSB(uint32_t value);
void PhysicsNormalizeVector(intptr_t param);
void PhysicsVectorAdd(void* obj1, void* obj2);
void PhysicsVectorSub(void* obj1, void* obj2, int8_t param3);
void PhysicsVectorAdjust(void* obj, int32_t* vec1, int32_t* vec2, int8_t param4, int32_t param5);
void PhysicsBoundsCheck(void* obj, int32_t* bounds, int32_t* result);
void PhysicsDistanceCalc(void* obj, int32_t param2, int32_t* result);
void PhysicsVelocityUpdateSimple(int32_t* obj, int32_t deltaTime);
void PhysicsVecMatWrapper(void* param1, void* param2, void* param3, int32_t* result);

// Calculation helpers
int64_t PhysicsCalculationA(int32_t* param1, int32_t param2, int32_t param3, int32_t param4, uint32_t param5);
int64_t PhysicsCalculationB(intptr_t param1, int32_t param2, int32_t param3, int32_t param4, uint32_t param5);
void CalculateObjectVector(void* param1, int32_t param2, int32_t* param3);

// Utility math
uint32_t CalculatePriority(int32_t param1, const int32_t* param2);
uint32_t HashMix(uint32_t value);
int32_t ColorDistance(uint32_t color1, uint32_t color2, int32_t param3);
void CalculateMSBValue(void* param);

// Vector addition
void VectorAddition671(void* param1);

} // namespace jjffe

#endif // JJFFE_CORE_MATH_H
