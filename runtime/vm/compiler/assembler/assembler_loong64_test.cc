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

ASSEMBLER_TEST_GENERATE(Simple, assembler) {
  __ addi_d(A0, A0, 42);
  __ ret();
}

ASSEMBLER_TEST_GENERATE(LoadStore, assembler) {
  __ addi_d(SP, SP, -16);
  __ st_d(A0, SP, 0);
  __ ld_d(A0, SP, 0);
  __ addi_d(SP, SP, 16);
  __ ret();
}

ASSEMBLER_TEST_GENERATE(CallAndRet, assembler) {
  Label target;
  __ bl(&target);
  __ ret();
  __ Bind(&target);
  __ ret();
}


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


intptr_t RegRegImmTests::Lsl(intptr_t value, intptr_t shift, OperandSize sz) {
  return ExtendValue(static_cast<uintptr_t>(value) << shift, sz);
}

intptr_t RegRegImmTests::Asr(intptr_t value, intptr_t shift, OperandSize sz) {
  return ExtendValue(SignExtendValue(value, sz) >> shift, sz);
}
}  // namespace compiler
}  // namespace dart

#endif  // defined TARGET_ARCH_LOONG64

