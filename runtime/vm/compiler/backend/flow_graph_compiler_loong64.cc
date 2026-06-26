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

bool FlowGraphCompiler::CanConvertInt64ToDouble() {
  return true;
}

void FlowGraphCompiler::EnterIntrinsicMode() {
  ASSERT(!intrinsic_mode());
  intrinsic_mode_ = true;
  ASSERT(!assembler()->constant_pool_allowed());
}

void FlowGraphCompiler::ExitIntrinsicMode() {
  ASSERT(intrinsic_mode());
  intrinsic_mode_ = false;
}

#define __ assembler->
void FlowGraphCompiler::GenerateIndirectTTSCall(compiler::Assembler* assembler,
                                                Register reg_to_call,
                                                intptr_t sub_type_cache_index) {
  // Type testing stub indirect call.
  __ jirl(RA, reg_to_call, 0);
}
#undef __
#define __ assembler()->
void FlowGraphCompiler::GenerateBoolToJump(Register bool_register,
                                           compiler::Label* is_true,
                                           compiler::Label* is_false) {
  compiler::Label fall_through;
  __ beq(bool_register, NULL_REG, &fall_through,
         compiler::Assembler::kNearJump);
  BranchLabels labels = {is_true, is_false, &fall_through};
  Condition true_condition =
      EmitBoolTest(bool_register, labels, /*invert=*/false);
  ASSERT(true_condition != kInvalidCondition);
  __ BranchIf(true_condition, is_true);
  __ b(is_false);
  __ Bind(&fall_through);
}

void FlowGraphCompiler::EmitFrameEntry() {
  const Function& function = parsed_function().function();
  if (CanOptimizeFunction() && function.IsOptimizable() &&
      (!is_optimizing() || may_reoptimize())) {
    __ Comment("Invocation Count Check");
    const Register function_reg = A0;
    const Register usage_reg = A1;
    // Load Function from the Code object via CODE_REG (which is already
    // set by the caller). We cannot use LoadObject here because PP has
    // not been set up yet (EnterDartFrame/LoadPoolPointer runs later).
    __ LoadFieldFromOffset(function_reg, CODE_REG,
                           Code::owner_offset());
    __ LoadFieldFromOffset(usage_reg, function_reg,
                           Function::usage_counter_offset(),
                           compiler::kFourBytes);
    if (!is_optimizing()) {
      __ addi_d(usage_reg, usage_reg, 1);
      __ StoreFieldToOffset(usage_reg, function_reg,
                            Function::usage_counter_offset(),
                            compiler::kFourBytes);
    }
    __ CompareImmediate(usage_reg, GetOptimizationThreshold());
    compiler::Label not_optimized;
    __ BranchIf(LESS, &not_optimized);
    __ jirl(ZR, RA, 0);  // Return to allow lazy compilation.
    __ Bind(&not_optimized);
  }
  __ Comment("Enter frame");
  if (flow_graph().IsCompiledForOsr()) {
    const intptr_t extra_size = StackSize();
    __ EnterOsrFrame(extra_size, kNoRegister);
  } else {
    __ EnterDartFrame(StackSize());
  }
  if (FLAG_target_thread_sanitizer && !is_optimizing()) {
    bool uses_args_desc = parsed_function().has_arg_desc_var();
    if (uses_args_desc) {
      __ MoveRegister(CALLEE_SAVED_TEMP, ARGS_DESC_REG);
    }
    __ TsanFuncEntry(/*preserve_registers=*/false);
    if (uses_args_desc) {
      __ MoveRegister(ARGS_DESC_REG, CALLEE_SAVED_TEMP);
    }
  }
}

const InstructionSource& PrologueSource() {
  static InstructionSource prologue_source(TokenPosition::kDartCodePrologue,
                                           /*inlining_id=*/0);
  return prologue_source;
}

void FlowGraphCompiler::EmitPrologue() {
  BeginCodeSourceRange(PrologueSource());

  EmitFrameEntry();
  ASSERT(assembler()->constant_pool_allowed());

  // In unoptimized code, initialize (non-argument) stack allocated slots.
  if (!is_optimizing()) {
    const int num_locals = parsed_function().num_stack_locals();
    intptr_t args_desc_slot = -1;
    if (parsed_function().has_arg_desc_var()) {
      args_desc_slot = compiler::target::frame_layout.FrameSlotForVariable(
          parsed_function().arg_desc_var());
    }

    __ Comment("Initialize spill slots");
    const intptr_t fp_to_sp_delta =
        StackSize() + compiler::target::frame_layout.dart_fixed_frame_size;
    USE(fp_to_sp_delta);
    for (intptr_t i = 0; i < num_locals; ++i) {
      const intptr_t slot_index =
          compiler::target::frame_layout.FrameSlotForVariableIndex(-i - 1);
      if (slot_index == args_desc_slot) continue;
      __ st_d(NULL_REG, SP, slot_index * compiler::target::kWordSize);
    }
  }

  EndCodeSourceRange(PrologueSource());
}

