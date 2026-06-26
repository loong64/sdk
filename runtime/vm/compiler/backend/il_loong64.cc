// Copyright (c) 2021, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(TARGET_ARCH_LOONG64)

#include "vm/compiler/backend/il.h"

#include "platform/memory_sanitizer.h"
#include "vm/compiler/backend/flow_graph.h"
#include "vm/compiler/backend/flow_graph_compiler.h"
#include "vm/compiler/backend/locations.h"
#include "vm/compiler/backend/locations_helpers.h"
#include "vm/compiler/backend/range_analysis.h"
#include "vm/compiler/ffi/native_calling_convention.h"
#include "vm/compiler/jit/compiler.h"
#include "vm/dart_entry.h"
#include "vm/instructions.h"
#include "vm/object_store.h"
#include "vm/parser.h"
#include "vm/simulator.h"
#include "vm/stack_frame.h"
#include "vm/stub_code.h"
#include "vm/symbols.h"
#include "vm/type_testing_stubs.h"

#define __ (compiler->assembler())->
#define Z (compiler->zone())

namespace dart {

static void EmitBranchOnCondition(FlowGraphCompiler* compiler, Condition true_condition, BranchLabels labels);
// Generic summary for call instructions.
LocationSummary* Instruction::MakeCallSummary(Zone* zone,
                                              const Instruction* instr,
                                              LocationSummary* locs) {
  ASSERT(locs == nullptr || locs->always_calls());
  LocationSummary* result =
      ((locs == nullptr)
          ? (new (zone) LocationSummary(zone, 0, 0, LocationSummary::kCall))
          : locs);
  const auto representation = instr->representation();
  switch (representation) {
    case kTagged:
    case kUntagged:
    case kUnboxedInt64:
      result->set_out(0, Location::RegisterLocation(CallingConventions::kReturnReg));
      break;
    case kPairOfTagged:
      result->set_out(0, Location::Pair(
          Location::RegisterLocation(CallingConventions::kReturnReg),
          Location::RegisterLocation(CallingConventions::kSecondReturnReg)));
      break;
    case kUnboxedDouble:
      result->set_out(0, Location::FpuRegisterLocation(CallingConventions::kReturnFpuReg));
      break;
    default:
      UNREACHABLE();
      break;
  }
  return result;
}

LocationSummary* LoadIndexedUnsafeInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = ((representation() == kUnboxedDouble) ? 1 : 0);
  LocationSummary* locs = new (zone) LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  locs->set_in(0, Location::RequiresRegister());
  switch (representation()) {
    case kTagged: case kUnboxedInt64: locs->set_out(0, Location::RequiresRegister()); break;
    case kUnboxedDouble: locs->set_temp(0, Location::RequiresRegister()); locs->set_out(0, Location::RequiresFpuRegister()); break;
    default: UNREACHABLE(); break;
  }
  return locs;
}

void LoadIndexedUnsafeInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  ASSERT(RequiredInputRepresentation(0) == kTagged);
  ASSERT(kSmiTag == 0); ASSERT(kSmiTagSize == 1);
  const Register index = locs()->in(0).reg();
  switch (representation()) {
    case kTagged: case kUnboxedInt64: {
      const auto out = locs()->out(0).reg();
      __ slli_d(TMP, index, 2);
      __ add_d(out, base_reg(), TMP);
      __ LoadFromOffset(out, out, offset());
      break;
    }
    case kUnboxedDouble: {
      const auto tmp = locs()->temp(0).reg();
      const auto out = locs()->out(0).fpu_reg();
      __ slli_d(TMP, index, 2);
      __ add_d(tmp, base_reg(), TMP);
      __ LoadDFromOffset(out, tmp, offset());
      break;
    }
    default: UNREACHABLE(); break;
  }
}

DEFINE_BACKEND(StoreIndexedUnsafe, (NoLocation, Register index, Register value)) {
  ASSERT(instr->RequiredInputRepresentation(StoreIndexedUnsafeInstr::kIndexPos) == kTagged);
  __ slli_d(TMP, index, 2);
  __ add_d(TMP, instr->base_reg(), TMP);
  __ StoreToOffset(value, TMP, instr->offset());
  ASSERT(kSmiTag == 0); ASSERT(kSmiTagSize == 1);
}

DEFINE_BACKEND(TailCall,
               (NoLocation,
                Fixed<Register, ARGS_DESC_REG>,
                Temp<Register> temp)) {
  compiler->EmitTailCallToStub(instr->code());
  __ set_constant_pool_allowed(true);
}
LocationSummary* CheckClassInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumInputs = 1;
  const bool need_mask_temp = IsBitTest();
  const intptr_t kNumTemps = !IsNullCheck() ? (need_mask_temp ? 2 : 1) : 0;
  LocationSummary* summary = new (zone) LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresRegister());
  if (!IsNullCheck()) { summary->set_temp(0, Location::RequiresRegister()); if (need_mask_temp) summary->set_temp(1, Location::RequiresRegister()); }
  return summary;
}

void CheckClassInstr::EmitNullCheck(FlowGraphCompiler* compiler, compiler::Label* deopt) {
  __ CompareObject(locs()->in(0).reg(), Object::null_object());
  ASSERT(IsDeoptIfNull() || IsDeoptIfNotNull());
  Condition cond = IsDeoptIfNull() ? EQ : NE;
  __ BranchIf(cond, deopt);
}

void CheckClassInstr::EmitBitTest(FlowGraphCompiler* compiler, intptr_t min, intptr_t max, intptr_t mask, compiler::Label* deopt) {
  Register biased_cid = locs()->temp(0).reg();
  __ AddImmediate(biased_cid, biased_cid, static_cast<int32_t>(-min));
  __ CompareImmediate(biased_cid, max - min);
  __ BranchIf(HI, deopt);
  Register bit_reg = locs()->temp(1).reg();
  __ LoadImmediate(bit_reg, 1);
  __ sll_d(bit_reg, bit_reg, biased_cid);
  __ TestImmediate(bit_reg, mask);
  __ BranchIf(EQ, deopt);
}

int CheckClassInstr::EmitCheckCid(FlowGraphCompiler* compiler, int bias, intptr_t cid_start, intptr_t cid_end, bool is_last, compiler::Label* is_ok, compiler::Label* deopt, bool use_near_jump) {
  Register biased_cid = locs()->temp(0).reg();
  Condition no_match, match;
  if (cid_start == cid_end) { __ CompareImmediate(biased_cid, cid_start - bias); no_match = NE; match = EQ; }
  else { __ AddImmediate(biased_cid, biased_cid, static_cast<int32_t>(bias - cid_start)); bias = cid_start; __ CompareImmediate(biased_cid, cid_end - cid_start); no_match = HI; match = LS; }
  if (is_last) __ BranchIf(no_match, deopt); else __ BranchIf(match, is_ok);
  return bias;
}

LocationSummary* CheckClassIdInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone) LocationSummary(zone, 1, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, cids_.IsSingleCid() ? Location::RequiresRegister() : Location::WritableRegister());
  return summary;
}

void CheckClassIdInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  Register value = locs()->in(0).reg();
  compiler::Label* deopt = compiler->AddDeoptStub(deopt_id(), ICData::kDeoptCheckClass);
  if (cids_.IsSingleCid()) { __ CompareImmediate(value, Smi::RawValue(cids_.cid_start)); __ BranchIf(NE, deopt); }
  else { __ AddImmediate(value, value, -Smi::RawValue(cids_.cid_start)); __ CompareImmediate(value, Smi::RawValue(cids_.cid_end - cids_.cid_start)); __ BranchIf(HI, deopt); }
}

LocationSummary* CheckSmiInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  LocationSummary* summary = new (zone) LocationSummary(zone, 1, 0, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresRegister());
  return summary;
}

void CheckSmiInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  compiler::Label* deopt = compiler->AddDeoptStub(deopt_id(), ICData::kDeoptCheckSmi);
  __ BranchIfNotSmi(locs()->in(0).reg(), deopt);
}

void CheckNullInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  ThrowErrorSlowPathCode* slow_path = new NullErrorSlowPath(this);
  compiler->AddSlowPathCode(slow_path);
  __ CompareObject(locs()->in(0).reg(), Object::null_object());
  __ BranchIf(EQ, slow_path->entry_label());
}

LocationSummary* CheckArrayBoundInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  LocationSummary* locs = new (zone) LocationSummary(zone, 2, 0, LocationSummary::kNoCall);
  locs->set_in(kLengthPos, LocationRegisterOrSmiConstant(length()));
  locs->set_in(kIndexPos, LocationRegisterOrSmiConstant(index()));
  return locs;
}

void CheckArrayBoundInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  uint32_t flags = generalized_ ? ICData::kGeneralized : 0;
  compiler::Label* deopt = compiler->AddDeoptStub(deopt_id(), ICData::kDeoptCheckArrayBound, flags);
  Location length_loc = locs()->in(kLengthPos);
  Location index_loc = locs()->in(kIndexPos);
  const intptr_t index_cid = index()->Type()->ToCid();
  if (length_loc.IsConstant() && index_loc.IsConstant()) {
    if ((Smi::Cast(length_loc.constant()).Value() > Smi::Cast(index_loc.constant()).Value()) && (Smi::Cast(index_loc.constant()).Value() >= 0)) return;
    __ b(deopt); return;
  }
  if (index_loc.IsConstant()) { __ CompareObject(length_loc.reg(), Smi::Cast(index_loc.constant())); __ BranchIf(LS, deopt); }
  else if (length_loc.IsConstant()) {
    const Register index = index_loc.reg();
    if (index_cid != kSmiCid) __ BranchIfNotSmi(index, deopt);
    if (Smi::Cast(length_loc.constant()).Value() == Smi::kMaxValue) { __ TestImmediate(index, index); __ BranchIf(MI, deopt); }
    else { __ CompareObject(index, length_loc.constant()); __ BranchIf(CS, deopt); }
  } else {
    if (index_cid != kSmiCid) __ BranchIfNotSmi(index_loc.reg(), deopt);
    __ CompareObjectRegisters(index_loc.reg(), length_loc.reg());
    __ BranchIf(CS, deopt);
  }
}

LocationSummary* CheckWritableInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  LocationSummary* locs = new (zone) LocationSummary(zone, 1, 0, UseSharedSlowPathStub(opt) ? LocationSummary::kCallOnSharedSlowPath : LocationSummary::kCallOnSlowPath);
  locs->set_in(kReceiver, Location::RequiresRegister());
  return locs;
}

void CheckWritableInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  WriteErrorSlowPath* slow_path = new WriteErrorSlowPath(this);
  compiler->AddSlowPathCode(slow_path);
  __ LoadFromOffset(TMP, locs()->in(0).reg(), compiler::target::Object::tags_offset(), compiler::kUnsignedByte);
  ASSERT(compiler::target::UntaggedObject::kDeeplyImmutableBit < 8);
  ASSERT(compiler::target::UntaggedObject::kShallowImmutableBit < 8);
  __ TestImmediate(TMP, 1 << compiler::target::UntaggedObject::kDeeplyImmutableBit | 1 << compiler::target::UntaggedObject::kShallowImmutableBit);
  __ BranchIf(NE, slow_path->entry_label());
}


// Helper to convert a non-Smi value_reg's cid into value_cid_reg,
// tagged as Smi.
static void LoadValueCid(FlowGraphCompiler* compiler,
                         Register value_cid_reg,
                         Register value_reg) {
  compiler::Label done;
  __ BranchIfSmi(value_reg, &done);
  __ LoadClassId(value_cid_reg, value_reg);
  __ SmiTag(value_cid_reg);
  __ Bind(&done);
}

LocationSummary* GuardFieldClassInstr::MakeLocationSummary(Zone* zone,
                                                           bool opt) const {
  const intptr_t kNumInputs = 1;

  const intptr_t value_cid = value()->Type()->ToCid();
  const intptr_t field_cid = field().guarded_cid();

  const bool emit_full_guard = !opt || (field_cid == kIllegalCid);

  const bool needs_value_cid_temp_reg =
      emit_full_guard || ((value_cid == kDynamicCid) && (field_cid != kSmiCid));

  const bool needs_field_temp_reg = emit_full_guard;

  intptr_t num_temps = 0;
  if (needs_value_cid_temp_reg) {
    num_temps++;
  }
  if (needs_field_temp_reg) {
    num_temps++;
  }

  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, num_temps, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresRegister());

  for (intptr_t i = 0; i < num_temps; i++) {
    summary->set_temp(i, Location::RequiresRegister());
  }

  return summary;
}

void GuardFieldClassInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  ASSERT(compiler::target::UntaggedObject::kClassIdTagSize == 20);
  ASSERT(sizeof(UntaggedField::guarded_cid_) == 4);
  ASSERT(sizeof(UntaggedField::is_nullable_) == 4);

  const intptr_t value_cid = value()->Type()->ToCid();
  const intptr_t field_cid = field().guarded_cid();
  const intptr_t nullability = field().is_nullable() ? kNullCid : kIllegalCid;

  if (field_cid == kDynamicCid) {
    return;  // Nothing to emit.
  }

  const bool emit_full_guard =
      !compiler->is_optimizing() || (field_cid == kIllegalCid);

  const bool needs_value_cid_temp_reg =
      emit_full_guard || ((value_cid == kDynamicCid) && (field_cid != kSmiCid));

  const bool needs_field_temp_reg = emit_full_guard;

  const Register value_reg = locs()->in(0).reg();

  const Register value_cid_reg =
      needs_value_cid_temp_reg ? locs()->temp(0).reg() : kNoRegister;

  const Register field_reg = needs_field_temp_reg
                                 ? locs()->temp(locs()->temp_count() - 1).reg()
                                 : kNoRegister;

  compiler::Label ok, fail_label;

  compiler::Label* deopt =
      compiler->is_optimizing()
          ? compiler->AddDeoptStub(deopt_id(), ICData::kDeoptGuardField)
          : nullptr;

  compiler::Label* fail = (deopt != nullptr) ? deopt : &fail_label;

  if (emit_full_guard) {
    __ LoadObject(field_reg, Field::ZoneHandle((field().Original())));

    const intptr_t field_cid_offset = Field::guarded_cid_offset();
    const intptr_t field_nullability_offset = Field::is_nullable_offset();

    if (value_cid == kDynamicCid) {
      LoadValueCid(compiler, value_cid_reg, value_reg);
      __ LoadFromOffset(TMP, field_reg, field_cid_offset);
      __ CompareRegisters(value_cid_reg, TMP);
      __ BranchIf(EQ, &ok, compiler::Assembler::kNearJump);
      __ LoadFromOffset(TMP, field_reg, field_nullability_offset);
      __ CompareRegisters(value_cid_reg, TMP);
    } else if (value_cid == kNullCid) {
      __ LoadFromOffset(value_cid_reg, field_reg, field_nullability_offset);
      __ CompareImmediate(value_cid_reg, value_cid);
    } else {
      __ LoadFromOffset(value_cid_reg, field_reg, field_cid_offset);
      __ CompareImmediate(value_cid_reg, value_cid);
    }
    __ BranchIf(EQ, &ok, compiler::Assembler::kNearJump);

    if (!field().needs_length_check()) {
      __ LoadFromOffset(TMP, field_reg, field_cid_offset);
      __ CompareImmediate(TMP, kIllegalCid);
      __ BranchIf(NE, fail);

      if (value_cid == kDynamicCid) {
        __ StoreToOffset(value_cid_reg, field_reg, field_cid_offset);
        __ StoreToOffset(value_cid_reg, field_reg, field_nullability_offset);
      } else {
        __ LoadImmediate(TMP, value_cid);
        __ StoreToOffset(TMP, field_reg, field_cid_offset);
        __ StoreToOffset(TMP, field_reg, field_nullability_offset);
      }

      __ b(&ok);
    }

    if (deopt == nullptr) {
      __ Bind(fail);

      __ LoadFieldFromOffset(TMP, field_reg, Field::guarded_cid_offset(),
                             compiler::kUnsignedTwoBytes);
      __ CompareImmediate(TMP, kDynamicCid);
      __ BranchIf(EQ, &ok, compiler::Assembler::kNearJump);

      __ PushRegisterPair(value_reg, field_reg);
      ASSERT(!compiler->is_optimizing());  // No deopt info needed.
      __ CallRuntime(kUpdateFieldCidRuntimeEntry, 2, /*tsan_enter_exit=*/false);
      __ Drop(2);  // Drop the field and the value.
    } else {
      __ b(fail);
    }
  } else {
    ASSERT(compiler->is_optimizing());
    ASSERT(deopt != nullptr);

    if (value_cid == kDynamicCid) {
      __ TestImmediate(value_reg, kSmiTagMask);

      if (field_cid != kSmiCid) {
        __ BranchIf(EQ, fail);
        __ LoadClassId(value_cid_reg, value_reg);
        __ CompareImmediate(value_cid_reg, field_cid);
      }

      if (field().is_nullable() && (field_cid != kNullCid)) {
        __ BranchIf(EQ, &ok, compiler::Assembler::kNearJump);
        __ CompareObject(value_reg, Object::null_object());
      }

      __ BranchIf(NE, fail);
    } else if (value_cid == field_cid) {
      // Cid matches; nothing to check.
    } else {
      ASSERT(value_cid != nullability);
      __ b(fail);
    }
  }
  __ Bind(&ok);
}

LocationSummary* GuardFieldLengthInstr::MakeLocationSummary(Zone* zone,
                                                            bool opt) const {
  const intptr_t kNumInputs = 1;
  if (!opt || (field().guarded_list_length() == Field::kUnknownFixedLength)) {
    const intptr_t kNumTemps = 3;
    LocationSummary* summary = new (zone)
        LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
    summary->set_in(0, Location::RequiresRegister());
    summary->set_temp(0, Location::RequiresRegister());
    summary->set_temp(1, Location::RequiresRegister());
    summary->set_temp(2, Location::RequiresRegister());
    return summary;
  } else {
    LocationSummary* summary = new (zone)
        LocationSummary(zone, kNumInputs, 0, LocationSummary::kNoCall);
    summary->set_in(0, Location::RequiresRegister());
    return summary;
  }
  UNREACHABLE();
}

void GuardFieldLengthInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  if (field().guarded_list_length() == Field::kNoFixedLength) {
    return;  // Nothing to emit.
  }

  compiler::Label* deopt =
      compiler->is_optimizing()
          ? compiler->AddDeoptStub(deopt_id(), ICData::kDeoptGuardField)
          : nullptr;

  const Register value_reg = locs()->in(0).reg();

  if (!compiler->is_optimizing() ||
      (field().guarded_list_length() == Field::kUnknownFixedLength)) {
    const Register field_reg = locs()->temp(0).reg();
    const Register offset_reg = locs()->temp(1).reg();
    const Register length_reg = locs()->temp(2).reg();

    compiler::Label ok;

    __ LoadObject(field_reg, Field::ZoneHandle(field().Original()));

    __ ld_b(offset_reg, field_reg,
            Field::guarded_list_length_in_object_offset_offset());
    __ LoadCompressed(
        length_reg,
        compiler::FieldAddress(field_reg,
                               Field::guarded_list_length_offset()));

    // branch if offset_reg < 0
    __ CompareImmediate(offset_reg, 0);
    __ BranchIf(LT, &ok, compiler::Assembler::kNearJump);

    __ add_d(TMP, value_reg, offset_reg);
    __ ld_d(TMP, TMP, 0);
    __ CompareObjectRegisters(length_reg, TMP);

    if (deopt == nullptr) {
      __ BranchIf(EQ, &ok, compiler::Assembler::kNearJump);

      __ PushRegisterPair(value_reg, field_reg);
      ASSERT(!compiler->is_optimizing());
      __ CallRuntime(kUpdateFieldCidRuntimeEntry, 2, /*tsan_enter_exit=*/false);
      __ Drop(2);
    } else {
      __ BranchIf(NE, deopt);
    }

    __ Bind(&ok);
  } else {
    ASSERT(compiler->is_optimizing());
    ASSERT(field().guarded_list_length() >= 0);
    ASSERT(field().guarded_list_length_in_object_offset() !=
           Field::kUnknownLengthOffset);

    __ LoadFromOffset(TMP, value_reg, field().guarded_list_length_in_object_offset());
    __ CompareImmediate(TMP,
                        Smi::RawValue(field().guarded_list_length()));
    __ BranchIf(NE, deopt);
  }
}

DEFINE_UNIMPLEMENTED_INSTRUCTION(GuardFieldTypeInstr)
// ==== MoveArgument, DartReturn, IfThenElse, ClosureCall, LoadLocal, StoreLocal ====
LocationSummary* MoveArgumentInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumTemps = 0;
  LocationSummary* locs = new (zone) LocationSummary(zone, 1, kNumTemps, LocationSummary::kNoCall);
  ConstantInstr* constant = value()->definition()->AsConstant();
  if (constant != nullptr && constant->HasZeroRepresentation()) locs->set_in(0, Location::Constant(constant));
  else if (representation() == kUnboxedDouble) locs->set_in(0, Location::RequiresFpuRegister());
  else if (representation() == kUnboxedInt64) locs->set_in(0, Location::RequiresRegister());
  else { ASSERT(representation() == kTagged); locs->set_in(0, LocationAnyOrConstant(value())); }
  return locs;
}

class ArgumentsMover : public ValueObject {
 public:
  void Flush(FlowGraphCompiler* compiler) {
    if (pending_register_ != kNoRegister) {
      __ StoreToOffset(pending_register_, SP, pending_sp_relative_index_ * compiler::target::kWordSize);
      pending_sp_relative_index_ = -1; pending_register_ = kNoRegister;
    }
  }
  void MoveRegister(FlowGraphCompiler* compiler, intptr_t sp_relative_index, Register reg) {
    Flush(compiler); pending_register_ = reg; pending_sp_relative_index_ = sp_relative_index;
  }
  Register GetFreeTempRegister(FlowGraphCompiler* compiler) { return (pending_register_ == TMP) ? RA : TMP; }
 private:
  intptr_t pending_sp_relative_index_ = -1;
  Register pending_register_ = kNoRegister;
};

void MoveArgumentInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  ASSERT(compiler->is_optimizing());
  if (previous()->IsMoveArgument()) return;
  ArgumentsMover pusher;
  for (MoveArgumentInstr* move_arg = this; move_arg != nullptr; move_arg = move_arg->next()->AsMoveArgument()) {
    const Location value = move_arg->locs()->in(0);
    Register reg = kNoRegister;
    if (value.IsRegister()) reg = value.reg();
    else if (value.IsConstant()) {
      if (value.constant_instruction()->HasZeroRepresentation()) reg = ZR;
      else {
        ASSERT(move_arg->representation() == kTagged);
        if (value.constant().IsNull()) reg = NULL_REG;
        else { reg = pusher.GetFreeTempRegister(compiler); __ LoadObject(reg, value.constant()); }
      }
    } else if (value.IsFpuRegister()) { pusher.Flush(compiler); __ StoreDToOffset(value.fpu_reg(), SP, move_arg->location().stack_index() * compiler::target::kWordSize); continue; }
    else { ASSERT(value.IsStackSlot()); reg = pusher.GetFreeTempRegister(compiler); __ LoadFromOffset(reg, value.base_reg(), value.ToStackSlotOffset()); }
    pusher.MoveRegister(compiler, move_arg->location().stack_index(), reg);
  }
  pusher.Flush(compiler);
}

LocationSummary* DartReturnInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  LocationSummary* locs = new (zone) LocationSummary(zone, 1, 0, LocationSummary::kNoCall);
  switch (representation()) {
    case kTagged: case kUnboxedInt64: locs->set_in(0, Location::RegisterLocation(CallingConventions::kReturnReg)); break;
    case kPairOfTagged: locs->set_in(0, Location::Pair(Location::RegisterLocation(CallingConventions::kReturnReg), Location::RegisterLocation(CallingConventions::kSecondReturnReg))); break;
    case kUnboxedDouble: locs->set_in(0, Location::FpuRegisterLocation(CallingConventions::kReturnFpuReg)); break;
    default: UNREACHABLE(); break;
  }
  return locs;
}

void DartReturnInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  if (compiler->parsed_function().function().IsAsyncFunction() || compiler->parsed_function().function().IsAsyncGenerator()) {
    ASSERT(compiler->flow_graph().graph_entry()->NeedsFrame());
    compiler->EmitJumpToStub(GetReturnStub(compiler));
    return;
  }
  if (!compiler->flow_graph().graph_entry()->NeedsFrame()) { __ ret(); return; }
  if (FLAG_target_thread_sanitizer && !compiler->is_optimizing()) { RELEASE_ASSERT(locs()->in(0).IsRegister()); __ Move(CALLEE_SAVED_TEMP, locs()->in(0).reg()); __ TsanFuncExit(false); __ Move(locs()->in(0).reg(), CALLEE_SAVED_TEMP); }
  ASSERT(__ constant_pool_allowed());
  __ LeaveDartFrame();
  __ ret();
  __ set_constant_pool_allowed(true);
}


LocationSummary* IfThenElseInstr::MakeLocationSummary(Zone* zone, bool opt) const { condition()->InitializeLocationSummary(zone, opt); return condition()->locs(); }

void IfThenElseInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  // Branch-based; loong64 lacks cset/csetm.
  const Register result = locs()->out(0).reg();
  compiler::Label is_true, done;
  BranchLabels labels = {&is_true, &done, &done};
  Condition true_condition = condition()->EmitConditionCode(compiler, labels);
  if (is_true.IsLinked() || done.IsLinked()) {
    if (true_condition != kInvalidCondition) {
      EmitBranchOnCondition(compiler, true_condition, labels);
    }
  }
  __ LoadImmediate(result, Smi::RawValue(if_false_));
  __ b(&done);
  __ Bind(&is_true);
  __ LoadImmediate(result, Smi::RawValue(if_true_));
  __ Bind(&done);
}

LocationSummary* ClosureCallInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone) LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kCall);
  summary->set_in(0, Location::RegisterLocation(FLAG_precompiled_mode ? CallingConventions::kReturnReg : FUNCTION_REG));
  return MakeCallSummary(zone, this, summary);
}

void ClosureCallInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const intptr_t argument_count = ArgumentCount();
  const Array& arguments_descriptor = Array::ZoneHandle(Z, GetArgumentsDescriptor());
  __ LoadObject(ARGS_DESC_REG, arguments_descriptor);
  if (FLAG_precompiled_mode) {
    ASSERT(locs()->in(0).reg() == CallingConventions::kReturnReg);
    __ LoadFieldFromOffset(TMP, locs()->in(0).reg(), compiler::target::Closure::entry_point_offset());
  } else {
    ASSERT(locs()->in(0).reg() == FUNCTION_REG);
    __ LoadCompressedFieldFromOffset(CODE_REG, FUNCTION_REG, compiler::target::Function::code_offset());
    __ LoadFieldFromOffset(TMP, FUNCTION_REG, compiler::target::Function::entry_point_offset());
    __ LoadImmediate(IC_DATA_REG, 0);
  }
  __ jirl(RA, TMP, 0);
  compiler->EmitCallsiteMetadata(source(), deopt_id(), UntaggedPcDescriptors::kOther, locs(), env());
  compiler->EmitDropArguments(argument_count);
}

LocationSummary* LoadLocalInstr::MakeLocationSummary(Zone* zone, bool opt) const { return LocationSummary::Make(zone, 0, Location::RequiresRegister(), LocationSummary::kNoCall); }
void LoadLocalInstr::EmitNativeCode(FlowGraphCompiler* compiler) { __ LoadFromOffset(locs()->out(0).reg(), FP, compiler::target::FrameOffsetInBytesForVariable(&local())); }
LocationSummary* StoreLocalInstr::MakeLocationSummary(Zone* zone, bool opt) const { return LocationSummary::Make(zone, 1, Location::SameAsFirstInput(), LocationSummary::kNoCall); }
void StoreLocalInstr::EmitNativeCode(FlowGraphCompiler* compiler) { __ StoreToOffset(locs()->in(0).reg(), FP, compiler::target::FrameOffsetInBytesForVariable(&local())); }
// ==== Constant, UnboxedConstant, AssertAssignable, Branch helpers ====
LocationSummary* ConstantInstr::MakeLocationSummary(Zone* zone, bool opt) const { return LocationSummary::Make(zone, 0, Location::RequiresRegister(), LocationSummary::kNoCall); }
void ConstantInstr::EmitNativeCode(FlowGraphCompiler* compiler) { EmitMoveToLocation(compiler, locs()->out(0)); }

void ConstantInstr::EmitMoveToLocation(FlowGraphCompiler* compiler, const Location& destination, Register tmp, intptr_t pair_index) { ASSERT(pair_index == 0);
  if (destination.IsRegister()) {
    if (value().IsSmi() && Smi::Cast(value()).Value() == 0) __ LoadImmediate(destination.reg(), 0);
    else if (value().IsNull()) __ MoveRegister(destination.reg(), NULL_REG);
    else __ LoadObject(destination.reg(), value());
  } else if (destination.IsFpuRegister()) {
    if (Utils::DoublesBitEqual(Double::Cast(value()).value(), 0.0)) { __ movgr2fr_d(destination.fpu_reg(), ZR); }
    else { __ LoadObject(TMP, value()); __ LoadDFromOffset(destination.fpu_reg(), TMP, compiler::target::Double::value_offset() - kHeapObjectTag); }
  } else if (destination.IsDoubleStackSlot()) {
    const Register tmp_reg = (tmp != kNoRegister) ? tmp : TMP;
    if (Utils::DoublesBitEqual(Double::Cast(value()).value(), 0.0)) __ LoadImmediate(tmp_reg, 0);
    else { __ LoadObject(tmp_reg, value()); __ LoadFromOffset(tmp_reg, tmp_reg, compiler::target::Double::value_offset() - kHeapObjectTag); }
    __ StoreToOffset(tmp_reg, destination.base_reg(), destination.ToStackSlotOffset());
  } else {
    ASSERT(destination.IsStackSlot());
    const Register tmp_reg = (tmp != kNoRegister) ? tmp : TMP;
    const intptr_t dest_offset = destination.ToStackSlotOffset();
    if (value().IsNull()) {
      __ StoreToOffset(NULL_REG, destination.base_reg(), dest_offset);
    } else if (value().IsSmi() && Smi::Cast(value()).Value() == 0) {
      __ StoreToOffset(ZR, destination.base_reg(), dest_offset);
    } else {
      __ LoadObject(tmp_reg, value());
      __ StoreToOffset(tmp_reg, destination.base_reg(), dest_offset);
    }
  }
}

LocationSummary* UnboxedConstantInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const bool is_unboxed_int = RepresentationUtils::IsUnboxedInteger(representation());
  const intptr_t kNumInputs = 0;
  const intptr_t kNumTemps = is_unboxed_int ? 0 : 1;
  LocationSummary* locs = new (zone) LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  if (is_unboxed_int) { locs->set_out(0, Location::RequiresRegister()); }
  else { switch (representation()) { case kUnboxedDouble: locs->set_out(0, Location::RequiresFpuRegister()); locs->set_temp(0, Location::RequiresRegister()); break; default: UNREACHABLE(); break; } }
  return locs;
}

void UnboxedConstantInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  switch (representation()) {
    case kUnboxedDouble: {
      if (Utils::DoublesBitEqual(Double::Cast(value()).value(), 0.0)) __ movgr2fr_d(locs()->out(0).fpu_reg(), ZR);
      else { __ LoadObject(TMP, value()); __ LoadDFromOffset(locs()->out(0).fpu_reg(), TMP, compiler::target::Double::value_offset() - kHeapObjectTag); }
      break;
    }
    case kUnboxedInt64: __ LoadImmediate(locs()->out(0).reg(), Integer::Cast(value()).Value()); break;
    default: UNREACHABLE(); break;
  }
}

LocationSummary* AssertAssignableInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  auto const dst_type_loc = LocationFixedRegisterOrConstant(dst_type(), TypeTestABI::kDstTypeReg);
  const intptr_t kNonChangeableInputRegs = (1 << TypeTestABI::kInstanceReg) | ((dst_type_loc.IsRegister() ? 1 : 0) << TypeTestABI::kDstTypeReg) | (1 << TypeTestABI::kInstantiatorTypeArgumentsReg) | (1 << TypeTestABI::kFunctionTypeArgumentsReg);
  const intptr_t kCpuRegistersToPreserve = kDartAvailableCpuRegs & ~kNonChangeableInputRegs;
  const intptr_t kFpuRegistersToPreserve = Utils::NBitMask<intptr_t>(kNumberOfFpuRegisters) & ~(1l << FpuTMP);
  const intptr_t kNumTemps = Utils::CountOneBits64(kCpuRegistersToPreserve) + Utils::CountOneBits64(kFpuRegistersToPreserve);
  LocationSummary* summary = new (zone) LocationSummary(zone, 4, kNumTemps, LocationSummary::kCallCalleeSafe);
  summary->set_in(kInstancePos, Location::RegisterLocation(TypeTestABI::kInstanceReg));
  summary->set_in(kDstTypePos, dst_type_loc);
  summary->set_in(kInstantiatorTAVPos, Location::RegisterLocation(TypeTestABI::kInstantiatorTypeArgumentsReg));
  summary->set_in(kFunctionTAVPos, Location::RegisterLocation(TypeTestABI::kFunctionTypeArgumentsReg));
  summary->set_out(0, Location::SameAsFirstInput());
  intptr_t next_temp = 0;
  for (intptr_t i = 0; i < kNumberOfCpuRegisters; ++i) { if (((1 << i) & kCpuRegistersToPreserve) != 0) summary->set_temp(next_temp++, Location::RegisterLocation(static_cast<Register>(i))); }
  for (intptr_t i = 0; i < kNumberOfFpuRegisters; i++) { if (((1l << i) & kFpuRegistersToPreserve) != 0) summary->set_temp(next_temp++, Location::FpuRegisterLocation(static_cast<FpuRegister>(i))); }
  return summary;
}

static void EmitBranchOnCondition(FlowGraphCompiler* compiler, Condition true_condition, BranchLabels labels) {
  if (labels.fall_through == labels.false_label) __ BranchIf(true_condition, labels.true_label);
  else { __ BranchIf(InvertCondition(true_condition), labels.false_label); if (labels.fall_through != labels.true_label) __ b(labels.true_label); }
}


static Condition EmitSmiComparisonOp(FlowGraphCompiler* compiler, const LocationSummary& locs, Token::Kind kind, BranchLabels labels) {
  Location left = locs.in(0); Location right = locs.in(1);
  Condition true_condition = TokenKindToIntCondition(kind, false);
  if (right.IsConstant()) __ CompareObject(left.reg(), right.constant()); else __ CompareObjectRegisters(left.reg(), right.reg());
  return true_condition;
}

static Condition EmitUnboxedIntComparisonOp(FlowGraphCompiler* compiler, const LocationSummary& locs, Token::Kind kind, Representation rep, BranchLabels labels) {
  Location left = locs.in(0); Location right = locs.in(1);
  const compiler::OperandSize size = (rep == kUnboxedInt64) ? compiler::kEightBytes : compiler::kFourBytes;
  Condition true_condition = TokenKindToIntCondition(kind, RepresentationUtils::IsUnsignedInteger(rep));
  if (right.IsConstant()) { int64_t value; RELEASE_ASSERT(compiler::HasIntegerValue(right.constant(), &value)); __ CompareImmediate(left.reg(), value, size); }
  else __ CompareRegisters(left.reg(), right.reg());
  return true_condition;
}

#define R(r) (1 << static_cast<int>(r))

