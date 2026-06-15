// Copyright (c) 2017, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef RUNTIME_VM_SIMULATOR_LOONG64_H_
#define RUNTIME_VM_SIMULATOR_LOONG64_H_

#if defined(DART_PRECOMPILED_RUNTIME)
#error "AOT runtime should not use simulator sources (including header files)"
#endif  // defined(DART_PRECOMPILED_RUNTIME)

#ifndef RUNTIME_VM_SIMULATOR_H_
#error Do not include simulator_loong64.h directly; use simulator.h.
#endif

#include "vm/constants.h"
#include "vm/random.h"
#include "vm/simulator_memory.h"

namespace dart {

class Isolate;
class Mutex;
class SimulatorSetjmpBuffer;
class Thread;

class Simulator {
 public:
  static constexpr intptr_t kElen = 64;
  static constexpr intptr_t kVlen = 128;

  struct PreservedRegisters {
    uintptr_t xregs[kNumberOfCpuRegisters];
    double fregs[kNumberOfFpuRegisters];
    uint32_t fcsr;
    uintptr_t pc;
  };
  static constexpr uword kSimulatorStackUnderflowSize = 64;

  Simulator();
  ~Simulator();

  static Simulator* Current();

  intptr_t CallX(intptr_t function,
                 intptr_t arg0 = 0,
                 intptr_t arg1 = 0,
                 intptr_t arg2 = 0,
                 intptr_t arg3 = 0) {
    PreservedRegisters preserved;
    PrepareCall(&preserved);
    set_xreg(A0, arg0);
    set_xreg(A1, arg1);
    set_xreg(A2, arg2);
    set_xreg(A3, arg3);
    RunCall(function, &preserved);
    return get_xreg(A0);
  }

  intptr_t CallI(intptr_t function, double arg0, double arg1 = 0.0) {
    PreservedRegisters preserved;
    PrepareCall(&preserved);
    set_fregd(FA0, arg0);
    set_fregd(FA1, arg1);
    RunCall(function, &preserved);
    return get_xreg(A0);
  }
  intptr_t CallI(intptr_t function, float arg0, float arg1 = 0.0f) {
    PreservedRegisters preserved;
    PrepareCall(&preserved);
    set_fregs(FA0, arg0);
    set_fregs(FA1, arg1);
    RunCall(function, &preserved);
    return get_xreg(A0);
  }

  int64_t CallI64(intptr_t function, double arg0, double arg1 = 0.0) {
    PreservedRegisters preserved;
    PrepareCall(&preserved);
    set_fregd(FA0, arg0);
    set_fregd(FA1, arg1);
    RunCall(function, &preserved);
    return get_xreg(A0);
  }

  double CallD(intptr_t function, intptr_t arg0, intptr_t arg1 = 0) {
    PreservedRegisters preserved;
    PrepareCall(&preserved);
    set_xreg(A0, arg0);
    set_xreg(A1, arg1);
    RunCall(function, &preserved);
    return get_fregd(FA0);
  }

  double CallD(intptr_t function,
               double arg0,
               double arg1 = 0.0,
               double arg2 = 0.0) {
    PreservedRegisters preserved;
    PrepareCall(&preserved);
    set_fregd(FA0, arg0);
    set_fregd(FA1, arg1);
    set_fregd(FA2, arg2);
    RunCall(function, &preserved);
    return get_fregd(FA0);
  }
  double CallD(intptr_t function, intptr_t arg0, double arg1) {
    PreservedRegisters preserved;
    PrepareCall(&preserved);
    set_xreg(A0, arg0);
    set_fregd(FA0, arg1);
    RunCall(function, &preserved);
    return get_fregd(FA0);
  }
  double CallD(intptr_t function, float arg0) {
    PreservedRegisters preserved;
    PrepareCall(&preserved);
    set_fregs(FA0, arg0);
    RunCall(function, &preserved);
    return get_fregd(FA0);
  }

  float CallF(intptr_t function, intptr_t arg0, intptr_t arg1 = 0) {
    PreservedRegisters preserved;
    PrepareCall(&preserved);
    set_xreg(A0, arg0);
    set_xreg(A1, arg1);
    RunCall(function, &preserved);
    return get_fregs(FA0);
  }
  float CallF(intptr_t function,
              float arg0,
              float arg1 = 0.0f,
              float arg2 = 0.0f) {
    PreservedRegisters preserved;
    PrepareCall(&preserved);
    set_fregs(FA0, arg0);
    set_fregs(FA1, arg1);
    set_fregs(FA2, arg2);
    RunCall(function, &preserved);
    return get_fregs(FA0);
  }
  float CallF(intptr_t function, intptr_t arg0, float arg1) {
    PreservedRegisters preserved;
    PrepareCall(&preserved);
    set_xreg(A0, arg0);
    set_fregs(FA0, arg1);
    RunCall(function, &preserved);
    return get_fregs(FA0);
  }
  float CallF(intptr_t function, double arg0) {
    PreservedRegisters preserved;
    PrepareCall(&preserved);
    set_fregd(FA0, arg0);
    RunCall(function, &preserved);
    return get_fregs(FA0);
  }