void FlowGraphCompiler::EmitCallToStub(
    const Code& stub,
    ObjectPool::SnapshotBehavior snapshot_behavior) {
  ASSERT(!stub.IsNull());
  if (CanPcRelativeCall(stub)) {
    __ GenerateUnRelocatedPcRelativeCall();
    AddPcRelativeCallStubTarget(stub);
  } else {
    __ BranchLink(stub, compiler::ObjectPoolBuilderEntry::kNotPatchable,
                  CodeEntryKind::kNormal, snapshot_behavior);
    AddStubCallTarget(stub);
  }
}

void FlowGraphCompiler::EmitJumpToStub(const Code& stub) {
  ASSERT(!stub.IsNull());
  __ LoadObject(CODE_REG, stub);
  __ Load(TMP, compiler::FieldAddress(
                 CODE_REG, compiler::target::Code::entry_point_offset()));
  __ jr(TMP);
  AddStubCallTarget(stub);
}

void FlowGraphCompiler::EmitTailCallToStub(const Code& stub) {
  ASSERT(!stub.IsNull());
  if (flow_graph().graph_entry()->NeedsFrame()) {
    if (FLAG_target_thread_sanitizer && !is_optimizing()) {
      __ TsanFuncExit();
    }
    __ LeaveDartFrame();
  }
  __ LoadObject(CODE_REG, stub);
  __ Load(TMP, compiler::FieldAddress(
                 CODE_REG, compiler::target::Code::entry_point_offset()));
  __ jr(TMP);
  AddStubCallTarget(stub);
}

void FlowGraphCompiler::GeneratePatchableCall(
    const InstructionSource& source,
    const Code& stub,
    UntaggedPcDescriptors::Kind kind,
    LocationSummary* locs,
    ObjectPool::SnapshotBehavior snapshot_behavior) {
  __ LoadObject(CODE_REG, stub);
  __ Call(compiler::FieldAddress(CODE_REG, compiler::target::Code::entry_point_offset()));
  EmitCallsiteMetadata(source, DeoptId::kNone, kind, locs,
                       pending_deoptimization_env_);
}

void FlowGraphCompiler::GenerateDartCall(intptr_t deopt_id,
                                         const InstructionSource& source,
                                         const Code& stub,
                                         UntaggedPcDescriptors::Kind kind,
                                         LocationSummary* locs,
                                         Code::EntryKind entry_kind) {
  ASSERT(CanCallDart());
  __ LoadObject(CODE_REG, stub);
  __ Call(compiler::FieldAddress(CODE_REG, compiler::target::Code::entry_point_offset(entry_kind)));
  EmitCallsiteMetadata(source, deopt_id, kind, locs,
                       pending_deoptimization_env_);
}

void FlowGraphCompiler::GenerateStaticDartCall(intptr_t deopt_id,
                                               const InstructionSource& source,
                                               UntaggedPcDescriptors::Kind kind,
                                               LocationSummary* locs,
                                               const Function& target,
                                               Code::EntryKind entry_kind) {
  ASSERT(CanCallDart());
  __ LoadObject(CODE_REG, target);
  __ Call(compiler::FieldAddress(CODE_REG, compiler::target::Code::entry_point_offset(entry_kind)));
  EmitCallsiteMetadata(source, deopt_id, kind, locs,
                       pending_deoptimization_env_);
}

void FlowGraphCompiler::EmitEdgeCounter(intptr_t edge_id) {
  // Edge counters are stored in the Array held in the Function.
  // Not implemented for LoongArch yet - no-op for now.
}

void FlowGraphCompiler::EmitOptimizedInstanceCall(
    const Code& stub,
    const ICData& ic_data,
    intptr_t deopt_id,
    const InstructionSource& source,
    LocationSummary* locs,
    Code::EntryKind entry_kind) {
  ASSERT(CanCallDart());
  __ LoadObject(CODE_REG, stub);
  __ Call(compiler::FieldAddress(CODE_REG, compiler::target::Code::entry_point_offset(entry_kind)));
  EmitCallsiteMetadata(source, deopt_id,
                       UntaggedPcDescriptors::kIcCall, locs,
                       pending_deoptimization_env_);
}

