// Copyright (c) 2017, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(TARGET_ARCH_LOONG64)

#define SHOULD_NOT_INCLUDE_RUNTIME

#include "vm/compiler/assembler/assembler.h"
#include "vm/compiler/backend/locations.h"

namespace dart {
namespace compiler {

void Assembler::PushRegisters(const RegisterSet& registers) {
  const intptr_t size = registers.SpillSize();
  if (size == 0) {
    return;
  }

  AddImmediate(SP, SP, -size);
  intptr_t offset = size;
  for (intptr_t i = kNumberOfFpuRegisters - 1; i >= 0; i--) {
    const FRegister reg = static_cast<FRegister>(i);
    if (registers.ContainsFpuRegister(reg)) {
      offset -= kFpuRegisterSize;
      StoreD(reg, Address(SP, offset));
    }
  }
  for (intptr_t i = kNumberOfCpuRegisters - 1; i >= 0; i--) {
    const Register reg = static_cast<Register>(i);
    if (registers.ContainsRegister(reg)) {
      offset -= target::kWordSize;
      Store(reg, Address(SP, offset));
    }
  }
  ASSERT(offset == 0);
}

void Assembler::PopRegisters(const RegisterSet& registers) {
  const intptr_t size = registers.SpillSize();
  if (size == 0) {
    return;
  }

  intptr_t offset = 0;
  for (intptr_t i = 0; i < kNumberOfCpuRegisters; i++) {
    const Register reg = static_cast<Register>(i);
    if (registers.ContainsRegister(reg)) {
      Load(reg, Address(SP, offset));
      offset += target::kWordSize;
    }
  }
  for (intptr_t i = 0; i < kNumberOfFpuRegisters; i++) {
    const FRegister reg = static_cast<FRegister>(i);
    if (registers.ContainsFpuRegister(reg)) {
      LoadD(reg, Address(SP, offset));
      offset += kFpuRegisterSize;
    }
  }
  ASSERT(offset == size);
  AddImmediate(SP, SP, size);
}

void Assembler::PushRegistersAligned(const RegisterSet& registers,
                                     intptr_t space) {
  PushRegisters(registers);
  const intptr_t aligned_space =
      Utils::RoundUp(registers.SpillSize() + space,
                     OS::ActivationFrameAlignment()) -
      registers.SpillSize();
  if (aligned_space != 0) {
    AddImmediate(SP, SP, -aligned_space);
  }
}

void Assembler::PopRegistersAligned(const RegisterSet& registers,
                                    intptr_t space) {
  const intptr_t aligned_space =
      Utils::RoundUp(registers.SpillSize() + space,
                     OS::ActivationFrameAlignment()) -
      registers.SpillSize();
  if (aligned_space != 0) {
    AddImmediate(SP, SP, aligned_space);
  }
  PopRegisters(registers);
}

void Assembler::CombineHashes(Register hash, Register other) {
  // hash += other_hash
  add_d(hash, hash, other);
  slli_d(hash, hash, 32);
  srai_d(hash, hash, 32);
  // hash += hash << 10
  slli_d(other, hash, 10);
  add_d(hash, hash, other);
  slli_d(hash, hash, 32);
  srai_d(hash, hash, 32);
  // hash ^= hash >> 6
  slli_d(other, hash, 32);
  srli_d(other, other, 32);
  srli_d(other, other, 6);
  xor_(hash, hash, other);
  slli_d(hash, hash, 32);
  srai_d(hash, hash, 32);
}

void Assembler::FinalizeHashForSize(intptr_t bit_size,
                                    Register hash,
                                    Register scratch) {
  ASSERT(bit_size > 0);
  ASSERT(bit_size <= kBitsPerInt32);
  ASSERT(scratch != kNoRegister);
  // hash += hash << 3
  slli_d(scratch, hash, 3);
  add_d(hash, hash, scratch);
  slli_d(hash, hash, 32);
  srai_d(hash, hash, 32);
  // hash ^= hash >> 11
  slli_d(scratch, hash, 32);
  srli_d(scratch, scratch, 32);
  srli_d(scratch, scratch, 11);
  xor_(hash, hash, scratch);
  slli_d(hash, hash, 32);
  srai_d(hash, hash, 32);
  // hash += hash << 15
  slli_d(scratch, hash, 15);
  add_d(hash, hash, scratch);
  slli_d(hash, hash, 32);
  srai_d(hash, hash, 32);
  if (bit_size < kBitsPerInt32) {
    AndImmediate(hash, hash, Utils::NBitMask(bit_size));
  }
  Label done;
  bnez(hash, &done, kNearJump);
  AddImmediate(hash, hash, 1);
  Bind(&done);
}

}  // namespace compiler
}  // namespace dart

#endif  // defined(TARGET_ARCH_LOONG64)