static Condition EmitNullAwareInt64ComparisonOp(FlowGraphCompiler* compiler, const LocationSummary& locs, Token::Kind kind, BranchLabels labels) {
  ASSERT(kind == Token::kEQ || kind == Token::kNE);
  const Register left = locs.in(0).reg(); const Register right = locs.in(1).reg();
  const Condition true_condition = TokenKindToIntCondition(kind, false);
  compiler::Label* equal_result = (true_condition == EQ) ? labels.true_label : labels.false_label;
  compiler::Label* not_equal_result = (true_condition == EQ) ? labels.false_label : labels.true_label;
  __ CompareObjectRegisters(left, right); __ BranchIf(EQ, equal_result);
  __ and_l(TMP, left, right); __ BranchIfSmi(TMP, not_equal_result);
  __ CompareClassId(left, kMintCid); __ BranchIf(NE, not_equal_result);
  __ CompareClassId(right, kMintCid); __ BranchIf(NE, not_equal_result);
  __ LoadFieldFromOffset(TMP, left, compiler::target::Mint::value_offset());
  __ LoadFieldFromOffset(TMP2, right, compiler::target::Mint::value_offset());
  __ CompareRegisters(TMP, TMP2);
  return true_condition;
}
// ==== EqualityCompare, TestInt, RelationalOp ====
LocationSummary* EqualityCompareInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  LocationSummary* locs = new (zone) LocationSummary(zone, 2, 0, LocationSummary::kNoCall);
  if (is_null_aware()) { locs->set_in(0, Location::RequiresRegister()); locs->set_in(1, Location::RequiresRegister()); }
  else if (input_representation() == kUnboxedDouble) { locs->set_in(0, Location::RequiresFpuRegister()); locs->set_in(1, Location::RequiresFpuRegister()); }
  else { locs->set_in(0, Location::RequiresRegister()); locs->set_in(1, LocationRegisterOrConstant(right())); }
  locs->set_out(0, Location::RequiresRegister());
  return locs;
}

static Condition EmitDoubleComparisonOp(FlowGraphCompiler* compiler, const LocationSummary& locs, Token::Kind kind, BranchLabels labels) {
  const FpuRegister left = locs.in(0).fpu_reg(); const FpuRegister right = locs.in(1).fpu_reg();
  switch (kind) {
    case Token::kEQ: __ fcmp_cond_d(left, right, EQ); __ bcnez(static_cast<FRegister>(0), labels.true_label); break;
    case Token::kNE: __ fcmp_cond_d(left, right, EQ); __ bceqz(static_cast<FRegister>(0), labels.true_label); break;
    case Token::kLT: __ fcmp_cond_d(right, left, GT); __ bcnez(static_cast<FRegister>(0), labels.true_label); break;
    case Token::kGT: __ fcmp_cond_d(left, right, GT); __ bcnez(static_cast<FRegister>(0), labels.true_label); break;
    case Token::kLTE: __ fcmp_cond_d(right, left, GE); __ bcnez(static_cast<FRegister>(0), labels.true_label); break;
    case Token::kGTE: __ fcmp_cond_d(left, right, GE); __ bcnez(static_cast<FRegister>(0), labels.true_label); break;
    default: UNREACHABLE();
  }
  if (labels.fall_through != labels.false_label) __ b(labels.false_label);
  return kInvalidCondition;
}

Condition EqualityCompareInstr::EmitConditionCode(FlowGraphCompiler* compiler, BranchLabels labels) {
  if (is_null_aware()) return EmitNullAwareInt64ComparisonOp(compiler, *locs(), kind(), labels);
  switch (input_representation()) {
    case kTagged: return EmitSmiComparisonOp(compiler, *locs(), kind(), labels);
    case kUnboxedInt64: case kUnboxedInt32: case kUnboxedUint32: return EmitUnboxedIntComparisonOp(compiler, *locs(), kind(), input_representation(), labels);
    case kUnboxedDouble: return EmitDoubleComparisonOp(compiler, *locs(), kind(), labels);
    default: UNREACHABLE();
  }
}

LocationSummary* TestIntInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  LocationSummary* locs = new (zone) LocationSummary(zone, 2, 0, LocationSummary::kNoCall);
  locs->set_in(0, Location::RequiresRegister()); locs->set_in(1, LocationRegisterOrConstant(right()));
  locs->set_out(0, Location::RequiresRegister());
  return locs;
}

Condition TestIntInstr::EmitConditionCode(FlowGraphCompiler* compiler, BranchLabels labels) {
  const Register left = locs()->in(0).reg(); Location right = locs()->in(1);
  const auto operand_size = representation_ == kTagged ? compiler::kObjectBytes : compiler::kEightBytes;
  if (right.IsConstant()) __ TestImmediate(left, ComputeImmediateMask(), operand_size);
  else { __ and_l(TMP, left, right.reg()); __ TestImmediate(TMP, 0, operand_size); }
  return (kind() == Token::kNE) ? NE : EQ;
}

// TestIntInstr::EmitBranchCode removed - not declared for LOONG64

LocationSummary* RelationalOpInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  LocationSummary* locs = new (zone) LocationSummary(zone, 2, 0, LocationSummary::kNoCall);
  if (input_representation() == kUnboxedDouble) { locs->set_in(0, Location::RequiresFpuRegister()); locs->set_in(1, Location::RequiresFpuRegister()); }
  else { locs->set_in(0, Location::RequiresRegister()); locs->set_in(1, LocationRegisterOrConstant(right())); }
  locs->set_out(0, Location::RequiresRegister());
  return locs;
}

Condition RelationalOpInstr::EmitConditionCode(FlowGraphCompiler* compiler, BranchLabels labels) {
  switch (input_representation()) {
    case kTagged: return EmitSmiComparisonOp(compiler, *locs(), kind(), labels);
    case kUnboxedInt64: case kUnboxedInt32: case kUnboxedUint32: return EmitUnboxedIntComparisonOp(compiler, *locs(), kind(), input_representation(), labels);
    case kUnboxedDouble: return EmitDoubleComparisonOp(compiler, *locs(), kind(), labels);
    default: UNREACHABLE();
  }
}
// ==== NativeCall, FfiCall, NativeReturn, NativeEntry, LeafRuntimeCall ====
void NativeCallInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  SetupNative();
  const Register result = locs()->out(0).reg();
  __ AddImmediate(A2, SP, (ArgumentCount() - 1) * compiler::target::kWordSize);
  uword entry;
  const intptr_t argc_tag = NativeArguments::ComputeArgcTag(function());
  const Code* stub;
  if (link_lazily()) {
    stub = &StubCode::CallBootstrapNative();
    entry = NativeEntry::LinkNativeCallEntry();
  } else {
    entry = reinterpret_cast<uword>(native_c_function());
    if (is_bootstrap_native()) {
      stub = &StubCode::CallBootstrapNative();
    } else if (is_auto_scope()) {
      stub = &StubCode::CallAutoScopeNative();
    } else {
      stub = &StubCode::CallNoScopeNative();
    }
  }
  __ LoadImmediate(A1, argc_tag);
  compiler::ExternalLabel label(entry);
  __ LoadNativeEntry(A3, &label,
                     link_lazily() ? ObjectPool::Patchability::kPatchable
                                   : ObjectPool::Patchability::kNotPatchable);
  if (link_lazily()) {
    compiler->GeneratePatchableCall(
        source(), *stub, UntaggedPcDescriptors::kOther, locs(),
        compiler::ObjectPoolBuilderEntry::kResetToBootstrapNative);
  } else {
    compiler->GenerateNonLazyDeoptableStubCall(
        source(), *stub, UntaggedPcDescriptors::kOther, locs(),
        compiler::ObjectPoolBuilderEntry::kNotSnapshotable);
  }
  __ LoadFromOffset(result, SP, 0);
  compiler->EmitDropArguments(ArgumentCount());
}

LocationSummary* FfiCallInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  return MakeLocationSummaryInternal(zone, opt,
      (R(CallingConventions::kSecondNonArgumentRegister) |
       R(CallingConventions::kFfiAnyNonAbiRegister) | R(CALLEE_SAVED_TEMP)));
}

void FfiCallInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const Register target = locs()->in(TargetAddressIndex()).reg();

  const Register temp1 = locs()->temp(0).reg();
  const Register saved_fp_or_sp = locs()->temp(1).reg();
  const Register temp2 = locs()->temp(2).reg();

  ASSERT(temp1 != target);
  ASSERT(temp2 != target);
  ASSERT(temp1 != saved_fp_or_sp);
  ASSERT(temp2 != saved_fp_or_sp);
  ASSERT(saved_fp_or_sp != target);
  ASSERT(IsCalleeSavedRegister(saved_fp_or_sp));

  __ Move(saved_fp_or_sp, is_leaf_ ? SPREG : FPREG);

  if (!is_leaf_) {
    // Create a dummy exit frame (EnterDartFrame without CODE_REG/PP).
    if (FLAG_precompiled_mode) {
      __ addi_d(SP, SP, -2 * compiler::target::kWordSize);
      __ st_d(RA, SP, 1 * compiler::target::kWordSize);
      __ st_d(FP, SP, 0 * compiler::target::kWordSize);
      __ addi_d(FP, SP, 2 * compiler::target::kWordSize);
    } else {
      __ addi_d(SP, SP, -4 * compiler::target::kWordSize);
      __ st_d(RA, SP, 3 * compiler::target::kWordSize);
      __ st_d(FP, SP, 2 * compiler::target::kWordSize);
      __ st_d(NULL_REG, SP, 1 * compiler::target::kWordSize);
      __ st_d(NULL_REG, SP, 0 * compiler::target::kWordSize);
      __ addi_d(FP, SP, 4 * compiler::target::kWordSize);
    }
  }

  intptr_t stack_space = marshaller_.RequiredStackSpaceInBytes();
  __ ReserveAlignedFrameSpace(stack_space);

  EmitParamMoves(compiler, is_leaf_ ? FPREG : saved_fp_or_sp, temp1, temp2);

  if (compiler::Assembler::EmittingComments()) {
    __ Comment(is_leaf_ ? "Leaf Call" : "Call");
  }

  if (is_leaf_) {
#if !defined(PRODUCT)
    __ StoreToOffset(FPREG, THR,
                     compiler::target::Thread::top_exit_frame_info_offset());
    __ StoreToOffset(target, THR, compiler::target::Thread::vm_tag_offset());
#endif

    __ Move(A3, T3);
    __ Move(A4, T4);
    __ Move(A5, T5);
    __ jirl(RA, target, 0);

#if !defined(PRODUCT)
    __ LoadImmediate(temp1, compiler::target::Thread::vm_tag_dart_id());
    __ StoreToOffset(temp1, THR, compiler::target::Thread::vm_tag_offset());
    __ StoreToOffset(ZR, THR,
                     compiler::target::Thread::top_exit_frame_info_offset());
#endif
  } else {
    compiler->EmitCallsiteMetadata(source(), deopt_id(),
                                   UntaggedPcDescriptors::Kind::kOther, locs(),
                                   env());
    __ pcaddu12i(temp1, 0);
    __ StoreToOffset(temp1, FPREG, kSavedCallerPcSlotFromFp * compiler::target::kWordSize);

    if (CanExecuteGeneratedCodeInSafepoint()) {
      __ LoadImmediate(temp1, compiler::target::Thread::exit_through_ffi());
      __ TransitionGeneratedToNative(target, FPREG, temp1,
                                     /*enter_safepoint=*/true);

      __ Move(A3, T3);
      __ Move(A4, T4);
      __ Move(A5, T5);
      __ jirl(RA, target, 0);

      __ TransitionNativeToGenerated(temp1, /*exit_safepoint=*/true);
    } else {
      __ LoadFromOffset(temp1, THR,
          compiler::target::Thread::
              call_native_through_safepoint_entry_point_offset());

      ASSERT(target == T0);
      __ Move(A3, T3);
      __ Move(A4, T4);
      __ Move(A5, T5);
      __ jirl(RA, temp1, 0);
    }

    if (marshaller_.IsHandleCType(compiler::ffi::kResultIndex)) {
      __ Comment("Check Dart_Handle for Error.");
      ASSERT(temp1 != CallingConventions::kReturnReg);
      ASSERT(temp2 != CallingConventions::kReturnReg);
      compiler::Label not_error;
      __ LoadFromOffset(temp1, CallingConventions::kReturnReg,
                        compiler::target::LocalHandle::ptr_offset());
      __ BranchIfSmi(temp1, &not_error);
      __ LoadClassId(temp1, temp1);
      __ RangeCheck(temp1, temp2, kFirstErrorCid, kLastErrorCid,
                    compiler::AssemblerBase::kIfNotInRange, &not_error);

      __ Comment("Slow path: call Dart_PropagateError through stub.");
      ASSERT(CallingConventions::ArgumentRegisters[0] ==
             CallingConventions::kReturnReg);
      __ LoadFromOffset(temp1, THR,
          compiler::target::Thread::
              call_native_through_safepoint_entry_point_offset());
      __ LoadFromOffset(target, THR,
          kPropagateErrorRuntimeEntry.OffsetFromThread());
      __ jirl(RA, temp1, 0);
#if defined(DEBUG)
      __ break_(0);
#endif

      __ Bind(&not_error);
    }

    __ RestorePinnedRegisters();
  }

  EmitReturnMoves(compiler, temp1, temp2);

  if (is_leaf_) {
    __ Move(SPREG, saved_fp_or_sp);
  } else {
    __ LeaveDartFrame();

    if (FLAG_precompiled_mode) {
      __ SetupGlobalPoolAndDispatchTable();
    }
  }

  __ RestorePoolPointer();
  __ set_constant_pool_allowed(true);
}

void NativeReturnInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  EmitReturnMoves(compiler);
  if (!compiler->flow_graph().graph_entry()->NeedsFrame()) { __ ret(); return; }
  ASSERT(__ constant_pool_allowed());
  __ LeaveDartFrame();
  __ ret();
  __ set_constant_pool_allowed(true);
}

void NativeEntryInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  __ Bind(compiler->GetJumpLabel(this));
  __ set_constant_pool_allowed(false);
  if (!compiler->flow_graph().graph_entry()->NeedsFrame()) {
    __ MonomorphicCheckedEntryJIT();
    return;
  }
  __ Comment("Enter frame");
  __ EnterDartFrame(compiler->StackSize() * compiler::target::kWordSize);
  __ set_constant_pool_allowed(true);
  __ BranchOnMonomorphicCheckedEntryJIT(compiler->GetJumpLabel(this));
}

LocationSummary* LeafRuntimeCallInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  constexpr Register saved_fp = CallingConventions::kSecondNonArgumentRegister;
  constexpr Register temp0 = CallingConventions::kFfiAnyNonAbiRegister;
  static_assert(saved_fp < temp0, "Unexpected ordering of registers in set.");
  LocationSummary* summary =
      MakeLocationSummaryInternal(zone, (1 << saved_fp) | (1 << temp0));
  return summary;
}
void LeafRuntimeCallInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const Register saved_fp = locs()->temp(0).reg();
  const Register temp0 = locs()->temp(1).reg();

  __ MoveRegister(saved_fp, FPREG);

  const intptr_t frame_space = native_calling_convention_.StackTopInBytes();
  __ EnterCFrame(frame_space);

  EmitParamMoves(compiler, saved_fp, temp0);

  const Register target_address = locs()->in(TargetAddressIndex()).reg();
  // I.e., no use of A3/A4/A5.
  RELEASE_ASSERT(native_calling_convention_.argument_locations().length() < 4);
  __ st_d(target_address, THR, compiler::target::Thread::vm_tag_offset());
  __ CallCFunction(target_address);
  __ LoadImmediate(temp0, VMTag::kDartTagId);
  __ st_d(temp0, THR, compiler::target::Thread::vm_tag_offset());

  __ LeaveCFrame();  // Also restores PP.
}
// ==== String ops, LoadIndexed, LoadCodeUnits, StoreIndexed ====
LocationSummary* OneByteStringFromCharCodeInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  LocationSummary* locs = new (zone) LocationSummary(zone, 1, 0, LocationSummary::kNoCall);
  locs->set_in(0, Location::RequiresRegister());
  locs->set_out(0, Location::RequiresRegister());
  return locs;
}
void OneByteStringFromCharCodeInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  ASSERT(compiler->is_optimizing());
  const Register char_code = locs()->in(0).reg();
  const Register result = locs()->out(0).reg();
  __ ld_d(result, THR, Thread::predefined_symbols_address_offset());
  __ slli_d(TMP, char_code, kWordSizeLog2 - kSmiTagSize);
  __ add_d(TMP, result, TMP);
  __ ld_d(result, TMP, Symbols::kNullCharCodeSymbolOffset * kWordSize);
}

LocationSummary* StringToCharCodeInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  LocationSummary* locs = new (zone) LocationSummary(zone, 1, 0, LocationSummary::kNoCall);
  locs->set_in(0, Location::RequiresRegister()); locs->set_out(0, Location::RequiresRegister());
  return locs;
}
void StringToCharCodeInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  ASSERT(cid_ == kOneByteStringCid);
  __ ld_bu(locs()->out(0).reg(), locs()->in(0).reg(), compiler::target::OneByteString::data_offset());
  __ SmiTag(locs()->out(0).reg());
}

LocationSummary* Utf8ScanInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumInputs = 5;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::Any());               // decoder
  summary->set_in(1, Location::WritableRegister());  // bytes
  summary->set_in(2, Location::WritableRegister());  // start
  summary->set_in(3, Location::WritableRegister());  // end
  summary->set_in(4, Location::WritableRegister());  // table
  summary->set_out(0, Location::RequiresRegister());
  return summary;
}
void Utf8ScanInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const Register bytes_reg = locs()->in(1).reg();
  const Register start_reg = locs()->in(2).reg();
  const Register end_reg = locs()->in(3).reg();
  const Register table_reg = locs()->in(4).reg();
  const Register size_reg = locs()->out(0).reg();

  const Register bytes_ptr_reg = start_reg;
  const Register bytes_end_reg = end_reg;
  const Register flags_reg = bytes_reg;
  const Register temp_reg = TMP;
  const Register decoder_temp_reg = start_reg;
  const Register flags_temp_reg = end_reg;

  const intptr_t kSizeMask = 0x03;
  const intptr_t kFlagsMask = 0x3C;

  compiler::Label loop, loop_in;

  // Address of input bytes.
  compiler::Address bytes_addr = compiler::Address(bytes_reg,
      compiler::target::PointerBase::data_offset() - kHeapObjectTag);
  __ ld_d(bytes_reg, bytes_addr.base(), bytes_addr.offset());

  // Table.
  __ AddImmediate(table_reg, table_reg,
      compiler::target::OneByteString::data_offset() - kHeapObjectTag);

  // Pointers to start and end.
  __ add_d(bytes_ptr_reg, bytes_reg, start_reg);
  __ add_d(bytes_end_reg, bytes_reg, end_reg);

  // Initialize size and flags.
  __ LoadImmediate(size_reg, 0);
  __ LoadImmediate(flags_reg, 0);

  __ b(&loop_in, compiler::Assembler::kNearJump);
  __ Bind(&loop);

  // Read byte and increment pointer.
  __ ld_bu(temp_reg, bytes_ptr_reg, 0);
  __ addi_d(bytes_ptr_reg, bytes_ptr_reg, 1);

  // Update size and flags based on byte value.
  __ add_d(temp_reg, table_reg, temp_reg);
  __ ld_bu(temp_reg, temp_reg, 0);
  __ or_l(flags_reg, flags_reg, temp_reg);
  __ andi(temp_reg, temp_reg, kSizeMask);
  __ add_d(size_reg, size_reg, temp_reg);

  // Stop if end is reached.
  __ Bind(&loop_in);
  __ bltu(bytes_ptr_reg, bytes_end_reg, &loop, compiler::Assembler::kNearJump);

  // Write flags to field.
  __ andi(flags_reg, flags_reg, kFlagsMask);
  if (!IsScanFlagsUnboxed()) {
    __ SmiTag(flags_reg);
  }
  Register decoder_reg;
  const Location decoder_location = locs()->in(0);
  if (decoder_location.IsStackSlot()) {
    __ LoadFromStack(decoder_temp_reg,
        decoder_location.ToStackSlotOffset());
    decoder_reg = decoder_temp_reg;
  } else {
    decoder_reg = decoder_location.reg();
  }
  const auto scan_flags_field_offset = scan_flags_field_.offset_in_bytes();
  if (scan_flags_field_.is_compressed() && !IsScanFlagsUnboxed()) {
    UNIMPLEMENTED();
  } else {
    __ LoadFieldFromOffset(flags_temp_reg, decoder_reg,
                           scan_flags_field_offset);
    __ or_l(flags_temp_reg, flags_temp_reg, flags_reg);
    __ StoreFieldToOffset(flags_temp_reg, decoder_reg, scan_flags_field_offset);
  }
}

