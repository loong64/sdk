// Copyright (c) 2017, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef RUNTIME_VM_COMPILER_ASSEMBLER_ASSEMBLER_LOONG64_H_
#define RUNTIME_VM_COMPILER_ASSEMBLER_ASSEMBLER_LOONG64_H_

#if defined(DART_PRECOMPILED_RUNTIME)
#error "AOT runtime should not use compiler sources (including header files)"
#endif  // defined(DART_PRECOMPILED_RUNTIME)

#ifndef RUNTIME_VM_COMPILER_ASSEMBLER_ASSEMBLER_H_
#error Do not include assembler_loong64.h directly; use assembler.h instead.
#endif

#include <functional>

#include "platform/assert.h"
#include "platform/utils.h"
#include "vm/class_id.h"
#include "vm/compiler/assembler/assembler_base.h"
#include "vm/compiler/assembler/object_pool_builder.h"
#include "vm/constants.h"
#include "vm/hash_map.h"
#include "vm/simulator.h"

namespace dart {

// Forward declarations.
class FlowGraphCompiler;
class RuntimeEntry;
class RegisterSet;
class StubEntry;

namespace compiler {

class Address {
 public:
  Address(Register base, intptr_t offset) : base_(base), offset_(offset) {}
  explicit Address(Register base) : base_(base), offset_(0) {}

  // Prevent implicit conversion of Register to intptr_t.
  Address(Register base, Register index) = delete;

  Register base() const { return base_; }
  intptr_t offset() const { return offset_; }

 private:
  Register base_;
  intptr_t offset_;
};

class FieldAddress : public Address {
 public:
  FieldAddress(Register base, intptr_t offset)
      : Address(base, offset - kHeapObjectTag) {}

  FieldAddress(Register base, Register index) = delete;
};

// All functions produce exactly one instruction.
class MicroAssembler : public AssemblerBase {
 public:
  MicroAssembler(ObjectPoolBuilder* object_pool_builder,
                 intptr_t far_branch_level);
  ~MicroAssembler();

  intptr_t far_branch_level() const { return far_branch_level_; }
  void set_far_branch_level(intptr_t level) { far_branch_level_ = level; }
  void Bind(Label* label) override;

  // ==== LOONG64 Base Instructions ====
  void lu12iw(Register rd, intptr_t imm20);
  void lu32id(Register rd, intptr_t imm20);
  void lu52id(Register rd, Register rj, intptr_t imm12);

  void pcaddu12i(Register rd, intptr_t si20);
  void pcalau12i(Register rd, intptr_t si20);
  void pcaddu18i(Register rd, intptr_t si20);

  void jirl(Register rd, Register rj, intptr_t si12 = 0);
  void jirl_fixed(Register rd, Register rj, intptr_t si12);
  void jirl(Register rj, intptr_t si12 = 0) { jirl(RA, rj, si12); }
  void jr(Register rj, intptr_t si12 = 0) { jirl(ZR, rj, si12); }
  void ret() { jirl(ZR, RA, 0); }

  void b(Label* label, JumpDistance d = kFarJump);
  void bl(Label* label, JumpDistance d = kFarJump);
  void beqz(Register rj, Label* label, JumpDistance d = kFarJump);
  void bnez(Register rj, Label* label, JumpDistance d = kFarJump);
  void bceqz(FRegister fcj, Label* label, JumpDistance d = kFarJump);
  void bcnez(FRegister fcj, Label* label, JumpDistance d = kFarJump);

  void beq(Register rj, Register rd, Label* label, JumpDistance d = kFarJump);
  void bne(Register rj, Register rd, Label* label, JumpDistance d = kFarJump);
  void blt(Register rj, Register rd, Label* label, JumpDistance d = kFarJump);
  void bge(Register rj, Register rd, Label* label, JumpDistance d = kFarJump);
  void bltu(Register rj, Register rd, Label* label, JumpDistance d = kFarJump);
  void bgeu(Register rj, Register rd, Label* label, JumpDistance d = kFarJump);

