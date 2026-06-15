// Copyright (c) 2021, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(TARGET_ARCH_LOONG64)

#include "vm/compiler/backend/flow_graph_compiler.h"

#include "vm/compiler/api/type_check_mode.h"
#include "vm/compiler/backend/il_printer.h"
#include "vm/compiler/backend/locations.h"
#include "vm/compiler/backend/parallel_move_resolver.h"
#include "vm/compiler/jit/compiler.h"
#include "vm/cpu.h"
#include "vm/dart_entry.h"
#include "vm/deopt_instructions.h"
#include "vm/dispatch_table.h"
#include "vm/instructions.h"
#include "vm/object_store.h"
#include "vm/parser.h"
#include "vm/stack_frame.h"
#include "vm/stub_code.h"
#include "vm/symbols.h"

namespace dart {

DEFINE_FLAG(bool, trap_on_deoptimization, false, "Trap on deoptimization.");
DECLARE_FLAG(bool, enable_simd_inline);

void FlowGraphCompiler::ArchSpecificInitialization() {
}

FlowGraphCompiler::~FlowGraphCompiler() {
  for (int i = 0; i < block_info_.length(); ++i) {
    ASSERT(!block_info_[i]->jump_label()->IsLinked());
  }
}

bool FlowGraphCompiler::SupportsUnboxedSimd128() {
  return false;
}

void FlowGraphCompiler::EnterDartFrame(intptr_t frame_size) {
  UNIMPLEMENTED();
}

void FlowGraphCompiler::LeaveDartFrame() {
  UNIMPLEMENTED();
}

void FlowGraphCompiler::EnterCFrame() {
  UNIMPLEMENTED();
}

void FlowGraphCompiler::LeaveCFrame() {
  UNIMPLEMENTED();
}

void FlowGraphCompiler::EmitFrameEntry() {
  UNIMPLEMENTED();
}

void FlowGraphCompiler::EmitMaterializeCode(const MaterializeObjectInstr* mat) {
  UNIMPLEMENTED();
}

void FlowGraphCompiler::EmitMove(MoveArgument argument,
                                  const Location& source,
                                  const Location& dest) {
  UNIMPLEMENTED();
}

void FlowGraphCompiler::EmitNativeMove(MoveArgument argument,
                                        const Location& source,
                                        const Location& dest) {
  UNIMPLEMENTED();
}

void FlowGraphCompiler::EmitParallelMoves(const MoveResolver& resolver) {
  UNIMPLEMENTED();
}

Location FlowGraphCompiler::RebaseIfImprovesAddressing(Location loc) const {
  return loc;
}

}  // namespace dart

#endif  // defined TARGET_ARCH_LOONG64