LocationSummary* LoadIndexedInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  LocationSummary* locs = new (zone) LocationSummary(zone, 2, 1, LocationSummary::kNoCall);
  locs->set_in(0, Location::RequiresRegister()); locs->set_in(1, LocationWritableRegisterOrSmiConstant(index()));
  locs->set_temp(0, Location::RequiresRegister());
  if (representation() == kUnboxedDouble) locs->set_out(0, Location::RequiresFpuRegister()); else locs->set_out(0, Location::RequiresRegister());
  return locs;
}

void LoadIndexedInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const Register array = locs()->in(0).reg(); const Location index = locs()->in(1); const Register address = locs()->temp(0).reg();
  if (IsUntagged()) {
    __ LoadFieldFromOffset(address, array, compiler::target::TypedData::payload_offset());
    if (index.IsConstant()) __ AddImmediate(address, address, Smi::Cast(index.constant()).Value() * index_scale());
    else { ASSERT(index.IsRegister()); __ Move(TMP, index.reg()); __ SmiUntag(TMP);
      if (index_scale() == 1) __ add_d(address, address, TMP);
      else if (index_scale() == 2) __ add_d(address, address, TMP);  // SmiUntag already did /2
      else { if (index_scale() == 4) __ slli_d(TMP, TMP, 2); else if (index_scale() == 8) __ slli_d(TMP, TMP, 3); else { __ LoadImmediate(TMP2, index_scale()); __ mul_d(TMP, TMP, TMP2); } __ add_d(address, address, TMP); }
    }
  } else { ASSERT(class_id() == kArrayCid);
    if (index.IsConstant()) __ AddImmediate(address, array, compiler::target::Array::data_offset() + Smi::Cast(index.constant()).Value() * index_scale());
    else { __ AddImmediate(address, array, compiler::target::Array::data_offset()); __ Move(TMP, index.reg()); __ SmiUntag(TMP); if (index_scale() != 1) __ slli_d(TMP, TMP, Utils::ShiftForPowerOfTwo(index_scale())); __ add_d(address, address, TMP); }
  }
  if (representation() == kUnboxedDouble) __ LoadDFromOffset(locs()->out(0).fpu_reg(), address, 0);
  else __ LoadFromOffset(locs()->out(0).reg(), address, 0);
}

LocationSummary* LoadCodeUnitsInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumInputs = 2;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresRegister());
  summary->set_in(1, Location::RequiresRegister());
  summary->set_out(0, Location::RequiresRegister());
  return summary;
}
void LoadCodeUnitsInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const Register str = locs()->in(0).reg();
  const Location index = locs()->in(1);
  compiler::OperandSize sz = compiler::kByte;
  Register result = locs()->out(0).reg();
  switch (class_id()) {
    case kOneByteStringCid:
      switch (element_count()) {
        case 1: sz = compiler::kUnsignedByte; break;
        case 2: sz = compiler::kUnsignedTwoBytes; break;
        case 4: sz = compiler::kUnsignedFourBytes; break;
        default: UNREACHABLE();
      }
      break;
    case kTwoByteStringCid:
      switch (element_count()) {
        case 1: sz = compiler::kUnsignedTwoBytes; break;
        case 2: sz = compiler::kUnsignedFourBytes; break;
        default: UNREACHABLE();
      }
      break;
    default:
      UNREACHABLE();
      break;
  }
  compiler::Address element_address = __ ElementAddressForRegIndex(
      IsExternal(), class_id(), index_scale(), /*index_unboxed=*/false, str,
      index.reg(), TMP);
  __ LoadFromOffset(result, element_address.base(), element_address.offset(), sz);
  __ SmiTag(result);
}

LocationSummary* StoreIndexedInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumTemps = 1;
  LocationSummary* locs = new (zone) LocationSummary(zone, 3, kNumTemps, LocationSummary::kNoCall);
  locs->set_in(0, Location::RequiresRegister()); locs->set_in(1, Location::RequiresRegister());
  locs->set_in(2, LocationWritableRegisterOrSmiConstant(index()));
  if (kNumTemps > 0) locs->set_temp(0, Location::RequiresRegister());
  return locs;
}

void StoreIndexedInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const Register array = locs()->in(0).reg(); const Location index = locs()->in(1);
  const Register address = locs()->temp(0).reg();
  if (IsUntagged()) { __ LoadFieldFromOffset(address, array, compiler::target::TypedData::payload_offset()); if (index.IsConstant()) __ AddImmediate(address, address, Smi::Cast(index.constant()).Value() * index_scale()); else { ASSERT(index.IsRegister()); __ Move(TMP2, index.reg()); __ SmiUntag(TMP2); if (index_scale() == 1) __ add_d(address, address, TMP2); else { if (index_scale() == 2) __ slli_d(TMP2, TMP2, 1); else if (index_scale() == 4) __ slli_d(TMP2, TMP2, 2); else { __ LoadImmediate(TMP, index_scale()); __ mul_d(TMP2, TMP2, TMP); } __ add_d(address, address, TMP2); } } }
  else { ASSERT(class_id() == kArrayCid); if (index.IsConstant()) __ AddImmediate(address, array, compiler::target::Array::data_offset() + Smi::Cast(index.constant()).Value() * index_scale()); else { __ AddImmediate(address, array, compiler::target::Array::data_offset()); __ Move(TMP2, index.reg()); __ SmiUntag(TMP2); if (index_scale() != 1) __ slli_d(TMP2, TMP2, Utils::ShiftForPowerOfTwo(index_scale())); __ add_d(address, address, TMP2); } }
  __ st_d(locs()->in(1).reg(), address, 0);
}

// ==== InvokeMathCFunction, ExtractNthOutput, UnboxLane, BoxLanes, TruncDivMod, Hash ====
void InvokeMathCFunctionInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  compiler::LeafRuntimeScope rt(compiler->assembler(), 0, false);
  ASSERT(locs()->in(0).fpu_reg() == CallingConventions::kReturnFpuReg);
  if (InputCount() == 2) ASSERT(locs()->in(1).fpu_reg() == CallingConventions::kSecondReturnFpuReg);
  rt.Call(TargetFunction(), InputCount());
  ASSERT(locs()->out(0).fpu_reg() == CallingConventions::kReturnFpuReg);
}

LocationSummary* ExtractNthOutputInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  ASSERT(opt); LocationSummary* summary = new (zone) LocationSummary(zone, 1, 0, LocationSummary::kNoCall);
  if (representation() == kUnboxedDouble) { if (index() == 0) summary->set_in(0, Location::Pair(Location::RequiresFpuRegister(), Location::Any())); else { ASSERT(index() == 1); summary->set_in(0, Location::Pair(Location::Any(), Location::RequiresFpuRegister())); } summary->set_out(0, Location::RequiresFpuRegister()); }
  else { ASSERT(representation() == kTagged); if (index() == 0) summary->set_in(0, Location::Pair(Location::RequiresRegister(), Location::Any())); else { ASSERT(index() == 1); summary->set_in(0, Location::Pair(Location::Any(), Location::RequiresRegister())); } summary->set_out(0, Location::RequiresRegister()); }
  return summary;
}

void ExtractNthOutputInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  ASSERT(locs()->in(0).IsPairLocation()); PairLocation* pair = locs()->in(0).AsPairLocation(); Location in_loc = pair->At(index());
  if (representation() == kUnboxedDouble) __ fmv_d(locs()->out(0).fpu_reg(), in_loc.fpu_reg());
  else { ASSERT(representation() == kTagged); __ Move(locs()->out(0).reg(), in_loc.reg()); }
}

LocationSummary* UnboxLaneInstr::MakeLocationSummary(Zone* zone, bool opt) const { UNREACHABLE(); return NULL; }
void UnboxLaneInstr::EmitNativeCode(FlowGraphCompiler* compiler) { UNREACHABLE(); }
LocationSummary* BoxLanesInstr::MakeLocationSummary(Zone* zone, bool opt) const { UNREACHABLE(); return NULL; }
void BoxLanesInstr::EmitNativeCode(FlowGraphCompiler* compiler) { UNREACHABLE(); }

LocationSummary* TruncDivModInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  LocationSummary* summary = new (zone) LocationSummary(zone, 2, 0, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresRegister()); summary->set_in(1, Location::RequiresRegister());
  summary->set_out(0, Location::Pair(Location::RequiresRegister(), Location::RequiresRegister()));
  return summary;
}

void TruncDivModInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  ASSERT(CanDeoptimize());
  compiler::Label* deopt = compiler->AddDeoptStub(deopt_id(), ICData::kDeoptBinarySmiOp);
  const Register left = locs()->in(0).reg(); const Register right = locs()->in(1).reg();
  const PairLocation* pair = locs()->out(0).AsPairLocation();
  const Register result_div = pair->At(0).reg(); const Register result_mod = pair->At(1).reg();
  if (RangeUtils::CanBeZero(divisor_range())) { __ CompareObjectRegisters(right, ZR); __ BranchIf(EQ, deopt); }
  __ SmiUnTag(result_mod, left); __ SmiUnTag(TMP, right);
#if !defined(DART_COMPRESSED_POINTERS)
  __ div_d(result_div, result_mod, TMP);
  __ CompareImmediate(result_div, static_cast<int64_t>(0x4000000000000000));
#else
  __ div_w(result_div, result_mod, TMP);
  __ CompareImmediate(result_div, 0x40000000, compiler::kFourBytes);
#endif
  __ BranchIf(EQ, deopt);
  __ mul_d(TMP2, TMP, result_div); __ sub_d(result_mod, result_mod, TMP2);
  __ SmiTag(result_div); __ SmiTag(result_mod);
  compiler::Label done; __ CompareObjectRegisters(result_mod, ZR); __ BranchIf(GE, &done);
  if (RangeUtils::IsNegative(divisor_range())) __ sub_d(result_mod, result_mod, right);
  else if (RangeUtils::IsPositive(divisor_range())) __ add_d(result_mod, result_mod, right);
  else { compiler::Label lt; __ CompareObjectRegisters(right, ZR); __ BranchIf(LT, &lt); __ add_d(result_mod, result_mod, right); __ b(&done); __ Bind(&lt); __ sub_d(result_mod, result_mod, right); }
  __ Bind(&done);
}

static void EmitHashIntegerCodeSequence(FlowGraphCompiler* compiler, const Register value, const Register result) {
  ASSERT(value != TMP2); ASSERT(result != TMP2); ASSERT(value != result);
  __ LoadImmediate(TMP2, 0x2d51);
  __ mul_d(result, value, TMP2);
  __ mulh_du(value, value, TMP2);
  __ xor_l(result, result, value);
  __ srli_d(TMP, result, 32);
  __ xor_l(result, result, TMP);
}

LocationSummary* HashDoubleOpInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  LocationSummary* summary = new (zone) LocationSummary(zone, 1, 1, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresFpuRegister()); summary->set_temp(0, Location::RequiresFpuRegister());
  summary->set_out(0, Location::RequiresRegister());
  return summary;
}

void HashDoubleOpInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const FpuRegister value = locs()->in(0).fpu_reg(); const FpuRegister temp_double = locs()->temp(0).fpu_reg(); const Register result = locs()->out(0).reg();
  compiler::Label done, hash_double;
  __ movfr2gr_d(TMP, value); __ LoadImmediate(TMP2, 0x7FF0000000000000LL); __ and_l(TMP, TMP, TMP2);
  __ CompareImmediate(TMP, 0x7FF0000000000000LL); __ BranchIf(EQ, &hash_double);
  __ ftintrz_l_d(temp_double, value); __ ffint_d_l(temp_double, temp_double);
  __ fcmp_cond_d(temp_double, value, EQ); __ bceqz(static_cast<FRegister>(0), &hash_double);
  __ movfr2gr_d(TMP, value);
  EmitHashIntegerCodeSequence(compiler, TMP, result);
  __ AndImmediate(result, result, 0x3fffffff);
  __ b(&done);
  __ Bind(&hash_double); __ movfr2gr_d(result, value); __ srli_d(TMP, result, 32); __ xor_l(result, result, TMP); __ AndImmediate(result, result, compiler::target::kSmiMax);
  __ Bind(&done);
}

LocationSummary* HashIntegerOpInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  LocationSummary* summary = new (zone) LocationSummary(zone, 1, 0, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresRegister()); summary->set_out(0, Location::RequiresRegister());
  return summary;
}

void HashIntegerOpInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  Register value = locs()->in(0).reg(); Register result = locs()->out(0).reg();
  if (smi_) __ SmiUnTag(TMP, value); else __ LoadFieldFromOffset(TMP, value, compiler::target::Mint::value_offset());
  EmitHashIntegerCodeSequence(compiler, TMP, result);
  __ slli_d(result, result, kSmiTagShift); __ srli_d(result, result, kSmiTagShift + 2); __ slli_d(result, result, kSmiTagShift);
}
// ==== BranchInstr, Int64Divide, BinaryInt64Op, shift helpers ====
LocationSummary* BranchInstr::MakeLocationSummary(Zone* zone, bool opt) const { condition()->InitializeLocationSummary(zone, opt); condition()->locs()->set_out(0, Location::NoLocation()); return condition()->locs(); }
void BranchInstr::EmitNativeCode(FlowGraphCompiler* compiler) { condition()->EmitBranchCode(compiler, this); }

class Int64DivideSlowPath : public ThrowErrorSlowPathCode {
 public:
  Int64DivideSlowPath(BinaryInt64OpInstr* instruction, Register divisor, Register tmp, Register out) : ThrowErrorSlowPathCode(instruction, kIntegerDivisionByZeroExceptionRuntimeEntry), is_mod_(instruction->op_kind() == Token::kMOD), divisor_(divisor), tmp_(tmp), out_(out) {}
  void EmitNativeCode(FlowGraphCompiler* compiler) override {
    if (has_divide_by_zero()) ThrowErrorSlowPathCode::EmitNativeCode(compiler);
    else { __ Bind(entry_label()); if (compiler::Assembler::EmittingComments()) __ Comment("slow path %s operation (no throw)", name()); }
    if (has_adjust_sign()) { __ Bind(adjust_sign_label()); if (instruction()->AsBinaryInt64Op()->RightOperandIsPositive()) __ add_d(out_, out_, divisor_); else if (instruction()->AsBinaryInt64Op()->RightOperandIsNegative()) __ sub_d(out_, out_, divisor_); else { compiler::Label lt_label; __ CompareRegisters(divisor_, ZR); __ BranchIf(LT, &lt_label); __ add_d(out_, out_, divisor_); __ b(exit_label()); __ Bind(&lt_label); __ sub_d(out_, out_, divisor_); } __ b(exit_label()); }
  }
  const char* name() override { return "int64 divide"; }
  bool has_divide_by_zero() { return instruction()->AsBinaryInt64Op()->RightOperandCanBeZero(); }
  bool has_adjust_sign() { return is_mod_; }
  bool is_needed() { return has_divide_by_zero() || has_adjust_sign(); }
  compiler::Label* adjust_sign_label() { ASSERT(has_adjust_sign()); return &adjust_sign_label_; }
 private: bool is_mod_; Register divisor_, tmp_, out_; compiler::Label adjust_sign_label_;
};

static void EmitInt64ModTruncDiv(FlowGraphCompiler* compiler, BinaryInt64OpInstr* instruction, Token::Kind op_kind, Register left, Register right, Register tmp, Register out) {
  ASSERT(op_kind == Token::kMOD || op_kind == Token::kTRUNCDIV);
  if (FLAG_optimization_level <= 2) {}
  else if (auto c = instruction->right()->definition()->AsConstant()) { if (c->value().IsInteger()) { const int64_t divisor = Integer::Cast(c->value()).Value(); if (divisor <= -2 || divisor >= 2) { int64_t magic = 0, shift = 0; Utils::CalculateMagicAndShiftForDivRem(divisor, &magic, &shift); __ LoadImmediate(TMP2, magic); __ mulh_d(TMP2, TMP2, left); if (divisor > 0 && magic < 0) __ add_d(TMP2, TMP2, left); else if (divisor < 0 && magic > 0) __ sub_d(TMP2, TMP2, left); if (shift != 0) __ srai_d(TMP2, TMP2, shift); if (op_kind == Token::kTRUNCDIV) { __ srai_d(TMP, TMP2, 63); __ sub_d(out, TMP2, TMP); } else { __ srai_d(TMP, TMP2, 63); __ sub_d(TMP2, TMP2, TMP); __ LoadImmediate(TMP, divisor); __ mul_d(TMP, TMP2, TMP); __ sub_d(out, left, TMP); compiler::Label done; __ CompareRegisters(out, ZR); __ BranchIf(GE, &done); if (divisor > 0) __ add_d(out, out, TMP); else __ sub_d(out, out, TMP); __ Bind(&done); } return; } } }
  Int64DivideSlowPath* slow_path = new (Z) Int64DivideSlowPath(instruction, right, tmp, out);
  if (slow_path->has_divide_by_zero()) __ beqz(right, slow_path->entry_label());
  if (op_kind == Token::kMOD) { __ div_d(tmp, left, right); __ mul_d(tmp, tmp, right); __ sub_d(out, left, tmp); __ CompareRegisters(out, ZR); __ BranchIf(LT, slow_path->adjust_sign_label()); }
  else __ div_d(out, left, right);
  if (slow_path->is_needed()) { __ Bind(slow_path->exit_label()); compiler->AddSlowPathCode(slow_path); }
}