void FlowGraphCompiler::EmitInstanceCallJIT(const Code& stub,
                                            const ICData& ic_data,
                                            intptr_t deopt_id,
                                            const InstructionSource& source,
                                            LocationSummary* locs,
                                            Code::EntryKind entry_kind) {
  ASSERT(CanCallDart());
  __ LoadObject(CODE_REG, stub);
  __ LoadObject(IC_DATA_REG, ic_data);
  __ Call(compiler::FieldAddress(CODE_REG, compiler::target::Code::entry_point_offset(entry_kind)));
  EmitCallsiteMetadata(source, deopt_id,
                       UntaggedPcDescriptors::kIcCall, locs,
                       pending_deoptimization_env_);
}

void FlowGraphCompiler::EmitMegamorphicInstanceCall(
    const String& name,
    const Array& arguments_descriptor,
    intptr_t deopt_id,
    const InstructionSource& source,
    LocationSummary* locs) {
  ASSERT(CanCallDart());
  // Call through the megamorphic cache.
  ASSERT(!FLAG_precompiled_mode);
  const intptr_t num_args =
      ArgumentsDescriptor(arguments_descriptor).Count();
  __ ld_d(A0, SP, (num_args - 1) * compiler::target::kWordSize);
  __ LoadUniqueObject(CODE_REG, StubCode::MegamorphicCall());
  __ Call(compiler::FieldAddress(
      CODE_REG, Code::entry_point_offset(Code::EntryKind::kMonomorphic)));
  EmitCallsiteMetadata(source, deopt_id,
                       UntaggedPcDescriptors::kOther, locs,
                       pending_deoptimization_env_);
}

void FlowGraphCompiler::EmitInstanceCallAOT(const ICData& ic_data,
                                            intptr_t deopt_id,
                                            const InstructionSource& source,
                                            LocationSummary* locs,
                                            Code::EntryKind entry_kind,
                                            bool receiver_can_be_smi) {
  ASSERT(CanCallDart());
  ASSERT(ic_data.NumArgsTested() == 1);
  const Code& initial_stub = StubCode::SwitchableCallMiss();
  const char* switchable_call_mode = "smiable";
  if (!receiver_can_be_smi) {
    switchable_call_mode = "non-smi";
    ic_data.set_receiver_cannot_be_smi(true);
  }
  const UnlinkedCall& data =
      UnlinkedCall::ZoneHandle(zone(), ic_data.AsUnlinkedCall());

  __ Comment("InstanceCallAOT (%s)", switchable_call_mode);
  __ LoadImmediate(ARGS_DESC_REG, 0);
  __ LoadFromOffset(A0, SP,
                    (ic_data.SizeWithoutTypeArgs() - 1) * kWordSize);
  const auto snapshot_behavior =
      compiler::ObjectPoolBuilderEntry::kResetToSwitchableCallMissEntryPoint;
  __ LoadUniqueObject(RA, initial_stub, snapshot_behavior);
  __ LoadUniqueObject(IC_DATA_REG, data);
  __ jirl(RA, RA, 0);

  EmitCallsiteMetadata(source, deopt_id,
                       UntaggedPcDescriptors::kOther, locs,
                       pending_deoptimization_env_);
  EmitDropArguments(ic_data.SizeWithTypeArgs());
}

void FlowGraphCompiler::EmitUnoptimizedStaticCall(
    intptr_t size_with_type_args,
    intptr_t deopt_id,
    const InstructionSource& source,
    LocationSummary* locs,
    const ICData& ic_data,
    Code::EntryKind entry_kind) {
  ASSERT(CanCallDart());
  __ LoadObject(CODE_REG, ic_data);
  __ Call(compiler::FieldAddress(CODE_REG, compiler::target::Code::entry_point_offset(entry_kind)));
  EmitCallsiteMetadata(source, deopt_id,
                       UntaggedPcDescriptors::kOther, locs,
                       pending_deoptimization_env_);
}

void FlowGraphCompiler::EmitOptimizedStaticCall(
    const Function& function,
    const Array& arguments_descriptor,
    intptr_t size_with_type_args,
    intptr_t deopt_id,
    const InstructionSource& source,
    LocationSummary* locs,
    Code::EntryKind entry_kind) {
  ASSERT(CanCallDart());
  __ LoadObject(CODE_REG, function);
  __ Call(compiler::FieldAddress(CODE_REG, compiler::target::Code::entry_point_offset(entry_kind)));
  EmitCallsiteMetadata(source, deopt_id,
                       UntaggedPcDescriptors::kOther, locs,
                       pending_deoptimization_env_);
}