  void beq(Register rj, intptr_t imm5, Register rd, Label* label, JumpDistance d = kFarJump);
  void bne(Register rj, intptr_t imm5, Register rd, Label* label, JumpDistance d = kFarJump);
  void blt(Register rj, intptr_t imm5, Register rd, Label* label, JumpDistance d = kFarJump);
  void bge(Register rj, intptr_t imm5, Register rd, Label* label, JumpDistance d = kFarJump);
  void bltu(Register rj, intptr_t imm5, Register rd, Label* label, JumpDistance d = kFarJump);
  void bgeu(Register rj, intptr_t imm5, Register rd, Label* label, JumpDistance d = kFarJump);

  void ld_b(Register rd, Register rj, intptr_t si12);
  void ld_h(Register rd, Register rj, intptr_t si12);
  void ld_w(Register rd, Register rj, intptr_t si12);
  void ld_d(Register rd, Register rj, intptr_t si12);

  void st_b(Register rd, Register rj, intptr_t si12);
  void st_h(Register rd, Register rj, intptr_t si12);
  void st_w(Register rd, Register rj, intptr_t si12);
  void st_d(Register rd, Register rj, intptr_t si12);

  void ld_bu(Register rd, Register rj, intptr_t si12);
  void ld_hu(Register rd, Register rj, intptr_t si12);
  void ld_wu(Register rd, Register rj, intptr_t si12);

  void addi_w(Register rd, Register rj, intptr_t si12);
  void addi_d(Register rd, Register rj, intptr_t si12);

  void addu16i_d(Register rd, Register rj, intptr_t si16);

  void slti(Register rd, Register rj, intptr_t si12);
  void sltui(Register rd, Register rj, intptr_t si12);

  void andi(Register rd, Register rj, intptr_t ui12);
  void ori(Register rd, Register rj, intptr_t ui12);
  void xori(Register rd, Register rj, intptr_t ui12);

  void add_w(Register rd, Register rj, Register rk);
  void add_d(Register rd, Register rj, Register rk);
  void sub_w(Register rd, Register rj, Register rk);
  void sub_d(Register rd, Register rj, Register rk);

  void and_l(Register rd, Register rj, Register rk);
  void or_l(Register rd, Register rj, Register rk);
  void xor_l(Register rd, Register rj, Register rk);

  void mul_w(Register rd, Register rj, Register rk);
  void mul_d(Register rd, Register rj, Register rk);
  void mulh_w(Register rd, Register rj, Register rk);
  void mulh_wu(Register rd, Register rj, Register rk);
  void mulh_d(Register rd, Register rj, Register rk);
  void mulh_du(Register rd, Register rj, Register rk);

  void div_w(Register rd, Register rj, Register rk);
  void div_wu(Register rd, Register rj, Register rk);
  void div_d(Register rd, Register rj, Register rk);
  void div_du(Register rd, Register rj, Register rk);
  void mod_w(Register rd, Register rj, Register rk);
  void mod_wu(Register rd, Register rj, Register rk);
  void mod_d(Register rd, Register rj, Register rk);
  void mod_du(Register rd, Register rj, Register rk);

  void sll_w(Register rd, Register rj, Register rk);
  void srl_w(Register rd, Register rj, Register rk);
  void sra_w(Register rd, Register rj, Register rk);

  void sll_d(Register rd, Register rj, Register rk);
  void srl_d(Register rd, Register rj, Register rk);
  void sra_d(Register rd, Register rj, Register rk);

  void slli_w(Register rd, Register rj, intptr_t ui5);
  void srli_w(Register rd, Register rj, intptr_t ui5);
  void srai_w(Register rd, Register rj, intptr_t ui5);

  void slli_d(Register rd, Register rj, intptr_t ui6);
  void srli_d(Register rd, Register rj, intptr_t ui6);
  void srai_d(Register rd, Register rj, intptr_t ui6);

  void slt(Register rd, Register rj, Register rk);
  void sltu(Register rd, Register rj, Register rk);

  void breakpoint() { break_(); }
  void break_(uint32_t code = 0);

  void dbar(intptr_t hint = 0);
  void ibar(intptr_t hint = 0);

