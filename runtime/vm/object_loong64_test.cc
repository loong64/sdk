// Copyright (c) 2021, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "platform/assert.h"
#include "vm/globals.h"
#if defined(TARGET_ARCH_LOONG64)

#include "vm/compiler/assembler/assembler.h"
#include "vm/object.h"
#include "vm/unit_test.h"

namespace dart {

#define __ assembler->

// Generate a simple dart code sequence.
// This is used to test Code and Instruction object creation.
void GenerateIncrement(compiler::Assembler* assembler) {
  __ EnterFrame(1 * kWordSize);
  __ LoadImmediate(A0, 0);
  __ PushRegister(A0);
  __ AddImmediate(A0, A0, 1);
  __ Store(A0, compiler::Address(SP));
  __ Load(A1, compiler::Address(SP));
  __ AddImmediate(A1, A1, 1);
  __ PopRegister(A0);
  __ MoveRegister(A0, A1);
  __ LeaveFrame();
  __ ret();
}

// Generate a dart code sequence that embeds a string object in it.
// This is used to test Embedded String objects in the instructions.
void GenerateEmbedStringInCode(compiler::Assembler* assembler,
                               const char* str) {
  const String& string_object =
      String::ZoneHandle(String::New(str, Heap::kOld));
  __ EnterStubFrame();
  __ LoadObject(A0, string_object);
  __ LeaveStubFrame();
  __ ret();
}

// Generate a dart code sequence that embeds a smi object in it.
// This is used to test Embedded Smi objects in the instructions.
void GenerateEmbedSmiInCode(compiler::Assembler* assembler, intptr_t value) {
  const Smi& smi_object = Smi::ZoneHandle(Smi::New(value));
  const intx_t val = static_cast<intx_t>(smi_object.ptr());
  __ LoadImmediate(A0, val);
  __ ret();
}

}  // namespace dart

#endif  // defined TARGET_ARCH_LOONG64