void FlowGraphCompiler::EmitDispatchTableCall(
    int32_t selector_offset,
    const Array& arguments_descriptor) {
  const auto cid_reg = DispatchTableNullErrorABI::kClassIdReg;
  ASSERT(CanCallDart());
  ASSERT(cid_reg != ARGS_DESC_REG);
  if (!arguments_descriptor.IsNull()) {
    __ LoadObject(ARGS_DESC_REG, arguments_descriptor);
  }
  const uintptr_t offset = selector_offset - DispatchTable::kOriginElement;
  ASSERT(cid_reg != TMP);
  __ slli_d(TMP2, cid_reg, compiler::target::kWordSizeLog2);
  __ add_d(TMP, TMP2, DISPATCH_TABLE_REG);
  __ ld_d(TMP, TMP, offset << compiler::target::kWordSizeLog2);
  __ jirl(RA, TMP, 0);
}

void FlowGraphCompiler::SaveLiveRegisters(LocationSummary* locs) {
  const RegisterSet& regs = *locs->live_registers();
  if (regs.IsEmpty()) return;
  __ PushRegisters(regs);
}

void FlowGraphCompiler::RestoreLiveRegisters(LocationSummary* locs) {
  const RegisterSet& regs = *locs->live_registers();
  if (regs.IsEmpty()) return;
  __ PopRegisters(regs);
}

#if defined(DEBUG)
void FlowGraphCompiler::ClobberDeadTempRegisters(LocationSummary* locs) {
  // No clobbering needed in stub.
}
#endif  // defined(DEBUG)

Register FlowGraphCompiler::EmitTestCidRegister() {
  return A1;
}

void FlowGraphCompiler::EmitTestAndCallLoadReceiver(
    intptr_t count_without_type_args,
    const Array& arguments_descriptor) {
  // Load the receiver from the stack into A0.
  __ LoadFromStack(A0, (count_without_type_args - 1) * compiler::target::kWordSize);
  __ addi_d(ARGS_DESC_REG, ZR, 0);
}

void FlowGraphCompiler::EmitTestAndCallSmiBranch(compiler::Label* label,
                                                 bool if_smi) {
  __ andi(TMP2, A0, kSmiTagMask);
  if (if_smi) {
    __ beqz(TMP2, label);
  } else {
    __ bnez(TMP2, label);
  }
}

void FlowGraphCompiler::EmitTestAndCallLoadCid(Register class_id_reg) {
  __ LoadClassId(class_id_reg, A0);
}

Location FlowGraphCompiler::RebaseIfImprovesAddressing(Location loc) const {
  return loc;
}