LocationSummary* BinaryInt64OpInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumInputs = 2;
  switch (op_kind()) {
    case Token::kMOD: case Token::kTRUNCDIV: { const intptr_t kNumTemps = (op_kind() == Token::kMOD) ? 1 : 0; LocationSummary* summary = new (zone) LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kCallOnSlowPath); summary->set_in(0, Location::RequiresRegister()); summary->set_in(1, Location::RequiresRegister()); summary->set_out(0, Location::RequiresRegister()); if (kNumTemps == 1) summary->set_temp(0, Location::RequiresRegister()); return summary; }
    case Token::kSHL: case Token::kSHR: case Token::kUSHR: { LocationSummary* summary = new (zone) LocationSummary(zone, kNumInputs, 0, LocationSummary::kCallOnSlowPath); summary->set_in(0, Location::RequiresRegister()); summary->set_in(1, RightOperandIsPositive() ? LocationRegisterOrConstant(right()) : Location::RequiresRegister()); summary->set_out(0, Location::RequiresRegister()); return summary; }
    default: { LocationSummary* summary = new (zone) LocationSummary(zone, kNumInputs, 0, LocationSummary::kNoCall); summary->set_in(0, Location::RequiresRegister()); summary->set_in(1, LocationRegisterOrConstant(right())); summary->set_out(0, Location::RequiresRegister()); return summary; }
  }
}

static void EmitShiftInt64ByConstant(FlowGraphCompiler* compiler, Token::Kind op_kind, Register left, int64_t right, Register out) {
  const int64_t masked = right & 63;
  switch (op_kind) { case Token::kSHL: __ slli_d(out, left, masked); break; case Token::kSHR: __ srai_d(out, left, masked); break; case Token::kUSHR: __ srli_d(out, left, masked); break; default: UNREACHABLE(); }
}
static void EmitShiftInt64ByRegister(FlowGraphCompiler* compiler, Token::Kind op_kind, Register left, Register right, Register out) {
  switch (op_kind) { case Token::kSHL: __ sll_d(out, left, right); break; case Token::kSHR: __ sra_d(out, left, right); break; case Token::kUSHR: __ srl_d(out, left, right); break; default: UNREACHABLE(); }
}
static void EmitShiftUint32ByConstant(FlowGraphCompiler* compiler, Token::Kind op_kind, Register left, int64_t right, Register out) {
  const int64_t masked = right & 31;
  switch (op_kind) { case Token::kSHL: __ slli_w(out, left, masked); __ slli_d(out, out, 32); __ srli_d(out, out, 32); break; case Token::kSHR: __ srai_w(out, left, masked); __ slli_d(out, out, 32); __ srli_d(out, out, 32); break; case Token::kUSHR: __ srli_w(out, left, masked); break; default: UNREACHABLE(); }
}
static void EmitShiftUint32ByRegister(FlowGraphCompiler* compiler, Token::Kind op_kind, Register left, Register right, Register out) {
  switch (op_kind) { case Token::kSHL: __ sll_w(out, left, right); __ slli_d(out, out, 32); __ srli_d(out, out, 32); break; case Token::kSHR: __ sra_w(out, left, right); __ slli_d(out, out, 32); __ srli_d(out, out, 32); break; case Token::kUSHR: __ srl_w(out, left, right); break; default: UNREACHABLE(); }
}

class ShiftInt64OpSlowPath : public ThrowErrorSlowPathCode { public: ShiftInt64OpSlowPath(BinaryInt64OpInstr* instruction) : ThrowErrorSlowPathCode(instruction, kArgumentErrorUnboxedInt64RuntimeEntry) {} const char* name() override { return "int64 shift"; } void EmitNativeCode(FlowGraphCompiler* compiler) override { ThrowErrorSlowPathCode::EmitNativeCode(compiler); } };

void BinaryInt64OpInstr::EmitShiftInt64(FlowGraphCompiler* compiler) {
  ASSERT(!CanDeoptimize()); const Register left = locs()->in(0).reg(); const Location right = locs()->in(1); const Register out = locs()->out(0).reg();
  ShiftInt64OpSlowPath* slow_path = nullptr;
  if (!RightOperandIsPositive()) { slow_path = new (Z) ShiftInt64OpSlowPath(this); compiler->AddSlowPathCode(slow_path); __ CompareRegisters(right.reg(), ZR); __ BranchIf(LT, slow_path->entry_label()); __ CompareImmediate(right.reg(), 64); __ BranchIf(GE, slow_path->entry_label()); }
  if (right.IsConstant()) { int64_t value; RELEASE_ASSERT(compiler::HasIntegerValue(right.constant(), &value)); EmitShiftInt64ByConstant(compiler, op_kind(), left, value, out); }
  else EmitShiftInt64ByRegister(compiler, op_kind(), left, right.reg(), out);
  if (slow_path != nullptr) __ Bind(slow_path->exit_label());
}

void BinaryUint32OpInstr::EmitShiftUint32(FlowGraphCompiler* compiler) {
  ASSERT(!CanDeoptimize()); const Register left = locs()->in(0).reg(); const Location right = locs()->in(1); const Register out = locs()->out(0).reg();
  if (right.IsConstant()) { int64_t value; RELEASE_ASSERT(compiler::HasIntegerValue(right.constant(), &value)); EmitShiftUint32ByConstant(compiler, op_kind(), left, value, out); }
  else EmitShiftUint32ByRegister(compiler, op_kind(), left, right.reg(), out);
}

void BinaryInt64OpInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  ASSERT(!can_overflow());
  if (op_kind() == Token::kSHL || op_kind() == Token::kSHR || op_kind() == Token::kUSHR) { EmitShiftInt64(compiler); return; }
  ASSERT(!CanDeoptimize());
  const Register left = locs()->in(0).reg(); const Location right = locs()->in(1); const Register out = locs()->out(0).reg();
  if (op_kind() == Token::kMOD || op_kind() == Token::kTRUNCDIV) { Register tmp = (op_kind() == Token::kMOD) ? locs()->temp(0).reg() : kNoRegister; EmitInt64ModTruncDiv(compiler, this, op_kind(), left, right.reg(), tmp, out); return; }
  else if (op_kind() == Token::kMUL) { Register r = TMP; if (right.IsConstant()) { int64_t value; RELEASE_ASSERT(compiler::HasIntegerValue(right.constant(), &value)); __ LoadImmediate(r, value); } else r = right.reg(); __ mul_d(out, left, r); return; }
  if (right.IsConstant()) { int64_t value; RELEASE_ASSERT(compiler::HasIntegerValue(right.constant(), &value)); switch (op_kind()) { case Token::kBIT_AND: __ andi(out, left, value); break; case Token::kBIT_OR: __ ori(out, left, value); break; case Token::kBIT_XOR: __ xori(out, left, value); break; case Token::kADD: __ addi_d(out, left, value); break; case Token::kSUB: { int64_t sub_val; RELEASE_ASSERT(compiler::HasIntegerValue(right.constant(), &sub_val)); __ addi_d(out, left, -sub_val); } break; default: UNREACHABLE(); } }
  else { Register r = right.reg(); switch (op_kind()) { case Token::kBIT_AND: __ and_l(out, left, r); break; case Token::kBIT_OR: __ or_l(out, left, r); break; case Token::kBIT_XOR: __ xor_l(out, left, r); break; case Token::kADD: __ add_d(out, left, r); break; case Token::kSUB: __ sub_d(out, left, r); break; default: UNREACHABLE(); } }
}
// ==== UnaryInt64, BinaryUint32, UnaryUint32, BinaryInt32, IntConverter, BitCast ====
LocationSummary* UnaryInt64OpInstr::MakeLocationSummary(Zone* zone, bool opt) const { LocationSummary* summary = new (zone) LocationSummary(zone, 1, 0, LocationSummary::kNoCall); summary->set_in(0, Location::RequiresRegister()); summary->set_out(0, Location::RequiresRegister()); return summary; }
void UnaryInt64OpInstr::EmitNativeCode(FlowGraphCompiler* compiler) { const Register left = locs()->in(0).reg(); const Register out = locs()->out(0).reg(); switch (op_kind()) { case Token::kBIT_NOT: __ xori(out, left, -1); break; case Token::kNEGATE: __ sub_d(out, ZR, left); break; default: UNREACHABLE(); } }

LocationSummary* BinaryUint32OpInstr::MakeLocationSummary(Zone* zone, bool opt) const { LocationSummary* summary = new (zone) LocationSummary(zone, 2, 0, LocationSummary::kNoCall); summary->set_in(0, Location::RequiresRegister()); summary->set_in(1, LocationRegisterOrConstant(right())); summary->set_out(0, Location::RequiresRegister()); return summary; }
void BinaryUint32OpInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  if (op_kind() == Token::kSHL || op_kind() == Token::kSHR || op_kind() == Token::kUSHR) { EmitShiftUint32(compiler); return; }
  Register out = locs()->out(0).reg(); Register left = locs()->in(0).reg();
  if (locs()->in(1).IsConstant()) {
    int64_t right;
    RELEASE_ASSERT(compiler::HasIntegerValue(locs()->in(1).constant(), &right));
    switch (op_kind()) {
      case Token::kBIT_AND:
        __ andi(out, left, right);
        break;
      case Token::kBIT_OR:
        __ ori(out, left, right);
        break;
      case Token::kBIT_XOR:
        __ xori(out, left, right);
        break;
      case Token::kADD:
        __ addi_w(out, left, static_cast<int32_t>(right));
        __ slli_d(out, out, 32);
        __ srli_d(out, out, 32);
        break;
      case Token::kSUB:
        __ addi_w(out, left, static_cast<int32_t>(-right));
        __ slli_d(out, out, 32);
        __ srli_d(out, out, 32);
        break;
      case Token::kMUL: {
        __ LoadImmediate(TMP, right);
        __ mul_w(out, left, TMP);
        __ slli_d(out, out, 32);
        __ srli_d(out, out, 32);
      } break;
      default:
        UNREACHABLE();
    }
  } else {
    Register right = locs()->in(1).reg();
    switch (op_kind()) {
      case Token::kBIT_AND:
        __ and_l(out, left, right);
        break;
      case Token::kBIT_OR:
        __ or_l(out, left, right);
        break;
      case Token::kBIT_XOR:
        __ xor_l(out, left, right);
        break;
      case Token::kADD:
        __ add_w(out, left, right);
        __ slli_d(out, out, 32);
        __ srli_d(out, out, 32);
        break;
      case Token::kSUB:
        __ sub_w(out, left, right);
        __ slli_d(out, out, 32);
        __ srli_d(out, out, 32);
        break;
      case Token::kMUL:
        __ mul_w(out, left, right);
        __ slli_d(out, out, 32);
        __ srli_d(out, out, 32);
        break;
      default:
        UNREACHABLE();
    }
  }
}

LocationSummary* UnaryUint32OpInstr::MakeLocationSummary(Zone* zone, bool opt) const { LocationSummary* summary = new (zone) LocationSummary(zone, 1, 0, LocationSummary::kNoCall); summary->set_in(0, Location::RequiresRegister()); summary->set_out(0, Location::RequiresRegister()); return summary; }
void UnaryUint32OpInstr::EmitNativeCode(FlowGraphCompiler* compiler) { ASSERT(op_kind() == Token::kBIT_NOT); __ xori(locs()->out(0).reg(), locs()->in(0).reg(), -1); __ slli_d(locs()->out(0).reg(), locs()->out(0).reg(), 32); __ srli_d(locs()->out(0).reg(), locs()->out(0).reg(), 32); }
DEFINE_UNIMPLEMENTED_INSTRUCTION(BinaryInt32OpInstr)

LocationSummary* IntConverterInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  LocationSummary* summary = new (zone) LocationSummary(zone, 1, 0, LocationSummary::kNoCall);
  if (from() == kUntagged || to() == kUntagged) { ASSERT((from() == kUntagged && to() == kUnboxedIntPtr) || (from() == kUnboxedIntPtr && to() == kUntagged)); }
  else if (from() == kUnboxedInt64) ASSERT(to() == kUnboxedUint32 || to() == kUnboxedInt32);
  else if (to() == kUnboxedInt64) ASSERT(from() == kUnboxedInt32 || from() == kUnboxedUint32);
  else { ASSERT(to() == kUnboxedUint32 || to() == kUnboxedInt32); ASSERT(from() == kUnboxedUint32 || from() == kUnboxedInt32); }
  summary->set_in(0, Location::RequiresRegister()); summary->set_out(0, Location::SameAsFirstInput());
  return summary;
}
void IntConverterInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  ASSERT(from() != to());
  const bool is_nop = (from() == kUntagged && to() == kUnboxedIntPtr) || (from() == kUnboxedIntPtr && to() == kUntagged);
  if (is_nop) { ASSERT(locs()->in(0).reg() == locs()->out(0).reg()); return; }
  const Register value = locs()->in(0).reg(); const Register out = locs()->out(0).reg();
  if (from() == kUnboxedInt32 && to() == kUnboxedUint32) { if (out != value) __ Move(out, value); }
  else if (from() == kUnboxedUint32 && to() == kUnboxedInt32) { if (out != value) __ Move(out, value); }
  else if (from() == kUnboxedInt64) { if (out != value) __ Move(out, value); if (to() == kUnboxedInt32) __ addi_w(out, out, 0); else { ASSERT(to() == kUnboxedUint32); __ slli_d(out, out, 32); __ srli_d(out, out, 32); } }
  else if (to() == kUnboxedInt64) { if (out != value) __ Move(out, value); if (from() == kUnboxedUint32) { __ slli_d(out, out, 32); __ srli_d(out, out, 32); } else { ASSERT(from() == kUnboxedInt32); __ addi_w(out, out, 0); } }
  else UNREACHABLE();
}

LocationSummary* BitCastInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  LocationSummary* summary = new (zone) LocationSummary(zone, InputCount(), 0, LocationSummary::kNoCall);
  switch (from()) { case kUnboxedInt32: case kUnboxedInt64: summary->set_in(0, Location::RequiresRegister()); break; case kUnboxedFloat: case kUnboxedDouble: summary->set_in(0, Location::RequiresFpuRegister()); break; default: UNREACHABLE(); }
  switch (to()) { case kUnboxedInt32: case kUnboxedInt64: summary->set_out(0, Location::RequiresRegister()); break; case kUnboxedFloat: case kUnboxedDouble: summary->set_out(0, Location::RequiresFpuRegister()); break; default: UNREACHABLE(); }
  return summary;
}
void BitCastInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  switch (from()) { case kUnboxedInt32: { ASSERT(to() == kUnboxedFloat); __ movgr2fr_w(locs()->out(0).fpu_reg(), locs()->in(0).reg()); break; } case kUnboxedFloat: { ASSERT(to() == kUnboxedInt32); __ movfr2gr_s(locs()->out(0).reg(), locs()->in(0).fpu_reg()); break; } case kUnboxedInt64: { ASSERT(to() == kUnboxedDouble); __ movgr2fr_d(locs()->out(0).fpu_reg(), locs()->in(0).reg()); break; } case kUnboxedDouble: { ASSERT(to() == kUnboxedInt64); __ movfr2gr_d(locs()->out(0).reg(), locs()->in(0).fpu_reg()); break; } default: UNREACHABLE(); }
}
// ==== Goto, IndirectGoto, StrictCompare, ConditionInstr, Boolean ====
LocationSummary* GotoInstr::MakeLocationSummary(Zone* zone, bool opt) const { return new (zone) LocationSummary(zone, 0, 0, LocationSummary::kNoCall); }
void GotoInstr::EmitNativeCode(FlowGraphCompiler* compiler) { if (!compiler->is_optimizing()) { if (FLAG_reorder_basic_blocks) compiler->EmitEdgeCounter(block()->preorder_number()); compiler->AddCurrentDescriptor(UntaggedPcDescriptors::kDeopt, GetDeoptId(), InstructionSource()); } if (HasParallelMove()) parallel_move()->EmitNativeCode(compiler); if (!compiler->CanFallThroughTo(successor())) __ b(compiler->GetJumpLabel(successor())); }

LocationSummary* IndirectGotoInstr::MakeLocationSummary(Zone* zone, bool opt) const { LocationSummary* summary = new (zone) LocationSummary(zone, 1, 2, LocationSummary::kNoCall); summary->set_in(0, Location::RequiresRegister()); summary->set_temp(0, Location::RequiresRegister()); summary->set_temp(1, Location::RequiresRegister()); return summary; }
void IndirectGotoInstr::EmitNativeCode(FlowGraphCompiler* compiler) { Register index_reg = locs()->in(0).reg(); Register target_address_reg = locs()->temp(0).reg(); Register offset_reg = locs()->temp(1).reg(); ASSERT(RequiredInputRepresentation(0) == kTagged); __ LoadObject(offset_reg, offsets_); const auto element_address = __ ElementAddressForRegIndex(false, kTypedDataInt32ArrayCid, 4, false, offset_reg, index_reg, TMP); __ LoadFromOffset(offset_reg, element_address.base(), element_address.offset(), compiler::kFourBytes); const intptr_t entry_offset = __ CodeSize(); __ pcaddu12i(target_address_reg, 0); __ AddImmediate(target_address_reg, -entry_offset); __ add_d(target_address_reg, target_address_reg, offset_reg); __ jr(target_address_reg); }