  void CallV(intptr_t function, intptr_t arg0, intptr_t arg1 = 0) {
    PreservedRegisters preserved;
    PrepareCall(&preserved);
    set_xreg(A0, arg0);
    set_xreg(A1, arg1);
    RunCall(function, &preserved);
  }

  // ==== Simulator stack ====
  uword StackLimit() const { return stack_limit_; }
  uword OverflowStackLimit() const { return overflow_stack_limit_; }

  void JumpToFrame(uword pc, uword sp, uword fp, Thread* thread);

  // ==== Accessors (get/set) for register values ====
  uintptr_t get_pc() const { return pc_; }
  void set_pc(uintptr_t value) { pc_ = value; }
  uintptr_t get_xreg(Register rs) const { return xregs_[rs]; }
  void set_xreg(Register rd, uintptr_t value) {
    if (rd != ZR) {
      xregs_[rd] = value;
    }
  }
  intptr_t get_lr() const { return get_xreg(RA); }
  void set_lr(intptr_t value) { set_xreg(RA, value); }
  intptr_t get_sp() const { return get_xreg(SP); }
  void set_sp(intptr_t value) { set_xreg(SP, value); }

  double get_fregd(FRegister rs) const { return fregs_[rs]; }
  void set_fregd(FRegister rd, double value) { fregs_[rd] = value; }

  static constexpr uint64_t kNaNBox = 0xFFFFFFFF00000000;

  float get_fregs_raw(FRegister rs) const {
    uint64_t bits64 = Utils::BitCast<uint64_t>(fregs_[rs]);
    uint32_t bits32 = static_cast<uint32_t>(bits64);
    return Utils::BitCast<float>(bits32);
  }
  float get_fregs(FRegister rs) const {
    uint64_t bits64 = Utils::BitCast<uint64_t>(fregs_[rs]);
    if ((bits64 & kNaNBox) != kNaNBox) {
      return Utils::BitCast<float>(static_cast<uint32_t>(0x7fc00000));
    }
    uint32_t bits32 = static_cast<uint32_t>(bits64);
    return Utils::BitCast<float>(bits32);
  }
  void set_fregs(FRegister rd, float value) {
    uint32_t bits32 = Utils::BitCast<uint32_t>(value);
    uint64_t bits64 = static_cast<uint64_t>(bits32);
    bits64 |= kNaNBox;
    fregs_[rd] = Utils::BitCast<double>(bits64);
  }

  // Known bad pc value to ensure that the simulator does not execute
  // without being properly setup.
  static constexpr uword kBadLR = -1;
  static constexpr uword kEndSimulatingPC = -2;

  // I state.
  uintptr_t pc_ = 0;
  uintptr_t xregs_[kNumberOfCpuRegisters];
  uint64_t instret_ = 0;

  // A state.
  uintptr_t reserved_address_ = 0;
  uintptr_t reserved_value_ = 0;

  // F/D state.
  double fregs_[kNumberOfFpuRegisters];
  uint32_t fcsr_ = 0;

  // Simulator support.
  char* stack_;
  char* shadow_stack_;
  uword stack_limit_;
  uword overflow_stack_limit_;
  uword stack_base_;
  Random random_;
  SimulatorSetjmpBuffer* last_setjmp_buffer_ = nullptr;
  SimulatorMemory memory_;

  static bool IsIllegalAddress(uword addr) { return addr < 64 * 1024; }

  void Execute();
  void ExecuteNoTrace();
  void ExecuteTrace();

  bool IsTracingExecution() const;

  SimulatorSetjmpBuffer* last_setjmp_buffer() { return last_setjmp_buffer_; }
  void set_last_setjmp_buffer(SimulatorSetjmpBuffer* buffer) {
    last_setjmp_buffer_ = buffer;
  }

  friend class SimulatorSetjmpBuffer;
  DISALLOW_COPY_AND_ASSIGN(Simulator);

 private:
  void PrepareCall(PreservedRegisters* preserved);
  void SavePreservedRegisters(PreservedRegisters* preserved);
  void RunCall(intptr_t function, PreservedRegisters* preserved);
};

COMPILE_ASSERT(Simulator::kElen >= 8);
COMPILE_ASSERT(Utils::IsPowerOfTwo(Simulator::kElen));
COMPILE_ASSERT(Simulator::kVlen >= Simulator::kElen);
COMPILE_ASSERT(Utils::IsPowerOfTwo(Simulator::kVlen));
COMPILE_ASSERT(Simulator::kVlen <= 0x10000);

}  // namespace dart

#endif  // RUNTIME_VM_SIMULATOR_LOONG64_H_