void FlowGraphCompiler::EmitMove(Location destination,
                                 Location source,
                                 TemporaryRegisterAllocator* allocator) {
  if (destination.Equals(source)) return;

  if (source.IsRegister()) {
    if (destination.IsRegister()) {
      __ MoveRegister(destination.reg(), source.reg());
    } else {
      ASSERT(destination.IsStackSlot());
      const intptr_t dest_offset = destination.ToStackSlotOffset();
      __ st_d(source.reg(), destination.base_reg(), dest_offset);
    }
  } else if (source.IsStackSlot()) {
    if (destination.IsRegister()) {
      const intptr_t source_offset = source.ToStackSlotOffset();
      __ ld_d(destination.reg(), source.base_reg(), source_offset);
    } else if (destination.IsFpuRegister()) {
      const intptr_t src_offset = source.ToStackSlotOffset();
      FRegister dst = destination.fpu_reg();
      __ LoadDFromOffset(dst, source.base_reg(), src_offset);
    } else {
      ASSERT(destination.IsStackSlot());
      const intptr_t source_offset = source.ToStackSlotOffset();
      const intptr_t dest_offset = destination.ToStackSlotOffset();
      Register tmp = allocator->AllocateTemporary();
      __ ld_d(tmp, source.base_reg(), source_offset);
      __ st_d(tmp, destination.base_reg(), dest_offset);
      allocator->ReleaseTemporary();
    }
  } else if (source.IsFpuRegister()) {
    if (destination.IsFpuRegister()) {
      __ fmv_d(destination.fpu_reg(), source.fpu_reg());
    } else {
      if (destination.IsStackSlot() /*32-bit float*/ ||
          destination.IsDoubleStackSlot()) {
        const intptr_t dest_offset = destination.ToStackSlotOffset();
        FRegister src = source.fpu_reg();
        __ StoreDToOffset(src, destination.base_reg(), dest_offset);
      } else {
        ASSERT(destination.IsQuadStackSlot());
        // LA64 does not have 128-bit FPRs (kFpuRegisterSize = 8); treat the
        // 64-bit FPR value as a double and store it into the quad slot.
        const intptr_t dest_offset = destination.ToStackSlotOffset();
        __ StoreDToOffset(source.fpu_reg(), destination.base_reg(),
                          dest_offset);
      }
    }
  } else if (source.IsDoubleStackSlot()) {
    if (destination.IsFpuRegister()) {
      const intptr_t source_offset = source.ToStackSlotOffset();
      const FRegister dst = destination.fpu_reg();
      __ LoadDFromOffset(dst, source.base_reg(), source_offset);
    } else {
      ASSERT(destination.IsDoubleStackSlot() ||
             destination.IsStackSlot() /*32-bit float*/);
      const intptr_t source_offset = source.ToStackSlotOffset();
      const intptr_t dest_offset = destination.ToStackSlotOffset();
      __ LoadDFromOffset(FTMP, source.base_reg(), source_offset);
      __ StoreDToOffset(FTMP, destination.base_reg(), dest_offset);
    }
  } else if (source.IsQuadStackSlot()) {
    if (destination.IsFpuRegister()) {
      const intptr_t source_offset = source.ToStackSlotOffset();
      __ LoadDFromOffset(destination.fpu_reg(), source.base_reg(),
                         source_offset);
    } else {
      ASSERT(destination.IsQuadStackSlot());
      const intptr_t source_offset = source.ToStackSlotOffset();
      const intptr_t dest_offset = destination.ToStackSlotOffset();
      __ LoadDFromOffset(FTMP, source.base_reg(), source_offset);
      __ StoreDToOffset(FTMP, destination.base_reg(), dest_offset);
    }
  } else if (source.IsConstant()) {
    source.constant_instruction()->EmitMoveToLocation(this, destination, TMP,
                                                      source.pair_index());
  } else if (source.IsPairLocation()) {
    // Decompose pair location into component moves (like RISC-V handles
    // double stack slots).
    if (destination.IsPairLocation()) {
      for (intptr_t i = 0; i < 2; i++) {
        EmitMove(destination.Component(i), source.Component(i), allocator);
      }
    } else {
      for (intptr_t i = 0; i < 2; i++) {
        EmitMove(destination, source.Component(i), allocator);
      }
    }
  } else if (source.IsUnallocated() &&
             source.policy() == Location::kPrefersRegister) {
    // Workaround: the register allocator failed to resolve a phi input
    // with kPrefersRegister policy. This happens when the allocator's
    // hint matching succeeds (the input value is already in the same
    // location as the phi output) but the use slot is not written back.
    // Since the value should already be in the destination location,
    // the move is a no-op.
    // TODO(loong64): Fix root cause in the linear scan allocator.
  } else {
    // Should never be reached - the register allocator should have resolved
    // all locations before code generation.
    UNREACHABLE();
  }
}
static compiler::OperandSize BytesToOperandSize(intptr_t bytes) {
  switch (bytes) {
    case 1: return compiler::kByte;
    case 2: return compiler::kTwoBytes;
    case 4: return compiler::kFourBytes;
    case 8: return compiler::kEightBytes;
    default: UNREACHABLE(); return compiler::kByte;
  }
}

