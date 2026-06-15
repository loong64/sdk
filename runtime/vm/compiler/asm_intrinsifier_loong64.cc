// Copyright (c) 2021, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(TARGET_ARCH_LOONG64)

#include "vm/compiler/assembler/assembler.h"
#include "vm/compiler/asm_intrinsifier.h"

namespace dart {
namespace compiler {

#define __ assembler->

void AsmIntrinsifier::Intrinsify(Assembler* assembler, const Function& function) {
  // Intrinsics not yet implemented for LOONG64.
}

}  // namespace compiler
}  // namespace dart

#endif  // defined TARGET_ARCH_LOONG64