LocationSummary* StrictCompareInstr::MakeLocationSummary(Zone* zone, bool opt) const { const intptr_t kNumTemps = 0; if (needs_number_check()) { LocationSummary* locs = new (zone) LocationSummary(zone, 2, kNumTemps, LocationSummary::kCall); locs->set_in(0, Location::RegisterLocation(CallingConventions::kReturnReg)); locs->set_in(1, Location::RegisterLocation(CallingConventions::kSecondReturnReg)); locs->set_out(0, Location::RegisterLocation(CallingConventions::kReturnReg)); return locs; } LocationSummary* locs = new (zone) LocationSummary(zone, 2, kNumTemps, LocationSummary::kNoCall); locs->set_in(0, Location::RequiresRegister()); locs->set_in(1, LocationRegisterOrConstant(right())); locs->set_out(0, Location::RequiresRegister()); return locs; }

Condition StrictCompareInstr::EmitComparisonCodeRegConstant(FlowGraphCompiler* compiler, BranchLabels labels, Register reg, const Object& obj) { return compiler->EmitEqualityRegConstCompare(reg, obj, needs_number_check(), source(), deopt_id()); }

LocationSummary* TestCidsInstr::MakeLocationSummary(Zone* zone, bool opt) const { LocationSummary* locs = new (zone) LocationSummary(zone, 1, 1, LocationSummary::kNoCall); locs->set_in(0, Location::RequiresRegister()); locs->set_temp(0, Location::RequiresRegister()); locs->set_out(0, Location::RequiresRegister()); return locs; }

Condition TestCidsInstr::EmitConditionCode(FlowGraphCompiler* compiler, BranchLabels labels) { ASSERT(kind() == Token::kIS || kind() == Token::kISNOT); const Register val_reg = locs()->in(0).reg(); const Register cid_reg = locs()->temp(0).reg(); compiler::Label* deopt = CanDeoptimize() ? compiler->AddDeoptStub(deopt_id(), ICData::kDeoptTestCids) : nullptr; const intptr_t true_result = (kind() == Token::kIS) ? 1 : 0; const ZoneGrowableArray<intptr_t>& data = cid_results(); ASSERT(data[0] == kSmiCid); bool result = data[1] == true_result; __ BranchIfSmi(val_reg, result ? labels.true_label : labels.false_label); __ LoadClassId(cid_reg, val_reg); for (intptr_t i = 2; i < data.length(); i += 2) { result = data[i + 1] == true_result; __ CompareImmediate(cid_reg, data[i]); __ BranchIf(EQ, result ? labels.true_label : labels.false_label); } if (deopt != nullptr) __ b(deopt); return kInvalidCondition; }

void ConditionInstr::EmitNativeCode(FlowGraphCompiler* compiler) { compiler::Label is_true, is_false; BranchLabels labels = { &is_true, &is_false, &is_false }; Condition true_condition = EmitConditionCode(compiler, labels); Register result = locs()->out(0).reg(); if (true_condition != kInvalidCondition) EmitBranchOnCondition(compiler, true_condition, labels); compiler::Label done; __ Bind(&is_false); __ LoadObject(result, Bool::False()); __ b(&done, compiler::Assembler::kNearJump); __ Bind(&is_true); __ LoadObject(result, Bool::True()); __ Bind(&done); }

void ConditionInstr::EmitBranchCode(FlowGraphCompiler* compiler, BranchInstr* branch) { BranchLabels labels = compiler->CreateBranchLabels(branch); Condition true_condition = EmitConditionCode(compiler, labels); if (true_condition != kInvalidCondition) EmitBranchOnCondition(compiler, true_condition, labels); }

LocationSummary* BooleanNegateInstr::MakeLocationSummary(Zone* zone, bool opt) const { return LocationSummary::Make(zone, 1, Location::RequiresRegister(), LocationSummary::kNoCall); }
void BooleanNegateInstr::EmitNativeCode(FlowGraphCompiler* compiler) { __ xori(locs()->out(0).reg(), locs()->in(0).reg(), compiler::target::ObjectAlignment::kBoolValueMask); }

LocationSummary* BoolToIntInstr::MakeLocationSummary(Zone* zone, bool opt) const { UNREACHABLE(); return NULL; }
void BoolToIntInstr::EmitNativeCode(FlowGraphCompiler* compiler) { UNREACHABLE(); }
LocationSummary* IntToBoolInstr::MakeLocationSummary(Zone* zone, bool opt) const { UNREACHABLE(); return NULL; }
void IntToBoolInstr::EmitNativeCode(FlowGraphCompiler* compiler) { UNREACHABLE(); }

LocationSummary* AllocateObjectInstr::MakeLocationSummary(Zone* zone, bool opt) const { const intptr_t kNumInputs = (type_arguments() != nullptr) ? 1 : 0; LocationSummary* locs = new (zone) LocationSummary(zone, kNumInputs, 0, LocationSummary::kCall); if (type_arguments() != nullptr) locs->set_in(kTypeArgumentsPos, Location::RegisterLocation(AllocateObjectABI::kTypeArgumentsReg)); locs->set_out(0, Location::RegisterLocation(AllocateObjectABI::kResultReg)); return locs; }
void AllocateObjectInstr::EmitNativeCode(FlowGraphCompiler* compiler) { if (type_arguments() != nullptr) { TypeUsageInfo* type_usage_info = compiler->thread()->type_usage_info(); if (type_usage_info != nullptr) RegisterTypeArgumentsUse(compiler->function(), type_usage_info, cls_, type_arguments()->definition()); } const Code& stub = Code::ZoneHandle(compiler->zone(), StubCode::GetAllocationStubForClass(cls())); compiler->GenerateStubCall(source(), stub, UntaggedPcDescriptors::kOther, locs(), deopt_id(), env()); }

void DebugStepCheckInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
#ifdef PRODUCT
  UNREACHABLE();
#else
  ASSERT(!compiler->is_optimizing());
  __ JumpAndLinkPatchable(StubCode::DebugStepCheck());
  compiler->AddCurrentDescriptor(stub_kind_, deopt_id_, source());
  compiler->RecordSafepoint(locs());
#endif
}
// ==== MemoryCopy ====
LocationSummary* MemoryCopyInstr::MakeLocationSummary(Zone* zone, bool opt) const { ASSERT((!IsTypedDataBaseClassId(src_cid_) && !IsTypedDataBaseClassId(dest_cid_)) || opt); LocationSummary* locs = new (zone) LocationSummary(zone, 5, 2, LocationSummary::kNoCall); locs->set_in(kSrcPos, Location::RequiresRegister()); locs->set_in(kDestPos, Location::RequiresRegister()); locs->set_in(kSrcStartPos, LocationRegisterOrSmiConstant(src_start())); locs->set_in(kDestStartPos, LocationRegisterOrSmiConstant(dest_start())); locs->set_in(kLengthPos, LocationWritableRegisterOrSmiConstant(length(), 0, 4)); locs->set_temp(0, Location::RequiresRegister()); locs->set_temp(1, Location::RequiresRegister()); return locs; }

void MemoryCopyInstr::PrepareLengthRegForLoop(FlowGraphCompiler* compiler, Register length_reg, compiler::Label* done) { __ BranchIfZero(length_reg, done); }


static void CopyBytesLoong64(FlowGraphCompiler* compiler, Register dest_reg, Register src_reg, intptr_t count, bool reversed) { ASSERT(Utils::IsPowerOfTwo(count)); compiler::OperandSize sz; if (count == 1) sz = compiler::kByte; else if (count == 2) sz = compiler::kTwoBytes; else if (count == 4) sz = compiler::kFourBytes; else sz = compiler::kEightBytes; const intptr_t offset = (reversed ? -1 : 1) * count; const intptr_t initial = reversed ? offset : 0; __ LoadFromOffset(TMP, src_reg, initial, sz); __ addi_d(src_reg, src_reg, offset); __ StoreToOffset(TMP, dest_reg, initial, sz); __ addi_d(dest_reg, dest_reg, offset); }

void MemoryCopyInstr::EmitLoopCopy(FlowGraphCompiler* compiler, Register dest_reg, Register src_reg, Register length_reg, compiler::Label* done, compiler::Label* copy_forwards) { const bool reversed = copy_forwards != nullptr; const intptr_t element_size = element_size_; const intptr_t shift = Utils::ShiftForPowerOfTwo(element_size) - (unboxed_inputs() ? 0 : kSmiTagShift); if (reversed) { if (!unboxed_inputs()) { __ Move(TMP, length_reg); __ SmiUntag(TMP); } else { __ Move(TMP, length_reg); } if (shift < 0) __ srai_d(TMP, TMP, -shift); else if (shift > 0) __ slli_d(TMP, TMP, shift); __ add_d(TMP, src_reg, TMP); __ CompareRegisters(dest_reg, TMP); __ BranchIf(GE, copy_forwards); __ Move(src_reg, TMP); if (shift < 0) { __ srai_d(TMP, length_reg, -shift); __ add_d(dest_reg, dest_reg, TMP); } else if (shift > 0) { __ slli_d(TMP, length_reg, shift); __ add_d(dest_reg, dest_reg, TMP); } } const intptr_t kChunkSize = 16; ASSERT(kChunkSize >= element_size); { __ Comment("LoongArch copy loop"); compiler::Label loop; __ Bind(&loop); CopyBytesLoong64(compiler, dest_reg, src_reg, kChunkSize, reversed); if (!unboxed_inputs()) { const intptr_t loop_subtract = (kChunkSize / element_size) << kSmiTagShift; __ CompareImmediate(length_reg, loop_subtract); } else { __ AddImmediate(length_reg, length_reg, -kChunkSize); __ CompareImmediate(length_reg, 0); } __ BranchIf(HI, &loop); } if (!unboxed_inputs()) { const intptr_t remaining = element_size; for (intptr_t i = element_size; i < kChunkSize; i *= 2) { compiler::Label skip; if (i >= remaining) break; } } if (done != nullptr) __ b(done); }

void MemoryCopyInstr::EmitComputeStartPointer(FlowGraphCompiler* compiler, classid_t array_cid, Register array_reg, Register payload_reg, Representation array_rep, Location start_loc) { if (IsTypedDataBaseClassId(array_cid)) { __ addi_d(payload_reg, array_reg, compiler::target::TypedData::payload_offset() - kHeapObjectTag); } else { ASSERT(array_cid == kArrayCid); __ addi_d(payload_reg, array_reg, compiler::target::Array::data_offset() - kHeapObjectTag); } if (IsTypedDataBaseClassId(array_cid) || array_rep == kTagged) { if (start_loc.IsConstant()) { ASSERT(array_rep == kTagged); intptr_t start_value = Smi::Cast(start_loc.constant()).Value(); if (start_value != 0) { if (element_size() != 1) start_value *= element_size(); __ AddImmediate(payload_reg, payload_reg, start_value); } } else { ASSERT(start_loc.IsRegister()); if (array_rep == kTagged) __ SmiUnTag(TMP, start_loc.reg()); else __ Move(TMP, start_loc.reg()); if (element_size() != 1) __ slli_d(TMP, TMP, Utils::ShiftForPowerOfTwo(element_size())); __ add_d(payload_reg, payload_reg, TMP); } } else { if (start_loc.IsConstant()) { const Smi& smi = Smi::Cast(start_loc.constant()); if (smi.Value() != 0) { __ LoadImmediate(TMP, smi.Value() * element_size()); __ add_d(payload_reg, payload_reg, TMP); } } else { ASSERT(start_loc.IsRegister()); if (element_size() != 1) __ slli_d(TMP, start_loc.reg(), Utils::ShiftForPowerOfTwo(element_size())); else __ Move(TMP, start_loc.reg()); __ add_d(payload_reg, payload_reg, TMP); } } }
// ==== UnarySmiOp, Unbox, AllocateContext, GraphEntry ====
LocationSummary* UnarySmiOpInstr::MakeLocationSummary(Zone* zone, bool opt) const { LocationSummary* summary = new (zone) LocationSummary(zone, 1, 0, LocationSummary::kNoCall); summary->set_in(0, Location::RequiresRegister()); summary->set_out(0, Location::SameAsFirstInput()); return summary; }
void UnarySmiOpInstr::EmitNativeCode(FlowGraphCompiler* compiler) { const Register value = locs()->in(0).reg(); switch (op_kind()) { case Token::kNEGATE: { compiler::Label* deopt = compiler->AddDeoptStub(deopt_id(), ICData::kDeoptUnaryOp); __ CompareObject(value, Smi::Handle(Smi::New(-Smi::kMaxValue))); __ BranchIf(EQ, deopt); __ sub_d(value, ZR, value); break; } case Token::kBIT_NOT: __ xori(value, value, -1); __ andi(value, value, ~kSmiTagMask); break; default: UNREACHABLE(); } }

LocationSummary* UnboxInstr::MakeLocationSummary(Zone* zone, bool opt) const { const intptr_t kNumTemps = 1; LocationSummary* summary = new (zone) LocationSummary(zone, 1, kNumTemps, LocationSummary::kNoCall); summary->set_in(0, Location::RequiresRegister());
  summary->set_temp(0, Location::RequiresRegister());
  switch (representation()) { case kUnboxedInt64: summary->set_out(0, Location::RequiresRegister()); break; case kUnboxedDouble: summary->set_out(0, Location::RequiresFpuRegister()); break; default: UNREACHABLE(); } return summary; }
void UnboxInstr::EmitLoadFromBox(FlowGraphCompiler* compiler) { const Register box = locs()->in(0).reg(); switch (representation()) { case kUnboxedInt64: __ LoadFieldFromOffset(locs()->out(0).reg(), box, compiler::target::Mint::value_offset()); break; case kUnboxedDouble: __ LoadDFromOffset(locs()->out(0).fpu_reg(), box, compiler::target::Double::value_offset()); break; default: UNREACHABLE(); } }
void UnboxInstr::EmitSmiConversion(FlowGraphCompiler* compiler) { __ SmiUnTag(locs()->out(0).reg(), locs()->in(0).reg()); }
void UnboxInstr::EmitLoadInt32FromBoxOrSmi(FlowGraphCompiler* compiler) { const Register box = locs()->in(0).reg(); const Register out = locs()->out(0).reg(); compiler::Label done; __ BranchIfSmi(box, &done); __ LoadFieldFromOffset(out, box, compiler::target::Mint::value_offset()); __ Bind(&done); }
void UnboxInstr::EmitLoadInt64FromBoxOrSmi(FlowGraphCompiler* compiler) { const Register box = locs()->in(0).reg(); const Register out = locs()->out(0).reg(); compiler::Label done; __ BranchIfSmi(box, &done); __ LoadFieldFromOffset(out, box, compiler::target::Mint::value_offset()); __ Bind(&done); }

LocationSummary* AllocateUninitializedContextInstr::MakeLocationSummary(Zone* zone, bool opt) const { const intptr_t kNumInputs = 0; const intptr_t kNumTemps = 3; LocationSummary* locs = new (zone) LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kCallOnSlowPath); locs->set_temp(0, Location::RequiresRegister()); locs->set_temp(1, Location::RequiresRegister()); locs->set_temp(2, Location::RequiresRegister()); locs->set_out(0, Location::RegisterLocation(A0)); return locs; }

class AllocateContextSlowPathLoong64 : public TemplateSlowPathCode<AllocateUninitializedContextInstr> { public: explicit AllocateContextSlowPathLoong64(AllocateUninitializedContextInstr* instruction) : TemplateSlowPathCode(instruction) {} virtual void EmitNativeCode(FlowGraphCompiler* compiler) { __ Comment("AllocateContextSlowPath"); __ Bind(entry_label()); LocationSummary* locs = instruction()->locs(); locs->live_registers()->Remove(locs->out(0)); compiler->SaveLiveRegisters(locs); auto slow_path_env = compiler->SlowPathEnvironmentFor(instruction(), 0); ASSERT(slow_path_env != nullptr); __ LoadImmediate(T1, instruction()->num_context_variables()); compiler->GenerateStubCall(instruction()->source(), StubCode::AllocateContext(), UntaggedPcDescriptors::kOther, locs, instruction()->deopt_id(), slow_path_env); ASSERT(instruction()->locs()->out(0).reg() == A0); compiler->RestoreLiveRegisters(instruction()->locs()); __ b(exit_label()); } };

void AllocateUninitializedContextInstr::EmitNativeCode(FlowGraphCompiler* compiler) { Register temp0 = locs()->temp(0).reg(); Register temp1 = locs()->temp(1).reg(); Register temp2 = locs()->temp(2).reg(); Register result = locs()->out(0).reg(); AllocateContextSlowPathLoong64* slow_path = new AllocateContextSlowPathLoong64(this); compiler->AddSlowPathCode(slow_path); if (!FLAG_use_slow_path && FLAG_inline_alloc) { __ LoadImmediate(temp0, num_context_variables()); __ SmiTag(temp0); __ TryAllocateArray(kContextCid, slow_path->entry_label(), compiler::Assembler::kFarJump, result, temp0, ZR, temp1, temp2); __ LoadImmediate(temp0, num_context_variables()); __ st_w(temp0, result, Context::num_variables_offset()); } else __ b(slow_path->entry_label()); __ Bind(slow_path->exit_label()); }

void GraphEntryInstr::EmitNativeCode(FlowGraphCompiler* compiler) { BlockEntryInstr* entry = normal_entry(); if (entry != nullptr) { if (!compiler->CanFallThroughTo(entry)) FATAL("Checked function entry must have no offset"); } else { entry = osr_entry(); if (!compiler->CanFallThroughTo(entry)) __ b(compiler->GetJumpLabel(entry)); } }




// ==== Missing stubs (to be fully implemented) ====

LocationSummary* BinaryDoubleOpInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumInputs = 2;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresFpuRegister());
  summary->set_in(1, Location::RequiresFpuRegister());
  summary->set_out(0, Location::RequiresFpuRegister());
  return summary;
}
void BinaryDoubleOpInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const FRegister left = locs()->in(0).fpu_reg();
  const FRegister right = locs()->in(1).fpu_reg();
  const FRegister result = locs()->out(0).fpu_reg();
  switch (op_kind()) {
    case Token::kADD:
      __ fadd_d(result, left, right);
      break;
    case Token::kSUB:
      __ fsub_d(result, left, right);
      break;
    case Token::kMUL:
      __ fmul_d(result, left, right);
      break;
    case Token::kDIV:
      __ fdiv_d(result, left, right);
      break;
    default:
      UNREACHABLE();
  }
}