  void syscall(uint32_t code = 0);

  // FP instructions
  void fadd_s(FRegister fd, FRegister fj, FRegister fk);
  void fadd_d(FRegister fd, FRegister fj, FRegister fk);
  void fsub_s(FRegister fd, FRegister fj, FRegister fk);
  void fsub_d(FRegister fd, FRegister fj, FRegister fk);
  void fmul_s(FRegister fd, FRegister fj, FRegister fk);
  void fmul_d(FRegister fd, FRegister fj, FRegister fk);
  void fdiv_s(FRegister fd, FRegister fj, FRegister fk);
  void fdiv_d(FRegister fd, FRegister fj, FRegister fk);

  void fmadd_s(FRegister fd, FRegister fj, FRegister fk, FRegister fa);
  void fmadd_d(FRegister fd, FRegister fj, FRegister fk, FRegister fa);
  void fmsub_s(FRegister fd, FRegister fj, FRegister fk, FRegister fa);
  void fmsub_d(FRegister fd, FRegister fj, FRegister fk, FRegister fa);
  void fnmadd_s(FRegister fd, FRegister fj, FRegister fk, FRegister fa);
  void fnmadd_d(FRegister fd, FRegister fj, FRegister fk, FRegister fa);
  void fnmsub_s(FRegister fd, FRegister fj, FRegister fk, FRegister fa);
  void fnmsub_d(FRegister fd, FRegister fj, FRegister fk, FRegister fa);

  void fcmp_cond_s(FRegister fj, FRegister fk, int cond);
  void fcmp_cond_d(FRegister fj, FRegister fk, int cond);

  void fcvts_d2s(FRegister fd, FRegister fj);
  void fcvts_s2d(FRegister fd, FRegister fj);
  void ftintrz_w_s(FRegister fd, FRegister fj);
  void ftintrz_w_d(FRegister fd, FRegister fj);
  void ftintrz_l_s(FRegister fd, FRegister fj);
  void ftintrz_l_d(FRegister fd, FRegister fj);
  void ffint_s_w(FRegister fd, FRegister fj);
  void ffint_s_l(FRegister fd, FRegister fj);
  void ffint_d_w(FRegister fd, FRegister fj);
  void ffint_d_l(FRegister fd, FRegister fj);

  void fld_s(FRegister fd, Register rj, intptr_t si12);
  void fld_d(FRegister fd, Register rj, intptr_t si12);
  void fst_s(FRegister fd, Register rj, intptr_t si12);
  void fst_d(FRegister fd, Register rj, intptr_t si12);

  void fmv_s(FRegister fd, FRegister fj);
  void fmv_d(FRegister fd, FRegister fj);
  void fabs_d(FRegister fd, FRegister fj);
  void fneg_d(FRegister fd, FRegister fj);

  void movgr2fr_w(FRegister fd, Register rj);
  void movgr2fr_d(FRegister fd, Register rj);
  void movfr2gr_s(Register rd, FRegister fj);
  void movfr2gr_d(Register rd, FRegister fj);
  void movgr2frh_w(FRegister fd, Register rj);
  void movfrh2gr_s(Register rd, FRegister fj);

  void fsel(FRegister fd, FRegister fj, FRegister fk, FRegister fa);

 protected:
  void Emit(uint32_t encoding);
  intptr_t far_branch_level_;

 private:
  friend class Assembler;

  DISALLOW_ALLOCATION();
  DISALLOW_COPY_AND_ASSIGN(MicroAssembler);
};

class Assembler : public MicroAssembler {
 public:
  Assembler(ObjectPoolBuilder* object_pool_builder,
            intptr_t far_branch_level = 0);
  ~Assembler();

  void set_constant_pool_allowed(bool b) { constant_pool_allowed_ = b; }
  bool constant_pool_allowed() const { return constant_pool_allowed_; }

  void Breakpoint() override { break_(); }
  void StoreStoreFence() override { dbar(0); }

  void TryAllocateObject(intptr_t cid,
                         intptr_t instance_size,
                         Label* failure,
                         JumpDistance distance,
                         Register instance_reg,
                         Register temp) override;