void FlowGraphCompiler::EmitNativeMoveArchitecture(
    const compiler::ffi::NativeLocation& destination,
    const compiler::ffi::NativeLocation& source) {
  const auto& src_payload_type = source.payload_type();
  const auto& dst_payload_type = destination.payload_type();
  const auto& src_container_type = source.container_type();
  const auto& dst_container_type = destination.container_type();
  ASSERT(src_container_type.IsFloat() == dst_container_type.IsFloat());
  ASSERT(src_container_type.IsInt() == dst_container_type.IsInt());
  ASSERT(src_payload_type.IsSigned() == dst_payload_type.IsSigned());
  ASSERT(src_payload_type.IsPrimitive());
  ASSERT(dst_payload_type.IsPrimitive());
  const intptr_t src_size = src_payload_type.SizeInBytes();
  const intptr_t dst_size = dst_payload_type.SizeInBytes();
  const bool sign_or_zero_extend = dst_size > src_size;

  if (source.IsRegisters()) {
    const auto& src = source.AsRegisters();
    ASSERT(src.num_regs() == 1);
    const auto src_reg = src.reg_at(0);

    if (destination.IsRegisters()) {
      const auto& dst = destination.AsRegisters();
      ASSERT(dst.num_regs() == 1);
      const auto dst_reg = dst.reg_at(0);
      ASSERT(destination.container_type().SizeInBytes() <=
             compiler::target::kWordSize);
      if (!sign_or_zero_extend) {
        if (src_size <= 4) {
          // Sign-extend to 64 bits, even for unsigned types.
          __ slli_w(dst_reg, src_reg, 0);
        } else {
          __ MoveRegister(dst_reg, src_reg);
        }
      } else {
        switch (src_payload_type.AsPrimitive().representation()) {
          case compiler::ffi::kInt8:
          case compiler::ffi::kInt16:
            __ slli_d(dst_reg, src_reg, 64 - src_size * kBitsPerByte);
            __ srai_d(dst_reg, dst_reg, 64 - src_size * kBitsPerByte);
            return;
          case compiler::ffi::kUint8:
            __ andi(dst_reg, src_reg, 0xFF);
            return;
          case compiler::ffi::kUint16:
            __ slli_d(dst_reg, src_reg, 64 - 16);
            __ srli_d(dst_reg, dst_reg, 64 - 16);
            return;
          case compiler::ffi::kUint32:
          case compiler::ffi::kInt32:
            __ slli_w(dst_reg, src_reg, 0);
            return;
          default:
            if (src_payload_type.IsSigned()) {
              __ slli_d(dst_reg, src_reg, 64 - src_size * kBitsPerByte);
              __ srai_d(dst_reg, dst_reg, 64 - src_size * kBitsPerByte);
            } else {
              __ slli_d(dst_reg, src_reg, 64 - src_size * kBitsPerByte);
              __ srli_d(dst_reg, dst_reg, 64 - src_size * kBitsPerByte);
            }
            return;
        }
      }

    } else if (destination.IsFpuRegisters()) {
      const auto& dst = destination.AsFpuRegisters();
      ASSERT(src_size == dst_size);
      ASSERT(src.num_regs() == 1);
      switch (src_size) {
        case 4:
          __ movgr2fr_w(dst.fpu_reg(), src.reg_at(0));
          return;
        case 8:
          __ movgr2fr_d(dst.fpu_reg(), src.reg_at(0));
          return;
        default:
          UNREACHABLE();
      }

    } else {
      ASSERT(destination.IsStack());
      const auto& dst = destination.AsStack();
      ASSERT(!sign_or_zero_extend);
      auto const op_size =
          BytesToOperandSize(destination.container_type().SizeInBytes());
      __ StoreToOffset(src.reg_at(0), dst.base_register(),
                       dst.offset_in_bytes(), op_size);
    }

  } else if (source.IsFpuRegisters()) {
    const auto& src = source.AsFpuRegisters();
    ASSERT(src_payload_type.Equals(dst_payload_type));

    if (destination.IsRegisters()) {
      const auto& dst = destination.AsRegisters();
      ASSERT(src_size == dst_size);
      ASSERT(dst.num_regs() == 1);
      switch (src_size) {
        case 4:
          __ movfr2gr_s(dst.reg_at(0), src.fpu_reg());
          return;
        case 8:
          __ movfr2gr_d(dst.reg_at(0), src.fpu_reg());
          return;
        default:
          UNREACHABLE();
      }

    } else if (destination.IsFpuRegisters()) {
      const auto& dst = destination.AsFpuRegisters();
      __ fmv_d(dst.fpu_reg(), src.fpu_reg());

    } else {
      ASSERT(destination.IsStack());
      ASSERT(src_payload_type.IsFloat());
      const auto& dst = destination.AsStack();
      switch (dst_size) {
        case 8:
          __ StoreDToOffset(src.fpu_reg(), dst.base_register(),
                            dst.offset_in_bytes());
          return;
        case 4:
          __ StoreSToOffset(src.fpu_reg(), dst.base_register(),
                            dst.offset_in_bytes());
          return;
        default:
          UNREACHABLE();
      }
    }

  } else {
    ASSERT(source.IsStack());
    const auto& src = source.AsStack();
    if (destination.IsRegisters()) {
      const auto& dst = destination.AsRegisters();
      ASSERT(dst.num_regs() == 1);
      const auto dst_reg = dst.reg_at(0);
      EmitNativeLoad(dst_reg, src.base_register(), src.offset_in_bytes(),
                     src_payload_type.AsPrimitive().representation());
    } else if (destination.IsFpuRegisters()) {
      ASSERT(src_payload_type.Equals(dst_payload_type));
      ASSERT(src_payload_type.IsFloat());
      const auto& dst = destination.AsFpuRegisters();
      switch (src_size) {
        case 8:
          __ LoadDFromOffset(dst.fpu_reg(), src.base_register(),
                             src.offset_in_bytes());
          return;
        case 4:
          __ LoadSFromOffset(dst.fpu_reg(), src.base_register(),
                             src.offset_in_bytes());
          return;
        default:
          UNIMPLEMENTED();
      }
    } else {
      ASSERT(destination.IsStack());
      UNREACHABLE();
    }
  }
}

