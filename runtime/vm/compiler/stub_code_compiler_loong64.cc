// Copyright (c) 2021, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"

// For `AllocateObjectInstr::WillAllocateNewOrRemembered`
// For `GenericCheckBoundInstr::UseUnboxedRepresentation`
#include "vm/compiler/backend/il.h"

#define SHOULD_NOT_INCLUDE_RUNTIME

#include "vm/compiler/stub_code_compiler.h"

#if defined(TARGET_ARCH_LOONG64)

#include "vm/class_id.h"
#include "vm/code_entry_kind.h"
#include "vm/compiler/api/type_check_mode.h"
#include "vm/compiler/assembler/assembler.h"
#include "vm/compiler/backend/locations.h"
#include "vm/constants.h"
#include "vm/instructions.h"
#include "vm/static_type_exactness_state.h"
#include "vm/tags.h"

#define __ assembler->

namespace dart {
namespace compiler {

// Helper to generate boxing stubs for unboxed FP values.
// Based on the architecture-independent implementation in stub_code_compiler.cc.
static void GenerateBoxFpuValueStub(Assembler* assembler,
                                    const dart::Class& cls,
                                    const RuntimeEntry& runtime_entry,
                                    void (Assembler::*store_value)(FpuRegister,
                                                                   Register,
                                                                   int32_t)) {
  Label call_runtime;
  if (!FLAG_use_slow_path && FLAG_inline_alloc) {
    __ TryAllocate(cls, &call_runtime, compiler::Assembler::kFarJump,
                   BoxDoubleStubABI::kResultReg, BoxDoubleStubABI::kTempReg);
    (assembler->*store_value)(
        BoxDoubleStubABI::kValueReg, BoxDoubleStubABI::kResultReg,
        compiler::target::Double::value_offset() - kHeapObjectTag);
    __ Ret();
  }
  __ Bind(&call_runtime);
  __ EnterStubFrame();
  __ PushObject(NullObject()); /* Make room for result. */
  (assembler->*store_value)(BoxDoubleStubABI::kValueReg, THR,
                            target::Thread::unboxed_runtime_arg_offset());
  __ CallRuntime(runtime_entry, 0);
  __ PopRegister(BoxDoubleStubABI::kResultReg);
  __ LeaveStubFrame();
  __ Ret();
}
class StackRegisterScope : ValueObject {
 public:
  StackRegisterScope(Assembler* assembler,
                     Register* reg,
                     intptr_t depth,
                     Register alt = TMP)
      : assembler(assembler), reg_(reg), depth_(depth), alt_(alt) {
    if (depth_ != kNoDepth) {
      ASSERT(depth_ >= 0);
      ASSERT(*reg_ == kNoRegister);
      ASSERT(alt_ != kNoRegister);
      __ LoadFromStack(alt_, depth_);
      *reg_ = alt_;
    } else {
      ASSERT(*reg_ != kNoRegister);
    }
  }

  ~StackRegisterScope() {
    if (depth_ != kNoDepth) {
      __ StoreToStack(alt_, depth_);
      *reg_ = kNoRegister;
    }
  }

  static constexpr intptr_t kNoDepth = kIntptrMin;

