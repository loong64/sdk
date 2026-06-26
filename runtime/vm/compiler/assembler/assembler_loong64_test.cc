// Copyright (c) 2017, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(TARGET_ARCH_LOONG64)

#include "vm/compiler/assembler/assembler.h"
#include "vm/compiler/assembler/assembler_test.h"
#include "vm/compiler/backend/locations.h"
#include "vm/cpu.h"
#include "vm/os.h"
#include "vm/unit_test.h"
#include "vm/virtual_memory.h"
#include "vm/zone_text_buffer.h"

namespace dart {
namespace compiler {

#define __ assembler->

#if defined(PRODUCT)
#define EXPECT_DISASSEMBLY(expected)
#else
#define EXPECT_DISASSEMBLY(expected)                                       \
  EXPECT_STREQ(expected, test->RelativeDisassembly())
#endif

// Called from assembler_test.cc.
// RA: return address.
// A0: value.
// A1: growable array.
// A2: current thread.
ASSEMBLER_TEST_GENERATE(StoreIntoObject, assembler) {
  __ PushRegister(RA);
  __ PushNativeCalleeSavedRegisters();

  __ MoveRegister(THR, A2);
  __ RestorePinnedRegisters();  // Setup WRITE_BARRIER_STATE.

  __ StoreIntoObject(A1, FieldAddress(A1, GrowableObjectArray::data_offset()),
                     A0);

  __ PopNativeCalleeSavedRegisters();
  __ PopRegister(RA);
  __ ret();
}

static intptr_t Call(intptr_t entry,
                     intptr_t arg0 = 0,
                     intptr_t arg1 = 0,
                     intptr_t arg2 = 0,
                     intptr_t arg3 = 0) {
#if defined(DART_INCLUDE_SIMULATOR)
  return Simulator::Current()->Call(entry, arg0, arg1, arg2, arg3);
#else
  typedef intptr_t (*F)(intptr_t, intptr_t, intptr_t, intptr_t);
  return reinterpret_cast<F>(entry)(arg0, arg1, arg2, arg3);
#endif
}
static float CallF(intptr_t entry, intptr_t arg0, intptr_t arg1 = 0) {
#if defined(DART_INCLUDE_SIMULATOR)
  return Simulator::Current()->CallF(entry, arg0, arg1);
#else
  typedef float (*F)(intptr_t, intptr_t);
  return reinterpret_cast<F>(entry)(arg0, arg1);
#endif
}
static float CallF(intptr_t entry, intptr_t arg0, float arg1) {
#if defined(DART_INCLUDE_SIMULATOR)
  return Simulator::Current()->CallF(entry, arg0, arg1);
#else
  typedef float (*F)(intptr_t, float);
  return reinterpret_cast<F>(entry)(arg0, arg1);
#endif
}
static float CallF(intptr_t entry, double arg0) {
#if defined(DART_INCLUDE_SIMULATOR)
  return Simulator::Current()->CallF(entry, arg0);
#else
  typedef float (*F)(double);
  return reinterpret_cast<F>(entry)(arg0);
#endif
}
static float CallF(intptr_t entry, float arg0) {
#if defined(DART_INCLUDE_SIMULATOR)
  return Simulator::Current()->CallF(entry, arg0);
#else
  typedef float (*F)(float);
  return reinterpret_cast<F>(entry)(arg0);
#endif
}
static float CallF(intptr_t entry, float arg0, float arg1) {
#if defined(DART_INCLUDE_SIMULATOR)
  return Simulator::Current()->CallF(entry, arg0, arg1);
#else
  typedef float (*F)(float, float);
  return reinterpret_cast<F>(entry)(arg0, arg1);
#endif
}
static float CallF(intptr_t entry, float arg0, float arg1, float arg2) {
#if defined(DART_INCLUDE_SIMULATOR)
  return Simulator::Current()->CallF(entry, arg0, arg1, arg2);
#else
  typedef float (*F)(float, float, float);
  return reinterpret_cast<F>(entry)(arg0, arg1, arg2);
#endif
}
static double CallD(intptr_t entry, intptr_t arg0, intptr_t arg1 = 0) {
#if defined(DART_INCLUDE_SIMULATOR)
  return Simulator::Current()->CallD(entry, arg0, arg1);
#else
  typedef double (*F)(intptr_t, intptr_t);
  return reinterpret_cast<F>(entry)(arg0, arg1);
#endif
}
static double CallD(intptr_t entry,
                    double arg0,
                    double arg1 = 0.0,
                    double arg2 = 0.0) {
#if defined(DART_INCLUDE_SIMULATOR)
  return Simulator::Current()->CallD(entry, arg0, arg1, arg2);
#else
  typedef double (*F)(double, double, double);
  return reinterpret_cast<F>(entry)(arg0, arg1, arg2);
#endif
}
static double CallD(intptr_t entry, intptr_t arg0, double arg1) {
#if defined(DART_INCLUDE_SIMULATOR)
  return Simulator::Current()->CallD(entry, arg0, arg1);
#else
  typedef double (*F)(intptr_t, double);
  return reinterpret_cast<F>(entry)(arg0, arg1);
#endif
}
static double CallD(intptr_t entry, float arg0) {
#if defined(DART_INCLUDE_SIMULATOR)
  return Simulator::Current()->CallD(entry, arg0);
#else
  typedef double (*F)(float);
  return reinterpret_cast<F>(entry)(arg0);
#endif
}
static intptr_t CallI(intptr_t entry, double arg0, double arg1 = 0.0) {
#if defined(DART_INCLUDE_SIMULATOR)
  return Simulator::Current()->CallI(entry, arg0, arg1);
#else
  typedef intptr_t (*F)(double, double);
  return reinterpret_cast<F>(entry)(arg0, arg1);
#endif
}
static intptr_t CallI(intptr_t entry, float arg0, float arg1 = 0.0f) {
#if defined(DART_INCLUDE_SIMULATOR)
  return Simulator::Current()->CallI(entry, arg0, arg1);
#else
  typedef intptr_t (*F)(float, float);
  return reinterpret_cast<F>(entry)(arg0, arg1);
#endif
}

void EnterTestFrame(Assembler* assembler) {
  __ EnterFrame(0);
  __ PushRegister(CODE_REG);
  __ PushRegister(THR);
  __ PushRegister(PP);
  __ MoveRegister(CODE_REG, A0);
  __ MoveRegister(THR, A1);
  __ LoadPoolPointer(PP);
}

void LeaveTestFrame(Assembler* assembler) {
  __ PopRegister(PP);
  __ PopRegister(THR);
  __ PopRegister(CODE_REG);
  __ LeaveFrame();
}

ASSEMBLER_TEST_GENERATE(Simple, assembler) {
  __ addi_d(A0, A0, 42);
  __ ret();
}

ASSEMBLER_TEST_RUN(Simple, test) {
  EXPECT_EQ(42, (test->Invoke<intptr_t, intptr_t>(0)));
}

ASSEMBLER_TEST_GENERATE(LoadStore, assembler) {
  __ addi_d(SP, SP, -16);
  __ st_d(A0, Address(SP));
  __ ld_d(A0, Address(SP));
  __ addi_d(SP, SP, 16);
  __ ret();
}

ASSEMBLER_TEST_RUN(LoadStore, test) {
  EXPECT_EQ(42, (test->Invoke<intptr_t, intptr_t>(42)));
  EXPECT_EQ(-42, (test->Invoke<intptr_t, intptr_t>(-42)));
}

ASSEMBLER_TEST_GENERATE(CallAndRet, assembler) {
  Label target;
  __ PushRegister(RA);
  __ bl(&target);
  __ PopRegister(RA);
  __ ret();
  __ Bind(&target);
  __ ret();
}

ASSEMBLER_TEST_RUN(CallAndRet, test) {
  EXPECT_EQ(0, (test->Invoke<intptr_t, intptr_t>(0)));
}

// ---------------------------------------------------------------------------
// Integer load instructions.
// ---------------------------------------------------------------------------

ASSEMBLER_TEST_GENERATE(LoadByte_0, assembler) {
  __ ld_b(A0, Address(A0, 0));
  __ ret();
}
ASSEMBLER_TEST_RUN(LoadByte_0, test) {
  uint8_t* values = reinterpret_cast<uint8_t*>(malloc(3 * sizeof(uint8_t)));
  values[0] = 0xAB;
  values[1] = 0xCD;
  values[2] = 0xEF;
  EXPECT_EQ(-51, Call(test->entry(), reinterpret_cast<intptr_t>(&values[1])));
  free(values);
}

ASSEMBLER_TEST_GENERATE(LoadByte_Pos, assembler) {
  __ ld_b(A0, Address(A0, 1));
  __ ret();
}
ASSEMBLER_TEST_RUN(LoadByte_Pos, test) {
  uint8_t* values = reinterpret_cast<uint8_t*>(malloc(3 * sizeof(uint8_t)));
  values[0] = 0xAB;
  values[1] = 0xCD;
  values[2] = 0xEF;
  EXPECT_EQ(-17, Call(test->entry(), reinterpret_cast<intptr_t>(&values[1])));
  free(values);
}

ASSEMBLER_TEST_GENERATE(LoadByte_Neg, assembler) {
  __ ld_b(A0, Address(A0, -1));
  __ ret();
}
ASSEMBLER_TEST_RUN(LoadByte_Neg, test) {
  uint8_t* values = reinterpret_cast<uint8_t*>(malloc(3 * sizeof(uint8_t)));
  values[0] = 0xAB;
  values[1] = 0xCD;
  values[2] = 0xEF;
  EXPECT_EQ(-85, Call(test->entry(), reinterpret_cast<intptr_t>(&values[1])));
  free(values);
}

ASSEMBLER_TEST_GENERATE(LoadByteUnsigned_0, assembler) {
  __ ld_bu(A0, Address(A0, 0));
  __ ret();
}
ASSEMBLER_TEST_RUN(LoadByteUnsigned_0, test) {
  uint8_t* values = reinterpret_cast<uint8_t*>(malloc(3 * sizeof(uint8_t)));
  values[0] = 0xAB;
  values[1] = 0xCD;
  values[2] = 0xEF;
  EXPECT_EQ(0xCD, Call(test->entry(), reinterpret_cast<intptr_t>(&values[1])));
  free(values);
}

ASSEMBLER_TEST_GENERATE(LoadByteUnsigned_Pos, assembler) {
  __ ld_bu(A0, Address(A0, 1));
  __ ret();
}
ASSEMBLER_TEST_RUN(LoadByteUnsigned_Pos, test) {
  uint8_t* values = reinterpret_cast<uint8_t*>(malloc(3 * sizeof(uint8_t)));
  values[0] = 0xAB;
  values[1] = 0xCD;
  values[2] = 0xEF;
  EXPECT_EQ(0xEF, Call(test->entry(), reinterpret_cast<intptr_t>(&values[1])));
  free(values);
}

ASSEMBLER_TEST_GENERATE(LoadByteUnsigned_Neg, assembler) {
  __ ld_bu(A0, Address(A0, -1));
  __ ret();
}
ASSEMBLER_TEST_RUN(LoadByteUnsigned_Neg, test) {
  uint8_t* values = reinterpret_cast<uint8_t*>(malloc(3 * sizeof(uint8_t)));
  values[0] = 0xAB;
  values[1] = 0xCD;
  values[2] = 0xEF;
  EXPECT_EQ(0xAB, Call(test->entry(), reinterpret_cast<intptr_t>(&values[1])));
  free(values);
}

ASSEMBLER_TEST_GENERATE(LoadHalfword_0, assembler) {
  __ ld_h(A0, Address(A0, 0));
  __ ret();
}
ASSEMBLER_TEST_RUN(LoadHalfword_0, test) {
  uint16_t* values = reinterpret_cast<uint16_t*>(malloc(3 * sizeof(uint16_t)));
  values[0] = 0xAB01;
  values[1] = 0xCD02;
  values[2] = 0xEF03;
  EXPECT_EQ(-13054, Call(test->entry(), reinterpret_cast<intptr_t>(&values[1])));
  free(values);
}

ASSEMBLER_TEST_GENERATE(LoadHalfword_Pos, assembler) {
  __ ld_h(A0, Address(A0, 2));
  __ ret();
}
ASSEMBLER_TEST_RUN(LoadHalfword_Pos, test) {
  uint16_t* values = reinterpret_cast<uint16_t*>(malloc(3 * sizeof(uint16_t)));
  values[0] = 0xAB01;
  values[1] = 0xCD02;
  values[2] = 0xEF03;
  EXPECT_EQ(-4349, Call(test->entry(), reinterpret_cast<intptr_t>(&values[1])));
  free(values);
}

ASSEMBLER_TEST_GENERATE(LoadHalfword_Neg, assembler) {
  __ ld_h(A0, Address(A0, -2));
  __ ret();
}
ASSEMBLER_TEST_RUN(LoadHalfword_Neg, test) {
  uint16_t* values = reinterpret_cast<uint16_t*>(malloc(3 * sizeof(uint16_t)));
  values[0] = 0xAB01;
  values[1] = 0xCD02;
  values[2] = 0xEF03;
  EXPECT_EQ(-21759, Call(test->entry(), reinterpret_cast<intptr_t>(&values[1])));
  free(values);
}

ASSEMBLER_TEST_GENERATE(LoadHalfwordUnsigned_0, assembler) {
  __ ld_hu(A0, Address(A0, 0));
  __ ret();
}
ASSEMBLER_TEST_RUN(LoadHalfwordUnsigned_0, test) {
  uint16_t* values = reinterpret_cast<uint16_t*>(malloc(3 * sizeof(uint16_t)));
  values[0] = 0xAB01;
  values[1] = 0xCD02;
  values[2] = 0xEF03;
  EXPECT_EQ(0xCD02, Call(test->entry(), reinterpret_cast<intptr_t>(&values[1])));
  free(values);
}

ASSEMBLER_TEST_GENERATE(LoadHalfwordUnsigned_Pos, assembler) {
  __ ld_hu(A0, Address(A0, 2));
  __ ret();
}
ASSEMBLER_TEST_RUN(LoadHalfwordUnsigned_Pos, test) {
  uint16_t* values = reinterpret_cast<uint16_t*>(malloc(3 * sizeof(uint16_t)));
  values[0] = 0xAB01;
  values[1] = 0xCD02;
  values[2] = 0xEF03;
  EXPECT_EQ(0xEF03, Call(test->entry(), reinterpret_cast<intptr_t>(&values[1])));
  free(values);
}

ASSEMBLER_TEST_GENERATE(LoadHalfwordUnsigned_Neg, assembler) {
  __ ld_hu(A0, Address(A0, -2));
  __ ret();
}
ASSEMBLER_TEST_RUN(LoadHalfwordUnsigned_Neg, test) {
  uint16_t* values = reinterpret_cast<uint16_t*>(malloc(3 * sizeof(uint16_t)));
  values[0] = 0xAB01;
  values[1] = 0xCD02;
  values[2] = 0xEF03;
  EXPECT_EQ(0xAB01, Call(test->entry(), reinterpret_cast<intptr_t>(&values[1])));
  free(values);
}

ASSEMBLER_TEST_GENERATE(LoadWord_0, assembler) {
  __ ld_w(A0, Address(A0, 0));
  __ ret();
}
ASSEMBLER_TEST_RUN(LoadWord_0, test) {
  uint32_t* values = reinterpret_cast<uint32_t*>(malloc(3 * sizeof(uint32_t)));
  values[0] = 0xAB010203;
  values[1] = 0xCD020405;
  values[2] = 0xEF030607;
  EXPECT_EQ(-855505915,
            Call(test->entry(), reinterpret_cast<intptr_t>(&values[1])));
  free(values);
}

ASSEMBLER_TEST_GENERATE(LoadWord_Pos, assembler) {
  __ ld_w(A0, Address(A0, 4));
  __ ret();
}
ASSEMBLER_TEST_RUN(LoadWord_Pos, test) {
  uint32_t* values = reinterpret_cast<uint32_t*>(malloc(3 * sizeof(uint32_t)));
  values[0] = 0xAB010203;
  values[1] = 0xCD020405;
  values[2] = 0xEF030607;
  EXPECT_EQ(-285014521,
            Call(test->entry(), reinterpret_cast<intptr_t>(&values[1])));
  free(values);
}

ASSEMBLER_TEST_GENERATE(LoadWord_Neg, assembler) {
  __ ld_w(A0, Address(A0, -4));
  __ ret();
}
ASSEMBLER_TEST_RUN(LoadWord_Neg, test) {
  uint32_t* values = reinterpret_cast<uint32_t*>(malloc(3 * sizeof(uint32_t)));
  values[0] = 0xAB010203;
  values[1] = 0xCD020405;
  values[2] = 0xEF030607;
  EXPECT_EQ(-1425997309,
            Call(test->entry(), reinterpret_cast<intptr_t>(&values[1])));
  free(values);
}

ASSEMBLER_TEST_GENERATE(LoadWordUnsigned_0, assembler) {
  __ ld_wu(A0, Address(A0, 0));
  __ ret();
}
ASSEMBLER_TEST_RUN(LoadWordUnsigned_0, test) {
  uint32_t* values = reinterpret_cast<uint32_t*>(malloc(3 * sizeof(uint32_t)));
  values[0] = 0xAB010203;
  values[1] = 0xCD020405;
  values[2] = 0xEF030607;
  EXPECT_EQ(0xCD020405,
            Call(test->entry(), reinterpret_cast<intptr_t>(&values[1])));
  free(values);
}

ASSEMBLER_TEST_GENERATE(LoadWordUnsigned_Pos, assembler) {
  __ ld_wu(A0, Address(A0, 4));
  __ ret();
}
ASSEMBLER_TEST_RUN(LoadWordUnsigned_Pos, test) {
  uint32_t* values = reinterpret_cast<uint32_t*>(malloc(3 * sizeof(uint32_t)));
  values[0] = 0xAB010203;
  values[1] = 0xCD020405;
  values[2] = 0xEF030607;
  EXPECT_EQ(0xEF030607,
            Call(test->entry(), reinterpret_cast<intptr_t>(&values[1])));
  free(values);
}

ASSEMBLER_TEST_GENERATE(LoadWordUnsigned_Neg, assembler) {
  __ ld_wu(A0, Address(A0, -4));
  __ ret();
}
ASSEMBLER_TEST_RUN(LoadWordUnsigned_Neg, test) {
  uint32_t* values = reinterpret_cast<uint32_t*>(malloc(3 * sizeof(uint32_t)));
  values[0] = 0xAB010203;
  values[1] = 0xCD020405;
  values[2] = 0xEF030607;
  EXPECT_EQ(0xAB010203,
            Call(test->entry(), reinterpret_cast<intptr_t>(&values[1])));
  free(values);
}

ASSEMBLER_TEST_GENERATE(LoadDoubleWord_0, assembler) {
  __ ld_d(A0, Address(A0, 0));
  __ ret();
}
ASSEMBLER_TEST_RUN(LoadDoubleWord_0, test) {
  uint64_t* values = reinterpret_cast<uint64_t*>(malloc(3 * sizeof(uint64_t)));
  values[0] = 0xAB01020304050607;
  values[1] = 0xCD02040505060708;
  values[2] = 0xEF03060708090A0B;
  EXPECT_EQ(-3674369926375274744,
            Call(test->entry(), reinterpret_cast<intptr_t>(&values[1])));
  free(values);
}

ASSEMBLER_TEST_GENERATE(LoadDoubleWord_Pos, assembler) {
  __ ld_d(A0, Address(A0, 8));
  __ ret();
}
ASSEMBLER_TEST_RUN(LoadDoubleWord_Pos, test) {
  uint64_t* values = reinterpret_cast<uint64_t*>(malloc(3 * sizeof(uint64_t)));
  values[0] = 0xAB01020304050607;
  values[1] = 0xCD02040505060708;
  values[2] = 0xEF03060708090A0B;
  EXPECT_EQ(-1224128046445295093,
            Call(test->entry(), reinterpret_cast<intptr_t>(&values[1])));
  free(values);
}

ASSEMBLER_TEST_GENERATE(LoadDoubleWord_Neg, assembler) {
  __ ld_d(A0, Address(A0, -8));
  __ ret();
}
ASSEMBLER_TEST_RUN(LoadDoubleWord_Neg, test) {
  uint64_t* values = reinterpret_cast<uint64_t*>(malloc(3 * sizeof(uint64_t)));
  values[0] = 0xAB01020304050607;
  values[1] = 0xCD02040505060708;
  values[2] = 0xEF03060708090A0B;
  EXPECT_EQ(-6124611806271568377,
            Call(test->entry(), reinterpret_cast<intptr_t>(&values[1])));
  free(values);
}

// ---------------------------------------------------------------------------
// Integer store instructions.
// ---------------------------------------------------------------------------

ASSEMBLER_TEST_GENERATE(StoreByte_0, assembler) {
  __ st_b(A1, Address(A0, 0));
  __ ret();
}
ASSEMBLER_TEST_RUN(StoreByte_0, test) {
  uint8_t* values = reinterpret_cast<uint8_t*>(malloc(3 * sizeof(uint8_t)));
  values[0] = 0;
  values[1] = 0;
  values[2] = 0;
  Call(test->entry(), reinterpret_cast<intptr_t>(&values[1]), 0xCD);
  EXPECT_EQ(0u, values[0]);
  EXPECT_EQ(0xCD, values[1]);
  EXPECT_EQ(0u, values[2]);
  free(values);
}

ASSEMBLER_TEST_GENERATE(StoreByte_Pos, assembler) {
  __ st_b(A1, Address(A0, 1));
  __ ret();
}
ASSEMBLER_TEST_RUN(StoreByte_Pos, test) {
  uint8_t* values = reinterpret_cast<uint8_t*>(malloc(3 * sizeof(uint8_t)));
  values[0] = 0;
  values[1] = 0;
  values[2] = 0;
  Call(test->entry(), reinterpret_cast<intptr_t>(&values[1]), 0xEF);
  EXPECT_EQ(0u, values[0]);
  EXPECT_EQ(0u, values[1]);
  EXPECT_EQ(0xEF, values[2]);
  free(values);
}

ASSEMBLER_TEST_GENERATE(StoreByte_Neg, assembler) {
  __ st_b(A1, Address(A0, -1));
  __ ret();
}
ASSEMBLER_TEST_RUN(StoreByte_Neg, test) {
  uint8_t* values = reinterpret_cast<uint8_t*>(malloc(3 * sizeof(uint8_t)));
  values[0] = 0;
  values[1] = 0;
  values[2] = 0;
  Call(test->entry(), reinterpret_cast<intptr_t>(&values[1]), 0xAB);
  EXPECT_EQ(0xAB, values[0]);
  EXPECT_EQ(0u, values[1]);
  EXPECT_EQ(0u, values[2]);
  free(values);
}

ASSEMBLER_TEST_GENERATE(StoreHalfword_0, assembler) {
  __ st_h(A1, Address(A0, 0));
  __ ret();
}
ASSEMBLER_TEST_RUN(StoreHalfword_0, test) {
  uint16_t* values = reinterpret_cast<uint16_t*>(malloc(3 * sizeof(uint16_t)));
  values[0] = 0;
  values[1] = 0;
  values[2] = 0;
  Call(test->entry(), reinterpret_cast<intptr_t>(&values[1]), 0xCD02);
  EXPECT_EQ(0u, values[0]);
  EXPECT_EQ(0xCD02, values[1]);
  EXPECT_EQ(0u, values[2]);
  free(values);
}

ASSEMBLER_TEST_GENERATE(StoreHalfword_Pos, assembler) {
  __ st_h(A1, Address(A0, 2));
  __ ret();
}
ASSEMBLER_TEST_RUN(StoreHalfword_Pos, test) {
  uint16_t* values = reinterpret_cast<uint16_t*>(malloc(3 * sizeof(uint16_t)));
  values[0] = 0;
  values[1] = 0;
  values[2] = 0;
  Call(test->entry(), reinterpret_cast<intptr_t>(&values[1]), 0xEF03);
  EXPECT_EQ(0u, values[0]);
  EXPECT_EQ(0u, values[1]);
  EXPECT_EQ(0xEF03, values[2]);
  free(values);
}

ASSEMBLER_TEST_GENERATE(StoreHalfword_Neg, assembler) {
  __ st_h(A1, Address(A0, -2));
  __ ret();
}
ASSEMBLER_TEST_RUN(StoreHalfword_Neg, test) {
  uint16_t* values = reinterpret_cast<uint16_t*>(malloc(3 * sizeof(uint16_t)));
  values[0] = 0;
  values[1] = 0;
  values[2] = 0;
  Call(test->entry(), reinterpret_cast<intptr_t>(&values[1]), 0xAB01);
  EXPECT_EQ(0xAB01, values[0]);
  EXPECT_EQ(0u, values[1]);
  EXPECT_EQ(0u, values[2]);
  free(values);
}

ASSEMBLER_TEST_GENERATE(StoreWord_0, assembler) {
  __ st_w(A1, Address(A0, 0));
  __ ret();
}
ASSEMBLER_TEST_RUN(StoreWord_0, test) {
  uint32_t* values = reinterpret_cast<uint32_t*>(malloc(3 * sizeof(uint32_t)));
  values[0] = 0;
  values[1] = 0;
  values[2] = 0;
  Call(test->entry(), reinterpret_cast<intptr_t>(&values[1]), 0xCD020405);
  EXPECT_EQ(0u, values[0]);
  EXPECT_EQ(0xCD020405, values[1]);
  EXPECT_EQ(0u, values[2]);
  free(values);
}

ASSEMBLER_TEST_GENERATE(StoreWord_Pos, assembler) {
  __ st_w(A1, Address(A0, 4));
  __ ret();
}
ASSEMBLER_TEST_RUN(StoreWord_Pos, test) {
  uint32_t* values = reinterpret_cast<uint32_t*>(malloc(3 * sizeof(uint32_t)));
  values[0] = 0;
  values[1] = 0;
  values[2] = 0;
  Call(test->entry(), reinterpret_cast<intptr_t>(&values[1]), 0xEF030607);
  EXPECT_EQ(0u, values[0]);
  EXPECT_EQ(0u, values[1]);
  EXPECT_EQ(0xEF030607, values[2]);
  free(values);
}

ASSEMBLER_TEST_GENERATE(StoreWord_Neg, assembler) {
  __ st_w(A1, Address(A0, -4));
  __ ret();
}
ASSEMBLER_TEST_RUN(StoreWord_Neg, test) {
  uint32_t* values = reinterpret_cast<uint32_t*>(malloc(3 * sizeof(uint32_t)));
  values[0] = 0;
  values[1] = 0;
  values[2] = 0;
  Call(test->entry(), reinterpret_cast<intptr_t>(&values[1]), 0xAB010203);
  EXPECT_EQ(0xAB010203, values[0]);
  EXPECT_EQ(0u, values[1]);
  EXPECT_EQ(0u, values[2]);
  free(values);
}

ASSEMBLER_TEST_GENERATE(StoreDoubleWord_0, assembler) {
  __ st_d(A1, Address(A0, 0));
  __ ret();
}
ASSEMBLER_TEST_RUN(StoreDoubleWord_0, test) {
  uint64_t* values = reinterpret_cast<uint64_t*>(malloc(3 * sizeof(uint64_t)));
  values[0] = 0;
  values[1] = 0;
  values[2] = 0;
  Call(test->entry(), reinterpret_cast<intptr_t>(&values[1]),
       0xCD02040505060708);
  EXPECT_EQ(0u, values[0]);
  EXPECT_EQ(0xCD02040505060708, values[1]);
  EXPECT_EQ(0u, values[2]);
  free(values);
}

ASSEMBLER_TEST_GENERATE(StoreDoubleWord_Pos, assembler) {
  __ st_d(A1, Address(A0, 8));
  __ ret();
}
ASSEMBLER_TEST_RUN(StoreDoubleWord_Pos, test) {
  uint64_t* values = reinterpret_cast<uint64_t*>(malloc(3 * sizeof(uint64_t)));
  values[0] = 0;
  values[1] = 0;
  values[2] = 0;
  Call(test->entry(), reinterpret_cast<intptr_t>(&values[1]),
       0xEF03060708090A0B);
  EXPECT_EQ(0u, values[0]);
  EXPECT_EQ(0u, values[1]);
  EXPECT_EQ(0xEF03060708090A0B, values[2]);
  free(values);
}

ASSEMBLER_TEST_GENERATE(StoreDoubleWord_Neg, assembler) {
  __ st_d(A1, Address(A0, -8));
  __ ret();
}
ASSEMBLER_TEST_RUN(StoreDoubleWord_Neg, test) {
  uint64_t* values = reinterpret_cast<uint64_t*>(malloc(3 * sizeof(uint64_t)));
  values[0] = 0;
  values[1] = 0;
  values[2] = 0;
  Call(test->entry(), reinterpret_cast<intptr_t>(&values[1]),
       0xAB01020304050607);
  EXPECT_EQ(0xAB01020304050607, values[0]);
  EXPECT_EQ(0u, values[1]);
  EXPECT_EQ(0u, values[2]);
  free(values);
}

// ---------------------------------------------------------------------------
// Immediate arithmetic and logical instructions.
// ---------------------------------------------------------------------------

ASSEMBLER_TEST_GENERATE(AddImmediate1, assembler) {
  __ addi_d(A0, A0, 42);
  __ ret();
}
ASSEMBLER_TEST_RUN(AddImmediate1, test) {
  EXPECT_EQ(42, Call(test->entry(), 0));
  EXPECT_EQ(40, Call(test->entry(), -2));
  EXPECT_EQ(0, Call(test->entry(), -42));
}

ASSEMBLER_TEST_GENERATE(AddImmediate2, assembler) {
  __ addi_d(A0, A0, -42);
  __ ret();
}
ASSEMBLER_TEST_RUN(AddImmediate2, test) {
  EXPECT_EQ(-42, Call(test->entry(), 0));
  EXPECT_EQ(-44, Call(test->entry(), -2));
  EXPECT_EQ(38, Call(test->entry(), 80));
}

ASSEMBLER_TEST_GENERATE(XorImmediate, assembler) {
  __ xori(A0, A0, 42);
  __ ret();
}
ASSEMBLER_TEST_RUN(XorImmediate, test) {
  EXPECT_EQ(42, Call(test->entry(), 0));
  EXPECT_EQ(43, Call(test->entry(), 1));
  EXPECT_EQ(32, Call(test->entry(), 10));
  EXPECT_EQ(-43, Call(test->entry(), -1));
  EXPECT_EQ(-36, Call(test->entry(), -10));
}

ASSEMBLER_TEST_GENERATE(OrImmediate, assembler) {
  __ ori(A0, A0, 6);
  __ ret();
}
ASSEMBLER_TEST_RUN(OrImmediate, test) {
  EXPECT_EQ(6, Call(test->entry(), 0));
  EXPECT_EQ(7, Call(test->entry(), 1));
  EXPECT_EQ(15, Call(test->entry(), 11));
  EXPECT_EQ(-1, Call(test->entry(), -1));
  EXPECT_EQ(-9, Call(test->entry(), -11));
}

ASSEMBLER_TEST_GENERATE(AndImmediate, assembler) {
  __ andi(A0, A0, 6);
  __ ret();
}
ASSEMBLER_TEST_RUN(AndImmediate, test) {
  EXPECT_EQ(0, Call(test->entry(), 0));
  EXPECT_EQ(0, Call(test->entry(), 1));
  EXPECT_EQ(2, Call(test->entry(), 11));
  EXPECT_EQ(6, Call(test->entry(), -1));
  EXPECT_EQ(4, Call(test->entry(), -11));
}

// ---------------------------------------------------------------------------
// Shift-by-immediate instructions.
// ---------------------------------------------------------------------------

ASSEMBLER_TEST_GENERATE(ShiftLeftLogicalImmediate, assembler) {
  __ slli_d(A0, A0, 2);
  __ ret();
}
ASSEMBLER_TEST_RUN(ShiftLeftLogicalImmediate, test) {
  EXPECT_EQ(84, Call(test->entry(), 21));
  EXPECT_EQ(4, Call(test->entry(), 1));
  EXPECT_EQ(0, Call(test->entry(), 0));
  EXPECT_EQ(-4, Call(test->entry(), -1));
  EXPECT_EQ(-84, Call(test->entry(), -21));
}

ASSEMBLER_TEST_GENERATE(ShiftLeftLogicalImmediate2, assembler) {
  __ slli_d(A0, A0, 63);
  __ ret();
}
ASSEMBLER_TEST_RUN(ShiftLeftLogicalImmediate2, test) {
  EXPECT_EQ(0, Call(test->entry(), 2));
  EXPECT_EQ(kMinIntX, Call(test->entry(), 1));
  EXPECT_EQ(0, Call(test->entry(), 0));
  EXPECT_EQ(kMinIntX, Call(test->entry(), -1));
  EXPECT_EQ(0, Call(test->entry(), -2));
}

ASSEMBLER_TEST_GENERATE(ShiftRightLogicalImmediate, assembler) {
  __ srli_d(A0, A0, 2);
  __ ret();
}
ASSEMBLER_TEST_RUN(ShiftRightLogicalImmediate, test) {
  EXPECT_EQ(5, Call(test->entry(), 21));
  EXPECT_EQ(0, Call(test->entry(), 1));
  EXPECT_EQ(0, Call(test->entry(), 0));
  EXPECT_EQ(static_cast<intptr_t>(static_cast<uintptr_t>(-1) >> 2),
            Call(test->entry(), -1));
  EXPECT_EQ(static_cast<intptr_t>(static_cast<uintptr_t>(-21) >> 2),
            Call(test->entry(), -21));
}

ASSEMBLER_TEST_GENERATE(ShiftRightLogicalImmediate2, assembler) {
  __ srli_d(A0, A0, 63);
  __ ret();
}
ASSEMBLER_TEST_RUN(ShiftRightLogicalImmediate2, test) {
  EXPECT_EQ(0, Call(test->entry(), 21));
  EXPECT_EQ(0, Call(test->entry(), 1));
  EXPECT_EQ(0, Call(test->entry(), 0));
  EXPECT_EQ(1, Call(test->entry(), -1));
  EXPECT_EQ(1, Call(test->entry(), -21));
}

ASSEMBLER_TEST_GENERATE(ShiftRightArithmeticImmediate, assembler) {
  __ srai_d(A0, A0, 2);
  __ ret();
}
ASSEMBLER_TEST_RUN(ShiftRightArithmeticImmediate, test) {
  EXPECT_EQ(5, Call(test->entry(), 21));
  EXPECT_EQ(0, Call(test->entry(), 1));
  EXPECT_EQ(0, Call(test->entry(), 0));
  EXPECT_EQ(-1, Call(test->entry(), -1));
  EXPECT_EQ(-6, Call(test->entry(), -21));
}

ASSEMBLER_TEST_GENERATE(ShiftRightArithmeticImmediate2, assembler) {
  __ srai_d(A0, A0, 63);
  __ ret();
}
ASSEMBLER_TEST_RUN(ShiftRightArithmeticImmediate2, test) {
  EXPECT_EQ(0, Call(test->entry(), 21));
  EXPECT_EQ(0, Call(test->entry(), 1));
  EXPECT_EQ(0, Call(test->entry(), 0));
  EXPECT_EQ(-1, Call(test->entry(), -1));
  EXPECT_EQ(-1, Call(test->entry(), -21));
}

// ---------------------------------------------------------------------------
// Register-register arithmetic and logical instructions.
// ---------------------------------------------------------------------------

ASSEMBLER_TEST_GENERATE(Add, assembler) {
  __ add_d(A0, A0, A1);
  __ ret();
}
ASSEMBLER_TEST_RUN(Add, test) {
  EXPECT_EQ(24, Call(test->entry(), 7, 17));
  EXPECT_EQ(-10, Call(test->entry(), 7, -17));
  EXPECT_EQ(10, Call(test->entry(), -7, 17));
  EXPECT_EQ(-24, Call(test->entry(), -7, -17));
  EXPECT_EQ(24, Call(test->entry(), 17, 7));
  EXPECT_EQ(10, Call(test->entry(), 17, -7));
  EXPECT_EQ(-10, Call(test->entry(), -17, 7));
  EXPECT_EQ(-24, Call(test->entry(), -17, -7));
}

ASSEMBLER_TEST_GENERATE(Subtract, assembler) {
  __ sub_d(A0, A0, A1);
  __ ret();
}
ASSEMBLER_TEST_RUN(Subtract, test) {
  EXPECT_EQ(-10, Call(test->entry(), 7, 17));
  EXPECT_EQ(24, Call(test->entry(), 7, -17));
  EXPECT_EQ(-24, Call(test->entry(), -7, 17));
  EXPECT_EQ(10, Call(test->entry(), -7, -17));
  EXPECT_EQ(10, Call(test->entry(), 17, 7));
  EXPECT_EQ(24, Call(test->entry(), 17, -7));
  EXPECT_EQ(-24, Call(test->entry(), -17, 7));
  EXPECT_EQ(-10, Call(test->entry(), -17, -7));
}

ASSEMBLER_TEST_GENERATE(ShiftLeftLogical, assembler) {
  __ sll_d(A0, A0, A1);
  __ ret();
}
ASSEMBLER_TEST_RUN(ShiftLeftLogical, test) {
  EXPECT_EQ(2176, Call(test->entry(), 17, 7));
  EXPECT_EQ(-2176, Call(test->entry(), -17, 7));
  EXPECT_EQ(34, Call(test->entry(), 17, 1));
  EXPECT_EQ(-34, Call(test->entry(), -17, 1));
  EXPECT_EQ(17, Call(test->entry(), 17, 0));
  EXPECT_EQ(-17, Call(test->entry(), -17, 0));
}

ASSEMBLER_TEST_GENERATE(ShiftRightLogical, assembler) {
  __ srl_d(A0, A0, A1);
  __ ret();
}
ASSEMBLER_TEST_RUN(ShiftRightLogical, test) {
  EXPECT_EQ(0, Call(test->entry(), 17, 7));
  EXPECT_EQ(static_cast<intptr_t>(static_cast<uintptr_t>(-17) >> 7),
            Call(test->entry(), -17, 7));
  EXPECT_EQ(8, Call(test->entry(), 17, 1));
  EXPECT_EQ(static_cast<intptr_t>(static_cast<uintptr_t>(-17) >> 1),
            Call(test->entry(), -17, 1));
  EXPECT_EQ(17, Call(test->entry(), 17, 0));
  EXPECT_EQ(-17, Call(test->entry(), -17, 0));
}

ASSEMBLER_TEST_GENERATE(ShiftRightArithmetic, assembler) {
  __ sra_d(A0, A0, A1);
  __ ret();
}
ASSEMBLER_TEST_RUN(ShiftRightArithmetic, test) {
  EXPECT_EQ(0, Call(test->entry(), 17, 7));
  EXPECT_EQ(-1, Call(test->entry(), -17, 7));
  EXPECT_EQ(8, Call(test->entry(), 17, 1));
  EXPECT_EQ(-9, Call(test->entry(), -17, 1));
  EXPECT_EQ(17, Call(test->entry(), 17, 0));
  EXPECT_EQ(-17, Call(test->entry(), -17, 0));
}

ASSEMBLER_TEST_GENERATE(SetLessThan, assembler) {
  __ slt(A0, A0, A1);
  __ ret();
}
ASSEMBLER_TEST_RUN(SetLessThan, test) {
  EXPECT_EQ(0, Call(test->entry(), 7, 7));
  EXPECT_EQ(0, Call(test->entry(), -7, -7));
  EXPECT_EQ(1, Call(test->entry(), 7, 17));
  EXPECT_EQ(0, Call(test->entry(), 7, -17));
  EXPECT_EQ(1, Call(test->entry(), -7, 17));
  EXPECT_EQ(0, Call(test->entry(), -7, -17));
  EXPECT_EQ(0, Call(test->entry(), 17, 7));
  EXPECT_EQ(0, Call(test->entry(), 17, -7));
  EXPECT_EQ(1, Call(test->entry(), -17, 7));
  EXPECT_EQ(1, Call(test->entry(), -17, -7));
}

ASSEMBLER_TEST_GENERATE(SetLessThanUnsigned, assembler) {
  __ sltu(A0, A0, A1);
  __ ret();
}
ASSEMBLER_TEST_RUN(SetLessThanUnsigned, test) {
  EXPECT_EQ(0, Call(test->entry(), 7, 7));
  EXPECT_EQ(0, Call(test->entry(), -7, -7));
  EXPECT_EQ(1, Call(test->entry(), 7, 17));
  EXPECT_EQ(1, Call(test->entry(), 7, -17));
  EXPECT_EQ(0, Call(test->entry(), -7, 17));
  EXPECT_EQ(0, Call(test->entry(), -7, -17));
  EXPECT_EQ(0, Call(test->entry(), 17, 7));
  EXPECT_EQ(1, Call(test->entry(), 17, -7));
  EXPECT_EQ(0, Call(test->entry(), -17, 7));
  EXPECT_EQ(1, Call(test->entry(), -17, -7));
}

ASSEMBLER_TEST_GENERATE(Xor, assembler) {
  __ xor_(A0, A0, A1);
  __ ret();
}
ASSEMBLER_TEST_RUN(Xor, test) {
  EXPECT_EQ(22, Call(test->entry(), 7, 17));
  EXPECT_EQ(-24, Call(test->entry(), 7, -17));
  EXPECT_EQ(-24, Call(test->entry(), -7, 17));
  EXPECT_EQ(22, Call(test->entry(), -7, -17));
  EXPECT_EQ(22, Call(test->entry(), 17, 7));
  EXPECT_EQ(-24, Call(test->entry(), 17, -7));
  EXPECT_EQ(-24, Call(test->entry(), -17, 7));
  EXPECT_EQ(22, Call(test->entry(), -17, -7));
}

ASSEMBLER_TEST_GENERATE(Or, assembler) {
  __ or_(A0, A0, A1);
  __ ret();
}
ASSEMBLER_TEST_RUN(Or, test) {
  EXPECT_EQ(23, Call(test->entry(), 7, 17));
  EXPECT_EQ(-17, Call(test->entry(), 7, -17));
  EXPECT_EQ(-7, Call(test->entry(), -7, 17));
  EXPECT_EQ(-1, Call(test->entry(), -7, -17));
  EXPECT_EQ(23, Call(test->entry(), 17, 7));
  EXPECT_EQ(-7, Call(test->entry(), 17, -7));
  EXPECT_EQ(-17, Call(test->entry(), -17, 7));
  EXPECT_EQ(-1, Call(test->entry(), -17, -7));
}

ASSEMBLER_TEST_GENERATE(And, assembler) {
  __ and_(A0, A0, A1);
  __ ret();
}
ASSEMBLER_TEST_RUN(And, test) {
  EXPECT_EQ(1, Call(test->entry(), 7, 17));
  EXPECT_EQ(7, Call(test->entry(), 7, -17));
  EXPECT_EQ(17, Call(test->entry(), -7, 17));
  EXPECT_EQ(-23, Call(test->entry(), -7, -17));
  EXPECT_EQ(1, Call(test->entry(), 17, 7));
  EXPECT_EQ(17, Call(test->entry(), 17, -7));
  EXPECT_EQ(7, Call(test->entry(), -17, 7));
  EXPECT_EQ(-23, Call(test->entry(), -17, -7));
}

// ---------------------------------------------------------------------------
// Multiply and divide instructions.
// ---------------------------------------------------------------------------

ASSEMBLER_TEST_GENERATE(Multiply, assembler) {
  __ mul_d(A0, A0, A1);
  __ ret();
}
ASSEMBLER_TEST_RUN(Multiply, test) {
  EXPECT_EQ(68, Call(test->entry(), 4, 17));
  EXPECT_EQ(-68, Call(test->entry(), -4, 17));
  EXPECT_EQ(-68, Call(test->entry(), 4, -17));
  EXPECT_EQ(68, Call(test->entry(), -4, -17));
  EXPECT_EQ(68, Call(test->entry(), 17, 4));
  EXPECT_EQ(-68, Call(test->entry(), -17, 4));
  EXPECT_EQ(-68, Call(test->entry(), 17, -4));
  EXPECT_EQ(68, Call(test->entry(), -17, -4));
}

ASSEMBLER_TEST_GENERATE(MultiplyHigh, assembler) {
  __ mulh_d(A0, A0, A1);
  __ ret();
}
ASSEMBLER_TEST_RUN(MultiplyHigh, test) {
  EXPECT_EQ(0, Call(test->entry(), 4, 17));
  EXPECT_EQ(-1, Call(test->entry(), -4, 17));
  EXPECT_EQ(-1, Call(test->entry(), 4, -17));
  EXPECT_EQ(0, Call(test->entry(), -4, -17));
  EXPECT_EQ(0, Call(test->entry(), 17, 4));
  EXPECT_EQ(-1, Call(test->entry(), -17, 4));
  EXPECT_EQ(-1, Call(test->entry(), 17, -4));
  EXPECT_EQ(0, Call(test->entry(), -17, -4));
}

ASSEMBLER_TEST_GENERATE(MultiplyHighUnsigned, assembler) {
  __ mulh_du(A0, A0, A1);
  __ ret();
}
ASSEMBLER_TEST_RUN(MultiplyHighUnsigned, test) {
  EXPECT_EQ(0, Call(test->entry(), 4, 17));
  EXPECT_EQ(16, Call(test->entry(), -4, 17));
  EXPECT_EQ(3, Call(test->entry(), 4, -17));
  EXPECT_EQ(-21, Call(test->entry(), -4, -17));
  EXPECT_EQ(0, Call(test->entry(), 17, 4));
  EXPECT_EQ(3, Call(test->entry(), -17, 4));
  EXPECT_EQ(16, Call(test->entry(), 17, -4));
  EXPECT_EQ(-21, Call(test->entry(), -17, -4));
}

ASSEMBLER_TEST_GENERATE(Divide, assembler) {
  __ div_d(A0, A0, A1);
  __ ret();
}
ASSEMBLER_TEST_RUN(Divide, test) {
  EXPECT_EQ(0, Call(test->entry(), 4, 17));
  EXPECT_EQ(0, Call(test->entry(), -4, 17));
  EXPECT_EQ(0, Call(test->entry(), 4, -17));
  EXPECT_EQ(0, Call(test->entry(), -4, -17));
  EXPECT_EQ(4, Call(test->entry(), 17, 4));
  EXPECT_EQ(-4, Call(test->entry(), -17, 4));
  EXPECT_EQ(-4, Call(test->entry(), 17, -4));
  EXPECT_EQ(4, Call(test->entry(), -17, -4));
}

ASSEMBLER_TEST_GENERATE(Remainder, assembler) {
  __ mod_d(A0, A0, A1);
  __ ret();
}
ASSEMBLER_TEST_RUN(Remainder, test) {
  EXPECT_EQ(4, Call(test->entry(), 4, 17));
  EXPECT_EQ(-4, Call(test->entry(), -4, 17));
  EXPECT_EQ(4, Call(test->entry(), 4, -17));
  EXPECT_EQ(-4, Call(test->entry(), -4, -17));
  EXPECT_EQ(1, Call(test->entry(), 17, 4));
  EXPECT_EQ(-1, Call(test->entry(), -17, 4));
  EXPECT_EQ(1, Call(test->entry(), 17, -4));
  EXPECT_EQ(-1, Call(test->entry(), -17, -4));
}

// ---------------------------------------------------------------------------
// Count leading/trailing zeroes.
// ---------------------------------------------------------------------------

ASSEMBLER_TEST_GENERATE(CountLeadingZeroes, assembler) {
  __ clz_d(A0, A0);
  __ ret();
}
ASSEMBLER_TEST_RUN(CountLeadingZeroes, test) {
  EXPECT_EQ(64, Call(test->entry(), 0));
  EXPECT_EQ(63, Call(test->entry(), 1));
  EXPECT_EQ(62, Call(test->entry(), 2));
  EXPECT_EQ(61, Call(test->entry(), 4));
  EXPECT_EQ(56, Call(test->entry(), 240));
  EXPECT_EQ(0, Call(test->entry(), -1));
  EXPECT_EQ(0, Call(test->entry(), -2));
  EXPECT_EQ(0, Call(test->entry(), -4));
  EXPECT_EQ(0, Call(test->entry(), -240));
}

ASSEMBLER_TEST_GENERATE(CountLeadingZeroesWord, assembler) {
  __ clz_w(A0, A0);
  __ ret();
}
ASSEMBLER_TEST_RUN(CountLeadingZeroesWord, test) {
  EXPECT_EQ(32, Call(test->entry(), 0));
  EXPECT_EQ(31, Call(test->entry(), 1));
  EXPECT_EQ(30, Call(test->entry(), 2));
  EXPECT_EQ(29, Call(test->entry(), 4));
  EXPECT_EQ(24, Call(test->entry(), 240));
  EXPECT_EQ(0, Call(test->entry(), -1));
  EXPECT_EQ(0, Call(test->entry(), -2));
  EXPECT_EQ(0, Call(test->entry(), -4));
  EXPECT_EQ(0, Call(test->entry(), -240));
}

ASSEMBLER_TEST_GENERATE(CountTrailingZeroes, assembler) {
  __ ctz_d(A0, A0);
  __ ret();
}
ASSEMBLER_TEST_RUN(CountTrailingZeroes, test) {
  EXPECT_EQ(64, Call(test->entry(), 0));
  EXPECT_EQ(0, Call(test->entry(), 1));
  EXPECT_EQ(1, Call(test->entry(), 2));
  EXPECT_EQ(2, Call(test->entry(), 4));
  EXPECT_EQ(4, Call(test->entry(), 240));
  EXPECT_EQ(0, Call(test->entry(), -1));
  EXPECT_EQ(1, Call(test->entry(), -2));
  EXPECT_EQ(2, Call(test->entry(), -4));
  EXPECT_EQ(4, Call(test->entry(), -240));
}

ASSEMBLER_TEST_GENERATE(CountTrailingZeroesWord, assembler) {
  __ ctz_w(A0, A0);
  __ ret();
}
ASSEMBLER_TEST_RUN(CountTrailingZeroesWord, test) {
  EXPECT_EQ(32, Call(test->entry(), 0));
  EXPECT_EQ(0, Call(test->entry(), 1));
  EXPECT_EQ(1, Call(test->entry(), 2));
  EXPECT_EQ(2, Call(test->entry(), 4));
  EXPECT_EQ(4, Call(test->entry(), 240));
  EXPECT_EQ(0, Call(test->entry(), -1));
  EXPECT_EQ(1, Call(test->entry(), -2));
  EXPECT_EQ(2, Call(test->entry(), -4));
  EXPECT_EQ(4, Call(test->entry(), -240));
}

// ---------------------------------------------------------------------------
// Branch instructions.
// ---------------------------------------------------------------------------

ASSEMBLER_TEST_GENERATE(BranchEqualForward, assembler) {
  Label label;
  __ beq(A0, A1, &label);
  __ li(A0, 3);
  __ ret();
  __ Bind(&label);
  __ li(A0, 4);
  __ ret();
}
ASSEMBLER_TEST_RUN(BranchEqualForward, test) {
  EXPECT_EQ(4, Call(test->entry(), 1, 1));
  EXPECT_EQ(3, Call(test->entry(), 1, 0));
  EXPECT_EQ(3, Call(test->entry(), 1, -1));
  EXPECT_EQ(3, Call(test->entry(), 0, 1));
  EXPECT_EQ(4, Call(test->entry(), 0, 0));
  EXPECT_EQ(3, Call(test->entry(), 0, -1));
  EXPECT_EQ(3, Call(test->entry(), -1, 1));
  EXPECT_EQ(3, Call(test->entry(), -1, 0));
  EXPECT_EQ(4, Call(test->entry(), -1, -1));
}

ASSEMBLER_TEST_GENERATE(BranchNotEqualForward, assembler) {
  Label label;
  __ bne(A0, A1, &label);
  __ li(A0, 3);
  __ ret();
  __ Bind(&label);
  __ li(A0, 4);
  __ ret();
}
ASSEMBLER_TEST_RUN(BranchNotEqualForward, test) {
  EXPECT_EQ(3, Call(test->entry(), 1, 1));
  EXPECT_EQ(4, Call(test->entry(), 1, 0));
  EXPECT_EQ(4, Call(test->entry(), 1, -1));
  EXPECT_EQ(4, Call(test->entry(), 0, 1));
  EXPECT_EQ(3, Call(test->entry(), 0, 0));
  EXPECT_EQ(4, Call(test->entry(), 0, -1));
  EXPECT_EQ(4, Call(test->entry(), -1, 1));
  EXPECT_EQ(4, Call(test->entry(), -1, 0));
  EXPECT_EQ(3, Call(test->entry(), -1, -1));
}

ASSEMBLER_TEST_GENERATE(BranchLessThanForward, assembler) {
  Label label;
  __ blt(A0, A1, &label);
  __ li(A0, 3);
  __ ret();
  __ Bind(&label);
  __ li(A0, 4);
  __ ret();
}
ASSEMBLER_TEST_RUN(BranchLessThanForward, test) {
  EXPECT_EQ(3, Call(test->entry(), 1, 1));
  EXPECT_EQ(3, Call(test->entry(), 1, 0));
  EXPECT_EQ(3, Call(test->entry(), 1, -1));
  EXPECT_EQ(4, Call(test->entry(), 0, 1));
  EXPECT_EQ(3, Call(test->entry(), 0, 0));
  EXPECT_EQ(3, Call(test->entry(), 0, -1));
  EXPECT_EQ(4, Call(test->entry(), -1, 1));
  EXPECT_EQ(4, Call(test->entry(), -1, 0));
  EXPECT_EQ(3, Call(test->entry(), -1, -1));
}

ASSEMBLER_TEST_GENERATE(BranchLessOrEqualForward, assembler) {
  Label label;
  // A0 <= A1 iff A1 >= A0.
  __ bge(A1, A0, &label);
  __ li(A0, 3);
  __ ret();
  __ Bind(&label);
  __ li(A0, 4);
  __ ret();
}
ASSEMBLER_TEST_RUN(BranchLessOrEqualForward, test) {
  EXPECT_EQ(4, Call(test->entry(), 1, 1));
  EXPECT_EQ(3, Call(test->entry(), 1, 0));
  EXPECT_EQ(3, Call(test->entry(), 1, -1));
  EXPECT_EQ(4, Call(test->entry(), 0, 1));
  EXPECT_EQ(4, Call(test->entry(), 0, 0));
  EXPECT_EQ(3, Call(test->entry(), 0, -1));
  EXPECT_EQ(4, Call(test->entry(), -1, 1));
  EXPECT_EQ(4, Call(test->entry(), -1, 0));
  EXPECT_EQ(4, Call(test->entry(), -1, -1));
}

ASSEMBLER_TEST_GENERATE(BranchGreaterThanForward, assembler) {
  Label label;
  // A0 > A1 iff A1 < A0.
  __ blt(A1, A0, &label);
  __ li(A0, 3);
  __ ret();
  __ Bind(&label);
  __ li(A0, 4);
  __ ret();
}
ASSEMBLER_TEST_RUN(BranchGreaterThanForward, test) {
  EXPECT_EQ(3, Call(test->entry(), 1, 1));
  EXPECT_EQ(4, Call(test->entry(), 1, 0));
  EXPECT_EQ(4, Call(test->entry(), 1, -1));
  EXPECT_EQ(3, Call(test->entry(), 0, 1));
  EXPECT_EQ(3, Call(test->entry(), 0, 0));
  EXPECT_EQ(4, Call(test->entry(), 0, -1));
  EXPECT_EQ(3, Call(test->entry(), -1, 1));
  EXPECT_EQ(3, Call(test->entry(), -1, 0));
  EXPECT_EQ(3, Call(test->entry(), -1, -1));
}

ASSEMBLER_TEST_GENERATE(BranchGreaterOrEqualForward, assembler) {
  Label label;
  __ bge(A0, A1, &label);
  __ li(A0, 3);
  __ ret();
  __ Bind(&label);
  __ li(A0, 4);
  __ ret();
}
ASSEMBLER_TEST_RUN(BranchGreaterOrEqualForward, test) {
  EXPECT_EQ(4, Call(test->entry(), 1, 1));
  EXPECT_EQ(4, Call(test->entry(), 1, 0));
  EXPECT_EQ(4, Call(test->entry(), 1, -1));
  EXPECT_EQ(3, Call(test->entry(), 0, 1));
  EXPECT_EQ(4, Call(test->entry(), 0, 0));
  EXPECT_EQ(4, Call(test->entry(), 0, -1));
  EXPECT_EQ(3, Call(test->entry(), -1, 1));
  EXPECT_EQ(3, Call(test->entry(), -1, 0));
  EXPECT_EQ(4, Call(test->entry(), -1, -1));
}

ASSEMBLER_TEST_GENERATE(BranchLessThanUnsignedForward, assembler) {
  Label label;
  __ bltu(A0, A1, &label);
  __ li(A0, 3);
  __ ret();
  __ Bind(&label);
  __ li(A0, 4);
  __ ret();
}
ASSEMBLER_TEST_RUN(BranchLessThanUnsignedForward, test) {
  EXPECT_EQ(3, Call(test->entry(), 1, 1));
  EXPECT_EQ(3, Call(test->entry(), 1, 0));
  EXPECT_EQ(4, Call(test->entry(), 1, -1));
  EXPECT_EQ(4, Call(test->entry(), 0, 1));
  EXPECT_EQ(3, Call(test->entry(), 0, 0));
  EXPECT_EQ(4, Call(test->entry(), 0, -1));
  EXPECT_EQ(3, Call(test->entry(), -1, 1));
  EXPECT_EQ(3, Call(test->entry(), -1, 0));
  EXPECT_EQ(3, Call(test->entry(), -1, -1));
}

ASSEMBLER_TEST_GENERATE(BranchLessOrEqualUnsignedForward, assembler) {
  Label label;
  __ bleu(A0, A1, &label);
  __ li(A0, 3);
  __ ret();
  __ Bind(&label);
  __ li(A0, 4);
  __ ret();
}
ASSEMBLER_TEST_RUN(BranchLessOrEqualUnsignedForward, test) {
  EXPECT_EQ(4, Call(test->entry(), 1, 1));
  EXPECT_EQ(3, Call(test->entry(), 1, 0));
  EXPECT_EQ(4, Call(test->entry(), 1, -1));
  EXPECT_EQ(4, Call(test->entry(), 0, 1));
  EXPECT_EQ(4, Call(test->entry(), 0, 0));
  EXPECT_EQ(4, Call(test->entry(), 0, -1));
  EXPECT_EQ(3, Call(test->entry(), -1, 1));
  EXPECT_EQ(3, Call(test->entry(), -1, 0));
  EXPECT_EQ(4, Call(test->entry(), -1, -1));
}

ASSEMBLER_TEST_GENERATE(BranchGreaterThanUnsignedForward, assembler) {
  Label label;
  // A0 > A1 (unsigned) iff A1 < A0 (unsigned).
  __ bltu(A1, A0, &label);
  __ li(A0, 3);
  __ ret();
  __ Bind(&label);
  __ li(A0, 4);
  __ ret();
}
ASSEMBLER_TEST_RUN(BranchGreaterThanUnsignedForward, test) {
  EXPECT_EQ(3, Call(test->entry(), 1, 1));
  EXPECT_EQ(4, Call(test->entry(), 1, 0));
  EXPECT_EQ(3, Call(test->entry(), 1, -1));
  EXPECT_EQ(3, Call(test->entry(), 0, 1));
  EXPECT_EQ(3, Call(test->entry(), 0, 0));
  EXPECT_EQ(3, Call(test->entry(), 0, -1));
  EXPECT_EQ(4, Call(test->entry(), -1, 1));
  EXPECT_EQ(4, Call(test->entry(), -1, 0));
  EXPECT_EQ(3, Call(test->entry(), -1, -1));
}

ASSEMBLER_TEST_GENERATE(BranchGreaterOrEqualUnsignedForward, assembler) {
  Label label;
  __ bgeu(A0, A1, &label);
  __ li(A0, 3);
  __ ret();
  __ Bind(&label);
  __ li(A0, 4);
  __ ret();
}
ASSEMBLER_TEST_RUN(BranchGreaterOrEqualUnsignedForward, test) {
  EXPECT_EQ(4, Call(test->entry(), 1, 1));
  EXPECT_EQ(4, Call(test->entry(), 1, 0));
  EXPECT_EQ(3, Call(test->entry(), 1, -1));
  EXPECT_EQ(3, Call(test->entry(), 0, 1));
  EXPECT_EQ(4, Call(test->entry(), 0, 0));
  EXPECT_EQ(3, Call(test->entry(), 0, -1));
  EXPECT_EQ(4, Call(test->entry(), -1, 1));
  EXPECT_EQ(4, Call(test->entry(), -1, 0));
  EXPECT_EQ(4, Call(test->entry(), -1, -1));
}

// ---------------------------------------------------------------------------
// Atomic instructions.
// ---------------------------------------------------------------------------

ASSEMBLER_TEST_GENERATE(LoadReserveStoreConditionalDoubleWord_Success, assembler) {
  // A0 = address.
  __ ll_d(A1, Address(A0, 0));  // Load and reserve: A1 = *A0.
  __ addi_d(A1, A1, 1);         // A1 += 1.
  __ sc_d(A1, Address(A0, 0));  // *A0 = A1; A1 = 1 on success, 0 on failure.
  __ mv(A0, A1);                // A0 = status.
  __ ret();
}
ASSEMBLER_TEST_RUN(LoadReserveStoreConditionalDoubleWord_Success, test) {
  int64_t x = 41;
  EXPECT_EQ(1, Call(test->entry(), reinterpret_cast<intptr_t>(&x)));
  EXPECT_EQ(42, x);
}

ASSEMBLER_TEST_GENERATE(AmoAndDoubleWord, assembler) {
  __ mv(A2, A0);                          // A2 = address.
  __ amoand_db_d(A0, A1, Address(A2, 0));  // A0 = old value; *A2 &= A1.
  __ ret();
}
ASSEMBLER_TEST_RUN(AmoAndDoubleWord, test) {
  int64_t x = 42;  // 0b101010.
  EXPECT_EQ(42, Call(test->entry(), reinterpret_cast<intptr_t>(&x), 10));
  EXPECT_EQ(10, x);
}

ASSEMBLER_TEST_GENERATE(AmoOrDoubleWord, assembler) {
  __ mv(A2, A0);                         // A2 = address.
  __ amoor_db_d(A0, A1, Address(A2, 0));  // A0 = old value; *A2 |= A1.
  __ ret();
}
ASSEMBLER_TEST_RUN(AmoOrDoubleWord, test) {
  int64_t x = 0;
  EXPECT_EQ(0, Call(test->entry(), reinterpret_cast<intptr_t>(&x), 0x42));
  EXPECT_EQ(0x42, x);
}

// ---------------------------------------------------------------------------
// Floating-point load and store instructions.
// ---------------------------------------------------------------------------

ASSEMBLER_TEST_GENERATE(LoadSingleFloat, assembler) {
  __ fld_s(FA0, Address(A0, 1 * sizeof(float)));
  __ ret();
}
ASSEMBLER_TEST_RUN(LoadSingleFloat, test) {
  float* data = reinterpret_cast<float*>(malloc(3 * sizeof(float)));
  data[0] = 1.7f;
  data[1] = 2.8f;
  data[2] = 3.9f;
  EXPECT_EQ(data[1], CallF(test->entry(), reinterpret_cast<intptr_t>(data)));
  free(data);
}

ASSEMBLER_TEST_GENERATE(StoreSingleFloat, assembler) {
  __ fst_s(FA0, Address(A0, 1 * sizeof(float)));
  __ ret();
}
ASSEMBLER_TEST_RUN(StoreSingleFloat, test) {
  float* data = reinterpret_cast<float*>(malloc(3 * sizeof(float)));
  data[0] = 1.7f;
  data[1] = 2.8f;
  data[2] = 3.9f;
  CallF(test->entry(), reinterpret_cast<intptr_t>(data), 4.2f);
  EXPECT_EQ(4.2f, data[1]);
  free(data);
}

ASSEMBLER_TEST_GENERATE(LoadDoubleFloat, assembler) {
  __ fld_d(FA0, Address(A0, 1 * sizeof(double)));
  __ ret();
}
ASSEMBLER_TEST_RUN(LoadDoubleFloat, test) {
  double* data = reinterpret_cast<double*>(malloc(3 * sizeof(double)));
  data[0] = 1.7;
  data[1] = 2.8;
  data[2] = 3.9;
  EXPECT_EQ(data[1], CallD(test->entry(), reinterpret_cast<intptr_t>(data)));
  free(data);
}

ASSEMBLER_TEST_GENERATE(StoreDoubleFloat, assembler) {
  __ fst_d(FA0, Address(A0, 1 * sizeof(double)));
  __ ret();
}
ASSEMBLER_TEST_RUN(StoreDoubleFloat, test) {
  double* data = reinterpret_cast<double*>(malloc(3 * sizeof(double)));
  data[0] = 1.7;
  data[1] = 2.8;
  data[2] = 3.9;
  CallD(test->entry(), reinterpret_cast<intptr_t>(data), 4.2);
  EXPECT_EQ(4.2, data[1]);
  free(data);
}

// ---------------------------------------------------------------------------
// Floating-point arithmetic instructions.
// ---------------------------------------------------------------------------

ASSEMBLER_TEST_GENERATE(SingleAdd, assembler) {
  __ fadd_s(FA0, FA0, FA1);
  __ ret();
}
ASSEMBLER_TEST_RUN(SingleAdd, test) {
  EXPECT_EQ(8.0f, CallF(test->entry(), 3.0f, 5.0f));
  EXPECT_EQ(2.0f, CallF(test->entry(), -3.0f, 5.0f));
  EXPECT_EQ(-2.0f, CallF(test->entry(), 3.0f, -5.0f));
  EXPECT_EQ(-8.0f, CallF(test->entry(), -3.0f, -5.0f));
  EXPECT_EQ(10.0f, CallF(test->entry(), 7.0f, 3.0f));
  EXPECT_EQ(-4.0f, CallF(test->entry(), -7.0f, 3.0f));
  EXPECT_EQ(4.0f, CallF(test->entry(), 7.0f, -3.0f));
  EXPECT_EQ(-10.0f, CallF(test->entry(), -7.0f, -3.0f));
}

ASSEMBLER_TEST_GENERATE(SingleSubtract, assembler) {
  __ fsub_s(FA0, FA0, FA1);
  __ ret();
}
ASSEMBLER_TEST_RUN(SingleSubtract, test) {
  EXPECT_EQ(-2.0f, CallF(test->entry(), 3.0f, 5.0f));
  EXPECT_EQ(-8.0f, CallF(test->entry(), -3.0f, 5.0f));
  EXPECT_EQ(8.0f, CallF(test->entry(), 3.0f, -5.0f));
  EXPECT_EQ(2.0f, CallF(test->entry(), -3.0f, -5.0f));
  EXPECT_EQ(4.0f, CallF(test->entry(), 7.0f, 3.0f));
  EXPECT_EQ(-10.0f, CallF(test->entry(), -7.0f, 3.0f));
  EXPECT_EQ(10.0f, CallF(test->entry(), 7.0f, -3.0f));
  EXPECT_EQ(-4.0f, CallF(test->entry(), -7.0f, -3.0f));
}

ASSEMBLER_TEST_GENERATE(SingleMultiply, assembler) {
  __ fmul_s(FA0, FA0, FA1);
  __ ret();
}
ASSEMBLER_TEST_RUN(SingleMultiply, test) {
  EXPECT_EQ(15.0f, CallF(test->entry(), 3.0f, 5.0f));
  EXPECT_EQ(-15.0f, CallF(test->entry(), -3.0f, 5.0f));
  EXPECT_EQ(-15.0f, CallF(test->entry(), 3.0f, -5.0f));
  EXPECT_EQ(15.0f, CallF(test->entry(), -3.0f, -5.0f));
  EXPECT_EQ(21.0f, CallF(test->entry(), 7.0f, 3.0f));
  EXPECT_EQ(-21.0f, CallF(test->entry(), -7.0f, 3.0f));
  EXPECT_EQ(-21.0f, CallF(test->entry(), 7.0f, -3.0f));
  EXPECT_EQ(21.0f, CallF(test->entry(), -7.0f, -3.0f));
}

ASSEMBLER_TEST_GENERATE(SingleDivide, assembler) {
  __ fdiv_s(FA0, FA0, FA1);
  __ ret();
}
ASSEMBLER_TEST_RUN(SingleDivide, test) {
  EXPECT_EQ(2.0f, CallF(test->entry(), 10.0f, 5.0f));
  EXPECT_EQ(-2.0f, CallF(test->entry(), -10.0f, 5.0f));
  EXPECT_EQ(-2.0f, CallF(test->entry(), 10.0f, -5.0f));
  EXPECT_EQ(2.0f, CallF(test->entry(), -10.0f, -5.0f));
}

ASSEMBLER_TEST_GENERATE(SingleSquareRoot, assembler) {
  __ fsqrt_s(FA0, FA0);
  __ ret();
}
ASSEMBLER_TEST_RUN(SingleSquareRoot, test) {
  EXPECT_EQ(0.0f, CallF(test->entry(), 0.0f));
  EXPECT_EQ(1.0f, CallF(test->entry(), 1.0f));
  EXPECT_EQ(2.0f, CallF(test->entry(), 4.0f));
  EXPECT_EQ(3.0f, CallF(test->entry(), 9.0f));
}

ASSEMBLER_TEST_GENERATE(SingleMin, assembler) {
  __ fmin_s(FA0, FA0, FA1);
  __ ret();
}
ASSEMBLER_TEST_RUN(SingleMin, test) {
  EXPECT_EQ(3.0f, CallF(test->entry(), 3.0f, 5.0f));
  EXPECT_EQ(-3.0f, CallF(test->entry(), -3.0f, 5.0f));
  EXPECT_EQ(-5.0f, CallF(test->entry(), 3.0f, -5.0f));
  EXPECT_EQ(-5.0f, CallF(test->entry(), -3.0f, -5.0f));
}

ASSEMBLER_TEST_GENERATE(SingleMax, assembler) {
  __ fmax_s(FA0, FA0, FA1);
  __ ret();
}
ASSEMBLER_TEST_RUN(SingleMax, test) {
  EXPECT_EQ(5.0f, CallF(test->entry(), 3.0f, 5.0f));
  EXPECT_EQ(5.0f, CallF(test->entry(), -3.0f, 5.0f));
  EXPECT_EQ(3.0f, CallF(test->entry(), 3.0f, -5.0f));
  EXPECT_EQ(-3.0f, CallF(test->entry(), -3.0f, -5.0f));
}

ASSEMBLER_TEST_GENERATE(DoubleAdd, assembler) {
  __ fadd_d(FA0, FA0, FA1);
  __ ret();
}
ASSEMBLER_TEST_RUN(DoubleAdd, test) {
  EXPECT_EQ(8.0, CallD(test->entry(), 3.0, 5.0));
  EXPECT_EQ(2.0, CallD(test->entry(), -3.0, 5.0));
  EXPECT_EQ(-2.0, CallD(test->entry(), 3.0, -5.0));
  EXPECT_EQ(-8.0, CallD(test->entry(), -3.0, -5.0));
  EXPECT_EQ(10.0, CallD(test->entry(), 7.0, 3.0));
  EXPECT_EQ(-4.0, CallD(test->entry(), -7.0, 3.0));
  EXPECT_EQ(4.0, CallD(test->entry(), 7.0, -3.0));
  EXPECT_EQ(-10.0, CallD(test->entry(), -7.0, -3.0));
}

ASSEMBLER_TEST_GENERATE(DoubleSubtract, assembler) {
  __ fsub_d(FA0, FA0, FA1);
  __ ret();
}
ASSEMBLER_TEST_RUN(DoubleSubtract, test) {
  EXPECT_EQ(-2.0, CallD(test->entry(), 3.0, 5.0));
  EXPECT_EQ(-8.0, CallD(test->entry(), -3.0, 5.0));
  EXPECT_EQ(8.0, CallD(test->entry(), 3.0, -5.0));
  EXPECT_EQ(2.0, CallD(test->entry(), -3.0, -5.0));
  EXPECT_EQ(4.0, CallD(test->entry(), 7.0, 3.0));
  EXPECT_EQ(-10.0, CallD(test->entry(), -7.0, 3.0));
  EXPECT_EQ(10.0, CallD(test->entry(), 7.0, -3.0));
  EXPECT_EQ(-4.0, CallD(test->entry(), -7.0, -3.0));
}

ASSEMBLER_TEST_GENERATE(DoubleMultiply, assembler) {
  __ fmul_d(FA0, FA0, FA1);
  __ ret();
}
ASSEMBLER_TEST_RUN(DoubleMultiply, test) {
  EXPECT_EQ(15.0, CallD(test->entry(), 3.0, 5.0));
  EXPECT_EQ(-15.0, CallD(test->entry(), -3.0, 5.0));
  EXPECT_EQ(-15.0, CallD(test->entry(), 3.0, -5.0));
  EXPECT_EQ(15.0, CallD(test->entry(), -3.0, -5.0));
  EXPECT_EQ(21.0, CallD(test->entry(), 7.0, 3.0));
  EXPECT_EQ(-21.0, CallD(test->entry(), -7.0, 3.0));
  EXPECT_EQ(-21.0, CallD(test->entry(), 7.0, -3.0));
  EXPECT_EQ(21.0, CallD(test->entry(), -7.0, -3.0));
}

ASSEMBLER_TEST_GENERATE(DoubleDivide, assembler) {
  __ fdiv_d(FA0, FA0, FA1);
  __ ret();
}
ASSEMBLER_TEST_RUN(DoubleDivide, test) {
  EXPECT_EQ(2.0, CallD(test->entry(), 10.0, 5.0));
  EXPECT_EQ(-2.0, CallD(test->entry(), -10.0, 5.0));
  EXPECT_EQ(-2.0, CallD(test->entry(), 10.0, -5.0));
  EXPECT_EQ(2.0, CallD(test->entry(), -10.0, -5.0));
}

ASSEMBLER_TEST_GENERATE(DoubleSquareRoot, assembler) {
  __ fsqrt_d(FA0, FA0);
  __ ret();
}
ASSEMBLER_TEST_RUN(DoubleSquareRoot, test) {
  EXPECT_EQ(0.0, CallD(test->entry(), 0.0));
  EXPECT_EQ(1.0, CallD(test->entry(), 1.0));
  EXPECT_EQ(2.0, CallD(test->entry(), 4.0));
  EXPECT_EQ(3.0, CallD(test->entry(), 9.0));
}

ASSEMBLER_TEST_GENERATE(DoubleMin, assembler) {
  __ fmin_d(FA0, FA0, FA1);
  __ ret();
}
ASSEMBLER_TEST_RUN(DoubleMin, test) {
  EXPECT_EQ(3.0, CallD(test->entry(), 3.0, 5.0));
  EXPECT_EQ(-3.0, CallD(test->entry(), -3.0, 5.0));
  EXPECT_EQ(-5.0, CallD(test->entry(), 3.0, -5.0));
  EXPECT_EQ(-5.0, CallD(test->entry(), -3.0, -5.0));
}

ASSEMBLER_TEST_GENERATE(DoubleMax, assembler) {
  __ fmax_d(FA0, FA0, FA1);
  __ ret();
}
ASSEMBLER_TEST_RUN(DoubleMax, test) {
  EXPECT_EQ(5.0, CallD(test->entry(), 3.0, 5.0));
  EXPECT_EQ(5.0, CallD(test->entry(), -3.0, 5.0));
  EXPECT_EQ(3.0, CallD(test->entry(), 3.0, -5.0));
  EXPECT_EQ(-3.0, CallD(test->entry(), -3.0, -5.0));
}

// ---------------------------------------------------------------------------
// Floating-point unary instructions.
// ---------------------------------------------------------------------------

ASSEMBLER_TEST_GENERATE(SingleMove, assembler) {
  __ fmov_s(FA0, FA0);
  __ ret();
}
ASSEMBLER_TEST_RUN(SingleMove, test) {
  EXPECT_EQ(42.0f, CallF(test->entry(), 42.0f));
  EXPECT_EQ(-42.0f, CallF(test->entry(), -42.0f));
}

ASSEMBLER_TEST_GENERATE(SingleAbsoluteValue, assembler) {
  __ fabs_s(FA0, FA0);
  __ ret();
}
ASSEMBLER_TEST_RUN(SingleAbsoluteValue, test) {
  EXPECT_EQ(42.0f, CallF(test->entry(), 42.0f));
  EXPECT_EQ(42.0f, CallF(test->entry(), -42.0f));
}

ASSEMBLER_TEST_GENERATE(SingleNegate, assembler) {
  __ fneg_s(FA0, FA0);
  __ ret();
}
ASSEMBLER_TEST_RUN(SingleNegate, test) {
  EXPECT_EQ(-42.0f, CallF(test->entry(), 42.0f));
  EXPECT_EQ(42.0f, CallF(test->entry(), -42.0f));
}

ASSEMBLER_TEST_GENERATE(DoubleMove, assembler) {
  __ fmov_d(FA0, FA0);
  __ ret();
}
ASSEMBLER_TEST_RUN(DoubleMove, test) {
  EXPECT_EQ(42.0, CallD(test->entry(), 42.0));
  EXPECT_EQ(-42.0, CallD(test->entry(), -42.0));
}

ASSEMBLER_TEST_GENERATE(DoubleAbsoluteValue, assembler) {
  __ fabs_d(FA0, FA0);
  __ ret();
}
ASSEMBLER_TEST_RUN(DoubleAbsoluteValue, test) {
  EXPECT_EQ(42.0, CallD(test->entry(), 42.0));
  EXPECT_EQ(42.0, CallD(test->entry(), -42.0));
}

ASSEMBLER_TEST_GENERATE(DoubleNegate, assembler) {
  __ fneg_d(FA0, FA0);
  __ ret();
}
ASSEMBLER_TEST_RUN(DoubleNegate, test) {
  EXPECT_EQ(-42.0, CallD(test->entry(), 42.0));
  EXPECT_EQ(42.0, CallD(test->entry(), -42.0));
}

// ---------------------------------------------------------------------------
// Floating-point conversion instructions.
// ---------------------------------------------------------------------------

ASSEMBLER_TEST_GENERATE(SingleToDouble, assembler) {
  __ fcvt_d_s(FA0, FA0);
  __ ret();
}
ASSEMBLER_TEST_RUN(SingleToDouble, test) {
  EXPECT_EQ(3.0, CallD(test->entry(), 3.0f));
  EXPECT_EQ(-3.0, CallD(test->entry(), -3.0f));
}

ASSEMBLER_TEST_GENERATE(DoubleToSingle, assembler) {
  __ fcvt_s_d(FA0, FA0);
  __ ret();
}
ASSEMBLER_TEST_RUN(DoubleToSingle, test) {
  EXPECT_EQ(3.0f, CallF(test->entry(), 3.0));
  EXPECT_EQ(-3.0f, CallF(test->entry(), -3.0));
}

// ---------------------------------------------------------------------------
// Load-immediate instruction.
// ---------------------------------------------------------------------------

ASSEMBLER_TEST_GENERATE(LoadImmediateSmall, assembler) {
  __ li(A0, 42);
  __ ret();
}
ASSEMBLER_TEST_RUN(LoadImmediateSmall, test) {
  EXPECT_EQ(42, Call(test->entry()));
}

ASSEMBLER_TEST_GENERATE(LoadImmediateWord, assembler) {
  __ li(A0, 0x12345678);
  __ ret();
}
ASSEMBLER_TEST_RUN(LoadImmediateWord, test) {
  EXPECT_EQ(0x12345678, Call(test->entry()));
}

intptr_t RegRegImmTests::Lsl(intptr_t value, intptr_t shift, OperandSize sz) {
  return ExtendValue(static_cast<uintptr_t>(value) << shift, sz);
}

intptr_t RegRegImmTests::Asr(intptr_t value, intptr_t shift, OperandSize sz) {
  return ExtendValue(SignExtendValue(value, sz) >> shift, sz);
}
}  // namespace compiler
}  // namespace dart

#endif  // defined TARGET_ARCH_LOONG64