void FlowGraphCompiler::EmitNativeLoad(Register dst,
                                       Register base,
                                       intptr_t offset,
                                       compiler::ffi::PrimitiveType type) {
  switch (type) {
    case compiler::ffi::kInt8:
      __ ld_b(dst, base, offset);
      return;
    case compiler::ffi::kUint8:
      __ ld_bu(dst, base, offset);
      return;
    case compiler::ffi::kInt16:
      __ ld_h(dst, base, offset);
      return;
    case compiler::ffi::kUint16:
      __ ld_hu(dst, base, offset);
      return;
    case compiler::ffi::kInt32:
      __ ld_w(dst, base, offset);
      return;
    case compiler::ffi::kUint32:
      __ ld_wu(dst, base, offset);
      return;
    case compiler::ffi::kInt64:
    case compiler::ffi::kUint64:
      __ ld_d(dst, base, offset);
      return;
    default:
      UNREACHABLE();
  }
}


TypedDataPtr CompilerDeoptInfo::CreateDeoptInfo(FlowGraphCompiler* compiler,
                                                DeoptInfoBuilder* builder,
                                                const Array& deopt_table) {
  if (deopt_env_ == nullptr) {
    ++builder->current_info_number_;
    return TypedData::null();
  }
  AllocateOutgoingArguments(deopt_env_);
  intptr_t slot_ix = 0;
  Environment* current = deopt_env_;
  EmitMaterializations(deopt_env_, builder);
  builder->MarkFrameStart();
  Zone* zone = compiler->zone();
  builder->AddPp(current->function(), slot_ix++);
  builder->AddPcMarker(Function::ZoneHandle(zone), slot_ix++);
  builder->AddCallerFp(slot_ix++);
  builder->AddReturnAddress(current->function(), deopt_id(), slot_ix++);
  slot_ix = builder->EmitMaterializationArguments(slot_ix);
  for (intptr_t i = current->Length() - 1;
       i >= current->fixed_parameter_count(); i--) {
    builder->AddCopy(current->ValueAt(i), current->LocationAt(i), slot_ix++);
  }
  Environment* previous = current;
  current = current->outer();
  while (current != nullptr) {
    builder->AddPp(current->function(), slot_ix++);
    builder->AddPcMarker(previous->function(), slot_ix++);
    builder->AddCallerFp(slot_ix++);
    builder->AddReturnAddress(current->function(),
                              DeoptId::ToDeoptAfter(current->GetDeoptId()),
                              slot_ix++);
    for (intptr_t i = previous->fixed_parameter_count() - 1; i >= 0; i--) {
      builder->AddCopy(previous->ValueAt(i), previous->LocationAt(i), slot_ix++);
    }
    for (intptr_t i = current->Length() - 1;
         i >= current->fixed_parameter_count(); i--) {
      builder->AddCopy(current->ValueAt(i), current->LocationAt(i), slot_ix++);
    }
    previous = current;
    current = current->outer();
  }
  ASSERT(previous != nullptr);
  builder->AddCallerPp(slot_ix++);
  builder->AddPcMarker(previous->function(), slot_ix++);
  builder->AddCallerFp(slot_ix++);
  builder->AddCallerPc(slot_ix++);
  for (intptr_t i = previous->fixed_parameter_count() - 1; i >= 0; i--) {
    builder->AddCopy(previous->ValueAt(i), previous->LocationAt(i), slot_ix++);
  }
  return builder->CreateDeoptInfo(deopt_table);
}

void CompilerDeoptInfoWithStub::GenerateCode(FlowGraphCompiler* compiler,
                                             intptr_t stub_ix) {
  ASSERT(reason() != ICData::kDeoptAtCall);
  compiler::Assembler* assembler = compiler->assembler();
#undef __
#define __ assembler->
  __ Comment("%s", Name());
  __ Bind(entry_label());
  if (FLAG_trap_on_deoptimization) {
    __ trap();
  }
  ASSERT(deopt_env() != nullptr);
  __ ld_d(TMP, THR, Thread::deoptimize_entry_offset());
  __ jr(TMP);
  set_pc_offset(assembler->CodeSize());
#undef __
}

#define __ assembler()->

