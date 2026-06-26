// Copyright (c) 2021, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // Needed here to get TARGET_ARCH_LOONG64.
#if defined(TARGET_ARCH_LOONG64)

#include "vm/instructions.h"
#include "vm/instructions_loong64.h"

#include "vm/constants.h"
#include "vm/cpu.h"
#include "vm/object.h"
#include "vm/object_store.h"
#include "vm/reverse_pc_lookup_cache.h"

namespace dart {

static bool IsJumpAndLinkScratch(Register reg) {
  return reg == (FLAG_precompiled_mode ? TMP : CODE_REG);
}

// LoongArch instruction encoding helpers for decoding
namespace loong_decode {
  // Check if instruction is ld_d (load doubleword): 0x28C00000 | si12<<10 | rj<<5 | rd
  static bool IsLdD(uint32_t instr) {
    const uint32_t mask = 0xFC000000;  // bits[31:26] only for opcode check
    const uint32_t pattern = 0x28C00000 & mask;  // LD.D opcode = 0x0A, subtype = 0011
    return (instr & 0xFC000000) == 0x28C00000;
  }
  // Check if instruction is ld_w (load word): 0x28800000 | si12<<10 | rj<<5 | rd
  static bool IsLdW(uint32_t instr) {
    return (instr & 0xFC000000) == 0x28800000;
  }
  // Check if instruction is pcaddu12i: 0x1C000000 | si20<<5 | rd
  static bool IsPcaddu12i(uint32_t instr) {
    return (instr & 0xFC000000) == 0x1C000000;
  }
  // Check if instruction is jirl: 0x4C000000 | offs16<<10 | rj<<5 | rd
  static bool IsJirl(uint32_t instr) {
    return (instr & 0xFC000000) == 0x4C000000;
  }
  // Check if instruction is pcalau12i: 0x1A000000 | si20<<5 | rd
  static bool IsPcalau12i(uint32_t instr) {
    return (instr & 0xFC000000) == 0x1A000000;
  }
}

CallPattern::CallPattern(uword pc, const Code& code)
    : object_pool_(ObjectPool::Handle(code.GetObjectPool())),
      target_code_pool_index_(-1) {
  ASSERT(code.ContainsInstructionAt(pc));
  ASSERT(*reinterpret_cast<uint32_t*>(pc - 4) ==
         (0x4C000000 | (static_cast<uint32_t>(TMP) << 5) |
          static_cast<uint32_t>(RA)));

  Register reg;
  InstructionPattern::DecodeLoadWordFromPool(pc - 8, &reg,
                                             &target_code_pool_index_);
  ASSERT(IsJumpAndLinkScratch(reg));
}

ICCallPattern::ICCallPattern(uword pc, const Code& code)
    : object_pool_(ObjectPool::Handle(code.GetObjectPool())),
      target_pool_index_(-1),
      data_pool_index_(-1) {
  ASSERT(code.ContainsInstructionAt(pc));
  ASSERT(*reinterpret_cast<uint32_t*>(pc - 4) ==
         (0x4C000000 | (static_cast<uint32_t>(TMP) << 5) |
          static_cast<uint32_t>(RA)));

  Register reg;
  uword target_load_end = InstructionPattern::DecodeLoadWordFromPool(
      pc - 8, &reg, &data_pool_index_);
  ASSERT(reg == IC_DATA_REG);

  InstructionPattern::DecodeLoadWordFromPool(target_load_end, &reg,
                                             &target_pool_index_);
  ASSERT(IsJumpAndLinkScratch(reg));
}

NativeCallPattern::NativeCallPattern(uword pc, const Code& code)
    : object_pool_(ObjectPool::Handle(code.GetObjectPool())),
      end_(pc),
      native_function_pool_index_(-1),
      target_code_pool_index_(-1) {
  ASSERT(code.ContainsInstructionAt(pc));
  ASSERT(*reinterpret_cast<uint32_t*>(pc - 4) ==
         (0x4C000000 | (static_cast<uint32_t>(TMP) << 5) |
          static_cast<uint32_t>(RA)));

  Register reg;
  uword native_function_load_end = InstructionPattern::DecodeLoadWordFromPool(
      pc - 8, &reg, &target_code_pool_index_);
  ASSERT(IsJumpAndLinkScratch(reg));
  InstructionPattern::DecodeLoadWordFromPool(native_function_load_end, &reg,
                                             &native_function_pool_index_);
  ASSERT(reg == T5);
}

CodePtr NativeCallPattern::target() const {
  return static_cast<CodePtr>(object_pool_.ObjectAt<std::memory_order_acquire>(
      target_code_pool_index_));
}

void NativeCallPattern::set_target(const Code& target) const {
  object_pool_.SetObjectAt<std::memory_order_release>(target_code_pool_index_,
                                                      target);
}

NativeFunction NativeCallPattern::native_function() const {
  return reinterpret_cast<NativeFunction>(
      object_pool_.RawValueAt(native_function_pool_index_));
}

void NativeCallPattern::set_native_function(NativeFunction func) const {
  object_pool_.SetRawValueAt<std::memory_order_relaxed>(
      native_function_pool_index_, reinterpret_cast<uword>(func));
}

uword InstructionPattern::DecodeLoadWordImmediate(uword end,
                                                  Register* reg,
                                                  intptr_t* value) {
  UNIMPLEMENTED();
  return 0;
}

static bool IsAddD(uint32_t instr) {
  // add_d encoding: 0x00108000 | (rk << 10) | (rj << 5) | rd
  return (instr & 0xFFC00000) == 0x00100000;
}

static bool IsOri(uint32_t instr) {
  // ori encoding: 0x03800000 | (ui12 << 10) | (rj << 5) | rd
  return (instr & 0xFFC00000) == 0x03800000;
}

static bool DecodeLoadX(uword end,
                        Register* dst,
                        Register* base,
                        intptr_t* offset,
                        intptr_t* length) {
  uint32_t instr = LoadUnaligned(reinterpret_cast<uint32_t*>(end - 4));
  // Check for ld_d (doubleword load)
  if (loong_decode::IsLdD(instr)) {
    *dst = static_cast<Register>(instr & 0x1f);
    if (base != nullptr) {
      *base = static_cast<Register>((instr >> 5) & 0x1f);
    }
    *offset = static_cast<int32_t>((instr >> 10) & 0xfff);
    if (*offset & 0x800) *offset |= 0xfffff000;  // sign extend
    *length = 4;
    return true;
  }
  // Check for ld_w (word load)
  if (loong_decode::IsLdW(instr)) {
    *dst = static_cast<Register>(instr & 0x1f);
    if (base != nullptr) {
      *base = static_cast<Register>((instr >> 5) & 0x1f);
    }
    *offset = static_cast<int32_t>((instr >> 10) & 0xfff);
    if (*offset & 0x800) *offset |= 0xfffff000;  // sign extend
    *length = 4;
    return true;
  }
  return false;
}

static bool DecodePCADDU12I(uword end,
                            Register* dst,
                            intptr_t* imm,
                            intptr_t* length) {
  uint32_t instr = LoadUnaligned(reinterpret_cast<uint32_t*>(end - 4));
  if (loong_decode::IsPcaddu12i(instr) || loong_decode::IsPcalau12i(instr)) {
    *dst = static_cast<Register>(instr & 0x1f);
    *imm = static_cast<intptr_t>((instr >> 5) & 0xfffff);
    if (*imm & 0x80000) *imm |= ~0xfffff;  // sign extend
    if (length != nullptr) {
      *length = 4;
    }
    return true;
  }
  return false;
}

static bool DecodeLoadImmediateX(uword end,
                                 Register* dst,
                                 intptr_t* value,
                                 intptr_t* length) {
  uint32_t instr = LoadUnaligned(reinterpret_cast<uint32_t*>(end - 4));
  // lu12iw rd, si20: 0x14000000 | (si20 << 5) | rd
  if ((instr & 0xFE000000) == 0x14000000) {
    *dst = static_cast<Register>(instr & 0x1f);
    *value = static_cast<intptr_t>((instr >> 5) & 0xfffff);
    if (*value & 0x80000) *value |= ~0xfffff;  // sign extend
    *value <<= 12;
    *length = 4;
    return true;
  }
  return false;
}

uword InstructionPattern::DecodeLoadWordFromPool(uword end,
                                                 Register* reg,
                                                 intptr_t* index) {
  // PP-relative pool load (matches assembler LoadWordFromPool).
  // PP is untagged on loong64.
  //
  // Pattern 1 (simple): ld_d dst, offset(PP)
  //   offset fits in signed 12 bits (-2048..2047).
  //
  // Pattern 2 (complex): LoadImmediate via lu12iw+ori, then:
  //   lu12iw TMP, hi20
  //   [ori TMP, TMP, lo12]
  //   add_d TMP, PP, TMP
  //   ld_d dst, 0(TMP)

  uint32_t instr = LoadUnaligned(reinterpret_cast<uint32_t*>(end - 4));
  if (!loong_decode::IsLdD(instr) && !loong_decode::IsLdW(instr)) {
    FATAL("DecodeLoadWordFromPool: expected ld_d or ld_w at end - 4");
  }

  Register dst = static_cast<Register>(instr & 0x1f);
  Register base = static_cast<Register>((instr >> 5) & 0x1f);
  intptr_t si12 = static_cast<int32_t>((instr >> 10) & 0xfff);
  if (si12 & 0x800) si12 |= ~0xfff;  // sign extend

  *reg = dst;

  if (base == PP) {
    // Pattern 1: ld_d dst, offset(PP)
    // PP untagged; instruction offset = element_offset(index).
    // IndexFromOffset expects offset from tagged pointer, so subtract tag.
    *index = ObjectPool::IndexFromOffset(si12 - kHeapObjectTag);
    return end - 4;
  }

  if (base == TMP && si12 == 0) {
    // Pattern 2: ld_d dst, 0(TMP) preceded by add_d TMP, PP, TMP
    // and LoadImmediate(TMP, offset)
    uint32_t add_instr = LoadUnaligned(reinterpret_cast<uint32_t*>(end - 8));
    if (!IsAddD(add_instr)) {
      FATAL("DecodeLoadWordFromPool: expected add_d after ld_d with base=TMP");
    }
    Register add_rd = static_cast<Register>(add_instr & 0x1f);
    Register add_rj = static_cast<Register>((add_instr >> 5) & 0x1f);
    Register add_rk = static_cast<Register>((add_instr >> 10) & 0x1f);
    if (add_rd != TMP || add_rj != PP || add_rk != TMP) {
      FATAL("DecodeLoadWordFromPool: unexpected add_d operands");
    }

    // Walk backwards through LoadImmediate expansion.
    uword imm_end = end - 8;
    intptr_t decoded_imm = 0;

    // Check for ori TMP, TMP, lo12 (optional, last instruction of LoadImmediate)
    uint32_t imm_instr = LoadUnaligned(reinterpret_cast<uint32_t*>(imm_end - 4));
    if (IsOri(imm_instr)) {
      Register ori_rd = static_cast<Register>(imm_instr & 0x1f);
      Register ori_rj = static_cast<Register>((imm_instr >> 5) & 0x1f);
      if (ori_rd == TMP && ori_rj == TMP) {
        decoded_imm = static_cast<intptr_t>((imm_instr >> 10) & 0xfff);
        imm_end -= 4;
      }
    }

    // Check for lu32id TMP, val (optional, for values with bits [51:32] set)
    imm_instr = LoadUnaligned(reinterpret_cast<uint32_t*>(imm_end - 4));
    if ((imm_instr & 0xFFE00000) == 0x16000000) {  // lu32id mask
      Register lu32_rd = static_cast<Register>(imm_instr & 0x1f);
      if (lu32_rd == TMP) {
        intptr_t val = static_cast<intptr_t>((imm_instr >> 5) & 0xfffff);
        if (val & 0x80000) val |= ~0xfffff;  // sign extend si20
        decoded_imm |= (val << 32);
        imm_end -= 4;
      }
    }

    // Must find lu12iw TMP, hi20 (first instruction of LoadImmediate)
    imm_instr = LoadUnaligned(reinterpret_cast<uint32_t*>(imm_end - 4));
    if ((imm_instr & 0xFFE00000) != 0x14000000) {  // lu12iw mask
      FATAL("DecodeLoadWordFromPool: expected lu12iw in LoadImmediate expansion");
    }
    Register lu12_rd = static_cast<Register>(imm_instr & 0x1f);
    if (lu12_rd != TMP) {
      FATAL("DecodeLoadWordFromPool: lu12iw destination is not TMP");
    }
    intptr_t hi20 = static_cast<intptr_t>((imm_instr >> 5) & 0xfffff);
    if (hi20 & 0x80000) hi20 |= ~0xfffff;  // sign extend si20
    decoded_imm |= (hi20 << 12);
    imm_end -= 4;

    // decoded_imm is the raw pool offset (element_offset).
    // PP is untagged, so subtract kHeapObjectTag for IndexFromOffset.
    *index = ObjectPool::IndexFromOffset(decoded_imm - kHeapObjectTag);
    return imm_end;
  }

  FATAL("DecodeLoadWordFromPool: unsupported pool load pattern");
  return 0;
}

void InstructionPattern::EncodeLoadWordFromPoolFixed(uword end,
                                                     int32_t offset) {
  // PP-relative pool load: rewrite the ld_d immediate.
  // Currently only handles the simple single-instruction pattern.
  // See RISC-V instructions_riscv.cc for approach (UNIMPLEMENTED).
  uint32_t instr = LoadUnaligned(reinterpret_cast<uint32_t*>(end - 4));
  Register reg;
  if (loong_decode::IsLdD(instr) || loong_decode::IsLdW(instr)) {
    reg = static_cast<Register>(instr & 0x1f);
    Register base = static_cast<Register>((instr >> 5) & 0x1f);
    if (base == PP) {
      if (!Utils::IsInt(12, offset)) {
        FATAL("EncodeLoadWordFromPoolFixed: offset too large for single ld_d");
      }
      StoreUnaligned(reinterpret_cast<uint32_t*>(end - 4),
                     0x28C00000 | (static_cast<uint32_t>(offset & 0xfff) << 10) |
                     (static_cast<uint32_t>(PP) << 5) | static_cast<uint32_t>(reg));
      return;
    }
    FATAL("EncodeLoadWordFromPoolFixed: complex pattern rewrite not implemented");
  }
  FATAL("EncodeLoadWordFromPoolFixed: expected ld_d/ld_w at end - 4");
}


// CallPattern / ICCallPattern target code access
CodePtr CallPattern::TargetCode() const {
  return static_cast<CodePtr>(object_pool_.ObjectAt<std::memory_order_acquire>(
      target_code_pool_index_));
}

void CallPattern::SetTargetCode(const Code& target) const {
  object_pool_.SetObjectAt<std::memory_order_release>(target_code_pool_index_,
                                                      target);
}

ObjectPtr ICCallPattern::Data() const {
  return object_pool_.ObjectAt<std::memory_order_acquire>(data_pool_index_);
}

void ICCallPattern::SetData(const Object& data) const {
  object_pool_.SetObjectAt<std::memory_order_release>(data_pool_index_, data);
}

CodePtr ICCallPattern::TargetCode() const {
  return static_cast<CodePtr>(object_pool_.ObjectAt<std::memory_order_acquire>(
      target_pool_index_));
}

void ICCallPattern::SetTargetCode(const Code& target) const {
  object_pool_.SetObjectAt<std::memory_order_release>(target_pool_index_,
                                                      target);
}

SwitchableCallPatternBase::SwitchableCallPatternBase(
    const ObjectPool& object_pool)
    : object_pool_(object_pool),
      data_pool_index_(-1),
      target_pool_index_(-1) {}

ObjectPtr SwitchableCallPatternBase::data() const {
  return object_pool_.ObjectAt<std::memory_order_acquire>(data_pool_index_);
}

void SwitchableCallPatternBase::SetDataRelease(const Object& data) const {
  object_pool_.SetObjectAt<std::memory_order_release>(data_pool_index_, data);
}

SwitchableCallPattern::SwitchableCallPattern(uword pc, const Code& code)
    : SwitchableCallPatternBase(ObjectPool::Handle(code.GetObjectPool())) {
  ASSERT(code.ContainsInstructionAt(pc));
  ASSERT(*reinterpret_cast<uint32_t*>(pc - 4) ==
         (0x4C000000 | (static_cast<uint32_t>(TMP) << 5) |
          static_cast<uint32_t>(RA)));

  Register reg;
  uword target_load_end = InstructionPattern::DecodeLoadWordFromPool(
      pc - 8, &reg, &data_pool_index_);
  ASSERT_EQUAL(reg, IC_DATA_REG);

  InstructionPattern::DecodeLoadWordFromPool(target_load_end, &reg,
                                             &target_pool_index_);
  ASSERT_EQUAL(reg, CODE_REG);
}

ObjectPtr SwitchableCallPattern::target() const {
  return object_pool_.ObjectAt<std::memory_order_acquire>(target_pool_index_);
}

void SwitchableCallPattern::SetTargetRelease(const Code& target) const {
  object_pool_.SetObjectAt<std::memory_order_release>(target_pool_index_,
                                                      target);
}

BareSwitchableCallPattern::BareSwitchableCallPattern(uword pc)
    : SwitchableCallPatternBase(
          ObjectPool::Handle(IsolateGroup::Current()->object_store()->global_object_pool())) {
  ASSERT(*reinterpret_cast<uint32_t*>(pc - 4) ==
         (0x4C000000 | (static_cast<uint32_t>(RA) << 5) |
          static_cast<uint32_t>(RA)));

  Register reg;
  uword target_load_end = InstructionPattern::DecodeLoadWordFromPool(
      pc - 4, &reg, &data_pool_index_);
  ASSERT_EQUAL(reg, IC_DATA_REG);

  InstructionPattern::DecodeLoadWordFromPool(target_load_end, &reg,
                                             &target_pool_index_);
  ASSERT_EQUAL(reg, RA);
}

uword BareSwitchableCallPattern::target_entry() const {
  return object_pool_.RawValueAt<std::memory_order_relaxed>(target_pool_index_);
}

void BareSwitchableCallPattern::SetTargetRelease(const Code& target) const {
  ASSERT(object_pool_.TypeAt(target_pool_index_) ==
         ObjectPool::EntryType::kImmediate);
  object_pool_.SetRawValueAt<std::memory_order_release>(
      target_pool_index_, target.MonomorphicEntryPoint());
}

ReturnPattern::ReturnPattern(uword pc) : pc_(pc) {}

bool ReturnPattern::IsValid() const {
  return *reinterpret_cast<uint32_t*>(pc_) == 0x4C000020;  // jirl r0, ra, 0 = ret
}

bool PcRelativePatternBase::IsValid() const {
  // Check for pcaddu12i + jirl pattern  
  uint32_t instr1 = LoadUnaligned(reinterpret_cast<uint32_t*>(pc_));
  uint32_t instr2 = LoadUnaligned(reinterpret_cast<uint32_t*>(pc_ + 4));
  return (instr1 & 0xFC000000) == 0x1C000000 &&  // pcaddu12i
         (instr2 & 0xFC000000) == 0x4C000000;   // jirl
}

bool PcRelativeCallPattern::IsValid() const {
  uint32_t instr1 = LoadUnaligned(reinterpret_cast<uint32_t*>(pc_));
  uint32_t instr2 = LoadUnaligned(reinterpret_cast<uint32_t*>(pc_ + 4));
  return (instr1 & 0xFC000000) == 0x1C000000 &&  // pcaddu12i
         (instr2 & 0xFC000000) == 0x4C000000 &&   // jirl
         (instr2 & 0x1f) == RA &&                  // rd = RA
         ((instr2 >> 5) & 0x1f) == RA;             // rj = RA
}

bool PcRelativeTailCallPattern::IsValid() const {
  uint32_t instr1 = LoadUnaligned(reinterpret_cast<uint32_t*>(pc_));
  uint32_t instr2 = LoadUnaligned(reinterpret_cast<uint32_t*>(pc_ + 4));
  return (instr1 & 0xFC000000) == 0x1C000000 &&  // pcaddu12i
         (instr2 & 0xFC000000) == 0x4C000000 &&   // jirl
         (instr2 & 0x1f) == ZR &&                  // rd = ZR
         ((instr2 >> 5) & 0x1f) == TMP;             // rj = TMP
}

void PcRelativeTrampolineJumpPattern::Initialize() {
  // pcaddu12i tmp, 0; jirl r0, tmp, 0
  StoreUnaligned(reinterpret_cast<uint32_t*>(pc_),
                 0x1C000000 | (static_cast<uint32_t>(TMP) << 0));
  StoreUnaligned(reinterpret_cast<uint32_t*>(pc_ + 4),
                 0x4C000000 | (static_cast<uint32_t>(TMP) << 5) |
                     static_cast<uint32_t>(ZR));
}


intptr_t TypeTestingStubCallPattern::GetSubtypeTestCachePoolIndex() {
  uword pc = pc_ - PcRelativeCallPattern::kLengthInBytes;
  PcRelativeCallPattern pattern(pc);
  if (!pattern.IsValid()) {
    pc = pc_ - Instr::kInstrSize;
    uint32_t instr = *reinterpret_cast<uint32_t*>(pc);
    ASSERT((instr & 0xFC000000) == 0x4C000000);
    ASSERT((instr & 0x1f) == RA);
  }
  const uword load_instr_end = pc;
  Register reg;
  intptr_t pool_index = -1;
  InstructionPattern::DecodeLoadWordFromPool(load_instr_end, &reg, &pool_index);
  ASSERT_EQUAL(reg, TypeTestABI::kSubtypeTestCacheReg);
  return pool_index;
}
}  // namespace dart

#endif  // defined TARGET_ARCH_LOONG64