 private:
  Assembler* const assembler;
  Register* const reg_;
  const intptr_t depth_;
  const Register alt_;
};

static intptr_t SuspendStateFpOffset() {
  return compiler::target::frame_layout.FrameSlotForVariableIndex(
             SuspendState::kSuspendStateVarIndex) *
         compiler::target::kWordSize;
}

static void CallDartCoreLibraryFunction(
    Assembler* assembler,
    intptr_t entry_point_offset_in_thread,
    intptr_t function_offset_in_object_store,
    bool uses_args_desc = false) {
  if (FLAG_target_thread_sanitizer) {
    __ TsanFuncEntry();
  }
  if (FLAG_precompiled_mode) {
    __ Call(Address(THR, entry_point_offset_in_thread));
  } else {
    __ LoadIsolateGroup(FUNCTION_REG);
    __ LoadFromOffset(FUNCTION_REG, FUNCTION_REG,
                      target::IsolateGroup::object_store_offset());
    __ LoadFromOffset(FUNCTION_REG, FUNCTION_REG,
                      function_offset_in_object_store);
    __ LoadCompressedFieldFromOffset(CODE_REG, FUNCTION_REG,
                                     target::Function::code_offset());
    if (!uses_args_desc) {
      // Load a GC-safe value for the arguments descriptor (unused but tagged).
      __ LoadImmediate(ARGS_DESC_REG, 0);
    }
    __ Call(FieldAddress(FUNCTION_REG, target::Function::entry_point_offset()));
  }
  if (FLAG_target_thread_sanitizer) {
    __ TsanFuncExit();
  }
}

static void GenerateAllocateSuspendState(Assembler* assembler,
                                         Label* slow_case,
                                         Register result_reg,
                                         Register frame_size_reg,
                                         Register temp_reg) {
  if (FLAG_use_slow_path || !FLAG_inline_alloc) {
    __ Jump(slow_case);
    return;
  }

  // Check for allocation tracing.
  NOT_IN_PRODUCT(
      __ MaybeTraceAllocation(kSuspendStateCid, slow_case, temp_reg));

  // Compute the rounded instance size.
  const intptr_t fixed_size_plus_alignment_padding =
      (target::SuspendState::HeaderSize() +
       target::SuspendState::FrameSizeGrowthGap() * target::kWordSize +
       target::ObjectAlignment::kObjectAlignment - 1);
  __ AddImmediate(temp_reg, frame_size_reg, fixed_size_plus_alignment_padding);
  __ AndImmediate(temp_reg, -target::ObjectAlignment::kObjectAlignment);

  // Now allocate the object.
  __ LoadFromOffset(result_reg, THR, target::Thread::top_offset());
  __ AddRegisters(temp_reg, result_reg);
  // Check if the allocation fits into the remaining space.
  __ CompareWithMemoryValue(temp_reg,
                            Address(THR, target::Thread::end_offset()));
  __ BranchIf(UNSIGNED_GREATER_EQUAL, slow_case);
  __ CheckAllocationCanary(result_reg);

  // Successfully allocated the object, now update top to point to
  // next object start and initialize the object.
  __ StoreToOffset(temp_reg, THR, target::Thread::top_offset());
  __ SubRegisters(temp_reg, result_reg);
  __ AddImmediate(result_reg, kHeapObjectTag);

  if (!FLAG_precompiled_mode) {
    // Use rounded object size to calculate and save frame capacity.
    __ AddImmediate(temp_reg, temp_reg,
                    -target::SuspendState::payload_offset());
    __ StoreFieldToOffset(temp_reg, result_reg,
                          target::SuspendState::frame_capacity_offset());
    // Restore rounded object size.
    __ AddImmediate(temp_reg, temp_reg, target::SuspendState::payload_offset());
  }

  // Calculate the size tag.
  {
    Label size_tag_overflow, done;
    __ CompareImmediate(temp_reg, target::UntaggedObject::kSizeTagMaxSizeTag);
    __ BranchIf(UNSIGNED_GREATER, &size_tag_overflow, Assembler::kNearJump);
    __ LslImmediate(temp_reg,
                    target::UntaggedObject::kSizeTagPos -
                        target::ObjectAlignment::kObjectAlignmentLog2);
    __ Jump(&done, Assembler::kNearJump);

    __ Bind(&size_tag_overflow);
    // Set overflow size tag value.
    __ LoadImmediate(temp_reg, 0);

    __ Bind(&done);
    uword tags = target::MakeTagWordForNewSpaceObject(kSuspendStateCid, 0);
    __ OrImmediate(temp_reg, tags);
    __ InitializeHeader(temp_reg, result_reg);
  }

  __ StoreFieldToOffset(frame_size_reg, result_reg,
                        target::SuspendState::frame_size_offset());
}

static void BuildInstantiateTypeRuntimeCall(Assembler* assembler) {
  __ EnterStubFrame();
  __ PushObject(Object::null_object());
  __ PushRegistersInOrder({InstantiateTypeABI::kTypeReg,
                           InstantiateTypeABI::kInstantiatorTypeArgumentsReg,
                           InstantiateTypeABI::kFunctionTypeArgumentsReg});
  __ CallRuntime(kInstantiateTypeRuntimeEntry, /*argument_count=*/3);
  __ Drop(3);
  __ PopRegister(InstantiateTypeABI::kResultTypeReg);
  __ LeaveStubFrame();
  __ Ret();
}

static void BuildInstantiateTypeParameterStub(Assembler* assembler,
                                              Nullability nullability,
                                              bool is_function_parameter) {
  Label runtime_call, return_dynamic, type_parameter_value_is_not_type;

  if (is_function_parameter) {
    __ CompareObject(InstantiateTypeABI::kFunctionTypeArgumentsReg,
                     TypeArguments::null_object());
    __ BranchIf(EQUAL, &return_dynamic);
    __ LoadFieldFromOffset(
        InstantiateTypeABI::kResultTypeReg, InstantiateTypeABI::kTypeReg,
        target::TypeParameter::index_offset(), kUnsignedTwoBytes);
    __ LoadIndexedCompressed(InstantiateTypeABI::kResultTypeReg,
                             InstantiateTypeABI::kFunctionTypeArgumentsReg,
                             target::TypeArguments::types_offset(),
                             InstantiateTypeABI::kResultTypeReg);
  } else {
    __ CompareObject(InstantiateTypeABI::kInstantiatorTypeArgumentsReg,
                     TypeArguments::null_object());
    __ BranchIf(EQUAL, &return_dynamic);
    __ LoadFieldFromOffset(
        InstantiateTypeABI::kResultTypeReg, InstantiateTypeABI::kTypeReg,
        target::TypeParameter::index_offset(), kUnsignedTwoBytes);
    __ LoadIndexedCompressed(InstantiateTypeABI::kResultTypeReg,
                             InstantiateTypeABI::kInstantiatorTypeArgumentsReg,
                             target::TypeArguments::types_offset(),
                             InstantiateTypeABI::kResultTypeReg);
  }

  switch (nullability) {
    case Nullability::kNonNullable:
      __ Ret();
      break;
    case Nullability::kNullable:
      __ CompareAbstractTypeNullabilityWith(
          InstantiateTypeABI::kResultTypeReg,
          static_cast<int8_t>(Nullability::kNullable),
          InstantiateTypeABI::kScratchReg);
      __ BranchIf(NOT_EQUAL, &runtime_call);
      __ Ret();
      break;
  }

  // The TAV was null, so the value of the type parameter is "dynamic".
  __ Bind(&return_dynamic);
  __ LoadObject(InstantiateTypeABI::kResultTypeReg, Type::dynamic_type());
  __ Ret();

  __ Bind(&runtime_call);
  BuildInstantiateTypeRuntimeCall(assembler);
}

static void EnsureIsSomeKindOfType(Assembler* assembler,
                                   Register type_reg,
                                   Register scratch_reg) {
#if defined(DEBUG)
  compiler::Label is_type_param_or_type_or_function_type;
  __ LoadClassIdMayBeSmi(scratch_reg, type_reg);
  __ CompareImmediate(scratch_reg, kTypeParameterCid);
  __ BranchIf(EQUAL, &is_type_param_or_type_or_function_type,
              compiler::Assembler::kNearJump);
  __ CompareImmediate(scratch_reg, kTypeCid);
  __ BranchIf(EQUAL, &is_type_param_or_type_or_function_type,
              compiler::Assembler::kNearJump);
  __ CompareImmediate(scratch_reg, kFunctionTypeCid);
  __ BranchIf(EQUAL, &is_type_param_or_type_or_function_type,
              compiler::Assembler::kNearJump);
  __ CompareImmediate(scratch_reg, kRecordTypeCid);
  __ BranchIf(EQUAL, &is_type_param_or_type_or_function_type,
              compiler::Assembler::kNearJump);
  __ Stop("not a type, function type, record type or type parameter");
  __ Bind(&is_type_param_or_type_or_function_type);
#endif
}

static void BuildTypeParameterTypeTestStub(Assembler* assembler,
                                           bool allow_null) {
  Label done;

  if (allow_null) {
    __ CompareObject(TypeTestABI::kInstanceReg, NullObject());
    __ BranchIf(EQUAL, &done, Assembler::kNearJump);
  }

  auto handle_case = [&](Register tav) {
    // If the TAV is null, then resolving the type parameter gives the dynamic
    // type, which is a top type.
    __ CompareObject(tav, NullObject());
    __ BranchIf(EQUAL, &done, Assembler::kNearJump);
    // Resolve the type parameter to its instantiated type and tail call the
    // instantiated type's TTS.
    __ LoadFieldFromOffset(TypeTestABI::kScratchReg, TypeTestABI::kDstTypeReg,
                           target::TypeParameter::index_offset(),
                           kUnsignedTwoBytes);
    __ LoadIndexedCompressed(TypeTestABI::kScratchReg, tav,
                             target::TypeArguments::types_offset(),
                             TypeTestABI::kScratchReg);
    __ Jump(FieldAddress(
        TypeTestABI::kScratchReg,
        target::AbstractType::type_test_stub_entry_point_offset()));
  };

  Label function_type_param;
  __ LoadFromSlot(TypeTestABI::kScratchReg, TypeTestABI::kDstTypeReg,
                  Slot::AbstractType_flags());
  __ BranchIfBit(TypeTestABI::kScratchReg,
                 target::UntaggedTypeParameter::kIsFunctionTypeParameterBit,
                 NOT_ZERO, &function_type_param, Assembler::kNearJump);
  handle_case(TypeTestABI::kInstantiatorTypeArgumentsReg);
  __ Bind(&function_type_param);
  handle_case(TypeTestABI::kFunctionTypeArgumentsReg);
  __ Bind(&done);
  __ Ret();
}

static void InvokeTypeCheckFromTypeTestStub(Assembler* assembler,
                                            TypeCheckMode mode) {
  __ PushObject(NullObject());  // Make room for result.
  __ PushRegistersInOrder({TypeTestABI::kInstanceReg, TypeTestABI::kDstTypeReg,
                           TypeTestABI::kInstantiatorTypeArgumentsReg,
                           TypeTestABI::kFunctionTypeArgumentsReg});
  __ PushObject(NullObject());
  __ PushRegister(TypeTestABI::kSubtypeTestCacheReg);
  __ PushImmediate(target::ToRawSmi(mode));
  __ CallRuntime(kTypeCheckRuntimeEntry, 7);
  __ Drop(1);  // mode
  __ PopRegister(TypeTestABI::kSubtypeTestCacheReg);
  __ Drop(1);  // dst_name
  __ PopRegister(TypeTestABI::kFunctionTypeArgumentsReg);
  __ PopRegister(TypeTestABI::kInstantiatorTypeArgumentsReg);
  __ PopRegister(TypeTestABI::kDstTypeReg);
  __ PopRegister(TypeTestABI::kInstanceReg);
  __ Drop(1);  // Discard return value.
}


void StubCodeCompiler::GenerateAllocateArrayStub() {
  __ Comment("AllocateArrayStub");
  const Register length_reg = A1;
  const Register type_args_reg = A2;
  const Register result_reg = A0;

  if (FLAG_inline_alloc) {
    Label slow_case;
    __ TryAllocateArray(kArrayCid, &slow_case,
                        Assembler::kNearJump,
                        result_reg, length_reg, type_args_reg, TMP, TMP2);
    __ ret();

    __ Bind(&slow_case);
  }

  // Slow case: call runtime to allocate the array.
  __ EnterStubFrame();
  __ PushRegistersInOrder({type_args_reg, length_reg});
  __ CallRuntime(kAllocateArrayRuntimeEntry, /*argument_count=*/2);
  __ PopRegister(result_reg);
  __ LeaveStubFrame();
  __ ret();
}

void StubCodeCompiler::GenerateAllocateContextStub() {
  __ Comment("AllocateContextStub");
  __ EnterStubFrame();
  __ SmiTag(A0);
  __ PushObject(NullObject());
  __ PushRegister(A0);
  __ CallRuntime(kAllocateContextRuntimeEntry, /*argument_count=*/1);
  __ Drop(1);
  __ PopRegister(A0);
  EnsureIsNewOrRemembered();
  __ LeaveStubFrame();
  __ ret();
}

void StubCodeCompiler::GenerateAllocateMintSharedWithFPURegsStub() {
  if (!FLAG_use_slow_path && FLAG_inline_alloc) {
    Label slow_case;
    __ TryAllocate(compiler::MintClass(), &slow_case, Assembler::kNearJump,
                   AllocateMintABI::kResultReg, AllocateMintABI::kTempReg);
    __ ret();
    __ Bind(&slow_case);
  }
  COMPILE_ASSERT(AllocateMintABI::kResultReg ==
                 SharedSlowPathStubABI::kResultReg);
  GenerateSharedStub(/*save_fpu_registers=*/true, &kAllocateMintRuntimeEntry,
                     target::Thread::allocate_mint_with_fpu_regs_stub_offset(),
                     /*allow_return=*/true,
                     /*store_runtime_result_in_result_register=*/true);
}

void StubCodeCompiler::GenerateAllocateMintSharedWithoutFPURegsStub() {
  if (!FLAG_use_slow_path && FLAG_inline_alloc) {
    Label slow_case;
    __ TryAllocate(compiler::MintClass(), &slow_case, Assembler::kNearJump,
                   AllocateMintABI::kResultReg, AllocateMintABI::kTempReg);
    __ ret();
    __ Bind(&slow_case);
  }
  COMPILE_ASSERT(AllocateMintABI::kResultReg ==
                 SharedSlowPathStubABI::kResultReg);
  GenerateSharedStub(/*save_fpu_registers=*/false, &kAllocateMintRuntimeEntry,
                     target::Thread::allocate_mint_without_fpu_regs_stub_offset(),
                     /*allow_return=*/true,
                     /*store_runtime_result_in_result_register=*/true);
}

static void GenerateAllocateObjectHelper(Assembler* assembler,
                                         bool is_cls_parameterized) {
  const Register kTagsReg = AllocateObjectABI::kTagsReg;

  {
    Label slow_case;

#if !defined(PRODUCT)
    {
      const Register kCidRegister = TMP2;
      __ ExtractClassIdFromTags(kCidRegister, AllocateObjectABI::kTagsReg);
      __ MaybeTraceAllocation(kCidRegister, &slow_case, TMP);
    }
#endif

    const Register kNewTopReg = T3;

    // Bump allocation.
    {
      const Register kInstanceSizeReg = T4;
      const Register kEndReg = T5;

      __ ExtractInstanceSizeFromTags(kInstanceSizeReg, kTagsReg);

      // Load two words from Thread::top: top and end.
      // AllocateObjectABI::kResultReg: potential next object start.
      __ LoadFromOffset(AllocateObjectABI::kResultReg, THR,
                        target::Thread::top_offset());
      __ LoadFromOffset(kEndReg, THR, target::Thread::end_offset());

      __ add_d(kNewTopReg, AllocateObjectABI::kResultReg, kInstanceSizeReg);

      __ CompareRegisters(kNewTopReg, kEndReg);
      __ BranchIf(CS, &slow_case);  // new_top >= end -> slow case
      __ CheckAllocationCanary(AllocateObjectABI::kResultReg);

      // Successfully allocated the object, now update top to point to
      // next object start and store the class in the class field of object.
      __ StoreToOffset(kNewTopReg, THR, target::Thread::top_offset());
    }  // kInstanceSizeReg = T4, kEndReg = T5

    // Tags.
    __ InitializeHeaderUntagged(kTagsReg, AllocateObjectABI::kResultReg);

    // Initialize the remaining words of the object.
    {
      const Register kFieldReg = T4;

      __ AddImmediate(kFieldReg, AllocateObjectABI::kResultReg,
                      target::Instance::first_field_offset());
      Label loop;
      __ Bind(&loop);
      for (intptr_t offset = 0; offset < target::kObjectAlignment;
           offset += target::kCompressedWordSize) {
        __ StoreCompressedIntoObjectNoBarrier(
            AllocateObjectABI::kResultReg, FieldAddress(kFieldReg, offset),
            NULL_REG);
      }
      // Safe to only check every kObjectAlignment bytes instead of each word.
      ASSERT(kAllocationRedZoneSize >= target::kObjectAlignment);
      __ AddImmediate(kFieldReg, kFieldReg, target::kObjectAlignment);
      __ CompareRegisters(kFieldReg, kNewTopReg);
      __ BranchIf(CC, &loop);
      __ WriteAllocationCanary(kNewTopReg);  // Fix overshoot.
    }  // kFieldReg = T4

    __ AddImmediate(AllocateObjectABI::kResultReg,
                    AllocateObjectABI::kResultReg, kHeapObjectTag);

    if (is_cls_parameterized) {
      const Register kClsIdReg = T4;
      const Register kTypeOffsetReg = T5;

      __ ExtractClassIdFromTags(kClsIdReg, kTagsReg);

      // Load class' type_arguments_field offset in words.
      __ LoadClassById(kTypeOffsetReg, kClsIdReg);
      __ LoadFromOffset(
          kTypeOffsetReg, kTypeOffsetReg,
          target::Class::host_type_arguments_field_offset_in_words_offset(),
          kFourBytes);

      // Set the type arguments in the new object.
      __ slli_d(TMP, kTypeOffsetReg, target::kWordSizeLog2);
      __ add_d(kTypeOffsetReg, AllocateObjectABI::kResultReg, TMP);
      __ StoreCompressedIntoObjectNoBarrier(
          AllocateObjectABI::kResultReg, FieldAddress(kTypeOffsetReg, 0),
          AllocateObjectABI::kTypeArgumentsReg);
    }  // kClsIdReg = T4, kTypeOffsetReg = T5

    __ ret();

    __ Bind(&slow_case);
  }  // kNewTopReg = T3

  // Fall back on slow case:
  if (!is_cls_parameterized) {
    __ MoveRegister(AllocateObjectABI::kTypeArgumentsReg, NULL_REG);
  }
  // Tail call to generic allocation stub.
  __ LoadFromOffset(
      TMP, THR,
      target::Thread::allocate_object_slow_entry_point_offset());
  __ jr(TMP);
}

void StubCodeCompiler::GenerateAllocateObjectStub() {
  GenerateAllocateObjectHelper(assembler, /*is_cls_parameterized=*/false);
}

void StubCodeCompiler::GenerateAllocateObjectParameterizedStub() {
  GenerateAllocateObjectHelper(assembler, /*is_cls_parameterized=*/true);
}

void StubCodeCompiler::GenerateAllocateObjectSlowStub() {
  __ Comment("AllocateObjectSlowStub");
  __ EnterStubFrame();
  __ PushRegister(A0);
  __ CallRuntime(kAllocateObjectRuntimeEntry, /*argument_count=*/1);
  __ Drop(1);
  __ LeaveStubFrame();
  __ ret();
}

void StubCodeCompiler::GenerateArrayWriteBarrierStub() {
  // Array write barrier stub for generational GC with card marking.
  // Called with object in kWriteBarrierObjectReg (A0) and
  // value in kWriteBarrierValueReg (A1).
  // Slot is in kWriteBarrierSlotReg (A6).
  __ Comment("ArrayWriteBarrierStub");
  Label done_label_;

  // Check if value is a Smi (no barrier needed for Smis).
  __ andi(TMP, kWriteBarrierValueReg, kSmiTagMask);
  __ bnez(TMP, &done_label_);

  // Check if the object is in new space (no barrier needed).
  __ andi(TMP, kWriteBarrierObjectReg,
          target::ObjectAlignment::kNewObjectAlignmentOffset);
  __ bnez(TMP, &done_label_);

  // Check if card remembered bit is already set.
  __ LoadFromOffset(TMP, kWriteBarrierObjectReg,
                         target::Object::tags_offset() - kHeapObjectTag,
                         kFourBytes);
  __ AndImmediate(TMP, TMP, 1 << target::UntaggedObject::kCardRememberedBit);
  Label remember_card;
  __ bnez(TMP, &remember_card);

  // Remembered set update.
  __ LoadFromOffset(TMP, THR, target::Thread::store_buffer_block_offset());
  __ LoadFromOffset(TMP2, TMP,
                    target::StoreBufferBlock::top_offset(),
                    kUnsignedFourBytes);
  __ LoadImmediate(T1, target::StoreBufferBlock::kSize);
  __ CompareRegisters(TMP2, T1);
  Label call_runtime;
  __ BranchIf(EQUAL, &call_runtime);

  // Store object pointer into the store buffer.
  __ slli_d(T1, TMP2, target::kWordSizeLog2);
  __ add_d(T1, TMP, T1);
  __ st_d(kWriteBarrierObjectReg, T1,
          target::StoreBufferBlock::pointers_offset());

  // Increment top.
  __ addi_d(TMP2, TMP2, 1);
  __ StoreToOffset(TMP2, TMP,
                   target::StoreBufferBlock::top_offset(),
                   kUnsignedFourBytes);
  __ b(&done_label_);

  __ Bind(&call_runtime);
  {
    LeafRuntimeScope rt(assembler, /*frame_size=*/0,
                        /*preserve_registers=*/false);
    __ MoveRegister(A0, THR);
    rt.Call(kStoreBufferBlockProcessRuntimeEntry, /*argument_count=*/1);
  }
  __ b(&done_label_);

  // Card marking: atomically set the card bit using LL/SC.
  __ Bind(&remember_card);
  // Get page base from the object.
  __ AndImmediate(TMP, kWriteBarrierObjectReg, target::Page::kPageMask);
  // Load the card table pointer from the page.
  __ LoadFromOffset(TMP2, TMP, target::Page::card_table_offset());
  // Compute offset within the page.
  __ sub_d(T1, kWriteBarrierSlotReg, TMP);
  // Compute card index.
  __ srai_d(T1, T1, target::Page::kBytesPerCardLog2);
  // Make bit mask: 1 << card_index.
  __ LoadImmediate(T3, 1);
  __ sll_d(T3, T3, T1);
  // Compute word index within card table.
  __ srai_d(T1, T1, target::kBitsPerWordLog2);
  __ slli_d(T1, T1, target::kWordSizeLog2);
  // Compute address of the card table word.
  __ add_d(TMP2, TMP2, T1);

  // Atomically OR the bit using LL/SC.
  Label retry_card;
  __ Bind(&retry_card);
  __ ll_d(T1, TMP2, 0);
  __ or_l(T1, T1, T3);
  __ sc_d(T1, TMP2, 0);
  __ beqz(T1, &retry_card);

  __ Bind(&done_label_);
  __ ret();
}


// Input parameters (as set up by NativeCallInstr::EmitNativeCode):
//   RA : return address.
//   SP : points to return value slot on caller's stack.
//   A3 : address of the native function to call.
//   A2 : address of first argument in argument array.
//   A1 : argc_tag including number of arguments and function kind.
static void GenerateCallNativeWithWrapperStub(Assembler* assembler,
                                              Address wrapper) {
  const intptr_t thread_offset = target::NativeArguments::thread_offset();
  const intptr_t argc_tag_offset = target::NativeArguments::argc_tag_offset();
  const intptr_t argv_offset = target::NativeArguments::argv_offset();
  const intptr_t retval_offset = target::NativeArguments::retval_offset();

  __ EnterStubFrame();

  // Save exit frame information to enable stack walking as we are about
  // to transition to native code.
  __ StoreToOffset(FP, THR, target::Thread::top_exit_frame_info_offset());

  // Mark that the thread exited generated code through a runtime call.
  __ LoadImmediate(TMP, target::Thread::exit_through_runtime_call());
  __ StoreToOffset(TMP, THR, target::Thread::exit_through_ffi_offset());

#if defined(DEBUG)
  {
    Label ok;
    // Check that we are always entering from Dart code.
    __ LoadFromOffset(TMP, THR, target::Thread::vm_tag_offset());
    __ CompareImmediate(TMP, VMTag::kDartTagId);
    __ BranchIf(EQUAL, &ok);
    __ Stop("Not coming from Dart code.");
    __ Bind(&ok);
  }
#endif

  // Mark that the thread is executing native code.
  __ StoreToOffset(A3, THR, target::Thread::vm_tag_offset());

  // Reserve space for the native arguments structure passed on the stack (the
  // outgoing pointer parameter to the native arguments structure is passed in
  // A0) and align frame before entering the C++ world.
  __ ReserveAlignedFrameSpace(target::NativeArguments::StructSize());

  // Initialize target::NativeArguments structure and call native function.
  ASSERT(thread_offset == 0 * target::kWordSize);
  ASSERT(argc_tag_offset == 1 * target::kWordSize);
  ASSERT(argv_offset == 2 * target::kWordSize);
  ASSERT(retval_offset == 3 * target::kWordSize);
  __ AddImmediate(
      T3, FP, (target::frame_layout.param_end_from_fp + 1) * target::kWordSize);

  // Passing the structure by value as in runtime calls would require changing
  // Dart API for native functions.
  // For now, space is reserved on the stack and we pass a pointer to it.
  __ StoreToOffset(THR, SP, thread_offset);
  __ StoreToOffset(A1, SP, argc_tag_offset);
  __ StoreToOffset(A2, SP, argv_offset);
  __ StoreToOffset(T3, SP, retval_offset);
  __ MoveRegister(A0, SP);  // Pass the pointer to the target::NativeArguments.
  __ MoveRegister(A1, A3);  // Pass the function entrypoint to call.

  // Call native function invocation wrapper or redirection via simulator.
  ASSERT(IsAbiPreservedRegister(THR));
  __ Call(wrapper);

  // Refresh pinned registers values (inc. write barrier mask and null object).
  __ RestorePinnedRegisters();

  // Mark that the thread is executing Dart code.
  __ LoadImmediate(TMP, VMTag::kDartTagId);
  __ StoreToOffset(TMP, THR, target::Thread::vm_tag_offset());

  // Mark that the thread has not exited generated Dart code.
  __ StoreToOffset(ZR, THR, target::Thread::exit_through_ffi_offset());

  // Reset exit frame information in Isolate's mutator thread structure.
  __ StoreToOffset(ZR, THR, target::Thread::top_exit_frame_info_offset());

  // Restore the global object pool after returning from runtime (old space is
  // moving, so the GOP could have been relocated).
  if (FLAG_precompiled_mode) {
    __ SetupGlobalPoolAndDispatchTable();
  }

  __ LeaveStubFrame();
  __ ret();
}
void StubCodeCompiler::GenerateCallAutoScopeNativeStub() {
  GenerateCallNativeWithWrapperStub(
      assembler,
      Address(THR,
              target::Thread::auto_scope_native_wrapper_entry_point_offset()));
}

void StubCodeCompiler::GenerateCallBootstrapNativeStub() {
  GenerateCallNativeWithWrapperStub(
      assembler,
      Address(THR,
              target::Thread::bootstrap_native_wrapper_entry_point_offset()));
}

static void PushArrayOfArguments(Assembler* assembler) {
  COMPILE_ASSERT(AllocateArrayABI::kLengthReg == S8);
  COMPILE_ASSERT(AllocateArrayABI::kTypeArgumentsReg == T1);

  // Allocate array to store arguments of caller.
  __ LoadObject(T1, NullObject());
  // T1: null element type for raw Array.
  // S8: smi-tagged argument count, may be zero.
  __ JumpAndLink(StubCodeAllocateArray());
  // A0: newly allocated array.
  // S8: smi-tagged argument count, may be zero (was preserved by the stub).
  __ PushRegister(A0);  // Array is in A0 and on top of stack.
  __ SmiUntag(S8);
  __ slli_d(T1, S8, target::kWordSizeLog2);
  __ add_d(T1, T1, FP);
  __ AddImmediate(T1,
                  target::frame_layout.param_end_from_fp * target::kWordSize);
  __ AddImmediate(T3, A0, target::Array::data_offset() - kHeapObjectTag);
  // T1: address of first argument on stack.
  // T3: address of first argument in array.

  Label loop, loop_exit;
  __ Bind(&loop);
  __ CompareRegisters(S8, ZR);
  __ BranchIf(EQUAL, &loop_exit);
  __ LoadFromOffset(T6, T1, 0);
  __ AddImmediate(T1, T1, -target::kWordSize);
  __ StoreCompressedIntoObject(A0, Address(T3, 0), T6);
  __ AddImmediate(T3, T3, target::kCompressedWordSize);
  __ AddImmediate(S8, S8, -1);
  __ Jump(&loop);
  __ Bind(&loop_exit);
}

void StubCodeCompiler::GenerateCallClosureNoSuchMethodStub() {
  __ EnterStubFrame();

  // Load the receiver.
  __ LoadCompressedSmiFieldFromOffset(
      S8, S4, target::ArgumentsDescriptor::size_offset());
  __ slli_d(TMP, S8, target::kWordSizeLog2 - 1);  // S8 is Smi
  __ add_d(TMP, FP, TMP);
  __ LoadFromOffset(A0, TMP,
                    target::frame_layout.param_end_from_fp * target::kWordSize);

  // Load the function.
  __ LoadCompressedFieldFromOffset(TMP, A0, target::Closure::function_offset());

  // Push result slot, receiver, function, arguments descriptor.
  __ PushRegistersInOrder({ZR, A0, TMP, S4});

  // Adjust arguments count.
  __ LoadCompressedSmiFieldFromOffset(
      T3, S4, target::ArgumentsDescriptor::type_args_len_offset());
  Label args_count_ok;
  __ CompareRegisters(T3, ZR);
  __ BranchIf(EQUAL, &args_count_ok);
  // Include the type arguments.
  __ AddImmediate(S8, S8, target::ToRawSmi(1));
  __ Bind(&args_count_ok);

  // S8: Smi-tagged arguments array length.
  PushArrayOfArguments(assembler);

  const intptr_t kNumArgs = 4;
  __ CallRuntime(kNoSuchMethodFromPrologueRuntimeEntry, kNumArgs);
  // noSuchMethod on closures always throws an error, so it will never return.
  __ Breakpoint();
}

void StubCodeCompiler::GenerateCallNativeThroughSafepointStub() {
  COMPILE_ASSERT(IsAbiPreservedRegister(S2));
  __ MoveRegister(S2, RA);
  __ LoadImmediate(T1, target::Thread::exit_through_ffi());
  __ TransitionGeneratedToNative(T0, FPREG, T1 /*volatile*/,
                                 /*enter_safepoint=*/true);

#if defined(DEBUG)
  // Check SP alignment.
  __ AndImmediate(S8 /*volatile*/, SP, ~(OS::ActivationFrameAlignment() - 1));
  Label done;
  __ CompareRegisters(S8, SP);
  __ BranchIf(EQUAL, &done);
  __ Breakpoint();
  __ Bind(&done);
#endif

  __ Call(T0);

  __ TransitionNativeToGenerated(T1, /*exit_safepoint=*/true);
  __ jr(S2);
}

void StubCodeCompiler::GenerateCallNoScopeNativeStub() {
  GenerateCallNativeWithWrapperStub(
      assembler,
      Address(THR,
              target::Thread::no_scope_native_wrapper_entry_point_offset()));
}
void StubCodeCompiler::GenerateCallStaticFunctionStub() {
  // A0: function to call.
  // A1: arguments descriptor.
  // Enters the function directly after setting up ARGS_DESC_REG and CODE_REG.
  __ Comment("CallStaticFunctionStub");
  __ MoveRegister(ARGS_DESC_REG, A1);
  __ LoadFieldFromOffset(CODE_REG, A0,
                         compiler::target::Function::code_offset(),
                         kEightBytes);
  __ LoadFieldFromOffset(TMP, CODE_REG,
                         compiler::target::Code::entry_point_offset(),
                         kEightBytes);
  __ jr(TMP);
}

void StubCodeCompiler::GenerateCallToRuntimeStub() {
  const intptr_t thread_offset = target::NativeArguments::thread_offset();
  const intptr_t argc_tag_offset = target::NativeArguments::argc_tag_offset();
  const intptr_t argv_offset = target::NativeArguments::argv_offset();
  const intptr_t retval_offset = target::NativeArguments::retval_offset();

  __ Comment("CallToRuntimeStub");
  __ LoadFromOffset(CODE_REG, THR, target::Thread::call_to_runtime_stub_offset());
  // __ SetPrologueOffset();  // removed - not available
  __ EnterStubFrame();

  // Save exit frame information to enable stack walking as we are about
  // to transition to Dart VM C++ code.
  __ StoreToOffset(FP, THR, target::Thread::top_exit_frame_info_offset());

  // Mark that the thread exited generated code through a runtime call.
  __ LoadImmediate(TMP, target::Thread::exit_through_runtime_call());
  __ StoreToOffset(TMP, THR, target::Thread::exit_through_ffi_offset());

#if defined(DEBUG)
  {
    Label ok;
    // Check that we are always entering from Dart code.
    __ LoadFromOffset(TMP, THR, target::Thread::vm_tag_offset());
    __ CompareImmediate(TMP, VMTag::kDartTagId);
    __ BranchIf(EQUAL, &ok);
    __ Stop("Not coming from Dart code.");
    __ Bind(&ok);
  }
#endif

  // Mark that the thread is executing VM code.
  __ StoreToOffset(T2, THR, target::Thread::vm_tag_offset());

  // Reserve space for arguments and align frame before entering C++ world.
  __ Comment("align stack");
  ASSERT(target::NativeArguments::StructSize() == 4 * target::kWordSize);
  __ ReserveAlignedFrameSpace(target::NativeArguments::StructSize());

  // Pass target::NativeArguments structure by value and call runtime.

  ASSERT(thread_offset == 0 * target::kWordSize);
  __ StoreToOffset(THR, SP, thread_offset);

  ASSERT(argc_tag_offset == 1 * target::kWordSize);
  __ StoreToOffset(T3, SP, argc_tag_offset);

  ASSERT(argv_offset == 2 * target::kWordSize);
  __ slli_d(T6, T3, target::kWordSizeLog2);
  __ add_d(T6, FP, T6);  // Compute argv.
  __ AddImmediate(T6,
                  target::frame_layout.param_end_from_fp * target::kWordSize);
  __ StoreToOffset(T6, SP, argv_offset);

  ASSERT(retval_offset == 3 * target::kWordSize);
  __ AddImmediate(T7, T6, target::kWordSize);
  __ StoreToOffset(T7, SP, retval_offset);
  __ MoveRegister(A0, SP);  // Pass the pointer to the target::NativeArguments.

  ASSERT(IsAbiPreservedRegister(THR));
  __ PushNativeCalleeSavedRegisters();
  __ jirl(RA, T2, 0);
  __ Comment("CallToRuntimeStub return");
  __ PopNativeCalleeSavedRegisters();

  // Refresh pinned registers values (inc. write barrier mask and null object).
  __ RestorePinnedRegisters();

  // Mark that the thread is executing Dart code.
  __ LoadImmediate(TMP, VMTag::kDartTagId);
  __ StoreToOffset(TMP, THR, target::Thread::vm_tag_offset());

  // Mark that the thread has not exited generated Dart code.
  __ StoreToOffset(ZR, THR, target::Thread::exit_through_ffi_offset());

  // Reset exit frame information in Isolate's mutator thread structure.
  __ StoreToOffset(ZR, THR, target::Thread::top_exit_frame_info_offset());

  // Restore the global object pool after returning from runtime.
  if (FLAG_precompiled_mode) {
    __ SetupGlobalPoolAndDispatchTable();
  }

  __ LeaveStubFrame();

  // The following return can jump to a lazy-deopt stub, which assumes A0
  // contains a return value and will save it in a GC-visible way.
  __ LoadImmediate(A0, 0);
  __ ret();
}

static void GenerateAllocateContextSpaceStub(Assembler* assembler,
                                             Label* slow_case) {
  // First compute the rounded instance size.
  // T1: number of context variables.
  intptr_t fixed_size_plus_alignment_padding =
      target::Context::header_size() +
      target::ObjectAlignment::kObjectAlignment - 1;
  __ slli_d(S8, T1, kCompressedWordSizeLog2);
  __ AddImmediate(S8, fixed_size_plus_alignment_padding);
  __ AndImmediate(S8, S8, ~(target::ObjectAlignment::kObjectAlignment - 1));

  NOT_IN_PRODUCT(__ MaybeTraceAllocation(kContextCid, slow_case, T4));
  // Now allocate the object.
  // T1: number of context variables.
  // S8: object size.
  __ LoadFromOffset(A0, THR, target::Thread::top_offset());
  __ add_d(T3, S8, A0);
  // Check if the allocation fits into the remaining space.
  // A0: potential new object.
  // S8: object size.
  // T3: potential next object start.
  __ LoadFromOffset(TMP, THR, target::Thread::end_offset());
  __ CompareRegisters(T3, TMP);
  __ BranchIf(CS, slow_case);  // Branch if unsigned higher or equal.
  __ CheckAllocationCanary(A0);

  // Successfully allocated the object, now update top to point to
  // next object start and initialize the object.
  __ StoreToOffset(T3, THR, target::Thread::top_offset());
  __ AddImmediate(A0, A0, kHeapObjectTag);

  // Calculate the size tag.
  const intptr_t shift = target::UntaggedObject::kSizeTagPos -
                         target::ObjectAlignment::kObjectAlignmentLog2;
  __ LoadImmediate(T3, 0);
  __ CompareImmediate(S8, target::UntaggedObject::kSizeTagMaxSizeTag);
  // If no size tag overflow, shift S8 left, else set T3 to zero.
  Label zero_tag;
  __ BranchIf(HI, &zero_tag);
  __ slli_d(T3, S8, shift);
  __ Bind(&zero_tag);

  // Get the class index and insert it into the tags.
  // T3: size and bit tags.
  const uword tags =
      target::MakeTagWordForNewSpaceObject(kContextCid, /*instance_size=*/0);

  __ OrImmediate(T3, T3, tags);
  __ InitializeHeader(T3, A0);

  // Setup up number of context variables field.
  // A0: new object.
  // T1: number of context variables as integer value (not object).
  __ StoreFieldToOffset(T1, A0, target::Context::num_variables_offset(),
                        kFourBytes);
}

void StubCodeCompiler::GenerateCloneContextStub() {
  // Input: A0 = context to clone.
  // Output: A0 = new context.
  if (!FLAG_use_slow_path && FLAG_inline_alloc) {
    Label slow_case;

    // Save old context in T0 before allocation overwrites A0.
    __ MoveRegister(T0, A0);

    // Load num. variable (int32) in the existing context.
    __ LoadFromOffset(T1, T0, target::Context::num_variables_offset(),
                      kFourBytes);

    GenerateAllocateContextSpaceStub(assembler, &slow_case);

    // A0 now holds the new context, T0 holds the old context.
    // Load parent in the existing context.
    __ LoadCompressed(T3, FieldAddress(T0, target::Context::parent_offset()));
    // Setup the parent field.
    __ StoreCompressedIntoObjectNoBarrier(
        A0, FieldAddress(A0, target::Context::parent_offset()), T3);

    // Clone the context variables.
    // A0: new context.
    // T1: number of context variables.
    // T0: old context.
    {
      Label loop, done;
      // T3: Variable array address, new context.
      __ AddImmediate(T3, A0,
                      target::Context::variable_offset(0) - kHeapObjectTag);
      // T4: Variable array address, old context.
      __ AddImmediate(T4, T0,
                      target::Context::variable_offset(0) - kHeapObjectTag);

      __ Bind(&loop);
      __ AddImmediate(T1, T1, -1);
      __ CompareImmediate(T1, 0);
      __ BranchIf(LT, &done);
      __ LoadFromOffset(T5, T4, 0);
      __ AddImmediate(T4, T4, target::kCompressedWordSize);
      __ StoreToOffset(T5, T3, 0);
      __ AddImmediate(T3, T3, target::kCompressedWordSize);
      __ Jump(&loop);

      __ Bind(&done);
    }

    // Done allocating and initializing the context.
    // A0: new object.
    __ ret();

    __ Bind(&slow_case);
  }

  // Fall back to slow case.
  __ EnterStubFrame();
  __ PushObject(NullObject());  // Make room for the result.
  __ PushRegister(A0);          // Old context.
  __ CallRuntime(kCloneContextRuntimeEntry, 1);  // Clone context.
  __ Drop(1);                   // Pop argument.
  __ PopRegister(A0);           // Pop the new context object.
  __ LeaveStubFrame();
  __ ret();
}

void StubCodeCompiler::GenerateDebugStepCheckStub() {
  __ Comment("DebugStepCheckStub");
  __ EnterStubFrame();
  __ CallRuntime(kSingleStepHandlerRuntimeEntry, /*argument_count=*/0);
  __ LeaveStubFrame();
  __ ret();
}

void StubCodeCompiler::GenerateDeoptForRewindStub() {
  // Push zap value instead of CODE_REG.
  __ LoadImmediate(TMP, kZapCodeReg);
  __ PushRegister(TMP);
  __ LoadFromOffset(CODE_REG, THR, target::Thread::deoptimize_stub_offset());
  { RegisterSet rs; rs.AddAllNonReservedRegisters(true); __ PushRegisters(rs); }
  __ MoveRegister(A0, SP);
  __ CallRuntime(kDeoptimizeMaterializeRuntimeEntry, /*argument_count=*/1);
  // Save the target address returned by the materializer.
  __ PushRegister(A0);
  // After we have deoptimized, jump to the correct frame.
  __ EnterStubFrame();
  __ CallRuntime(kRewindPostDeoptRuntimeEntry, /*argument_count=*/0);
  __ LeaveStubFrame();
  // Jump to the deoptimized code.
  __ PopRegister(A0);
  __ jr(A0);
}

void StubCodeCompiler::GenerateDeoptimizeStub() {
  __ PushRegister(CODE_REG);
  __ LoadFromOffset(CODE_REG, THR, target::Thread::deoptimize_stub_offset());
  { RegisterSet rs; rs.AddAllNonReservedRegisters(true); __ PushRegisters(rs); }
  __ MoveRegister(A0, SP);
  __ CallRuntime(kDeoptimizeMaterializeRuntimeEntry, /*argument_count=*/1);
  __ jr(A0);
}

void StubCodeCompiler::GenerateDeoptimizeLazyFromReturnStub() {
  // Push zap value instead of CODE_REG for lazy deopt.
  __ LoadImmediate(TMP, kZapCodeReg);
  __ PushRegister(TMP);
  // Return address for "call" to deopt stub.
  __ LoadImmediate(RA, kZapReturnAddress);
  __ LoadFromOffset(CODE_REG, THR,
                    target::Thread::lazy_deopt_from_return_stub_offset());
  { RegisterSet rs; rs.AddAllNonReservedRegisters(true); __ PushRegisters(rs); }
  __ MoveRegister(A0, SP);
  __ CallRuntime(kDeoptimizeMaterializeRuntimeEntry, /*argument_count=*/1);
  __ jr(A0);
}

void StubCodeCompiler::GenerateDeoptimizeLazyFromThrowStub() {
  __ Comment("DeoptimizeLazyFromThrowStub");
  // Push zap value instead of CODE_REG for lazy deopt.
  __ LoadImmediate(TMP, kZapCodeReg);
  __ PushRegister(TMP);
  // Return address for "call" to deopt stub.
  __ LoadImmediate(RA, kZapReturnAddress);
  __ LoadFromOffset(CODE_REG, THR,
                    target::Thread::lazy_deopt_from_throw_stub_offset());
  // Save all registers for deoptimization materialization.
  { RegisterSet rs; rs.AddAllNonReservedRegisters(true); __ PushRegisters(rs); }
  __ MoveRegister(A0, SP);
  __ CallRuntime(kDeoptimizeMaterializeRuntimeEntry, /*argument_count=*/1);
  __ jr(A0);
}

void StubCodeCompiler::GenerateDispatchTableNullErrorStub() {
  __ EnterStubFrame();
  __ SmiTag(DispatchTableNullErrorABI::kClassIdReg);
  __ PushRegister(DispatchTableNullErrorABI::kClassIdReg);
  __ CallRuntime(kDispatchTableNullErrorRuntimeEntry, /*argument_count=*/1);
  // The NullError runtime entry does not return.
  __ Breakpoint();
}

void StubCodeCompiler::GenerateEnterSafepointStub() {
  RegisterSet all_registers;
  all_registers.AddAllGeneralRegisters();
  __ PushRegisters(all_registers);
  __ EnterFrame(0);
  __ ReserveAlignedFrameSpace(0);
  __ CallRuntime(kEnterSafepointRuntimeEntry, /*argument_count=*/0);
  __ LeaveFrame();
  __ PopRegisters(all_registers);
  __ ret();
}

void StubCodeCompiler::GenerateExitSafepointStub() {
  RegisterSet all_registers;
  all_registers.AddAllGeneralRegisters();
  __ PushRegisters(all_registers);
  __ EnterFrame(0);
  __ ReserveAlignedFrameSpace(0);
  __ CallRuntime(kExitSafepointRuntimeEntry, /*argument_count=*/0);
  __ LeaveFrame();
  __ PopRegisters(all_registers);
  __ ret();
}

static const RegisterSet kFfiCallbackArgumentRegisterSet(
    CallingConventions::kArgumentRegisters,
    CallingConventions::kFpuArgumentRegisters);
static const RegisterSet kFfiCallbackReturnRegisterSet(
    (1 << CallingConventions::kReturnReg) |
        (1 << CallingConventions::kSecondReturnReg),
    (1 << CallingConventions::kReturnFpuReg) |
        (1 << CallingConventions::kSecondReturnFpuReg));

void StubCodeCompiler::GenerateFfiCallbackTrampolineStub() {
#if defined(DART_INCLUDE_SIMULATOR) && !defined(DART_PRECOMPILER)
  __ Breakpoint();
#else
  Label body;

  // T3 is volatile and not used for passing any arguments.
  COMPILE_ASSERT(!IsCalleeSavedRegister(T3) && !IsArgumentRegister(T3));
  for (intptr_t i = 0; i < FfiCallbackMetadata::NumCallbackTrampolinesPerPage();
       ++i) {
    __ pcaddu12i(T3, 0);
    __ b(&body);
  }

  ASSERT_EQUAL(__ CodeSize(),
               FfiCallbackMetadata::kNativeCallbackTrampolineSize *
                   FfiCallbackMetadata::NumCallbackTrampolinesPerPage());

  const intptr_t shared_stub_start = __ CodeSize();

  __ Bind(&body);

  COMPILE_ASSERT(FfiCallbackMetadata::kNativeCallbackTrampolineStackDelta == 4);
  __ AddImmediate(SP, -4 * target::kWordSize);
  __ StoreToOffset(RA, SP, 3 * target::kWordSize);
  __ StoreToOffset(FP, SP, 2 * target::kWordSize);
  __ StoreToOffset(THR, SP, 1 * target::kWordSize);
  __ StoreToOffset(S2, SP, 0 * target::kWordSize);
  __ AddImmediate(FP, SP, 4 * target::kWordSize);
  COMPILE_ASSERT(!IsArgumentRegister(THR));

  // Load the thread, verify the callback ID and exit the safepoint.
  //
  // We exit the safepoint inside DLRT_GetFfiCallbackMetadata in order to save
  // code size on this shared stub.
  {
    // Reserve space for 3 return values: entry_point, is_tail, epilogue.
    __ AddImmediate(SP, -3 * target::kWordSize);
    __ PushRegisters(kFfiCallbackArgumentRegisterSet);
    __ MoveRegister(A0, T3);
    __ MoveRegister(A1, SP);

    GenerateLoadFfiCallbackMetadataRuntimeFunction(
        FfiCallbackMetadata::kGetFfiCallbackMetadata, T3);
    __ jirl(RA, T3);

    __ MoveRegister(THR, A0);
    __ LoadFromOffset(T4, SP, 0 * target::kWordSize);  // entry_point
    __ LoadFromOffset(T3, SP, 1 * target::kWordSize);  // is_tail
    __ LoadFromOffset(S2, SP, 2 * target::kWordSize);  // epilogue

    __ PopRegisters(kFfiCallbackArgumentRegisterSet);
    __ AddImmediate(SP, 3 * target::kWordSize);
  }

  Label tail;
  __ CompareImmediate(T3, 0);
  __ BranchIf(NOT_EQUAL, &tail, Assembler::kNearJump);

  {
    __ jirl(RA, T4);  // entry_point
    __ PushRegisters(kFfiCallbackReturnRegisterSet);
    __ MoveRegister(A0, THR);
    __ jirl(RA, S2);  // DLRT_ExitSyncCallback, etc
    if (FLAG_target_memory_sanitizer) {
      __ jirl(RA, A0);  // dart_msan_unpoison_retval
    }
    __ PopRegisters(kFfiCallbackReturnRegisterSet);
    __ LoadFromOffset(S2, SP, 0 * target::kWordSize);
    __ LoadFromOffset(THR, SP, 1 * target::kWordSize);
    __ LoadFromOffset(FP, SP, 2 * target::kWordSize);
    __ LoadFromOffset(RA, SP, 3 * target::kWordSize);
    __ AddImmediate(SP, 4 * target::kWordSize);
    __ ret();
  }

  {
    __ Bind(&tail);
    __ jirl(RA, T4);  // entry_point
    __ MoveRegister(A0, THR);
    __ MoveRegister(A1, S2);
    __ LoadFromOffset(S2, SP, 0 * target::kWordSize);
    __ LoadFromOffset(THR, SP, 1 * target::kWordSize);
    __ LoadFromOffset(FP, SP, 2 * target::kWordSize);
    __ LoadFromOffset(RA, SP, 3 * target::kWordSize);
    __ AddImmediate(SP, 4 * target::kWordSize);
    // Tail-call DLRT_ExitTemporaryIsolate. It is not safe to return to this
    // stub, since it might be deleted once DLRT_ExitTemporaryIsolate proceeds
    // enough for VM shutdown.
    __ jr(A1);
    __ Breakpoint();
  }

  ASSERT_LESS_OR_EQUAL(__ CodeSize() - shared_stub_start,
                       FfiCallbackMetadata::kNativeCallbackSharedStubSize);
  ASSERT_LESS_OR_EQUAL(__ CodeSize(), FfiCallbackMetadata::kPageSize);

#if defined(DEBUG)
  while (__ CodeSize() < FfiCallbackMetadata::kPageSize) {
    __ Breakpoint();
  }
#endif
#endif  // DART_INCLUDE_SIMULATOR
}

void StubCodeCompiler::GenerateFfiCallTrampolineStub() {
  __ Breakpoint();  // Not implemented.
}

void StubCodeCompiler::GenerateFixAllocationStubTargetStub() {
  // Load code pointer to this stub from the thread:
  // The one that is passed in, is not correct - it points to the code object
  // that needs to be replaced.
  __ LoadFromOffset(CODE_REG, THR,
                    target::Thread::fix_allocation_stub_code_offset());
  __ EnterStubFrame();
  // Setup space on stack for return value.
  __ PushRegister(ZR);
  __ CallRuntime(kFixAllocationStubTargetRuntimeEntry, 0);
  // Get Code object result.
  __ PopRegister(CODE_REG);
  // Remove the stub frame.
  __ LeaveStubFrame();
  // Jump to the dart function.
  __ LoadFieldFromOffset(TMP, CODE_REG, target::Code::entry_point_offset());
  __ jr(TMP);
}

void StubCodeCompiler::GenerateFixCallersTargetStub() {
  __ Comment("FixCallersTargetStub");
  __ EnterStubFrame();
  __ PushRegistersInOrder({A0, A1});
  __ CallRuntime(kFixCallersTargetRuntimeEntry, /*argument_count=*/2);
  __ Drop(2);
  __ LeaveStubFrame();
  __ ret();
}

void StubCodeCompiler::GenerateFixParameterizedAllocationStubTargetStub() {
  // Load code pointer to this stub from the thread:
  // The one that is passed in, is not correct - it points to the code object
  // that needs to be replaced.
  __ LoadFromOffset(CODE_REG, THR,
                    target::Thread::fix_allocation_stub_code_offset());
  __ EnterStubFrame();
  // Preserve type arguments register.
  __ PushRegister(AllocateObjectABI::kTypeArgumentsReg);
  // Setup space on stack for return value.
  __ PushRegister(ZR);
  __ CallRuntime(kFixAllocationStubTargetRuntimeEntry, 0);
  // Get Code object result.
  __ PopRegister(CODE_REG);
  // Restore type arguments register.
  __ PopRegister(AllocateObjectABI::kTypeArgumentsReg);
  // Remove the stub frame.
  __ LeaveStubFrame();
  // Jump to the dart function.
  __ LoadFieldFromOffset(TMP, CODE_REG, target::Code::entry_point_offset());
  __ jr(TMP);
}

void StubCodeCompiler::GenerateICCallBreakpointStub() {
#if defined(PRODUCT)
  __ Stop("No debugging in PRODUCT mode");
#else
  __ EnterStubFrame();
  __ PushRegister(A0);  // Preserve receiver.
  __ PushRegister(S5);  // Preserve IC data.
  __ PushRegister(ZR);  // Space for result.
  __ CallRuntime(kBreakpointRuntimeHandlerRuntimeEntry, 0);
  __ PopRegister(CODE_REG);  // Original stub.
  __ PopRegister(S5);        // Restore IC data.
  __ PopRegister(A0);        // Restore receiver.
  __ LeaveStubFrame();
  __ LoadFieldFromOffset(TMP, CODE_REG, target::Code::entry_point_offset());
  __ jr(TMP);
#endif
}

void StubCodeCompiler::GenerateICCallThroughCodeStub() {
  Label loop, found, miss;
  __ LoadFromOffset(T1, IC_DATA_REG,
                    target::ICData::entries_offset() - kHeapObjectTag);
  __ LoadFromOffset(ARGS_DESC_REG, IC_DATA_REG,
                    target::CallSiteData::arguments_descriptor_offset() -
                        kHeapObjectTag);
  __ AddImmediate(T1, target::Array::data_offset() - kHeapObjectTag);
  // T1: first IC entry
  __ LoadTaggedClassIdMayBeSmi(A1, A0);
  // A1: receiver cid as Smi

  __ Bind(&loop);
  __ LoadCompressedSmi(TMP2, Address(T1, 0));
  __ beq(A1, TMP2, &found);
  __ CompareImmediate(TMP2, target::ToRawSmi(kIllegalCid));
  __ BranchIf(EQ, &miss);

  const intptr_t entry_length =
      target::ICData::TestEntryLengthFor(1, /*exactness_check=*/false) *
      target::kCompressedWordSize;
  __ AddImmediate(T1, entry_length);  // Next entry.
  __ b(&loop);

  __ Bind(&found);
  if (FLAG_precompiled_mode) {
    const intptr_t entry_offset =
        target::ICData::EntryPointIndexFor(1) * target::kCompressedWordSize;
    __ LoadCompressed(FUNCTION_REG, Address(T1, entry_offset));
    __ LoadFromOffset(A1, FUNCTION_REG,
                      target::Function::entry_point_offset() - kHeapObjectTag);
  } else {
    const intptr_t code_offset =
        target::ICData::CodeIndexFor(1) * target::kCompressedWordSize;
    __ LoadCompressed(CODE_REG, Address(T1, code_offset));
    __ LoadFromOffset(A1, CODE_REG,
                      target::Code::entry_point_offset() - kHeapObjectTag);
  }
  __ jr(A1);

  __ Bind(&miss);
  __ LoadFromOffset(A1, THR,
                    target::Thread::switchable_call_miss_entry_offset());
  __ jr(A1);
}

void StubCodeCompiler::GenerateInterpretCallStub() {
  __ Stop("Not implemented on LoongArch.");
}

void StubCodeCompiler::GenerateInvokeDartCodeStub() {
  __ Comment("InvokeDartCodeStub");

  __ EnterFrame(1 * target::kWordSize);

  // Push code object to PC marker slot.
  __ LoadFromOffset(TMP2, A3, target::Thread::invoke_dart_code_stub_offset());
  __ StoreToOffset(TMP2, SP, 0 * target::kWordSize);

  // TODO(loong64): Consider using only volatile FPU registers in Dart code so we
  // don't need to save the preserved FPU registers here.
  __ PushNativeCalleeSavedRegisters();

  // Initialize callee-saved registers for Dart code execution.
  // The cross-compilation environment can leave undefined values in S5-S8
  // which causes sign-extended pointer corruption in nested Dart calls.
  // We zero them AFTER saving so the C++ return path sees preserved values.
  __ MoveRegister(S5, ZR);
  __ MoveRegister(S6, ZR);
  __ MoveRegister(S7, ZR);
  __ MoveRegister(S8, ZR);

  // Set up THR, which caches the current thread in Dart code.
  if (THR != A3) {
    __ MoveRegister(THR, A3);
  }

  // Refresh pinned registers values (inc. write barrier mask and null object).
  __ RestorePinnedRegisters();

  // Save the current VMTag, top resource and top exit frame info on the stack.
  // StackFrameIterator reads the top exit frame info saved in this frame.
  __ AddImmediate(SP, SP, -4 * target::kWordSize);
  __ LoadFromOffset(TMP, THR, target::Thread::vm_tag_offset());
  __ StoreToOffset(TMP, SP, 3 * target::kWordSize);
  __ LoadFromOffset(TMP, THR, target::Thread::top_resource_offset());
  __ StoreToOffset(ZR, THR, target::Thread::top_resource_offset());
  __ StoreToOffset(TMP, SP, 2 * target::kWordSize);
  __ LoadFromOffset(TMP, THR, target::Thread::exit_through_ffi_offset());
  __ StoreToOffset(ZR, THR, target::Thread::exit_through_ffi_offset());
  __ StoreToOffset(TMP, SP, 1 * target::kWordSize);
  __ LoadFromOffset(TMP, THR, target::Thread::top_exit_frame_info_offset());
  __ StoreToOffset(ZR, THR, target::Thread::top_exit_frame_info_offset());
  __ StoreToOffset(TMP, SP, 0 * target::kWordSize);
  // target::frame_layout.exit_link_slot_from_entry_fp must be kept in sync
  // with the code below.
  ASSERT_EQUAL(target::frame_layout.exit_link_slot_from_entry_fp, -24);
  // In debug mode, verify that we've pushed the top exit frame info at the
  // correct offset from FP.
  __ EmitEntryFrameVerification();

  // Mark that the thread is executing Dart code. Do this after initializing the
  // exit link for the profiler.
  __ LoadImmediate(TMP, VMTag::kDartTagId);
  __ StoreToOffset(TMP, THR, target::Thread::vm_tag_offset());

  // Load arguments descriptor array, which is passed to Dart code.
  __ MoveRegister(ARGS_DESC_REG, A1);

  // Load number of arguments into T5 and adjust count for type arguments.
  __ LoadFieldFromOffset(T5, ARGS_DESC_REG,
                         target::ArgumentsDescriptor::count_offset());
  __ LoadFieldFromOffset(T3, ARGS_DESC_REG,
                         target::ArgumentsDescriptor::type_args_len_offset());
  __ SmiUntag(T5);
  // Include the type arguments.
  __ sltu(T3, ZR, T3);  // T3 <- (T3 != 0) ? 1 : 0
  __ add_d(T5, T5, T3);

  // Compute address of 'arguments array' data area into A2.
  __ AddImmediate(A2, A2, target::Array::data_offset() - kHeapObjectTag);

  // Set up arguments for the Dart call.
  Label push_arguments;
  Label done_push_arguments;
  __ BranchIfZero(T5, &done_push_arguments);  // check if there are arguments.
  __ LoadImmediate(S8, 0);
  __ Bind(&push_arguments);
  __ LoadFromOffset(T3, A2, 0);
  __ PushRegister(T3);
  __ addi_d(S8, S8, 1);
  __ AddImmediate(A2, A2, target::kWordSize);
  __ CompareRegisters(S8, T5);
  __ BranchIf(LT, &push_arguments, compiler::Assembler::kNearJump);
  __ Bind(&done_push_arguments);

  if (FLAG_precompiled_mode) {
    __ SetupGlobalPoolAndDispatchTable();
    __ MoveRegister(CODE_REG, ZR);  // GC-safe value into CODE_REG.
  } else {
    // We now load the pool pointer(PP) with a GC safe value as we are about to
    // invoke dart code. We don't need a real object pool here.
    __ LoadImmediate(PP, 1);  // PP is untagged, callee will tag and spill PP.
    __ MoveRegister(CODE_REG, A0);
    __ LoadFieldFromOffset(A0, CODE_REG, target::Code::entry_point_offset());
  }

  // Diagnostic: check if A0 is 1 (kHeapObjectTag) which indicates a bug
  Label diag_ok;
  __ CompareImmediate(A0, 1);
  __ BranchIf(NE, &diag_ok);
  __ break_(0);
  __ Bind(&diag_ok);
  // Call the Dart code entrypoint.
  __ Call(A0);  // ARGS_DESC_REG is the arguments descriptor array.
  __ Comment("InvokeDartCodeStub return");

  // Get rid of arguments pushed on the stack.
  __ AddImmediate(
      SP, FP,
      target::frame_layout.exit_link_slot_from_entry_fp * target::kWordSize);

  // Restore the current VMTag, the saved top exit frame info and top resource
  // back into the Thread structure.
  __ LoadFromOffset(TMP, SP, 0 * target::kWordSize);
  __ StoreToOffset(TMP, THR, target::Thread::top_exit_frame_info_offset());
  __ LoadFromOffset(TMP, SP, 1 * target::kWordSize);
  __ StoreToOffset(TMP, THR, target::Thread::exit_through_ffi_offset());
  __ LoadFromOffset(TMP, SP, 2 * target::kWordSize);
  __ StoreToOffset(TMP, THR, target::Thread::top_resource_offset());
  __ LoadFromOffset(TMP, SP, 3 * target::kWordSize);
  __ StoreToOffset(TMP, THR, target::Thread::vm_tag_offset());
  __ AddImmediate(SP, SP, 4 * target::kWordSize);

  __ PopNativeCalleeSavedRegisters();

  // Restore the frame pointer and C stack pointer and return.
  __ LeaveFrame();
  __ ret();
}

void StubCodeCompiler::GenerateInvokeDartCodeFromBytecodeStub() {
  __ Stop("Not implemented on LoongArch.");
}

void StubCodeCompiler::GenerateJumpToFrameStub() {
  ASSERT(kExceptionObjectReg == A0);
  ASSERT(kStackTraceObjectReg == A1);
  __ MoveRegister(THR, A3);
  __ MoveRegister(CALLEE_SAVED_TEMP, A0);  // Program counter.
  __ MoveRegister(SP, A1);                 // Stack pointer.
  __ MoveRegister(FP, A2);                 // Frame_pointer.
  // Refresh pinned registers values (inc. write barrier mask and null object).
  __ RestorePinnedRegisters();
  // Set the tag.
  __ LoadImmediate(TMP, VMTag::kDartTagId);
  __ StoreToOffset(TMP, THR, target::Thread::vm_tag_offset());
  // Clear top exit frame.
  __ StoreToOffset(ZR, THR, target::Thread::top_exit_frame_info_offset());
  // Restore the pool pointer.
  __ RestoreCodePointer();
  if (FLAG_precompiled_mode) {
    __ SetupGlobalPoolAndDispatchTable();
  } else {
    __ LoadPoolPointer();
  }
  // Jump to continuation point (exception handler).
  __ jr(CALLEE_SAVED_TEMP);
}

void StubCodeCompiler::GenerateLazyCompileStub() {
  __ EnterStubFrame();
  // Save arguments descriptor and pass function.
  __ PushRegistersInOrder({ARGS_DESC_REG, FUNCTION_REG});
  __ CallRuntime(kCompileFunctionRuntimeEntry, 1);
  __ PopRegister(FUNCTION_REG);   // Restore function.
  __ PopRegister(ARGS_DESC_REG);  // Restore arg desc.
  __ LeaveStubFrame();
  __ LoadCompressedFieldFromOffset(CODE_REG, FUNCTION_REG,
                                   target::Function::code_offset());
  __ LoadFieldFromOffset(TMP, FUNCTION_REG,
                         target::Function::entry_point_offset());
  __ jr(TMP);
}

void StubCodeCompiler::GenerateMegamorphicCallStub() {
  GenerateNArgsCheckInlineCacheStub(
      1, kInlineCacheMissHandlerOneArgRuntimeEntry, Token::kILLEGAL,
      kUnoptimized, kInstanceCall, kIgnoreExactness);
}

void StubCodeCompiler::GenerateMonomorphicSmiableCheckStub() {
  Label miss;
  __ LoadClassIdMayBeSmi(T1, A0);
  // Note: this stub is only used in AOT mode, hence the direct (bare) call.
  __ LoadFieldFromOffset(
      S8, S5, target::MonomorphicSmiableCall::expected_cid_offset());
  __ LoadFieldFromOffset(
      TMP, S5, target::MonomorphicSmiableCall::entrypoint_offset());
  __ CompareRegisters(T1, S8);
  __ BranchIf(NOT_EQUAL, &miss);
  __ jr(TMP);
  __ Bind(&miss);
  __ LoadFromOffset(TMP, THR, target::Thread::switchable_call_miss_entry_offset());
  __ jr(TMP);
}

static void GenerateNoSuchMethodDispatcherBody(Assembler* assembler) {
  __ EnterStubFrame();

  __ LoadFromOffset(ARGS_DESC_REG,
                    IC_DATA_REG,
                    target::CallSiteData::arguments_descriptor_offset());

  // Load the receiver.
  __ LoadCompressedSmiFieldFromOffset(
      S8, ARGS_DESC_REG, target::ArgumentsDescriptor::size_offset());
  // S8 is Smi: compute FP + (S8 >> 1) * kWordSize
  __ slli_d(TMP, S8, target::kWordSizeLog2 - 1);
  __ add_d(TMP, TMP, FP);
  __ LoadFromOffset(A0, TMP,
                    target::frame_layout.param_end_from_fp * target::kWordSize);

  // Push: result slot, receiver, ICData/MegamorphicCache, args descriptor.
  __ PushRegister(ZR);  // Result slot.
  __ PushRegister(A0);
  __ PushRegister(IC_DATA_REG);
  __ PushRegister(ARGS_DESC_REG);

  // Adjust arguments count.
  __ LoadCompressedSmiFieldFromOffset(
      T3, ARGS_DESC_REG, target::ArgumentsDescriptor::type_args_len_offset());
  Label args_count_ok;
  __ BranchIfZero(T3, &args_count_ok);
  // Include the type arguments.
  __ AddImmediate(S8, S8, target::ToRawSmi(1));
  __ Bind(&args_count_ok);

  // S8: Smi-tagged arguments array length.
  PushArrayOfArguments(assembler);
  const intptr_t kNumArgs = 4;
  __ CallRuntime(kNoSuchMethodFromCallStubRuntimeEntry, kNumArgs);
  __ Drop(4);
  __ PopRegister(A0);  // Return value.
  __ LeaveStubFrame();
  __ ret();
}

void StubCodeCompiler::GenerateNoSuchMethodDispatcherStub() {
  GenerateNoSuchMethodDispatcherBody(assembler);
}

void StubCodeCompiler::GenerateOneArgCheckInlineCacheStub() {
  GenerateNArgsCheckInlineCacheStub(
      1, kInlineCacheMissHandlerOneArgRuntimeEntry, Token::kILLEGAL,
      kUnoptimized, kInstanceCall, kIgnoreExactness);
}

void StubCodeCompiler::GenerateOneArgCheckInlineCacheWithExactnessCheckStub() {
  GenerateNArgsCheckInlineCacheStub(
      1, kInlineCacheMissHandlerOneArgRuntimeEntry, Token::kILLEGAL,
      kUnoptimized, kInstanceCall, kCheckExactness);
}

void StubCodeCompiler::GenerateOneArgOptimizedCheckInlineCacheStub() {
  GenerateNArgsCheckInlineCacheStub(
      1, kInlineCacheMissHandlerOneArgRuntimeEntry, Token::kILLEGAL, kOptimized,
      kInstanceCall, kIgnoreExactness);
}

void StubCodeCompiler::GenerateOneArgOptimizedCheckInlineCacheWithExactnessCheckStub() {
  GenerateNArgsCheckInlineCacheStub(
      1, kInlineCacheMissHandlerOneArgRuntimeEntry, Token::kILLEGAL, kOptimized,
      kInstanceCall, kCheckExactness);
}

void StubCodeCompiler::GenerateOneArgUnoptimizedStaticCallStub() {
  GenerateUsageCounterIncrement(/* scratch */ T0);
  GenerateNArgsCheckInlineCacheStub(1, kStaticCallMissHandlerOneArgRuntimeEntry,
                                    Token::kILLEGAL, kUnoptimized, kStaticCall,
                                    kIgnoreExactness);
}


// Left: A0, Right: A1 (not guaranteed to be preserved)
static void GenerateIdenticalWithNumberCheckStub(Assembler* assembler,
                                                 const Register left,
                                                 const Register right) {
  Label reference_compare, check_mint, done;
  __ BranchIfSmi(left, &reference_compare, Assembler::kNearJump);
  __ BranchIfSmi(right, &reference_compare, Assembler::kNearJump);

  // Value compare for two doubles.
  __ CompareClassId(left, kDoubleCid, /*scratch*/ TMP);
  __ BranchIf(NOT_EQUAL, &check_mint, Assembler::kNearJump);
  __ CompareClassId(right, kDoubleCid, /*scratch*/ TMP);
  __ BranchIf(NOT_EQUAL, &reference_compare, Assembler::kNearJump);

  // Double values bitwise compare (64-bit).
  __ LoadFromOffset(T0, left, target::Double::value_offset());
  __ LoadFromOffset(T1, right, target::Double::value_offset());
  __ xor_l(TMP, T0, T1);
  __ Jump(&done, Assembler::kNearJump);

  __ Bind(&check_mint);
  __ CompareClassId(left, kMintCid, /*scratch*/ TMP);
  __ BranchIf(NOT_EQUAL, &reference_compare, Assembler::kNearJump);
  __ CompareClassId(right, kMintCid, /*scratch*/ TMP);
  __ BranchIf(NOT_EQUAL, &reference_compare, Assembler::kNearJump);
  __ LoadFromOffset(T0, left, target::Mint::value_offset());
  __ LoadFromOffset(T1, right, target::Mint::value_offset());
  __ xor_l(TMP, T0, T1);
  __ Jump(&done, Assembler::kNearJump);

  __ Bind(&reference_compare);
  __ CompareRegisters(left, right);

  __ Bind(&done);
}

void StubCodeCompiler::GenerateOptimizedIdenticalWithNumberCheckStub() {
  GenerateIdenticalWithNumberCheckStub(assembler, A0, A0);
}

void StubCodeCompiler::GenerateOptimizeFunctionStub() {
  __ Comment("OptimizeFunctionStub");
  __ EnterStubFrame();
  __ PushRegister(A0);
  __ CallRuntime(kOptimizeInvokedFunctionRuntimeEntry, /*argument_count=*/1);
  __ Drop(1);
  __ LeaveStubFrame();
  __ ret();
}

static void GenerateRunExceptionHandler(Assembler* assembler,
                                        bool unbox_exception) {
  // Exception object.
  ASSERT(kExceptionObjectReg == A0);
  __ LoadFromOffset(A0, THR, target::Thread::active_exception_offset());
  __ StoreToOffset(NULL_REG, THR, target::Thread::active_exception_offset());
  if (unbox_exception) {
    compiler::Label not_smi, done;
    __ BranchIfNotSmi(A0, &not_smi);
    __ SmiUntag(A0);
    __ Jump(&done);
    __ Bind(&not_smi);
    __ LoadFieldFromOffset(A0, A0, Mint::value_offset());
    __ Bind(&done);
  }

  // StackTrace object.
  ASSERT(kStackTraceObjectReg == A1);
  __ LoadFromOffset(A1, THR, target::Thread::active_stacktrace_offset());
  __ StoreToOffset(NULL_REG, THR, target::Thread::active_stacktrace_offset());

  __ LoadFromOffset(RA, THR, target::Thread::resume_pc_offset());
  __ ret();  // Jump to the exception handler code.
}

void StubCodeCompiler::GenerateRunExceptionHandlerStub() {
  GenerateRunExceptionHandler(assembler, false);
}

void StubCodeCompiler::GenerateRunExceptionHandlerUnboxStub() {
  GenerateRunExceptionHandler(assembler, true);
}

void StubCodeCompiler::GenerateRuntimeCallBreakpointStub() {
#if defined(PRODUCT)
  __ Stop("No debugging in PRODUCT mode");
#else
  __ EnterStubFrame();
  __ PushRegister(ZR);  // Space for result.
  __ CallRuntime(kBreakpointRuntimeHandlerRuntimeEntry, 0);
  __ PopRegister(CODE_REG);
  __ LeaveStubFrame();
  __ LoadFieldFromOffset(TMP, CODE_REG, target::Code::entry_point_offset());
  __ jr(TMP);
#endif  // defined(PRODUCT)
}

void StubCodeCompiler::GenerateSingleTargetCallStub() {
  Label miss;
  __ LoadClassIdMayBeSmi(A1, A0);
  __ LoadFromOffset(TMP2, S5,
          target::SingleTargetCache::lower_limit_offset() - kHeapObjectTag, kUnsignedTwoBytes);
  __ LoadFromOffset(T3, S5,
                    target::SingleTargetCache::upper_limit_offset() - kHeapObjectTag,
                    kUnsignedTwoBytes);

  __ blt(A1, TMP2, &miss);
  __ CompareRegisters(A1, T3);
  __ BranchIf(GREATER, &miss);

  __ LoadFromOffset(TMP, S5,
                    target::SingleTargetCache::entry_point_offset() -
                        kHeapObjectTag);
  __ LoadFromOffset(CODE_REG, S5,
                    target::SingleTargetCache::target_offset() -
                        kHeapObjectTag);
  __ jr(TMP);

  __ Bind(&miss);
  __ LoadFromOffset(TMP, THR,
                    target::Thread::switchable_call_miss_entry_offset());
  __ jr(TMP);
}

void StubCodeCompiler::GenerateSmiAddInlineCacheStub() {
  GenerateNArgsCheckInlineCacheStub(
      2, kInlineCacheMissHandlerTwoArgsRuntimeEntry, Token::kADD, kUnoptimized,
      kInstanceCall, kIgnoreExactness);
}

void StubCodeCompiler::GenerateSmiEqualInlineCacheStub() {
  GenerateNArgsCheckInlineCacheStub(
      2, kInlineCacheMissHandlerTwoArgsRuntimeEntry, Token::kEQ, kUnoptimized,
      kInstanceCall, kIgnoreExactness);
}

void StubCodeCompiler::GenerateSmiLessInlineCacheStub() {
  GenerateNArgsCheckInlineCacheStub(
      2, kInlineCacheMissHandlerTwoArgsRuntimeEntry, Token::kLT, kUnoptimized,
      kInstanceCall, kIgnoreExactness);
}

void StubCodeCompiler::GenerateSwitchableCallMissStub() {
  __ Comment("SwitchableCallMissStub");
  __ EnterStubFrame();
  __ PushRegistersInOrder({A0, A1, A2, A3});
  __ CallRuntime(kSwitchableCallMissRuntimeEntry, /*argument_count=*/4);
  __ Drop(4);
  __ LeaveStubFrame();
  __ ret();
}

void StubCodeCompiler::GenerateTwoArgsCheckInlineCacheStub() {
  GenerateNArgsCheckInlineCacheStub(
      2, kInlineCacheMissHandlerTwoArgsRuntimeEntry, Token::kILLEGAL,
      kUnoptimized, kInstanceCall, kIgnoreExactness);
}

void StubCodeCompiler::GenerateTwoArgsOptimizedCheckInlineCacheStub() {
  GenerateNArgsCheckInlineCacheStub(
      2, kInlineCacheMissHandlerTwoArgsRuntimeEntry, Token::kILLEGAL, kOptimized,
      kInstanceCall, kIgnoreExactness);
}

void StubCodeCompiler::GenerateTwoArgsUnoptimizedStaticCallStub() {
  GenerateUsageCounterIncrement(/* scratch */ T0);
  GenerateNArgsCheckInlineCacheStub(
      2, kStaticCallMissHandlerTwoArgsRuntimeEntry, Token::kILLEGAL,
      kUnoptimized, kStaticCall, kIgnoreExactness);
}

void StubCodeCompiler::GenerateUnoptimizedIdenticalWithNumberCheckStub() {
  GenerateIdenticalWithNumberCheckStub(assembler, A0, A0);
}

void StubCodeCompiler::GenerateUnoptStaticCallBreakpointStub() {
#if defined(PRODUCT)
  __ Stop("No debugging in PRODUCT mode");
#else
  __ EnterStubFrame();
  __ PushRegister(S5);  // Preserve IC data.
  __ PushRegister(ZR);  // Space for result.
  __ CallRuntime(kBreakpointRuntimeHandlerRuntimeEntry, 0);
  __ PopRegister(CODE_REG);  // Original stub.
  __ PopRegister(S5);        // Restore IC data.
  __ LeaveStubFrame();
  __ LoadFieldFromOffset(TMP, CODE_REG, target::Code::entry_point_offset());
  __ jr(TMP);
#endif  // defined(PRODUCT)
}

void StubCodeCompiler::GenerateWriteBarrierStub() {
  // Write barrier stub for generational GC.
  // Called with object in kWriteBarrierObjectReg (A0) and
  // value in kWriteBarrierValueReg (A1).
  // Slot is in kWriteBarrierSlotReg (A6).
  __ Comment("WriteBarrierStub");
  Label done_label_;

  // Check if value is a Smi (no barrier needed for Smis).
  __ andi(TMP, kWriteBarrierValueReg, kSmiTagMask);
  __ bnez(TMP, &done_label_);

  // Check if the object is in new space (no barrier needed).
  __ andi(TMP, kWriteBarrierObjectReg,
          target::ObjectAlignment::kNewObjectAlignmentOffset);
  __ bnez(TMP, &done_label_);

  // Remembered set update.
  __ LoadFromOffset(TMP, THR, target::Thread::store_buffer_block_offset());
  __ LoadFromOffset(TMP2, TMP,
                    target::StoreBufferBlock::top_offset(),
                    kUnsignedFourBytes);
  __ LoadImmediate(T1, target::StoreBufferBlock::kSize);
  __ CompareRegisters(TMP2, T1);
  Label call_runtime;
  __ BranchIf(EQUAL, &call_runtime);

  // Store object pointer into the store buffer.
  __ slli_d(T1, TMP2, target::kWordSizeLog2);
  __ add_d(T1, TMP, T1);
  __ st_d(kWriteBarrierSlotReg, T1,
          target::StoreBufferBlock::pointers_offset());

  // Increment top.
  __ addi_d(TMP2, TMP2, 1);
  __ StoreToOffset(TMP2, TMP,
                   target::StoreBufferBlock::top_offset(),
                   kUnsignedFourBytes);
  __ b(&done_label_);

  __ Bind(&call_runtime);
  {
    LeafRuntimeScope rt(assembler, /*frame_size=*/0,
                        /*preserve_registers=*/false);
    __ MoveRegister(A0, THR);
    rt.Call(kStoreBufferBlockProcessRuntimeEntry, /*argument_count=*/1);
  }

  __ Bind(&done_label_);
  __ ret();
}

void StubCodeCompiler::GenerateWriteBarrierWrappersStub() {
  for (intptr_t i = 0; i < kNumberOfCpuRegisters; ++i) {
    if ((kDartAvailableCpuRegs & (1 << i)) == 0) continue;

    Register reg = static_cast<Register>(i);
    intptr_t start = __ CodeSize();
    __ AddImmediate(SP, SP, -3 * target::kWordSize);
    __ StoreToOffset(RA, SP, 2 * target::kWordSize);
    __ StoreToOffset(TMP, SP, 1 * target::kWordSize);
    __ StoreToOffset(kWriteBarrierObjectReg, SP, 0 * target::kWordSize);
    __ MoveRegister(kWriteBarrierObjectReg, reg);
    __ Call(Address(THR, target::Thread::write_barrier_entry_point_offset()));
    __ LoadFromOffset(kWriteBarrierObjectReg, SP, 0 * target::kWordSize);
    __ LoadFromOffset(TMP, SP, 1 * target::kWordSize);
    __ LoadFromOffset(RA, SP, 2 * target::kWordSize);
    __ AddImmediate(SP, SP, 3 * target::kWordSize);
    __ jr(TMP);
    intptr_t end = __ CodeSize();
    ASSERT_EQUAL(end - start, kStoreBufferWrapperSize);
  }
}

// Record the entry point of an unoptimized static call.
static void GenerateRecordEntryPoint(Assembler* assembler) {
  Label done;
  __ LoadImmediate(T3, target::Function::entry_point_offset() - kHeapObjectTag);
  __ Jump(&done, Assembler::kNearJump);
  __ BindUncheckedEntryPoint();
  __ LoadImmediate(T3, target::Function::entry_point_offset(CodeEntryKind::kUnchecked) - kHeapObjectTag);
  __ Bind(&done);
}

// S5: ICData
// RA: return address
void StubCodeCompiler::GenerateZeroArgsUnoptimizedStaticCallStub() {
  GenerateRecordEntryPoint(assembler);
  GenerateUsageCounterIncrement(/* scratch */ T0);

#if defined(DEBUG)
  {
    Label ok;
    // Check that the IC data array has NumArgsTested() == 0.
    // NumArgsTested is stored in the least significant bits of state_bits.
    __ LoadFromOffset(TMP, IC_DATA_REG,
                      target::ICData::state_bits_offset() - kHeapObjectTag,
                      kUnsignedFourBytes);
    ASSERT(target::ICData::NumArgsTestedShift() == 0);
    __ AndImmediate(TMP, TMP, target::ICData::NumArgsTestedMask());
    __ CompareImmediate(TMP, 0);
    __ BranchIf(EQUAL, &ok);
    __ Stop("Incorrect IC data for unoptimized static call");
    __ Bind(&ok);
  }
#endif  // DEBUG

  // Check single stepping.
#if !defined(PRODUCT)
  Label stepping, done_stepping;
  __ LoadFromOffset(TMP, THR, target::Thread::single_step_offset(), kUnsignedByte);
  __ CompareImmediate(TMP, 0);
  __ BranchIf(NOT_EQUAL, &stepping);
  __ Bind(&done_stepping);
#endif

  // S5: IC data object (preserved).
  __ LoadFieldFromOffset(A0, IC_DATA_REG, target::ICData::entries_offset());
  // A0: ic_data_array with entries: target functions and count.
  __ AddImmediate(A0, target::Array::data_offset() - kHeapObjectTag);
  // A0: points directly to the first ic data array element.
  const intptr_t target_offset =
      target::ICData::TargetIndexFor(0) * target::kCompressedWordSize;
  const intptr_t count_offset =
      target::ICData::CountIndexFor(0) * target::kCompressedWordSize;

  if (FLAG_optimization_counter_threshold >= 0) {
    // Increment count for this call, ignore overflow.
    __ LoadCompressedSmi(TMP, Address(A0, count_offset));
    __ AddImmediate(TMP, target::ToRawSmi(1));
    __ StoreToOffset(TMP, A0, count_offset);
  }

  // Load arguments descriptor into S4.
  __ LoadFieldFromOffset(ARGS_DESC_REG, IC_DATA_REG,
                         target::CallSiteData::arguments_descriptor_offset());

  // Get function and call it, if possible.
  __ LoadCompressedFromOffset(FUNCTION_REG, A0, target_offset);
  __ LoadCompressedFieldFromOffset(CODE_REG, FUNCTION_REG,
                                   target::Function::code_offset());
  __ add_d(A0, FUNCTION_REG, T3);
  __ LoadFromOffset(TMP, A0, 0);
  __ jr(TMP);

#if !defined(PRODUCT)
  __ Bind(&stepping);
  __ EnterStubFrame();
  __ PushRegister(IC_DATA_REG);  // Preserve IC data.
  __ CallRuntime(kSingleStepHandlerRuntimeEntry, 0);
  __ PopRegister(IC_DATA_REG);
  __ RestoreCodePointer();
  __ LeaveStubFrame();
  __ Ret();
#endif
}

void StubCodeCompiler::EnsureIsNewOrRemembered() {
  Label done;
  __ AndImmediate(TMP, A0, target::Page::kPageMask);
  __ LoadFromOffset(TMP, TMP, target::Page::original_top_offset());
  __ CompareRegisters(A0, TMP);
  __ BranchIf(UNSIGNED_GREATER_EQUAL, &done);
  {
    LeafRuntimeScope rt(assembler, /*frame_size=*/0,
                        /*preserve_registers=*/false);
    __ MoveRegister(A1, THR);
    rt.Call(kEnsureRememberedAndMarkingDeferredRuntimeEntry,
            /*argument_count=*/2);
  }
  __ Bind(&done);
}


void StubCodeCompiler::GenerateAllocationStubForClass(
    UnresolvedPcRelativeCalls* unresolved_calls,
    const Class& cls,
    const dart::Code& allocate_object,
    const dart::Code& allocat_object_parametrized) {
  classid_t cls_id = target::Class::GetId(cls);
  ASSERT(cls_id != kIllegalCid);
  const bool is_cls_parameterized = target::Class::NumTypeArguments(cls) > 0;
  ASSERT(!is_cls_parameterized || target::Class::TypeArgumentsFieldOffset(
                                      cls) != target::Class::kNoTypeArguments);
  const intptr_t instance_size = target::Class::GetInstanceSize(cls);
  ASSERT(instance_size > 0);
  const uword tags =
      target::MakeTagWordForNewSpaceObject(cls_id, instance_size);
  const Register kTagsReg = AllocateObjectABI::kTagsReg;
  ASSERT(kTagsReg != AllocateObjectABI::kTypeArgumentsReg);
  __ LoadImmediate(kTagsReg, tags);
  if (!FLAG_use_slow_path && FLAG_inline_alloc &&
      !target::Class::TraceAllocation(cls) &&
      target::SizeFitsInSizeTag(instance_size)) {
    RELEASE_ASSERT(AllocateObjectInstr::WillAllocateNewOrRemembered(cls));
    RELEASE_ASSERT(target::Heap::IsAllocatableInNewSpace(instance_size));
    if (is_cls_parameterized) {
      if (!IsSameObject(NullObject(),
                        CastHandle<Object>(allocat_object_parametrized))) {
        __ GenerateUnRelocatedPcRelativeTailCall();
        unresolved_calls->Add(new UnresolvedPcRelativeCall(
            __ CodeSize(), allocat_object_parametrized, /*is_tail_call=*/true));
      } else {
        __ ld_d(TMP, THR,
                target::Thread::
                    allocate_object_parameterized_entry_point_offset());
        __ jr(TMP);
      }
    } else {
      if (!IsSameObject(NullObject(), CastHandle<Object>(allocate_object))) {
        __ GenerateUnRelocatedPcRelativeTailCall();
        unresolved_calls->Add(new UnresolvedPcRelativeCall(
            __ CodeSize(), allocate_object, /*is_tail_call=*/true));
      } else {
        __ ld_d(TMP, THR, target::Thread::allocate_object_entry_point_offset());
        __ jr(TMP);
      }
    }
  } else {
    if (!is_cls_parameterized) {
      __ LoadObject(AllocateObjectABI::kTypeArgumentsReg, NullObject());
    }
    __ ld_d(TMP, THR,
            target::Thread::allocate_object_slow_entry_point_offset());
    __ jr(TMP);
  }
}

void StubCodeCompiler::GenerateNArgsCheckInlineCacheStub(
    intptr_t num_args,
    const RuntimeEntry& handle_ic_miss,
    Token::Kind kind,
    Optimized optimized,
    CallType type,
    Exactness exactness) {
  __ Breakpoint();
}

void StubCodeCompiler::GenerateNArgsCheckInlineCacheStubForEntryKind(
    intptr_t num_args,
    const RuntimeEntry& handle_ic_miss,
    Token::Kind kind,
    Optimized optimized,
    CallType type,
    Exactness exactness,
    CodeEntryKind entry_kind) {
  if (FLAG_precompiled_mode) {
    __ Breakpoint();
    return;
  }
  GenerateNArgsCheckInlineCacheStub(num_args, handle_ic_miss, kind, optimized,
                                    type, exactness);
}


void StubCodeCompiler::GenerateUsageCounterIncrement(Register temp_reg) {
  if (FLAG_precompiled_mode) {
    __ Breakpoint();
    return;
  }
  if (FLAG_optimization_counter_threshold >= 0) {
    Register func_reg = temp_reg;
    __ Comment("Increment function counter");
    __ LoadFieldFromOffset(func_reg, IC_DATA_REG,
                           target::ICData::owner_offset());
    __ LoadFieldFromOffset(TMP, func_reg,
                           target::Function::usage_counter_offset(),
                           kFourBytes);
    __ addi_d(TMP, TMP, 1);
    __ StoreFieldToOffset(TMP, func_reg,
                          target::Function::usage_counter_offset(),
                          kFourBytes);
  }
}

void StubCodeCompiler::GenerateOptimizedUsageCounterIncrement() {
  if (FLAG_precompiled_mode) {
    __ Breakpoint();
    return;
  }
  __ LoadFieldFromOffset(TMP, A6, target::Function::usage_counter_offset(),
                         kFourBytes);
  __ addi_d(TMP, TMP, 1);
  __ StoreFieldToOffset(TMP, A6, target::Function::usage_counter_offset(),
                        kFourBytes);
}

void StubCodeCompiler::GenerateSubtypeNTestCacheStub(Assembler* assembler, int n) {
  ASSERT(n >= 1);
  ASSERT(n <= SubtypeTestCache::kMaxInputs);
  ASSERT(n != 5);

  const Register kCacheArrayReg = TypeTestABI::kSubtypeTestCacheResultReg;

  GenerateSubtypeTestCacheSearch(
      assembler, n, NULL_REG, kCacheArrayReg,
      STCInternalRegs::kInstanceCidOrSignatureReg,
      STCInternalRegs::kInstanceInstantiatorTypeArgumentsReg,
      STCInternalRegs::kInstanceParentFunctionTypeArgumentsReg,
      STCInternalRegs::kInstanceDelayedFunctionTypeArgumentsReg,
      STCInternalRegs::kCacheEntriesEndReg,
      STCInternalRegs::kCacheContentsSizeReg,
      STCInternalRegs::kProbeDistanceReg,
      [&](Assembler* assembler, int n) {
        __ LoadCompressed(
            TypeTestABI::kSubtypeTestCacheResultReg,
            Address(kCacheArrayReg, target::kCompressedWordSize *
                                        target::SubtypeTestCache::kTestResult));
        __ ret();
      },
      [&](Assembler* assembler, int n) {
        __ MoveRegister(TypeTestABI::kSubtypeTestCacheResultReg, NULL_REG);
        __ ret();
      });
}

static int GetScaleFactor(intptr_t size) {
  switch (size) {
    case 1: return 0;
    case 2: return 1;
    case 4: return 2;
    case 8: return 3;
    case 16: return 4;
    default: UNREACHABLE(); return -1;
  }
}

void StubCodeCompiler::GenerateAllocateTypedDataArrayStub(intptr_t cid) {
  const intptr_t element_size = TypedDataElementSizeInBytes(cid);
  const intptr_t max_len = TypedDataMaxNewSpaceElements(cid);
  const intptr_t scale_shift = GetScaleFactor(element_size);
  COMPILE_ASSERT(AllocateTypedDataArrayABI::kLengthReg == S8);
  COMPILE_ASSERT(AllocateTypedDataArrayABI::kResultReg == A0);
  if (!FLAG_use_slow_path && FLAG_inline_alloc) {
    Label call_runtime;
    NOT_IN_PRODUCT(__ MaybeTraceAllocation(cid, &call_runtime, T3));
    __ MoveRegister(T3, AllocateTypedDataArrayABI::kLengthReg);
    __ BranchIfNotSmi(T3, &call_runtime);
    __ SmiUntag(T3);
    __ CompareImmediate(T3, max_len, kObjectBytes);
    __ BranchIf(UNSIGNED_GREATER, &call_runtime);
    if (scale_shift != 0) {
      __ slli_d(T3, T3, scale_shift);
    }
    const intptr_t fixed_size_plus_alignment_padding =
        target::TypedData::HeaderSize() +
        target::ObjectAlignment::kObjectAlignment - 1;
    __ AddImmediate(T3, fixed_size_plus_alignment_padding);
    __ AndImmediate(T3, ~(target::ObjectAlignment::kObjectAlignment - 1));
    __ ld_d(A0, THR, target::Thread::top_offset());
    __ add_d(T4, A0, T3);
    __ bltu(T4, A0, &call_runtime);
    __ ld_d(TMP, THR, target::Thread::end_offset());
    __ bgeu(T4, TMP, &call_runtime);
    __ CheckAllocationCanary(A0);
    __ st_d(T4, THR, target::Thread::top_offset());
    __ AddImmediate(A0, kHeapObjectTag);
    {
      __ LoadImmediate(T5, 0);
      __ CompareImmediate(T3, target::UntaggedObject::kSizeTagMaxSizeTag);
      Label zero_tags;
      __ BranchIf(HI, &zero_tags);
      __ slli_d(T5, T3,
                target::UntaggedObject::kSizeTagPos -
                    target::ObjectAlignment::kObjectAlignmentLog2);
      __ Bind(&zero_tags);
      uword tags =
          target::MakeTagWordForNewSpaceObject(cid, /*instance_size=*/0);
      __ OrImmediate(T5, T5, tags);
      __ InitializeHeader(T5, A0);
    }
    __ MoveRegister(T3, AllocateTypedDataArrayABI::kLengthReg);
    __ StoreToOffset(T3, A0,
                     target::TypedDataBase::length_offset() - kHeapObjectTag,
                     kObjectBytes);
    __ ret();
    __ Bind(&call_runtime);
  }
  __ EnterStubFrame();
  __ PushRegister(AllocateTypedDataArrayABI::kLengthReg);
  __ CallRuntime(kAllocateTypedDataRuntimeEntry, /*argument_count=*/1);
  __ PopRegister(AllocateTypedDataArrayABI::kResultReg);
  __ LeaveStubFrame();
  __ ret();
}

void StubCodeCompiler::GenerateSharedStubGeneric(
    bool save_fpu_registers,
    intptr_t self_code_stub_offset_from_thread,
    bool allow_return,
    std::function<void()> perform_runtime_call) {
  RegisterSet all_registers;
  all_registers.AddAllNonReservedRegisters(save_fpu_registers);
  __ PushRegister(RA);
  __ PushRegisters(all_registers);
  __ ld_d(CODE_REG, THR, self_code_stub_offset_from_thread);
  __ EnterStubFrame();
  perform_runtime_call();
  if (!allow_return) {
    __ Breakpoint();
    return;
  }
  __ LeaveStubFrame();
  __ PopRegisters(all_registers);
  __ Drop(1);
  __ ret();
}

void StubCodeCompiler::GenerateSharedStub(
    bool save_fpu_registers,
    const RuntimeEntry* target,
    intptr_t self_code_stub_offset_from_thread,
    bool allow_return,
    bool store_runtime_result_in_result_register) {
  ASSERT(!store_runtime_result_in_result_register || allow_return);
  auto perform_runtime_call = [&]() {
    if (store_runtime_result_in_result_register) {
      __ PushRegister(NULL_REG);
    }
    __ CallRuntime(*target, /*argument_count=*/0);
    if (store_runtime_result_in_result_register) {
      __ PopRegister(A0);
      __ st_d(A0, FP, target::kWordSize *
                        StubCodeCompiler::WordOffsetFromFpToCpuRegister(
                            SharedSlowPathStubABI::kResultReg));
    }
  };
  GenerateSharedStubGeneric(save_fpu_registers,
                            self_code_stub_offset_from_thread, allow_return,
                            perform_runtime_call);
}

void StubCodeCompiler::GenerateRangeError(bool with_fpu_regs) {
  auto perform_runtime_call = [&]() {
    __ CallRuntime(kRangeErrorRuntimeEntry, /*argument_count=*/2);
    __ Breakpoint();
  };
  GenerateSharedStubGeneric(
      /*save_fpu_registers=*/with_fpu_regs,
      with_fpu_regs
          ? target::Thread::range_error_shared_with_fpu_regs_stub_offset()
          : target::Thread::range_error_shared_without_fpu_regs_stub_offset(),
      /*allow_return=*/false, perform_runtime_call);
}

void StubCodeCompiler::GenerateWriteError(bool with_fpu_regs) {
  auto perform_runtime_call = [&]() {
    __ CallRuntime(kWriteErrorRuntimeEntry, /*argument_count=*/2);
    __ Breakpoint();
  };
  GenerateSharedStubGeneric(
      /*save_fpu_registers=*/with_fpu_regs,
      with_fpu_regs
          ? target::Thread::write_error_shared_with_fpu_regs_stub_offset()
          : target::Thread::write_error_shared_without_fpu_regs_stub_offset(),
      /*allow_return=*/false, perform_runtime_call);
}

void StubCodeCompiler::GenerateLoadFfiCallbackMetadataRuntimeFunction(
    uword function_index,
    Register dst) {
  // Keep in sync with FfiCallbackMetadata::EnsureFirstTrampolinePageLocked.
  // Note: If the stub was aligned, this could be a single PC relative load.

  // Load a pointer to the beginning of the stub into dst.
  const intptr_t code_size = __ CodeSize();
  __ pcaddu12i(dst, 0);
  __ AddImmediate(dst, -code_size);

  // Round dst down to the page size.
  __ AndImmediate(dst, FfiCallbackMetadata::kPageMask);

  // Load the function from the function table.
  __ LoadFromOffset(dst, dst,
                    FfiCallbackMetadata::RuntimeFunctionOffset(function_index));
}
}  // namespace compiler
}  // namespace dart

#endif  // defined TARGET_ARCH_LOONG64