  void StoreObjectIntoObjectNoBarrier(Register object,
                                      const Address& address,
                                      const Object& value,
                                      MemoryOrder memory_order = kRelaxedNonAtomic,
                                      OperandSize size = kWordBytes) override;

  void EnterFrame(intptr_t frame_size);
  void LeaveFrame();

  void EnterStubFrame();
  void LeaveStubFrame();

  void EnterDartFrame(intptr_t frame_size);
  void LeaveDartFrame();

  void EnterCFrame();
  void LeaveCFrame();

  void Call(Register target);
  void Call(const Address& address);
  void Call(intptr_t target_code_pool_index,
            CodeEntryKind entry_kind = CodeEntryKind::kMonomorphic);
  void Call(Label* label);
  void Call(const StubEntry& stub);
  void Call(const Code& target);
  void Jump(const Address& address);

  void LoadWordFromPool(Register dst, int32_t offset);
  void LoadWordFromPoolIndex(Register dst, int32_t index);

  void LoadObject(Register dst, const Object& obj);
  void LoadUniqueObject(Register dst, const Object& obj);
  void LoadIntoObject(Register dst, Register obj, int32_t offset);
  void LoadField(Register dst, Register instance, int32_t offset);

  void StoreIntoObject(Register object, const Address& address, Register value);
  void StoreIntoObjectNoBarrier(Register object, const Address& address, Register value);
  void StoreIntoObjectNoBarrier(Register object, Register value, int32_t offset);

  void PushRegister(Register r);
  void PopRegister(Register r);
  void PushRegisterPair(Register r0, Register r1);
  void PopRegisterPair(Register r0, Register r1);
  void PushRegisters(const RegisterSet& regs);
  void PopRegisters(const RegisterSet& regs);
  void PushNativeCalleeSavedRegisters();
  void PopNativeCalleeSavedRegisters();

  void LoadImmediate(Register rd, intx_t value);

  void AddImmediate(Register rd, Register rs, intx_t value);
  void AddImmediateBranchOverflow(Register rd, Register rs1, intx_t imm, Label* overflow);
  void SubtractImmediateBranchOverflow(Register rd, Register rs1, intx_t imm, Label* overflow);
  void MultiplyImmediateBranchOverflow(Register rd, Register rs1, intx_t imm, Label* overflow);
  void AddBranchOverflow(Register rd, Register rs1, Register rs2, Label* overflow);
  void SubtractBranchOverflow(Register rd, Register rs1, Register rs2, Label* overflow);
  void MultiplyBranchOverflow(Register rd, Register rs1, Register rs2, Label* overflow);
  void CountLeadingZeroes(Register rd, Register rs);

  void CompareWithMemoryValue(Register value, const Address& address, int8_t expected_cid);
  void CompareRegisters(Register rn, Register rm);
  void CompareObjectRegisters(Register rn, Register rm);
  void TestRegisters(Register rn, Register rm);
  void BranchIf(Condition condition, Label* label, JumpDistance d = kFarJump);
  void BranchIfZero(Register rn, Label* label, JumpDistance distance = kFarJump);
  void BranchIfBit(Register rn, int bit, Label* label, JumpDistance distance = kFarJump);
  void BranchIfNotSmi(Register reg, Label* label, JumpDistance distance = kFarJump);

  void BranchIfSmi(Register reg, Label* label, JumpDistance distance = kFarJump) override;
  void ArithmeticShiftRightImmediate(Register dst, Register src, int32_t shift, OperandSize sz = kWordBytes) override;
  
  void CompareWords(Register reg1, Register reg2, Label* not_equal);
  void CompareWords(Register reg1,
                    Register reg2,
                    intptr_t offset,
                    Register count,
                    Register temp,
                    Label* equals) override;

  void ExtendValue(Register rd, Register rn, OperandSize sz) override;
  void ExtendAndSmiTagValue(Register rd, Register rn, OperandSize sz) override;

