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
  // Check if instruction is pcaddu12i: 0x18000000 | si20<<5 | rd
  static bool IsPcaddu12i(uint32_t instr) {
    // pcaddu12i has bits[31:26] = 000110. 0x18 in byte3 = bits[31:26]=000110, bits[25:24]=00
    return (instr & 0xFC000000) == 0x18000000;
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
  // Pattern: pcalau12i + ld_d sequence to load from pool, then jirl/jr
  // Last instruction: ret (jirl r0, ra, 0) = 0x4C000020
  ASSERT(*reinterpret_cast<uint32_t*>(pc - 4) == 0x4C000020);

  Register reg;
  InstructionPattern::DecodeLoadWordFromPool(pc - 12, &reg,
                                             &target_code_pool_index_);
  ASSERT(IsJumpAndLinkScratch(reg));
}

ICCallPattern::ICCallPattern(uword pc, const Code& code)
    : object_pool_(ObjectPool::Handle(code.GetObjectPool())),
      target_pool_index_(-1),
      data_pool_index_(-1) {
  ASSERT(code.ContainsInstructionAt(pc));
  // Last instruction: ret (jirl r0, ra, 0) = 0x4C000020
  ASSERT(*reinterpret_cast<uint32_t*>(pc - 4) == 0x4C000020);

  Register reg;
  uword target_load_end = InstructionPattern::DecodeLoadWordFromPool(
      pc - 12, &reg, &data_pool_index_);
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
  // Last instruction: ret (jirl r0, ra, 0) = 0x4C000020
  ASSERT(*reinterpret_cast<uint32_t*>(pc - 4) == 0x4C000020);

  Register reg;
  uword native_function_load_end = InstructionPattern::DecodeLoadWordFromPool(
      pc - 12, &reg, &target_code_pool_index_);
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
  // LoongArch pool load pattern: pcalau12i rd, offset>>12; ld_d rd, rd, offset & 0xfff
  Register dst1, base;
  intptr_t hi20;
  if (!DecodePCADDU12I(end, &dst1, &hi20, nullptr)) {
    FATAL("DecodeLoadWordFromPool: expected pcalau12i");
  }
  Register dst2;
  intptr_t offset;
  intptr_t load_length;
  if (!DecodeLoadX(end - 4, &dst2, &base, &offset, &load_length)) {
    FATAL("DecodeLoadWordFromPool: expected ld_d");
  }
  ASSERT(dst1 == dst2);
  *reg = dst1;
  // offset = hi20 << 12 + offset (12-bit signed)
  *index = ObjectPool::IndexFromOffset((hi20 << 12) + offset);
  return end - 4 - load_length;
}

void InstructionPattern::EncodeLoadWordFromPoolFixed(uword end,
                                                     int32_t offset) {
  Register reg;
  intptr_t current_offset;
  intptr_t decoded_length;
  // Decode the current instruction sequence to find the register used.
  DecodeLoadX(end, &reg, nullptr, &current_offset, &decoded_length);

  intx_t hi = (offset >> 12) + ((offset >> 11) & 1);
  intx_t lo = offset & 0xfff;
  // pcalau12i reg, hi
  StoreUnaligned(reinterpret_cast<uint32_t*>(end - 8),
                 0x1A000000 | (static_cast<uint32_t>(hi & 0xfffff) << 5) |
                     static_cast<uint32_t>(reg));
  // ld_d reg, reg, lo
  StoreUnaligned(reinterpret_cast<uint32_t*>(end - 4),
                 0x28C00000 | (static_cast<uint32_t>(lo & 0xfff) << 10) |
                     (static_cast<uint32_t>(reg) << 5) |
                     static_cast<uint32_t>(reg));
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
  // Last instruction: ret (jirl r0, ra, 0) = 0x4C000020
  ASSERT(*reinterpret_cast<uint32_t*>(pc - 4) == 0x4C000020);

  Register reg;
  InstructionPattern::DecodeLoadWordFromPool(pc - 12, &reg,
                                             &data_pool_index_);
  ASSERT(reg == DISPATCH_TABLE_REG);

  InstructionPattern::DecodeLoadWordFromPool(pc + 0x80, &reg,
                                             &target_pool_index_);
  UNREACHABLE();  // Not implemented for non-bare mode.
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
          ObjectPool::Handle(Code::Handle(ReversePcLookupCache::FindCode(pc))
                                 .GetObjectPool())) {
  // Pattern: pcalau12i rd, (offset >> 12); ld_d rd, rd, (offset & 0xfff)
  uint32_t instr1 = LoadUnaligned(reinterpret_cast<uint32_t*>(pc));
  uint32_t instr2 = LoadUnaligned(reinterpret_cast<uint32_t*>(pc + 4));
  // pcalau12i: 0x1A000000 | (si20 << 5) | rd
  if ((instr1 & 0xFE000000) == 0x1A000000) {
    uint32_t hi20 = (instr1 >> 5) & 0xfffff;
    uint32_t lo12 = (instr2 >> 10) & 0xfff;
    data_pool_index_ = ObjectPool::IndexFromOffset((hi20 << 12) + lo12);
  }
}

uword BareSwitchableCallPattern::target_entry() const {
  return static_cast<uword>(
      object_pool_.RawValueAt(target_pool_index_));
}

void BareSwitchableCallPattern::SetTargetRelease(const Code& target) const {
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
  return (instr1 & 0xFC000000) == 0x18000000 &&  // pcaddu12i
         (instr2 & 0xFC000000) == 0x4C000000;   // jirl
}

bool PcRelativeCallPattern::IsValid() const {
  uint32_t instr1 = LoadUnaligned(reinterpret_cast<uint32_t*>(pc_));
  uint32_t instr2 = LoadUnaligned(reinterpret_cast<uint32_t*>(pc_ + 4));
  return (instr1 & 0xFC000000) == 0x18000000 &&  // pcaddu12i
         (instr2 & 0xFC000000) == 0x4C000000 &&   // jirl
         (instr2 & 0x1f) == RA &&                  // rd = RA
         ((instr2 >> 5) & 0x1f) == RA;             // rj = RA
}

bool PcRelativeTailCallPattern::IsValid() const {
  uint32_t instr1 = LoadUnaligned(reinterpret_cast<uint32_t*>(pc_));
  uint32_t instr2 = LoadUnaligned(reinterpret_cast<uint32_t*>(pc_ + 4));
  return (instr1 & 0xFC000000) == 0x18000000 &&  // pcaddu12i
         (instr2 & 0xFC000000) == 0x4C000000 &&   // jirl
         (instr2 & 0x1f) == ZR &&                  // rd = ZR
         ((instr2 >> 5) & 0x1f) == TMP;             // rj = TMP
}

void PcRelativeTrampolineJumpPattern::Initialize() {
  // pcaddu12i tmp, 0; jirl r0, tmp, 0
  StoreUnaligned(reinterpret_cast<uint32_t*>(pc_),
                 0x18000000 | (static_cast<uint32_t>(TMP) << 0));
  StoreUnaligned(reinterpret_cast<uint32_t*>(pc_ + 4),
                 0x4C000000 | (static_cast<uint32_t>(TMP) << 5) |
                     static_cast<uint32_t>(ZR));
}

}  // namespace dart

#endif  // defined TARGET_ARCH_LOONG64