LocationSummary* DoubleTestOpInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresFpuRegister());
  summary->set_out(0, Location::RequiresRegister());
  return summary;
}
Condition DoubleTestOpInstr::EmitConditionCode(FlowGraphCompiler* compiler, BranchLabels labels) {
  ASSERT(compiler->is_optimizing());
  const FpuRegister value = locs()->in(0).fpu_reg();
  const bool is_negated = kind() != Token::kEQ;

  switch (op_kind()) {
    case MethodRecognizer::kDouble_getIsNaN: {
      // NaN: exponent all 1s (bits 52-62) and mantissa (bits 0-51) non-zero.
      __ movfr2gr_d(TMP, value);
      __ LoadImmediate(TMP2, 0x7FF0000000000000LL);
      __ and_l(TMP2, TMP, TMP2);
      __ CompareImmediate(TMP2, 0x7FF0000000000000LL);
      // If exponent != 0x7FF, it is not NaN.
      __ BranchIf(NOT_EQUAL, is_negated ? labels.true_label : labels.false_label);
      // Exponent is 0x7FF, test mantissa.
      __ LoadImmediate(TMP2, 0x000FFFFFFFFFFFFFLL);
      __ and_l(TMP, TMP, TMP2);
      __ CompareImmediate(TMP, 0);
      // Mantissa != 0 means NaN, mantissa == 0 means infinity.
      return is_negated ? EQUAL : NOT_EQUAL;
    }
    case MethodRecognizer::kDouble_getIsInfinite: {
      // Infinity: exponent all 1s and mantissa zero.
      // Mask off the sign bit and compare with +infinity.
      __ movfr2gr_d(TMP, value);
      __ LoadImmediate(TMP2, 0x7FFFFFFFFFFFFFFFLL);
      __ and_l(TMP, TMP, TMP2);
      __ CompareImmediate(TMP, 0x7FF0000000000000LL);
      return is_negated ? NOT_EQUAL : EQUAL;
    }
    case MethodRecognizer::kDouble_getIsNegative: {
      // Negative: sign bit set and not NaN.
      __ movfr2gr_d(TMP, value);
      // First check for NaN (exponent all 1s and mantissa non-zero).
      __ LoadImmediate(TMP2, 0x7FF0000000000000LL);
      __ and_l(TMP2, TMP, TMP2);
      __ CompareImmediate(TMP2, 0x7FF0000000000000LL);
      __ BranchIf(NOT_EQUAL, is_negated ? labels.true_label : labels.false_label);
      __ LoadImmediate(TMP2, 0x000FFFFFFFFFFFFFLL);
      __ and_l(TMP2, TMP, TMP2);
      __ CompareImmediate(TMP2, 0);
      __ BranchIf(NOT_EQUAL, is_negated ? labels.true_label : labels.false_label);
      // Not NaN, check sign bit (bit 63).
      __ CompareImmediate(TMP, 0);
      return is_negated ? GREATER_EQUAL : LESS;
    }
    default:
      UNREACHABLE();
  }
  return kInvalidCondition;
}

LocationSummary* InvokeMathCFunctionInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  ASSERT((InputCount() == 1) || (InputCount() == 2));
  const intptr_t kNumTemps = 0;
  LocationSummary* result = new (zone) LocationSummary(
      zone, InputCount(), kNumTemps, LocationSummary::kNativeLeafCall);
  result->set_in(0, Location::FpuRegisterLocation(FA0));
  if (InputCount() == 2) {
    result->set_in(1, Location::FpuRegisterLocation(FA1));
  }
  result->set_out(0, Location::FpuRegisterLocation(FA0));
  return result;
}

LocationSummary* CatchBlockEntryInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  return new (zone) LocationSummary(zone, 0, 0, LocationSummary::kCall);
}
void CatchBlockEntryInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  __ Bind(compiler->GetJumpLabel(this));
  compiler->AddExceptionHandler(this);

  // Restore SP from FP as we are coming from a throw and the code for
  // popping arguments has not been run.
  const intptr_t fp_sp_dist =
      (compiler::target::frame_layout.first_local_from_fp + 1 -
       compiler->StackSize()) *
      kWordSize;
  ASSERT(fp_sp_dist <= 0);
  __ AddImmediate(SP, FP, fp_sp_dist);

  // Parallel moves are using updated SP.
  if (HasParallelMove()) {
    parallel_move()->EmitNativeCode(compiler);
  }

  if (!compiler->is_optimizing()) {
    if (raw_exception_var_ != nullptr) {
      __ st_d(
          kExceptionObjectReg, FP,
          compiler::target::FrameOffsetInBytesForVariable(raw_exception_var_));
    }
    if (raw_stacktrace_var_ != nullptr) {
      __ st_d(
          kStackTraceObjectReg, FP,
          compiler::target::FrameOffsetInBytesForVariable(raw_stacktrace_var_));
    }
  }
}

LocationSummary* CheckFieldImmutabilityInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 1;
  LocationSummary* summary = new (zone) LocationSummary(
      zone, kNumInputs, kNumTemps, LocationSummary::kCallOnSlowPath);
  summary->set_in(
      0, Location::RegisterLocation(EnsureDeeplyImmutableStubABI::kValueReg));
  summary->set_temp(
      0, Location::RegisterLocation(EnsureDeeplyImmutableStubABI::kTempReg));
  return summary;
}

void CheckFieldImmutabilityInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const Register value = locs()->in(0).reg();
  const Register temp = locs()->temp(0).reg();

  ASSERT(value == EnsureDeeplyImmutableStubABI::kValueReg);
  ASSERT(temp == EnsureDeeplyImmutableStubABI::kTempReg);

  auto slow_path = new EnsureDeeplyImmutableSlowPath(this, value);
  compiler->AddSlowPathCode(slow_path);

  __ BranchIfSmi(value, slow_path->exit_label(),
                 compiler::Assembler::kNearJump);
  __ ld_bu(temp, value, compiler::target::Object::tags_offset());
  __ andi(temp, temp, (1 << compiler::target::UntaggedObject::kDeeplyImmutableBit));
  __ beqz(temp, slow_path->entry_label());
  __ Bind(slow_path->exit_label());
}

LocationSummary* StoreStaticFieldInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* locs = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  locs->set_in(0, Location::RequiresRegister());
  return locs;
}

void StoreStaticFieldInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const Register value = locs()->in(0).reg();
  compiler->used_static_fields().Add(&field());
  __ LoadFromOffset(TMP, THR, compiler::target::Thread::field_table_values_offset());
  __ StoreToOffset(value, TMP, compiler::target::FieldTable::OffsetOf(field()));
}

LocationSummary* InstanceOfInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumInputs = 3;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kCall);
  summary->set_in(0, Location::RegisterLocation(TypeTestABI::kInstanceReg));
  summary->set_in(1, Location::RegisterLocation(
                         TypeTestABI::kInstantiatorTypeArgumentsReg));
  summary->set_in(
      2, Location::RegisterLocation(TypeTestABI::kFunctionTypeArgumentsReg));
  summary->set_out(
      0, Location::RegisterLocation(TypeTestABI::kInstanceOfResultReg));
  return summary;
}

void InstanceOfInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  ASSERT(locs()->in(0).reg() == TypeTestABI::kInstanceReg);
  ASSERT(locs()->in(1).reg() == TypeTestABI::kInstantiatorTypeArgumentsReg);
  ASSERT(locs()->in(2).reg() == TypeTestABI::kFunctionTypeArgumentsReg);
  compiler->GenerateInstanceOf(source(), deopt_id(), env(), type(), locs());
  ASSERT(locs()->out(0).reg() == TypeTestABI::kInstanceOfResultReg);
}

LocationSummary* CreateArrayInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumInputs = 2;
  const intptr_t kNumTemps = 0;
  LocationSummary* locs = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kCall);
  locs->set_in(kTypeArgumentsPos,
               Location::RegisterLocation(AllocateArrayABI::kTypeArgumentsReg));
  locs->set_in(kLengthPos,
               Location::RegisterLocation(AllocateArrayABI::kLengthReg));
  locs->set_out(0, Location::RegisterLocation(AllocateArrayABI::kResultReg));
  return locs;
}

void CreateArrayInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  compiler->GenerateStubCall(source(), StubCode::AllocateArray(),
                             UntaggedPcDescriptors::kOther, locs(),
                             DeoptId::kNone,
                             compiler->SlowPathEnvironmentFor(this, 0));
}

LocationSummary* AllocateContextInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumInputs = 0;
  const intptr_t kNumTemps = 1;
  LocationSummary* locs = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kCall);
  locs->set_temp(0, Location::RegisterLocation(T1));
  locs->set_out(0, Location::RegisterLocation(A0));
  return locs;
}

void AllocateContextInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  ASSERT(locs()->temp(0).reg() == T1);
  ASSERT(locs()->out(0).reg() == A0);
  __ LoadImmediate(T1, num_context_variables());
  __ MoveRegister(A0, T1);  // num_vars to A0 for stub
  compiler->GenerateStubCall(source(), StubCode::AllocateContext(),
                             UntaggedPcDescriptors::kOther, locs(), deopt_id(),
                             env());
}

LocationSummary* CloneContextInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* locs = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kCall);
  locs->set_in(0, Location::RegisterLocation(A0));
  locs->set_out(0, Location::RegisterLocation(A0));
  return locs;
}

void CloneContextInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  compiler->GenerateStubCall(source(), StubCode::CloneContext(),
                             UntaggedPcDescriptors::kOther, locs(),
                             DeoptId::kNone, env());
}

// ==== More missing stubs ====


LocationSummary* CheckEitherNonSmiInstr::MakeLocationSummary(Zone* zone,
                                                           bool opt) const {
  intptr_t left_cid = left()->Type()->ToCid();
  intptr_t right_cid = right()->Type()->ToCid();
  ASSERT((left_cid != kDoubleCid) && (right_cid != kDoubleCid));
  const intptr_t kNumInputs = 2;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresRegister());
  summary->set_in(1, Location::RequiresRegister());
  return summary;
}

void CheckEitherNonSmiInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  compiler::Label* deopt =
      compiler->AddDeoptStub(deopt_id(), ICData::kDeoptBinaryDoubleOp);
  intptr_t left_cid = left()->Type()->ToCid();
  intptr_t right_cid = right()->Type()->ToCid();
  const Register left = locs()->in(0).reg();
  const Register right = locs()->in(1).reg();
  if (this->left()->definition() == this->right()->definition()) {
    __ BranchIfSmi(left, deopt);
  } else if (left_cid == kSmiCid) {
    __ BranchIfSmi(right, deopt);
  } else if (right_cid == kSmiCid) {
    __ BranchIfSmi(left, deopt);
  } else {
    __ or_l(TMP, left, right);
    __ BranchIfSmi(TMP, deopt);
  }
}

LocationSummary* BoxInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone) LocationSummary(
      zone, kNumInputs, kNumTemps, LocationSummary::kCallOnSlowPath);
  summary->set_in(0, Location::RequiresFpuRegister());
  summary->set_out(0, Location::RequiresRegister());
  return summary;
}

void BoxInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const Register out_reg = locs()->out(0).reg();
  const FRegister value = locs()->in(0).fpu_reg();

  BoxAllocationSlowPath::Allocate(compiler, this,
                                  compiler->BoxClassFor(from_representation()),
                                  out_reg, TMP);

  switch (from_representation()) {
    case kUnboxedDouble:
      __ StoreDToOffset(value, out_reg, ValueOffset() - kHeapObjectTag);
      break;
    case kUnboxedFloat:
      __ StoreSToOffset(value, out_reg, ValueOffset() - kHeapObjectTag);
      break;
    default:
      UNREACHABLE();
      break;
  }
}

LocationSummary* BoxInteger32Instr::MakeLocationSummary(Zone* zone, bool opt) const {
  ASSERT((from_representation() == kUnboxedInt32) ||
         (from_representation() == kUnboxedUint32));
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresRegister());
  summary->set_out(0, Location::RequiresRegister());
  return summary;
}

void BoxInteger32Instr::EmitNativeCode(FlowGraphCompiler* compiler) {
  Register value = locs()->in(0).reg();
  Register out = locs()->out(0).reg();
  ASSERT(value != out);
  ASSERT(compiler::target::kSmiBits >= 32);
  __ slli_d(out, value, 64 - 32);
  if (from_representation() == kUnboxedInt32) {
    __ srai_d(out, out, 64 - 32 - kSmiTagShift);
  } else {
    ASSERT(from_representation() == kUnboxedUint32);
    __ srli_d(out, out, 64 - 32 - kSmiTagShift);
  }
}

LocationSummary* BoxInt64Instr::MakeLocationSummary(Zone* zone, bool opt) const {
  const bool shared_slow_path_call = SlowPathSharingSupported(opt);
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = ValueFitsSmi() ? 0 : 1;
  LocationSummary* summary = new (zone) LocationSummary(
      zone, kNumInputs, kNumTemps,
      ValueFitsSmi()
          ? LocationSummary::kNoCall
          : (shared_slow_path_call ? LocationSummary::kCallOnSharedSlowPath
                                    : LocationSummary::kCallOnSlowPath));
  summary->set_in(0, Location::RequiresRegister());
  if (ValueFitsSmi()) {
    summary->set_out(0, Location::RequiresRegister());
  } else if (shared_slow_path_call) {
    summary->set_out(0,
                     Location::RegisterLocation(AllocateMintABI::kResultReg));
    summary->set_temp(0, Location::RegisterLocation(AllocateMintABI::kTempReg));
  } else {
    summary->set_out(0, Location::RequiresRegister());
    summary->set_temp(0, Location::RequiresRegister());
  }
  return summary;
}

void BoxInt64Instr::EmitNativeCode(FlowGraphCompiler* compiler) {
  Register in = locs()->in(0).reg();
  Register out = locs()->out(0).reg();
  if (ValueFitsSmi()) {
    __ slli_d(out, in, kSmiTagShift);
    return;
  }
  compiler::Label done;
  __ slli_d(out, in, kSmiTagShift);
  __ srai_d(TMP, out, kSmiTagShift);
  __ beq(in, TMP, &done);

  if (locs()->call_on_shared_slow_path()) {
    const bool has_frame = compiler->flow_graph().graph_entry()->NeedsFrame();
    if (!has_frame) {
      __ EnterStubFrame();
    }
    const bool live_fpu_regs = locs()->live_registers()->FpuRegisterCount() > 0;
    const auto& stub = live_fpu_regs
                           ? StubCode::AllocateMintSharedWithFPURegs()
                           : StubCode::AllocateMintSharedWithoutFPURegs();
    ASSERT(!locs()->live_registers()->ContainsRegister(
        AllocateMintABI::kResultReg));
    auto extended_env = compiler->SlowPathEnvironmentFor(this, 0);
    compiler->GenerateStubCall(source(), stub, UntaggedPcDescriptors::kOther,
                               locs(), DeoptId::kNone, extended_env);
    if (!has_frame) {
      __ LeaveStubFrame();
    }
  } else {
    BoxAllocationSlowPath::Allocate(compiler, this, compiler->mint_class(), out,
                                    TMP);
  }

  __ StoreToOffset(in, out, Mint::value_offset() - kHeapObjectTag);
  __ Bind(&done);
}

LocationSummary* UnboxInteger32Instr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresRegister());
  summary->set_out(0, Location::RequiresRegister());
  return summary;
}

void UnboxInteger32Instr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const Register value = locs()->in(0).reg();
  const Register out = locs()->out(0).reg();
  compiler::Label* deopt =
      CanDeoptimize()
          ? compiler->AddDeoptStub(GetDeoptId(), ICData::kDeoptUnboxInteger)
          : nullptr;
  ASSERT(value != out);
  __ SmiUnTag(out, value);
  if (deopt != nullptr) {
    compiler::Label done;
    __ BranchIfSmi(value, &done, compiler::Assembler::kNearJump);
    __ CompareClassId(value, kMintCid, TMP);
    __ BranchIf(NE, deopt);
    __ LoadFieldFromOffset(out, value, compiler::target::Mint::value_offset());
    __ Bind(&done);
  }
}

LocationSummary* MathMinMaxInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  if (representation() == kUnboxedDouble) {
    const intptr_t kNumInputs = 2;
    const intptr_t kNumTemps = 0;
    LocationSummary* summary = new (zone)
        LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
    summary->set_in(0, Location::RequiresFpuRegister());
    summary->set_in(1, Location::RequiresFpuRegister());
    summary->set_out(0, Location::RequiresFpuRegister());
    return summary;
  }
  ASSERT(representation() == kTagged || representation() == kUnboxedInt64);
  const intptr_t kNumInputs = 2;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresRegister());
  summary->set_in(1, Location::RequiresRegister());
  summary->set_out(0, Location::SameAsFirstInput());
  return summary;
}

void MathMinMaxInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  ASSERT((op_kind() == MethodRecognizer::kMathMin) ||
         (op_kind() == MethodRecognizer::kMathMax));
  const bool is_min = (op_kind() == MethodRecognizer::kMathMin);
  if (representation() == kUnboxedDouble) {
    const FRegister left = locs()->in(0).fpu_reg();
    const FRegister right = locs()->in(1).fpu_reg();
    const FRegister result = locs()->out(0).fpu_reg();
    compiler::Label done;
    __ fmv_d(result, left);
    if (is_min) {
      __ fcmp_cond_d(left, right, LE);
    } else {
      __ fcmp_cond_d(left, right, GE);
    }
    __ bcnez(static_cast<FRegister>(0), &done);
    __ fmv_d(result, right);
    __ Bind(&done);
    return;
  }
  ASSERT(representation() == kUnboxedInt64);
  const Register left = locs()->in(0).reg();
  const Register right = locs()->in(1).reg();
  const Register result = locs()->out(0).reg();
  compiler::Label done;
  ASSERT(result == left);
  if (is_min) {
    __ blt(left, right, &done, compiler::Assembler::kNearJump);
  } else {
    __ blt(right, left, &done, compiler::Assembler::kNearJump);
  }
  __ Move(result, right);
  __ Bind(&done);
}