Condition FlowGraphCompiler::EmitEqualityRegConstCompare(
    Register reg, const Object& obj, bool needs_number_check,
    const InstructionSource& source, intptr_t deopt_id) {
  if (needs_number_check) {
    ASSERT(!obj.IsMint() && !obj.IsDouble());
    __ LoadObject(TMP, obj);
    __ PushRegisterPair(TMP, reg);
    if (is_optimizing()) {
      __ BranchLink(StubCode::OptimizedIdenticalWithNumberCheck());
      AddCurrentDescriptor(UntaggedPcDescriptors::kOther, deopt_id, source);
    } else {
      __ BranchLinkPatchable(StubCode::UnoptimizedIdenticalWithNumberCheck());
      AddCurrentDescriptor(UntaggedPcDescriptors::kRuntimeCall, deopt_id, source);
    }
    __ PopRegisterPair(ZR, reg);
    // LoongArch has no condition flags, so the result is instead returned as
    // TMP zero if equal, non-zero if non-equal.
    ASSERT(reg != TMP);
    __ CompareImmediate(TMP, 0);
  } else {
    __ CompareObject(reg, obj);
  }
  return EQ;
}

Condition FlowGraphCompiler::EmitEqualityRegRegCompare(
    Register left, Register right, bool needs_number_check,
    const InstructionSource& source, intptr_t deopt_id) {
  if (needs_number_check) {
    __ PushRegisterPair(right, left);
    if (is_optimizing()) {
      __ BranchLink(StubCode::OptimizedIdenticalWithNumberCheck());
    } else {
      __ BranchLinkPatchable(StubCode::UnoptimizedIdenticalWithNumberCheck());
    }
    AddCurrentDescriptor(UntaggedPcDescriptors::kRuntimeCall, deopt_id, source);
    __ PopRegisterPair(right, left);
    // LoongArch has no condition flags, so the result is instead returned as
    // TMP zero if equal, non-zero if non-equal.
    ASSERT(left != TMP);
    ASSERT(right != TMP);
    __ CompareImmediate(TMP, 0);
  } else {
    __ CompareObjectRegisters(left, right);
  }
  return EQ;
}

Condition FlowGraphCompiler::EmitBoolTest(Register value,
                                          BranchLabels labels,
                                          bool invert) {
  __ Comment("BoolTest");
  __ TestImmediate(value, compiler::target::ObjectAlignment::kBoolValueMask);
  return invert ? NE : EQ;
}
#undef __
#define __ compiler_->assembler()->

void ParallelMoveEmitter::EmitSwap(const MoveOperands& move) {
  const Location src = move.src();
  const Location dst = move.dest();
  ASSERT(dst.IsRegister() && src.IsRegister());
  ASSERT(dst.reg() != src.reg());
  // Swap using TMP scratch.
  __ Move(TMP, src.reg());
  __ Move(src.reg(), dst.reg());
  __ Move(dst.reg(), TMP);
}

void ParallelMoveEmitter::MoveMemoryToMemory(const compiler::Address& dst,
                                             const compiler::Address& src) {
  UNREACHABLE();
}

void ParallelMoveEmitter::Exchange(Register reg, const compiler::Address& mem) {
  UNREACHABLE();
}

void ParallelMoveEmitter::Exchange(const compiler::Address& mem1,
                                   const compiler::Address& mem2) {
  UNREACHABLE();
}

void ParallelMoveEmitter::Exchange(Register reg,
                                   Register base_reg,
                                   intptr_t stack_offset) {
  // Exchange reg with memory at [base_reg + stack_offset].
  __ ld_d(TMP, base_reg, stack_offset);
  __ st_d(reg, base_reg, stack_offset);
  __ Move(reg, TMP);
}

void ParallelMoveEmitter::Exchange(Register base_reg1,
                                   intptr_t stack_offset1,
                                   Register base_reg2,
                                   intptr_t stack_offset2) {
  __ ld_d(TMP, base_reg1, stack_offset1);
  __ ld_d(TMP2, base_reg2, stack_offset2);
  __ st_d(TMP2, base_reg1, stack_offset1);
  __ st_d(TMP, base_reg2, stack_offset2);
}

void ParallelMoveEmitter::SpillScratch(Register reg) {
  __ PushRegister(reg);
}

void ParallelMoveEmitter::RestoreScratch(Register reg) {
  __ PopRegister(reg);
}

void ParallelMoveEmitter::SpillFpuScratch(FpuRegister reg) {
  __ addi_d(SP, SP, -kFpuRegisterSize);
  __ fst_d(reg, SP, 0);
}

void ParallelMoveEmitter::RestoreFpuScratch(FpuRegister reg) {
  __ fld_d(reg, SP, 0);
  __ addi_d(SP, SP, kFpuRegisterSize);
}

#undef __

}  // namespace dart

#endif  // defined TARGET_ARCH_LOONG64