  void Load(Register dest, const Address& address, OperandSize sz = kWordBytes) override;
  void LoadIndexedPayload(Register dest, Register base, int32_t offset, Register index, ScaleFactor scale, OperandSize sz = kWordBytes) override;
  void LoadSFromOffset(FRegister dest, Register base, int32_t offset);
  void LoadDFromOffset(FRegister dest, Register base, int32_t offset);
  void LoadFromStack(Register dst, intptr_t depth);
  void StoreToStack(Register src, intptr_t depth);
  void Store(Register src, const Address& address, OperandSize sz = kWordBytes) override;
  void Move(Register dst, Register src);
  void Mov(Register dst, Register src) { Move(dst, src); }

  void SmiTag(Register r) override;
  void SmiTag(Register dst, Register src);
  void SmiUnTag(Register dst, Register src);

  void Bind(Label* label) override;
  intptr_t CodeSize() const;

  void LoadAcquire(Register dst, const Address& address, OperandSize size = kWordBytes) override;
  void StoreRelease(Register src, const Address& address, OperandSize size = kWordBytes) override;
  void TsanLoadAcquire(Register dst, const Address& address);
  void TsanStoreRelease(Register src, const Address& address);
  void TsanFuncEntry(bool preserve_registers);
  void TsanFuncExit(bool preserve_registers);
  void ReserveAlignedFrameSpace(intptr_t frame_space);

  void EmitEntryFrameVerification();

  static bool AddressCanHoldConstantIndex(const Object& constant,
                                         bool is_external,
                                         intptr_t cid,
                                         intptr_t index_scale);
  Address ElementAddressForIntIndex(bool is_external, intptr_t cid,
                                    intptr_t index_scale, Register array,
                                    intptr_t index) const;

  void ComputeElementAddressForIntIndex(Register address, bool is_external,
                                        intptr_t cid, intptr_t index_scale,
                                        Register array, intptr_t index);
  Address ElementAddressForRegIndex(bool is_external, intptr_t cid,
                                    intptr_t index_scale, bool index_unboxed,
                                    Register array, Register index, Register temp);
  void ComputeElementAddressForRegIndex(Register address, bool is_external,
                                        intptr_t cid, intptr_t index_scale,
                                        bool index_unboxed, Register array,
                                        Register index);

  void LoadStaticFieldAddress(Register address, Register field, Register scratch, bool is_shared);
  void LoadFieldAddressForRegOffset(Register address, Register instance, Register offset_in_words_as_smi) override;
  void LoadFieldAddressForOffset(Register address, Register instance, int32_t offset) override;

  static int32_t HeapDataOffset(bool is_external, intptr_t cid);

  void SmiUntagOrCheck(Register tmp, Label* label);
  void MaybePatchCodeStart(Register tmp);

  void EmitPcRelativeCall(Label* label);
  void EmitPcRelativeTailCall(intptr_t offset_into_target = 0);
  void GenerateUnRelocatedPcRelativeCall(intptr_t offset_into_target = 0);
  void GenerateUnRelocatedPcRelativeTailCall(intptr_t offset_into_target = 0);

 private:
  bool constant_pool_allowed_;

  enum DeferredCompareType {
    kNone,
    kCompareReg,
    kCompareImm,
    kTestReg,
    kTestImm,
  };
  DeferredCompareType deferred_compare_ = kNone;
  Register deferred_left_ = kNoRegister;
  Register deferred_reg_ = kNoRegister;
  intptr_t deferred_imm_ = 0;

  void LoadObjectHelper(Register dst, const Object& obj, bool is_unique);

  void JumpAndLink(intptr_t target_code_pool_index, CodeEntryKind entry_kind);

  friend class dart::FlowGraphCompiler;
  std::function<void(Register reg)> generate_invoke_write_barrier_wrapper_;
  std::function<void()> generate_invoke_array_write_barrier_;

  DISALLOW_ALLOCATION();
  DISALLOW_COPY_AND_ASSIGN(Assembler);
};

}  // namespace compiler
}  // namespace dart

#endif  // RUNTIME_VM_COMPILER_ASSEMBLER_ASSEMBLER_LOONG64_H_
