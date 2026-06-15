// Copyright (c) 2021, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.
// Classes that describe assembly patterns as used by inline caches.

#ifndef RUNTIME_VM_INSTRUCTIONS_LOONG64_H_
#define RUNTIME_VM_INSTRUCTIONS_LOONG64_H_

#ifndef RUNTIME_VM_INSTRUCTIONS_H_
#error Do not include instructions_loong64.h directly; use instructions.h instead.
#endif

#include "platform/unaligned.h"
#include "vm/allocation.h"
#include "vm/constants.h"
#include "vm/native_function.h"
#include "vm/tagged_pointer.h"

#if !defined(DART_PRECOMPILED_RUNTIME)
#include "vm/compiler/assembler/assembler.h"
#endif  // !defined(DART_PRECOMPILED_RUNTIME)

namespace dart {

class Code;
class ICData;
class Object;
class ObjectPool;

class InstructionPattern : public AllStatic {
 public:
  static uword DecodeLoadWordImmediate(uword end,
                                       Register* reg,
                                       intptr_t* value);

  static uword DecodeLoadWordFromPool(uword end,
                                      Register* reg,
                                      intptr_t* index);

  static void EncodeLoadWordFromPoolFixed(uword end, int32_t offset);
};

class CallPattern : public ValueObject {
 public:
  CallPattern(uword pc, const Code& code);

  CodePtr TargetCode() const;
  void SetTargetCode(const Code& target) const;

 private:
  const ObjectPool& object_pool_;

  intptr_t target_code_pool_index_;

  DISALLOW_COPY_AND_ASSIGN(CallPattern);
};

class ICCallPattern : public ValueObject {
 public:
  ICCallPattern(uword pc, const Code& caller_code);

  ObjectPtr Data() const;
  void SetData(const Object& data) const;

  CodePtr TargetCode() const;
  void SetTargetCode(const Code& target) const;

 private:
  const ObjectPool& object_pool_;

  intptr_t target_pool_index_;
  intptr_t data_pool_index_;

  DISALLOW_COPY_AND_ASSIGN(ICCallPattern);
};

class NativeCallPattern : public ValueObject {
 public:
  NativeCallPattern(uword pc, const Code& code);

  CodePtr target() const;
  void set_target(const Code& target) const;

  NativeFunction native_function() const;
  void set_native_function(NativeFunction target) const;

 private:
  const ObjectPool& object_pool_;

  uword end_;
  intptr_t native_function_pool_index_;
  intptr_t target_code_pool_index_;

  DISALLOW_COPY_AND_ASSIGN(NativeCallPattern);
};

class SwitchableCallPatternBase : public ValueObject {
 public:
  explicit SwitchableCallPatternBase(const ObjectPool& object_pool);

  ObjectPtr data() const;
  void SetDataRelease(const Object& data) const;

 protected:
  const ObjectPool& object_pool_;
  intptr_t data_pool_index_;
  intptr_t target_pool_index_;

 private:
  DISALLOW_COPY_AND_ASSIGN(SwitchableCallPatternBase);
};

class SwitchableCallPattern : public SwitchableCallPatternBase {
 public:
  SwitchableCallPattern(uword pc, const Code& code);

  ObjectPtr target() const;
  void SetTargetRelease(const Code& target) const;

 private:
  DISALLOW_COPY_AND_ASSIGN(SwitchableCallPattern);
};

class BareSwitchableCallPattern : public SwitchableCallPatternBase {
 public:
  explicit BareSwitchableCallPattern(uword pc);

  uword target_entry() const;
  void SetTargetRelease(const Code& target) const;

 private:
  DISALLOW_COPY_AND_ASSIGN(BareSwitchableCallPattern);
};

class ReturnPattern : public ValueObject {
 public:
  explicit ReturnPattern(uword pc);

  // ret = 1 instruction (4 bytes)
  static constexpr intptr_t kLengthInBytes = 4;

  int pattern_length_in_bytes() const { return kLengthInBytes; }

  bool IsValid() const;

 private:
  const uword pc_;
};

class PcRelativePatternBase : public ValueObject {
 public:
  static constexpr intptr_t kLengthInBytes = 8;
  static constexpr intptr_t kLowerCallingRange =
      static_cast<int32_t>(0x80000000);
  static constexpr intptr_t kUpperCallingRange =
      static_cast<int32_t>(0x7FFFFFFE);

  explicit PcRelativePatternBase(uword pc) : pc_(pc) {}

  int32_t distance() {
    uint32_t pcaddu = LoadUnaligned(reinterpret_cast<uint32_t*>(pc_));
    uint32_t jirl = LoadUnaligned(reinterpret_cast<uint32_t*>(pc_ + 4));
    // LA: pcaddu12i stores si20@bits[25:5], jirl stores si12@bits[25:10]
    int32_t si20 = static_cast<int32_t>((pcaddu >> 5) & 0xfffff);
    if (si20 & 0x80000) si20 |= 0xfff00000;  // sign extend
    int32_t si12 = static_cast<int32_t>((jirl >> 10) & 0xfff);
    if (si12 & 0x800) si12 |= 0xfffff000;  // sign extend
    return (si20 << 12) + (si12 << 0);
  }

  void set_distance(int32_t distance) {
    uint32_t pcaddu = LoadUnaligned(reinterpret_cast<uint32_t*>(pc_));
    uint32_t jirl = LoadUnaligned(reinterpret_cast<uint32_t*>(pc_ + 4)));
    intx_t imm = distance;
    intx_t lo = imm & 0xfff;
    intx_t hi = (imm - lo) >> 12;
    StoreUnaligned(reinterpret_cast<uint32_t*>(pc_),
                   EncodeSi20(hi) | EncodeRd(static_cast<Register>(pcaddu & 0x1f)) |
                       0x18000000);  // pcaddu12i opcode
    StoreUnaligned(reinterpret_cast<uint32_t*>(pc_ + 4),
                   EncodeSi12(lo) | EncodeRj(static_cast<Register>((jirl >> 5) & 0x1f)) |
                       EncodeRd(static_cast<Register>(jirl & 0x1f)) |
                       0x4C000000);  // jirl opcode
  }

  bool IsValid() const;

 protected:
  uword pc_;
};

// pcaddu12i ra, si20
// jirl ra, ra, si12
class PcRelativeCallPattern : public PcRelativePatternBase {
 public:
  explicit PcRelativeCallPattern(uword pc) : PcRelativePatternBase(pc) {}

  bool IsValid() const;
};

// pcaddu12i tmp, si20
// jirl zero, tmp, si12
class PcRelativeTailCallPattern : public PcRelativePatternBase {
 public:
  explicit PcRelativeTailCallPattern(uword pc) : PcRelativePatternBase(pc) {}

  bool IsValid() const;
};

class PcRelativeTrampolineJumpPattern : public PcRelativeTailCallPattern {
 public:
  explicit PcRelativeTrampolineJumpPattern(uword pc)
      : PcRelativeTailCallPattern(pc) {}

  void Initialize();
};

}  // namespace dart

#endif  // RUNTIME_VM_INSTRUCTIONS_LOONG64_H_