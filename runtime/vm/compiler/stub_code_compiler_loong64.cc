// Copyright (c) 2021, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(TARGET_ARCH_LOONG64)

#include "vm/compiler/assembler/assembler.h"
#include "vm/compiler/stub_code_compiler.h"

namespace dart {
namespace compiler {

#define __ assembler->

void StubCodeCompiler::Generate_UnknownJITDispatch(Assembler* assembler) {
  UNIMPLEMENTED();
}

void StubCodeCompiler::Generate_UnknownCall(Assembler* assembler) {
  UNIMPLEMENTED();
}

void StubCodeCompiler::Generate_WriteBarrierWrappers(Assembler* assembler) {
  UNIMPLEMENTED();
}

void StubCodeCompiler::Generate_ArrayWriteBarrier(Assembler* assembler) {
  UNIMPLEMENTED();
}

void StubCodeCompiler::Generate_Move(Assembler* assembler) {
  UNIMPLEMENTED();
}

}  // namespace compiler
}  // namespace dart

#endif  // defined TARGET_ARCH_LOONG64