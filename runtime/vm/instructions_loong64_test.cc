// Copyright (c) 2021, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(TARGET_ARCH_LOONG64)

#include "vm/compiler/assembler/assembler.h"
#include "vm/instructions.h"
#include "vm/stub_code.h"
#include "vm/unit_test.h"
#include "vm/virtual_memory.h"

namespace dart {

#define __ assembler->

ASSEMBLER_TEST_GENERATE(PcRelativeCall, assembler) {
  __ set_constant_pool_allowed(true);
  compiler::Label target;
  __ bl(&target);
  __ ret();
  __ Bind(&target);
  __ ret();
}

ASSEMBLER_TEST_RUN(PcRelativeCall, test) {
  PcRelativeCallPattern pattern(test->entry());
  EXPECT(pattern.IsValid());
  EXPECT_EQ(static_cast<int32_t>(2 * Instr::kInstrSize), pattern.distance());
}

ASSEMBLER_TEST_GENERATE(PcRelativeTailCall, assembler) {
  __ set_constant_pool_allowed(true);
  __ GenerateUnRelocatedPcRelativeTailCall();
  __ ret();
}

ASSEMBLER_TEST_RUN(PcRelativeTailCall, test) {
  PcRelativeTailCallPattern pattern(test->entry());
  EXPECT(pattern.IsValid());
  EXPECT_EQ(0, pattern.distance());
}

}  // namespace dart

#endif  // defined TARGET_ARCH_LOONG64