LocationSummary* BinarySmiOpInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumInputs = 2;
  const intptr_t kNumTemps =
      ((op_kind() == Token::kUSHR) || (op_kind() == Token::kMUL)) ? 1 : 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  if (op_kind() == Token::kTRUNCDIV) {
    summary->set_in(0, Location::RequiresRegister());
    if (RightOperandIsPowerOfTwoConstant()) {
      ConstantInstr* right_constant = right()->definition()->AsConstant();
      summary->set_in(1, Location::Constant(right_constant));
    } else {
      summary->set_in(1, Location::RequiresRegister());
    }
    summary->set_out(0, Location::RequiresRegister());
    return summary;
  }
  if (op_kind() == Token::kMOD) {
    summary->set_in(0, Location::RequiresRegister());
    summary->set_in(1, Location::RequiresRegister());
    summary->set_out(0, Location::RequiresRegister());
    return summary;
  }
  summary->set_in(0, Location::RequiresRegister());
  summary->set_in(1, LocationRegisterOrSmiConstant(right()));
  if (kNumTemps == 1) {
    summary->set_temp(0, Location::RequiresRegister());
  }
  if (CanDeoptimize() || (op_kind() == Token::kUSHR)) {
    summary->set_out(0, Location::RequiresRegister());
  } else {
    summary->set_out(0, Location::MayBeSameAsFirstInput());
  }
  return summary;
}

void BinarySmiOpInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  if (op_kind() == Token::kSHL) {
    const Register left = locs()->in(0).reg();
    const Register result = locs()->out(0).reg();
    compiler::Label* deopt = nullptr;
    if (CanDeoptimize()) {
      deopt = compiler->AddDeoptStub(deopt_id(), ICData::kDeoptBinarySmiOp);
    }
    if (locs()->in(1).IsConstant()) {
      const Object& constant = locs()->in(1).constant();
      ASSERT(constant.IsSmi());
      const intptr_t value = Smi::Cast(constant).Value();
      if (deopt != nullptr) {
        __ sub_d(TMP, left, ZR);
        __ slli_d(TMP, TMP, value);
        __ srai_d(TMP, TMP, value);
        __ bne(left, TMP, deopt);
      }
      __ slli_d(result, left, value);
    } else {
      const Register right = locs()->in(1).reg();
      if (deopt != nullptr) {
        __ sub_d(TMP, left, ZR);
        __ slli_d(TMP, TMP, right);
        __ srai_d(TMP, TMP, right);
        __ bne(left, TMP, deopt);
      }
      __ slli_d(result, left, right);
    }
    return;
  }

  const Register left = locs()->in(0).reg();
  const Register result = locs()->out(0).reg();
  compiler::Label* deopt = nullptr;
  if (CanDeoptimize()) {
    deopt = compiler->AddDeoptStub(deopt_id(), ICData::kDeoptBinarySmiOp);
  }

  if (locs()->in(1).IsConstant()) {
    const Object& constant = locs()->in(1).constant();
    ASSERT(constant.IsSmi());
    const intx_t imm = static_cast<intx_t>(constant.ptr());
    switch (op_kind()) {
      case Token::kADD: {
        if (Utils::IsInt(12, imm)) { __ addi_d(result, left, static_cast<int32_t>(imm)); } else { __ LoadImmediate(TMP2, imm); __ add_d(result, left, TMP2); }
        if (deopt != nullptr) {
          __ b(deopt);  // overflow deopt - simplified
        }
        break;
      }
      case Token::kSUB: {
        if (Utils::IsInt(12, -imm)) { __ addi_d(result, left, static_cast<int32_t>(-imm)); } else { __ LoadImmediate(TMP2, imm); __ sub_d(result, left, TMP2); }
        if (deopt != nullptr) {
          __ b(deopt);
        }
        break;
      }
      case Token::kMUL: {
        const intptr_t value = Smi::Cast(constant).Value();
        __ LoadImmediate(TMP, value);
        __ mul_d(result, left, TMP);
        break;
      }
      case Token::kTRUNCDIV: {
        const intptr_t value = Smi::Cast(constant).Value();
        ASSERT(Utils::IsPowerOfTwo(Utils::Abs(value)));
        const intptr_t shift_count =
            Utils::ShiftForPowerOfTwo(Utils::Abs(value)) + kSmiTagSize;
        __ srai_d(TMP, left, 63);
        __ srli_d(TMP, TMP, 64 - shift_count);
        __ add_d(TMP2, left, TMP);
        __ srai_d(result, TMP2, shift_count);
        if (value < 0) {
          __ sub_d(result, ZR, result);
        }
        __ slli_d(result, result, kSmiTagSize);
        break;
      }
      case Token::kBIT_AND:
        if (Utils::IsUint(12, imm)) { __ andi(result, left, static_cast<uint32_t>(imm)); } else { __ LoadImmediate(TMP2, imm); __ and_l(result, left, TMP2); }
        break;
      case Token::kBIT_OR:
        if (Utils::IsUint(12, imm)) { __ ori(result, left, static_cast<uint32_t>(imm)); } else { __ LoadImmediate(TMP2, imm); __ or_l(result, left, TMP2); }
        break;
      case Token::kBIT_XOR:
        if (Utils::IsUint(12, imm)) { __ xori(result, left, static_cast<uint32_t>(imm)); } else { __ LoadImmediate(TMP2, imm); __ xor_l(result, left, TMP2); }
        break;
      case Token::kSHR: {
        __ srai_d(result, left, imm);
        __ andi(result, result, ~kSmiTagMask);
        break;
      }
      default:
        UNIMPLEMENTED();
    }
  } else {
    const Register right = locs()->in(1).reg();
    switch (op_kind()) {
      case Token::kADD: {
        __ add_d(result, left, right);
        if (deopt != nullptr) {
          __ b(deopt);
        }
        break;
      }
      case Token::kSUB: {
        __ sub_d(result, left, right);
        if (deopt != nullptr) {
          __ b(deopt);
        }
        break;
      }
      case Token::kMUL: {
        __ SmiUnTag(TMP, right);
        __ mul_d(result, left, TMP);
        break;
      }
      case Token::kBIT_AND:
        __ and_l(result, left, right);
        break;
      case Token::kBIT_OR:
        __ or_l(result, left, right);
        break;
      case Token::kBIT_XOR:
        __ xor_l(result, left, right);
        break;
      case Token::kSHR: {
        __ SmiUnTag(TMP, right);
        __ srai_d(result, left, TMP);
        __ andi(result, result, ~kSmiTagMask);
        break;
      }
      case Token::kUSHR: {
        __ SmiUnTag(TMP, right);
        __ srli_d(result, left, TMP);
        __ andi(result, result, ~kSmiTagMask);
        break;
      }
      case Token::kTRUNCDIV: {
        __ SmiUnTag(TMP, right);
        __ div_d(result, left, TMP);
        __ slli_d(result, result, kSmiTagSize);
        break;
      }
      case Token::kMOD: {
        __ SmiUnTag(TMP, right);
        __ div_d(TMP2, left, TMP);
        __ mul_d(TMP2, TMP2, TMP);
        __ sub_d(result, left, TMP2);
        break;
      }
      default:
        UNIMPLEMENTED();
    }
  }
}

LocationSummary* UnaryDoubleOpInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresFpuRegister());
  summary->set_out(0, Location::RequiresFpuRegister());
  return summary;
}
void UnaryDoubleOpInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const FRegister result = locs()->out(0).fpu_reg();
  const FRegister value = locs()->in(0).fpu_reg();
  switch (op_kind()) {
    case Token::kNEGATE:
      __ fneg_d(result, value);
      break;
    case Token::kABS:
      __ fabs_d(result, value);
      break;
    default:
      UNREACHABLE();
  }
}

LocationSummary* CheckStackOverflowInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumInputs = 0;
  const intptr_t kNumTemps = 1;
  const bool using_shared_stub = UseSharedSlowPathStub(opt);
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps,
                      using_shared_stub ? LocationSummary::kCallOnSharedSlowPath
                                        : LocationSummary::kCallOnSlowPath);
  summary->set_temp(0, Location::RequiresRegister());
  return summary;
}

class CheckStackOverflowSlowPath : public TemplateSlowPathCode<CheckStackOverflowInstr> {
 public:
  static constexpr intptr_t kNumSlowPathArgs = 0;

  explicit CheckStackOverflowSlowPath(CheckStackOverflowInstr* instruction)
      : TemplateSlowPathCode(instruction) {}

  virtual void EmitNativeCode(FlowGraphCompiler* compiler) {
    auto locs = instruction()->locs();
    if (compiler->isolate_group()->use_osr() && osr_entry_label()->IsLinked()) {
      const Register value = locs->temp(0).reg();
      __ Comment("CheckStackOverflowSlowPathOsr");
      __ Bind(osr_entry_label());
      __ LoadImmediate(value, Thread::kOsrRequest);
      __ StoreToOffset(value, THR, Thread::stack_overflow_flags_offset());
    }
    __ Comment("CheckStackOverflowSlowPath");
    __ Bind(entry_label());
    const bool using_shared_stub = locs->call_on_shared_slow_path();
    if (!using_shared_stub) {
      compiler->SaveLiveRegisters(locs);
    }
    ASSERT(compiler->pending_deoptimization_env_ == nullptr);
    Environment* env =
        compiler->SlowPathEnvironmentFor(instruction(), kNumSlowPathArgs);
    compiler->pending_deoptimization_env_ = env;

    const bool has_frame = compiler->flow_graph().graph_entry()->NeedsFrame();
    if (using_shared_stub) {
      if (!has_frame) {
        __ EnterStubFrame();
      }
      const bool live_fpu_regs = locs->live_registers()->FpuRegisterCount() > 0;
      const auto& stub = live_fpu_regs
                             ? StubCode::StackOverflowSharedWithFPURegs()
                             : StubCode::StackOverflowSharedWithoutFPURegs();
      compiler->GenerateStubCall(instruction()->source(), stub,
                                 UntaggedPcDescriptors::kOther, locs,
                                 instruction()->deopt_id(), env);
      compiler->RecordCatchEntryMoves(env);
      compiler->AddCurrentDescriptor(UntaggedPcDescriptors::kOther,
                                     instruction()->deopt_id(),
                                     instruction()->source());
      if (!has_frame) {
        __ LeaveStubFrame();
      }
    } else {
      ASSERT(has_frame);
      __ CallRuntime(kInterruptOrStackOverflowRuntimeEntry, kNumSlowPathArgs);
      compiler->EmitCallsiteMetadata(
          instruction()->source(), instruction()->deopt_id(),
          UntaggedPcDescriptors::kOther, instruction()->locs(), env);
    }

    if (compiler->isolate_group()->use_osr() && !compiler->is_optimizing() &&
        instruction()->in_loop()) {
      compiler->AddCurrentDescriptor(UntaggedPcDescriptors::kOsrEntry,
                                     instruction()->deopt_id(),
                                     InstructionSource());
    }
    compiler->pending_deoptimization_env_ = nullptr;
    if (!using_shared_stub) {
      compiler->RestoreLiveRegisters(locs);
    }
    __ b(exit_label());
  }

  compiler::Label* osr_entry_label() {
    ASSERT(IsolateGroup::Current()->use_osr());
    return &osr_entry_label_;
  }

 private:
  compiler::Label osr_entry_label_;
};

void CheckStackOverflowInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  CheckStackOverflowSlowPath* slow_path = new CheckStackOverflowSlowPath(this);
  compiler->AddSlowPathCode(slow_path);

  __ ld_d(TMP, THR, compiler::target::Thread::stack_limit_offset());
  __ bgeu(TMP, SP, slow_path->entry_label());
  if (compiler->CanOSRFunction() && in_loop()) {
    const Register function = locs()->temp(0).reg();
    __ LoadObject(function, compiler->parsed_function().function());
    const intptr_t configured_optimization_counter_threshold =
        compiler->thread()->isolate_group()->optimization_counter_threshold();
    const int32_t threshold =
        configured_optimization_counter_threshold * (loop_depth() + 1);
    __ LoadFieldFromOffset(TMP, function, Function::usage_counter_offset(),
                           compiler::kFourBytes);
    __ addi_d(TMP, TMP, 1);
    __ StoreFieldToOffset(TMP, function, Function::usage_counter_offset(),
                          compiler::kFourBytes);
    __ CompareImmediate(TMP, threshold);
    __ BranchIf(GE, slow_path->osr_entry_label());
  }
}

LocationSummary* SmiToDoubleInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* result = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  result->set_in(0, Location::RequiresRegister());
  result->set_out(0, Location::RequiresFpuRegister());
  return result;
}
void SmiToDoubleInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const Register value = locs()->in(0).reg();
  const FRegister result = locs()->out(0).fpu_reg();
  __ SmiUnTag(TMP, value);
  __ movgr2fr_d(FTMP, TMP);
  __ ffint_d_l(result, FTMP);
}

// ==== Float/double conversion and SIMD stubs ====


LocationSummary* Int32ToDoubleInstr::MakeLocationSummary(Zone* zone,
                                                         bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* result = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  result->set_in(0, Location::RequiresRegister());
  result->set_out(0, Location::RequiresFpuRegister());
  return result;
}

void Int32ToDoubleInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const Register value = locs()->in(0).reg();
  const FRegister result = locs()->out(0).fpu_reg();
  __ movgr2fr_w(FTMP, value);
  __ ffint_d_w(result, FTMP);
}

LocationSummary* Int64ToDoubleInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* result = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  result->set_in(0, Location::RequiresRegister());
  result->set_out(0, Location::RequiresFpuRegister());
  return result;
}
void Int64ToDoubleInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const Register value = locs()->in(0).reg();
  const FRegister result = locs()->out(0).fpu_reg();
  __ movgr2fr_d(result, value);
  __ ffint_d_l(result, result);
}

LocationSummary* DoubleToIntegerInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* result = new (zone) LocationSummary(
      zone, kNumInputs, kNumTemps, LocationSummary::kCallOnSlowPath);
  result->set_in(0, Location::RequiresFpuRegister());
  result->set_out(0, Location::RequiresRegister());
  return result;
}
void DoubleToIntegerInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const Register result = locs()->out(0).reg();
  const FpuRegister value_double = locs()->in(0).fpu_reg();

  DoubleToIntegerSlowPath* slow_path = new DoubleToIntegerSlowPath(this, value_double);
  compiler->AddSlowPathCode(slow_path);

  // Convert based on rounding mode.
  switch (recognized_kind()) {
    case MethodRecognizer::kDoubleToInteger:
      __ ftintrz_l_d(FTMP, value_double);
      break;
    case MethodRecognizer::kDoubleFloorToInt:
      __ ftintrm_l_d(FTMP, value_double);
      break;
    case MethodRecognizer::kDoubleCeilToInt:
      __ ftintrp_l_d(FTMP, value_double);
      break;
    default:
      UNREACHABLE();
  }

  // Move integer result from FPR to GPR.
  __ movfr2gr_d(TMP, FTMP);
  __ Move(TMP2, TMP);             // TMP2 = original int value
  __ SmiTag(TMP);                 // TMP = Smi tagged
  __ Move(result, TMP);           // result = tagged output
  __ SmiUntag(TMP);               // TMP = untagged back
  __ CompareRegisters(TMP, TMP2);
  __ BranchIf(NOT_EQUAL, slow_path->entry_label());

  __ Bind(slow_path->exit_label());
}

LocationSummary* DoubleToSmiInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* result = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  result->set_in(0, Location::RequiresFpuRegister());
  result->set_out(0, Location::RequiresRegister());
  return result;
}

void DoubleToSmiInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  compiler::Label* deopt =
      compiler->AddDeoptStub(deopt_id(), ICData::kDeoptDoubleToSmi);
  const Register result = locs()->out(0).reg();
  const FRegister value = locs()->in(0).fpu_reg();
  // Convert double to int64 with truncation (round toward zero).
  __ ftintrz_l_d(FTMP, value);
  // Move FP result to GPR.
  __ movfr2gr_d(TMP, FTMP);
  // Copy to result and SmiTag it.
  __ MoveRegister(result, TMP);
  __ SmiTag(result);
  // Untag result into TMP2 for comparison.
  __ SmiUnTag(TMP2, result);
  __ bne(TMP, TMP2, deopt);
}

LocationSummary* DoubleToFloatInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* result = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  result->set_in(0, Location::RequiresFpuRegister());
  result->set_out(0, Location::RequiresFpuRegister());
  return result;
}
void DoubleToFloatInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const FRegister value = locs()->in(0).fpu_reg();
  const FRegister result = locs()->out(0).fpu_reg();
  __ fcvts_d2s(result, value);
}

LocationSummary* FloatToDoubleInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* result = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  result->set_in(0, Location::RequiresFpuRegister());
  result->set_out(0, Location::RequiresFpuRegister());
  return result;
}
void FloatToDoubleInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const FRegister value = locs()->in(0).fpu_reg();
  const FRegister result = locs()->out(0).fpu_reg();
  __ fcvts_s2d(result, value);
}


LocationSummary* FloatCompareInstr::MakeLocationSummary(Zone* zone,
                                                        bool opt) const {
  const intptr_t kNumInputs = 2;
  const intptr_t kNumTemps = 0;
  LocationSummary* result = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  result->set_in(0, Location::RequiresFpuRegister());
  result->set_in(1, Location::RequiresFpuRegister());
  result->set_out(0, Location::RequiresRegister());
  return result;
}

void FloatCompareInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const FRegister lhs = locs()->in(0).fpu_reg();
  const FRegister rhs = locs()->in(1).fpu_reg();
  const Register result = locs()->out(0).reg();

  switch (op_kind()) {
    case Token::kEQ:
      __ fcmp_cond_d(lhs, rhs, EQ);
      break;
    case Token::kNE:
      __ fcmp_cond_d(lhs, rhs, EQ);
      break;
    case Token::kLT:
      __ fcmp_cond_d(rhs, lhs, GT);
      break;
    case Token::kGT:
      __ fcmp_cond_d(lhs, rhs, GT);
      break;
    case Token::kLTE:
      __ fcmp_cond_d(rhs, lhs, GE);
      break;
    case Token::kGTE:
      __ fcmp_cond_d(lhs, rhs, GE);
      break;
    default:
      UNREACHABLE();
  }

  // Move FCC0 to result register: result = (condition true) ? 1 : 0
  compiler::Label is_true, done;
  __ bcnez(static_cast<FRegister>(0), &is_true);
  __ LoadImmediate(result, 0);
  __ b(&done);
  __ Bind(&is_true);
  __ LoadImmediate(result, 1);
  __ Bind(&done);

  if (op_kind() == Token::kNE) {
    __ xori(result, result, 1);
  }
}

LocationSummary* SimdOpInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  UNREACHABLE();
  return NULL;
}
void SimdOpInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  UNREACHABLE();
}
}  // namespace dart
#endif  // defined TARGET_ARCH_LOONG64















