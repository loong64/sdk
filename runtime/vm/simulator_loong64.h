// Copyright (c) 2017, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef RUNTIME_VM_SIMULATOR_LOONG64_H_
#define RUNTIME_VM_SIMULATOR_LOONG64_H_


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

  
  // Dart generally calls into generated code with 4 parameters. This is a
  // convenience function, which sets up the simulator state and grabs the
  // result on return. The return value is A0. The parameters are placed in
  // A0-3.
  int64_t Call(intptr_t entry,
               intptr_t parameter0,
               intptr_t parameter1,
               intptr_t parameter2,
               intptr_t parameter3,
               bool fp_return = false,
               bool fp_args = false);

  void CallV(intptr_t function, intptr_t arg0, intptr_t arg1 = 0) {
    PreservedRegisters preserved;
    PrepareCall(&preserved);
    set_xreg(A0, arg0);
    set_xreg(A1, arg1);
    RunCall(function, &preserved);
  }
  uword stack_base() const { return stack_base_; }


  // Runtime and native call support.
  enum CallKind {
    kRuntimeCall,
    kLeafRuntimeCall,
    kLeafFloatRuntimeCall,
    kNativeCallWrapper
  };
  static uword RedirectExternalReference(uword function,
                                         CallKind call_kind,
                                         int argument_count);

  static uword FunctionForRedirect(uword redirect);
  // ==== Simulator stack ====
  uword stack_limit() const { return stack_limit_; }
  uword overflow_stack_limit() const { return overflow_stack_limit_; }


  // Call on program start.
  static void Init();

    void JumpToFrame(uword pc, uword sp, uword fp, Thread* thread);

  // ==== Accessors (get/set) for register values ====
  uintptr_t get_pc() const { return pc_; }
  void set_pc(uintptr_t value) { pc_ = value; }
  uintptr_t get_xreg(Register rs) const { return xregs_[rs]; }
  int64_t get_register(Register reg) const { return static_cast<int64_t>(get_xreg(reg)); }
  void set_register(Register reg, int64_t value) { set_xreg(reg, static_cast<uintptr_t>(value)); }
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
    uint64_t bits64 = bit_cast<uint64_t>(fregs_[rs]);
    uint32_t bits32 = static_cast<uint32_t>(bits64);
    return bit_cast<float>(bits32);
  }
  float get_fregs(FRegister rs) const {
    uint64_t bits64 = bit_cast<uint64_t>(fregs_[rs]);
    if ((bits64 & kNaNBox) != kNaNBox) {
      return bit_cast<float>(static_cast<uint32_t>(0x7fc00000));
    }
    uint32_t bits32 = static_cast<uint32_t>(bits64);
    return bit_cast<float>(bits32);
  }
  void set_fregs(FRegister rd, float value) {
    uint32_t bits32 = bit_cast<uint32_t>(value);
    uint64_t bits64 = static_cast<uint64_t>(bits32);
    bits64 |= kNaNBox;
    fregs_[rd] = bit_cast<double>(bits64);
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
  uint32_t fcrcmp_result_ = 0;  // Result of last fcmp instruction (for bceqz/bcnez)

  // Simulator support.
  char* stack_;
  char* shadow_stack_;
  uword stack_limit_;
  uword overflow_stack_limit_;
  uword stack_base_;
  Random random_;
  SimulatorSetjmpBuffer* last_setjmp_buffer_ = nullptr;
  SimulatorMemory memory_;

  static bool IsIllegalAddress(uword addr) {
    // Null-like or sign-extension-poisoned addresses.
    // Addresses in the low 64KB or top 256TB are likely bugs.
    return addr < 64 * 1024 || addr > 0x0000FFFFFFFFFFFFULL;
  }

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
  void RestorePreservedRegisters(PreservedRegisters* preserved);
  DART_NORETURN void Fault(const char* message);
  DART_NORETURN void IllegalInstruction(Instr instr);
  intx_t CSRRead(uint16_t csr);
  void CSRWrite(uint16_t csr, intx_t value);
  uint64_t ReadMem(uintx_t address, int size);
  void WriteMem(uintx_t address, uint64_t value, int size);
  template <typename type>
  type MemoryRead(uintx_t address, Register base);
  template <typename type>
  void MemoryWrite(uintx_t address, type value, Register base);
  static void set_current_simulator(Simulator* simulator);


  void CheckPreservedRegisters(PreservedRegisters* preserved);
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
