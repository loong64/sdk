// Copyright (c) 2017, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#include "vm/constants.h"
#if defined(TARGET_ARCH_LOONG64) && defined(DART_INCLUDE_SIMULATOR)

#include "vm/simulator.h"
#include "vm/simulator_loong64.h"

#include "platform/atomic.h"
#include "vm/compiler/assembler/disassembler.h"
#include "vm/compiler/runtime_api.h"
#include "vm/cpu.h"
#include "vm/dart_entry.h"
#include "vm/isolate.h"
#include "vm/longjump.h"
#include "vm/os_thread.h"
#include "vm/random.h"
#include "vm/stub_code.h"
#include "vm/symbols.h"
#include "vm/thread.h"
#include "vm/thread_state.h"

namespace dart {
class SimulatorSetjmpBuffer {
 public:
  void Longjmp() {
    simulator_->set_last_setjmp_buffer(this);
    DART_LONGJMP(buffer_, 1);
  }

  explicit SimulatorSetjmpBuffer(Simulator* sim) {
    simulator_ = sim;
    link_ = sim->last_setjmp_buffer();
    sim->set_last_setjmp_buffer(this);
    sp_ = static_cast<uword>(sim->get_register(SPREG));
  }

  ~SimulatorSetjmpBuffer() {
    ASSERT(simulator_->last_setjmp_buffer() == this);
    simulator_->set_last_setjmp_buffer(link_);
  }

  SimulatorSetjmpBuffer* link() { return link_; }
  uword sp() { return sp_; }

 private:
  uword sp_;
  Simulator* simulator_;
  SimulatorSetjmpBuffer* link_;
  jmp_buf buffer_;

  friend class Simulator;
  DISALLOW_COPY_AND_ASSIGN(SimulatorSetjmpBuffer);
};


DECLARE_FLAG(bool, trace_simulator);
DEFINE_FLAG(int, sim_stack_size, 128, "Simulator stack size (in kB)");
DEFINE_FLAG(int, sim_stack_hard_limit, 0,
            "If set, the simulated stack hard limit in kB");

DEFINE_FLAG(bool, sim_buffer_memory, false, "Simulate weak memory ordering.");
DEFINE_FLAG(bool, trace_simulator, false, "Trace simulator execution");

// LoongArch instruction field extraction helpers.
// All LoongArch instructions are 32 bits.
static uint32_t LA_Rd(uint32_t instr) { return instr & 0x1f; }
static uint32_t LA_Rj(uint32_t instr) { return (instr >> 5) & 0x1f; }
static uint32_t LA_Rk(uint32_t instr) { return (instr >> 10) & 0x1f; }
static uint32_t LA_Opcode(uint32_t instr) { return (instr >> 26) & 0x3f; }
static uint32_t LA_Op10(uint32_t instr) { return (instr >> 22) & 0x3ff; }
static int32_t LA_Si12(uint32_t instr) {
  int32_t v = (instr >> 10) & 0xfff;
  return (v & 0x800) ? (v | 0xfffff000) : v;
}
static uint32_t LA_Ui12(uint32_t instr) { return (instr >> 10) & 0xfff; }
static int32_t LA_Si14(uint32_t instr) {
  int32_t v = (instr >> 10) & 0x3fff;
  return (v & 0x2000) ? (v | 0xffffc000) : v;
}
static int32_t LA_Si16(uint32_t instr) {
  int32_t v = (instr >> 10) & 0xffff;
  return (v & 0x8000) ? (v | 0xffff0000) : v;
}
static int32_t LA_Si20(uint32_t instr) {
  int32_t v = (instr >> 5) & 0xfffff;
  return (v & 0x80000) ? (v | 0xfff00000) : v;
}
static int32_t LA_BranchOff21(uint32_t instr) {
  const uint32_t value = ((instr & 0x1f) << 16) | ((instr >> 10) & 0xffff);
  return static_cast<int32_t>(value << 11) >> 11;
}
static int32_t LA_BranchOff26(uint32_t instr) {
  const uint32_t value = ((instr & 0x3ff) << 16) | ((instr >> 10) & 0xffff);
  return static_cast<int32_t>(value << 6) >> 6;
}
static uint32_t LA_Ui5(uint32_t instr) { return (instr >> 10) & 0x1f; }
static uint32_t LA_Ui6(uint32_t instr) { return (instr >> 10) & 0x3f; }

// Redirection class used by the simulator to intercept and redirect
// calls from simulated code.
class Redirection {
 public:
  uword address_of_syscall_instruction() {
    return reinterpret_cast<uword>(&syscall_instruction_);
  }

  uword external_function() const { return external_function_; }

  Simulator::CallKind call_kind() const { return call_kind_; }

  int argument_count() const { return argument_count_; }

  static Redirection* Get(uword external_function,
                          Simulator::CallKind call_kind,
                          int argument_count) {
    MutexLocker ml(mutex_);

    Redirection* old_head = list_.load(std::memory_order_relaxed);
    for (Redirection* current = old_head; current != nullptr;
         current = current->next_) {
      if (current->external_function_ == external_function) return current;
    }

    Redirection* redirection =
        new Redirection(external_function, call_kind, argument_count);
    redirection->next_ = old_head;

    list_.store(redirection, std::memory_order_release);

    return redirection;
  }

  static Redirection* FromSyscallInstruction(Instr* syscall_instruction) {
    char* addr_of_syscall = reinterpret_cast<char*>(syscall_instruction);
    char* addr_of_redirection =
        addr_of_syscall - OFFSET_OF(Redirection, syscall_instruction_);
    return reinterpret_cast<Redirection*>(addr_of_redirection);
  }

  static uword FunctionForRedirect(uword address_of_syscall) {
    for (Redirection* current = list_.load(std::memory_order_acquire);
         current != nullptr; current = current->next_) {
      if (current->address_of_syscall_instruction() == address_of_syscall) {
        return current->external_function_;
      }
    }
    return 0;
  }

 private:
  Redirection(uword external_function,
              Simulator::CallKind call_kind,
              int argument_count)
      : external_function_(external_function),
        call_kind_(call_kind),
        argument_count_(argument_count),
        syscall_instruction_(Instr::kSimulatorRedirectInstruction),
        next_(nullptr) {}

  uword external_function_;
  Simulator::CallKind call_kind_;
  int argument_count_;
  uint32_t syscall_instruction_;
  Redirection* next_;
  static std::atomic<Redirection*> list_;
  static Mutex* mutex_;
};

std::atomic<Redirection*> Redirection::list_ = {nullptr};
Mutex* Redirection::mutex_ = new Mutex();

uword Simulator::RedirectExternalReference(uword function,
                                           CallKind call_kind,
                                           int argument_count) {
  Redirection* redirection = Redirection::Get(function, call_kind, argument_count);
  return redirection->address_of_syscall_instruction();
}

uword Simulator::FunctionForRedirect(uword redirect) {
  return Redirection::FunctionForRedirect(redirect);
}

void Simulator::Init() {}

Simulator::Simulator()
    : memory_(FLAG_sim_buffer_memory) {
  // Allocate the simulator stack with overflow/underflow buffer space.
  stack_ =
      new char[(OSThread::GetSpecifiedStackSize() +
                OSThread::kStackSizeBufferMax + kSimulatorStackUnderflowSize)];

  // Low address.
  stack_limit_ = reinterpret_cast<uword>(stack_);
  // Limit for StackOverflowError.
  overflow_stack_limit_ = stack_limit_ + OSThread::kStackSizeBufferMax;
  // High address.
  stack_base_ = overflow_stack_limit_ + OSThread::GetSpecifiedStackSize();

  // Initialize architecture state.
  pc_ = kBadLR;
  instret_ = 0;
  fcsr_ = 0;
  fcrcmp_result_ = 0;

  // Initialize LoongArch registers.
  for (intptr_t i = 0; i < kNumberOfCpuRegisters; i++) {
    xregs_[i] = 0;
  }
  for (intptr_t i = 0; i < kNumberOfFpuRegisters; i++) {
    fregs_[i] = 0.0;
  }

  // The sp is initialized to point to the bottom (high address) of the
  // allocated stack area.
  set_register(SPREG, stack_base_);
  // The ra is initialized to a known bad value that will cause an
  // access violation if the simulator ever tries to execute it.
  set_register(RA, kBadLR);
}

Simulator::~Simulator() {
  delete[] stack_;
  Isolate* isolate = Isolate::Current();
  if (isolate != nullptr) {
    isolate->set_simulator(nullptr);
  }
}

void Simulator::PrepareCall(PreservedRegisters* preserved) {
#if defined(DEBUG)
  for (intptr_t i = 0; i < kNumberOfCpuRegisters; i++) {
    preserved->xregs[i] = xregs_[i];
    if ((kAbiVolatileCpuRegs & (1 << i)) != 0) {
      xregs_[i] = random_.NextUInt64();
    }
  }
  for (intptr_t i = 0; i < kNumberOfFpuRegisters; i++) {
    preserved->fregs[i] = fregs_[i];
    if ((kAbiVolatileFpuRegs & (1 << i)) != 0) {
      fregs_[i] = bit_cast<double>(kNaNBox);
    }
  }
  preserved->fcsr = fcsr_;
  preserved->pc = pc_;
#endif
}

void Simulator::SavePreservedRegisters(PreservedRegisters* preserved) {
#if defined(DEBUG)
  for (intptr_t i = 0; i < kNumberOfCpuRegisters; i++) {
    preserved->xregs[i] = xregs_[i];
  }
  for (intptr_t i = 0; i < kNumberOfFpuRegisters; i++) {
    preserved->fregs[i] = fregs_[i];
  }
  preserved->fcsr = fcsr_;
  preserved->pc = pc_;
#endif
}

void Simulator::CheckPreservedRegisters(PreservedRegisters* preserved) {
#if defined(DEBUG)
  if (preserved->xregs[SP] != xregs_[SP]) {
    FATAL("Stack unbalanced");
  }
  // Registers that must be preserved across calls on LoongArch:
  //   Callee-saved: S0-S8, FP, TP, SP
  const RegList kPreservedAtCall =
      kAbiPreservedCpuRegs | (static_cast<RegList>(1) << (TP)) | (static_cast<RegList>(1) << (SP));
  for (intptr_t i = 0; i < kNumberOfCpuRegisters; i++) {
    if ((kPreservedAtCall & (1 << i)) != 0) {
      if (preserved->xregs[i] != xregs_[i]) {
        FATAL("Register %s was not preserved across call", cpu_reg_names[i]);
      }
    }
  }
  for (intptr_t i = 0; i < kNumberOfFpuRegisters; i++) {
    if ((kAbiPreservedFpuRegs & (1 << i)) != 0) {
      if (bit_cast<uint64_t>(preserved->fregs[i]) !=
          bit_cast<uint64_t>(fregs_[i])) {
        FATAL("FPU register fs%d was not preserved across call", i);
      }
    }
  }
  if (preserved->fcsr != fcsr_) {
    FATAL("FCSR was not preserved across call");
  }
#endif
}

void Simulator::RunCall(intptr_t entry, PreservedRegisters* preserved) {
  // Set up the return address so simulated code returns to kEndSimulatingPC.
  pc_ = entry;
  set_xreg(RA, kEndSimulatingPC);
  Execute();
  CheckPreservedRegisters(preserved);
  // We can't instrument the runtime.
  memory_.FlushAll();
}

void Simulator::Execute() {
  if (FLAG_trace_simulator) {
    ExecuteTrace();
  } else {
    ExecuteNoTrace();
  }
}

void Simulator::ExecuteNoTrace() {
  while (pc_ != kEndSimulatingPC) {
    uint32_t instr = LoadUnaligned(reinterpret_cast<uint32_t*>(pc_));
    if (instr == Instr::kSimulatorRedirectInstruction) {
      // Redirect to host function.
      uword target = Redirection::FunctionForRedirect(pc_);
      if (target != 0) {
        pc_ = kEndSimulatingPC;
        break;
      }
    }
    uint32_t op6 = LA_Opcode(instr);
    uint32_t rd = LA_Rd(instr);
    uint32_t rj = LA_Rj(instr);
    uint32_t rk = LA_Rk(instr);
    int64_t val_rd = get_xreg(static_cast<Register>(rd));
    int64_t val_rj = get_xreg(static_cast<Register>(rj));
    int64_t val_rk = get_xreg(static_cast<Register>(rk));

    if ((instr & 0xffc00000) == 0x03000000) {
      // lu52i.d
      const uint64_t upper = static_cast<uint64_t>(LA_Ui12(instr)) << 52;
      const uint64_t lower = static_cast<uint64_t>(val_rj) & ((1ULL << 52) - 1);
      set_xreg(static_cast<Register>(rd), static_cast<int64_t>(upper | lower));
    } else if (op6 == 0x00) {
      uint32_t func4 = (instr >> 22) & 0xf;
      if (func4 >= 8) {
        // ALU immediate: func at bits[25:22], si12 at bits[21:10]
        int32_t imm = LA_Si12(instr);
        uint32_t ui12 = LA_Ui12(instr);
        switch (func4) {
          case 0x8: set_xreg(static_cast<Register>(rd), val_rj < imm ? 1 : 0); break;  // slti
          case 0x9: set_xreg(static_cast<Register>(rd), static_cast<uint64_t>(val_rj) < static_cast<uint64_t>(imm) ? 1 : 0); break;  // sltui
          case 0xa: set_xreg(static_cast<Register>(rd), static_cast<int32_t>(static_cast<int32_t>(val_rj) + imm)); break;  // addi.w
          case 0xb: set_xreg(static_cast<Register>(rd), val_rj + imm); break;  // addi.d
          case 0xd: set_xreg(static_cast<Register>(rd), val_rj & static_cast<int64_t>(ui12)); break;  // andi
          case 0xe: set_xreg(static_cast<Register>(rd), val_rj | static_cast<int64_t>(ui12)); break;  // ori
          case 0xf: set_xreg(static_cast<Register>(rd), val_rj ^ static_cast<int64_t>(ui12)); break;  // xori
          default: Fault("Unknown ALU immediate");
        }
      } else {
        // ALU 3-register or shift-immediate: operation encoded in bits[31:15]
        uint32_t op17 = (instr & 0xFFFF8000) >> 15;
        if (op17 >= 0x80) {
          // Shift-immediate instructions: ui6 at bits[15:10]
          uint32_t ui6 = (instr >> 10) & 0x3F;
          switch (op17) {
            case 0x80:  // slli.w
              set_xreg(static_cast<Register>(rd), static_cast<int32_t>(static_cast<uint32_t>(val_rj) << ui6));
              break;
            case 0x82:
            case 0x83:  // slli.d
              set_xreg(static_cast<Register>(rd), val_rj << ui6);
              break;
            case 0x88:  // srli.w
              set_xreg(static_cast<Register>(rd), static_cast<int32_t>(static_cast<uint32_t>(val_rj) >> ui6));
              break;
            case 0x8a:
            case 0x8b:  // srli.d
              set_xreg(static_cast<Register>(rd), static_cast<uint64_t>(val_rj) >> ui6);
              break;
            case 0x90:  // srai.w
              set_xreg(static_cast<Register>(rd), static_cast<int32_t>(static_cast<int32_t>(val_rj) >> ui6));
              break;
            case 0x92:
            case 0x93:  // srai.d
              set_xreg(static_cast<Register>(rd), val_rj >> ui6);
              break;
            case 0x98:  // rotri.w
              set_xreg(static_cast<Register>(rd), static_cast<int32_t>((static_cast<uint32_t>(val_rj) >> ui6) | (static_cast<uint32_t>(val_rj) << (32 - ui6))));
              break;
            case 0x9a:
            case 0x9b:  // rotri.d
              set_xreg(static_cast<Register>(rd), static_cast<int64_t>((static_cast<uint64_t>(val_rj) >> ui6) | (static_cast<uint64_t>(val_rj) << (64 - ui6))));
              break;
            default:
              Fault("Unknown shift-immediate instruction");
          }
        } else {
        switch (op17) {
          case 32:  // add.w: 0x00100000
            set_xreg(static_cast<Register>(rd), static_cast<int32_t>(static_cast<int32_t>(val_rj) + static_cast<int32_t>(val_rk)));
            break;
          case 33:  // add.d: 0x00108000
            set_xreg(static_cast<Register>(rd), val_rj + val_rk);
            break;
          case 34:  // sub.w: 0x00110000
            set_xreg(static_cast<Register>(rd), static_cast<int32_t>(static_cast<int32_t>(val_rj) - static_cast<int32_t>(val_rk)));
            break;
          case 35:  // sub.d: 0x00118000
            set_xreg(static_cast<Register>(rd), val_rj - val_rk);
            break;
          case 36:  // slt: 0x00120000
            set_xreg(static_cast<Register>(rd), val_rj < val_rk ? 1 : 0);
            break;
          case 37:  // sltu: 0x00128000
            set_xreg(static_cast<Register>(rd), static_cast<uint64_t>(val_rj) < static_cast<uint64_t>(val_rk) ? 1 : 0);
            break;
          case 41:  // and: 0x00148000
            set_xreg(static_cast<Register>(rd), val_rj & val_rk);
            break;
          case 42:  // or: 0x00150000
            set_xreg(static_cast<Register>(rd), val_rj | val_rk);
            break;
          case 43:  // xor: 0x00158000
            set_xreg(static_cast<Register>(rd), val_rj ^ val_rk);
            break;
          case 46:  // sll.w: 0x00170000
            set_xreg(static_cast<Register>(rd), static_cast<int32_t>(static_cast<uint32_t>(val_rj) << (val_rk & 0x1f)));
            break;
          case 47:  // srl.w: 0x00178000
            set_xreg(static_cast<Register>(rd), static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint32_t>(val_rj)) >> (val_rk & 0x1f)));
            break;
          case 48:  // sra.w: 0x00180000
            set_xreg(static_cast<Register>(rd), static_cast<int32_t>(static_cast<int32_t>(val_rj) >> (val_rk & 0x1f)));
            break;
          case 49:  // sll.d: 0x00188000
            set_xreg(static_cast<Register>(rd), val_rj << (val_rk & 0x3f));
            break;
          case 50:  // srl.d: 0x00190000
            set_xreg(static_cast<Register>(rd), static_cast<uint64_t>(val_rj) >> (val_rk & 0x3f));
            break;
          case 51:  // sra.d: 0x00198000
            set_xreg(static_cast<Register>(rd), val_rj >> (val_rk & 0x3f));
            break;
          case 56:  // mul.w: 0x001C0000
            set_xreg(static_cast<Register>(rd), static_cast<int32_t>(static_cast<int32_t>(val_rj) * static_cast<int32_t>(val_rk)));
            break;
          case 57:  // mulh.w: 0x001C8000
            set_xreg(static_cast<Register>(rd), static_cast<int32_t>((static_cast<int64_t>(static_cast<int32_t>(val_rj)) * static_cast<int64_t>(static_cast<int32_t>(val_rk))) >> 32));
            break;
          case 58:  // mulh.wu: 0x001D0000
            set_xreg(static_cast<Register>(rd), static_cast<int32_t>((static_cast<uint64_t>(static_cast<uint32_t>(val_rj)) * static_cast<uint64_t>(static_cast<uint32_t>(val_rk))) >> 32));
            break;
          case 59:  // mul.d: 0x001D8000
            set_xreg(static_cast<Register>(rd), val_rj * val_rk);
            break;
          case 60:  // mulh.d: 0x001E0000
            set_xreg(static_cast<Register>(rd), static_cast<int64_t>((static_cast<__int128>(val_rj) * static_cast<__int128>(val_rk)) >> 64));
            break;
          case 61:  // mulh.du: 0x001E8000
            set_xreg(static_cast<Register>(rd), static_cast<int64_t>((static_cast<unsigned __int128>(static_cast<uint64_t>(val_rj)) * static_cast<unsigned __int128>(static_cast<uint64_t>(val_rk))) >> 64));
            break;
          case 64:  // div.w: 0x00200000
            set_xreg(static_cast<Register>(rd), val_rk == 0 ? 0 : static_cast<int32_t>(static_cast<int32_t>(val_rj) / static_cast<int32_t>(val_rk)));
            break;
          case 65:  // mod.w: 0x00208000
            set_xreg(static_cast<Register>(rd), val_rk == 0 ? 0 : static_cast<int32_t>(static_cast<int32_t>(val_rj) % static_cast<int32_t>(val_rk)));
            break;
          case 66:  // div.wu: 0x00210000
            set_xreg(static_cast<Register>(rd), val_rk == 0 ? 0 : static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint32_t>(val_rj)) / static_cast<uint32_t>(static_cast<uint32_t>(val_rk))));
            break;
          case 67:  // mod.wu: 0x00218000
            set_xreg(static_cast<Register>(rd), val_rk == 0 ? 0 : static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint32_t>(val_rj)) % static_cast<uint32_t>(static_cast<uint32_t>(val_rk))));
            break;
          case 68:  // div.d: 0x00220000
            set_xreg(static_cast<Register>(rd), val_rk == 0 ? 0 : val_rj / val_rk);
            break;
          case 69:  // mod.d: 0x00228000
            set_xreg(static_cast<Register>(rd), val_rk == 0 ? 0 : val_rj % val_rk);
            break;
          case 70:  // div.du: 0x00230000
            set_xreg(static_cast<Register>(rd), val_rk == 0 ? 0 : static_cast<int64_t>(static_cast<uint64_t>(val_rj) / static_cast<uint64_t>(val_rk)));
            break;
          case 71:  // mod.du: 0x00238000
            set_xreg(static_cast<Register>(rd), val_rk == 0 ? 0 : static_cast<int64_t>(static_cast<uint64_t>(val_rj) % static_cast<uint64_t>(val_rk)));
            break;
          default:
            FATAL("Unknown ALU 3R instruction: 0x%08x (op17=0x%x)", instr,
                  op17);
        }
        }
      }
    } else if ((instr & 0xff000000) == 0x28000000) {
      // Integer loads: ld.b/h/w/d (opcode 0x0a, sub=00)
      int32_t offset = LA_Si12(instr);
      uint64_t addr = static_cast<uint64_t>(val_rj) + offset;
      uint32_t size = (instr >> 22) & 0x3;
      switch (size) {
        case 0x0: set_xreg(static_cast<Register>(rd), static_cast<int64_t>(MemoryRead<int8_t>(addr, static_cast<Register>(rj)))); break;
        case 0x1: set_xreg(static_cast<Register>(rd), static_cast<int64_t>(MemoryRead<int16_t>(addr, static_cast<Register>(rj)))); break;
        case 0x2: set_xreg(static_cast<Register>(rd), static_cast<int64_t>(MemoryRead<int32_t>(addr, static_cast<Register>(rj)))); break;
        case 0x3: set_xreg(static_cast<Register>(rd), MemoryRead<int64_t>(addr, static_cast<Register>(rj))); break;
        default: Fault("Unknown signed load type");
      }
    } else if ((instr & 0xff000000) == 0x2A000000) {
      // Integer unsigned loads: ld.bu/hu/wu (opcode 0x0a, sub=01)
      int32_t offset = LA_Si12(instr);
      uint64_t addr = static_cast<uint64_t>(val_rj) + offset;
      uint32_t size = (instr >> 22) & 0x3;
      switch (size) {
        case 0x0: set_xreg(static_cast<Register>(rd), MemoryRead<uint8_t>(addr, static_cast<Register>(rj))); break;
        case 0x1: set_xreg(static_cast<Register>(rd), MemoryRead<uint16_t>(addr, static_cast<Register>(rj))); break;
        case 0x2: set_xreg(static_cast<Register>(rd), MemoryRead<uint32_t>(addr, static_cast<Register>(rj))); break;
        default: Fault("Unknown unsigned load type");
      }
    } else if ((instr & 0xff000000) == 0x29000000) {
      // Integer stores: st.b/h/w/d (opcode 0x0a, sub=10)
      int32_t offset = LA_Si12(instr);
      uint64_t addr = static_cast<uint64_t>(val_rj) + offset;
      uint32_t size = (instr >> 22) & 0x3;
      int64_t val_rd = get_xreg(static_cast<Register>(rd));
      switch (size) {
        case 0x0: MemoryWrite<uint8_t>(addr, static_cast<uint8_t>(val_rd), static_cast<Register>(rj)); break;
        case 0x1: MemoryWrite<uint16_t>(addr, static_cast<uint16_t>(val_rd), static_cast<Register>(rj)); break;
        case 0x2: MemoryWrite<uint32_t>(addr, static_cast<uint32_t>(val_rd), static_cast<Register>(rj)); break;
        case 0x3: MemoryWrite<uint64_t>(addr, static_cast<uint64_t>(val_rd), static_cast<Register>(rj)); break;
        default: Fault("Unknown store type");
      }
    } else if (op6 == 0x10) {
      // beqz: branch if == 0
      int32_t offset = LA_BranchOff21(instr) * 4;
      if (val_rj == 0) { pc_ += offset; continue; }
    } else if (op6 == 0x11) {
      // bnez: branch if != 0
      int32_t offset = LA_BranchOff21(instr) * 4;
      if (val_rj != 0) { pc_ += offset; continue; }
    } else if (op6 == 0x14) {
      // b: unconditional branch
      int32_t offset = LA_BranchOff26(instr) * 4;
      pc_ += offset;
      continue;
    } else if (op6 == 0x15) {
      // bl: branch and link
      int32_t offset = LA_BranchOff26(instr) * 4;
      set_xreg(RA, pc_ + 4);
      pc_ += offset;
      continue;
    } else if (op6 >= 0x16 && op6 <= 0x1b) {
      // beq/bne/blt/bge/bltu/bgeu
      // LA_Si16 is in word units (BranchBytesToWords encoded), convert to bytes
      int32_t offset = LA_Si16(instr) * 4;
      bool taken = false;
      switch (op6) {
        case 0x16:
          taken = (val_rj == val_rd);
          break;
        case 0x17:
          taken = (val_rj != val_rd);
          break;
        case 0x18:
          taken = (val_rj < val_rd);
          break;
        case 0x19:
          taken = (val_rj >= val_rd);
          break;
        case 0x1a:
          taken =
              (static_cast<uint64_t>(val_rj) < static_cast<uint64_t>(val_rd));
          break;
        case 0x1b:
          taken =
              (static_cast<uint64_t>(val_rj) >= static_cast<uint64_t>(val_rd));
          break;
      }
      if (taken) { pc_ += offset; continue; }
    } else if (op6 == 0x1c) {
      // bceqz: branch if FP condition == 0
      int32_t offset = LA_BranchOff21(instr) * 4;
      uint32_t fcmp = fcrcmp_result_;
      if (fcmp == 0) { pc_ += offset; continue; }
    } else if (op6 == 0x1d) {
      // bcnez: branch if FP condition != 0
      int32_t offset = LA_BranchOff21(instr) * 4;
      uint32_t fcmp = fcrcmp_result_;
      if (fcmp != 0) { pc_ += offset; continue; }
    } else if (op6 == 0x13) {
      // jirl (op6=0x13): jump and link register
      int32_t offset = LA_Si16(instr) * 4;
      if (rd != 0) set_xreg(static_cast<Register>(rd), pc_ + 4);
      uint64_t target = static_cast<uint64_t>(val_rj + offset);
      if (target == 0 || target == kEndSimulatingPC) {
        pc_ = kEndSimulatingPC;
        continue;
      }
      pc_ = static_cast<uword>(target);
      continue;
    } else if (op6 == 0x06) {
      // pcaddu12i, pcalau12i, pcaddu18i
      int32_t si20 = LA_Si20(instr);
      uint32_t sub = (instr >> 24) & 0x3;
      if (sub == 0) set_xreg(static_cast<Register>(rd), (pc_ & ~0xfffULL) + (static_cast<int64_t>(si20) << 12));
      else if (sub == 1) set_xreg(static_cast<Register>(rd), (pc_ & ~0xfffULL) + (static_cast<int64_t>(si20) << 12));
      else Fault("pcaddu18i not implemented");
    } else if (op6 == 0x05) {
      // lu12i.w / lu32i.d
      int32_t si20 = LA_Si20(instr);
      uint32_t sub = (instr >> 24) & 0x3;
      if (sub == 0) {
        set_xreg(static_cast<Register>(rd), static_cast<int64_t>(si20) << 12);
      } else if (sub == 2) {
        uint64_t cur = get_xreg(static_cast<Register>(rd));
        cur = (cur & 0xffffffff) | (static_cast<uint64_t>(si20) << 32);
        set_xreg(static_cast<Register>(rd), static_cast<int64_t>(cur));
      } else {
        Fault("Unknown upper immediate instruction");
      }
    } else if (op6 == 0x04) {
      // slli/srli/srai immediate (shift by immediate)
      uint32_t func = (instr >> 22) & 0xf;
      if (func < 4) {
        // slli.w, srli.w, srai.w (5-bit shift)
        uint32_t shamt = LA_Ui5(instr);
        switch (func) {
          case 0: set_xreg(static_cast<Register>(rd), static_cast<int32_t>(static_cast<uint32_t>(val_rj) << shamt)); break;
          case 1: set_xreg(static_cast<Register>(rd), static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint32_t>(val_rj)) >> shamt)); break;
          case 2: set_xreg(static_cast<Register>(rd), static_cast<int32_t>(static_cast<int32_t>(static_cast<int32_t>(val_rj)) >> shamt)); break;
          default: Fault("Unknown shift immediate");
        }
      } else {
        // slli.d, srli.d, srai.d (6-bit shift)
        uint32_t shamt = LA_Ui6(instr);
        switch (func) {
          case 4: set_xreg(static_cast<Register>(rd), val_rj << shamt); break;
          case 5: set_xreg(static_cast<Register>(rd), static_cast<int64_t>(static_cast<uint64_t>(val_rj) >> shamt)); break;
          case 6: set_xreg(static_cast<Register>(rd), val_rj >> shamt); break;
          default: Fault("Unknown shift immediate");
        }
      }
    } else if ((instr & 0xff000000) == 0x2b000000) {
      // FP loads/stores
      int32_t offset = LA_Si12(instr);
      uint64_t addr = static_cast<uint64_t>(val_rj) + offset;
      switch ((instr >> 22) & 0x3) {
        case 0x0:  // fld.s
          set_fregs(static_cast<FRegister>(rd),
                    MemoryRead<float>(addr, static_cast<Register>(rj)));
          break;
        case 0x1:  // fst.s
          MemoryWrite<float>(addr, get_fregs(static_cast<FRegister>(rd)),
                             static_cast<Register>(rj));
          break;
        case 0x2:  // fld.d
          set_fregd(static_cast<FRegister>(rd),
                    MemoryRead<double>(addr, static_cast<Register>(rj)));
          break;
        case 0x3:  // fst.d
          MemoryWrite<double>(addr, get_fregd(static_cast<FRegister>(rd)),
                              static_cast<Register>(rj));
          break;
      }
    } else if ((instr & 0xff000000) == 0x01000000) {
      // FP arithmetic and compare.
      uint32_t op10_fp = LA_Op10(instr);
      // fadd/fsub/fmul/fdiv single: op10=0x010, double: op10=0x014, etc.
      if ((op10_fp & 0x3fc) == 0x010) {
        // fadd.s=0x010, fsub.s=0x008, fmul.s=0x018, fdiv.s=0x020
        float s1 = get_fregs(static_cast<FRegister>(rj));
        float s2 = get_fregs(static_cast<FRegister>(rk));
        float result = 0.0f;
        if (op10_fp == 0x010)
          result = s1 + s2;
        else if (op10_fp == 0x008)
          result = s1 - s2;
        else if (op10_fp == 0x018)
          result = s1 * s2;
        else if (op10_fp == 0x020)
          result = s1 / s2;
        else
          Fault("Unknown FP single op");
        set_fregs(static_cast<FRegister>(rd), result);
      } else if ((op10_fp & 0x3fc) == 0x014) {
        // fadd.d=0x014, fsub.d=0x00c, fmul.d=0x01c, fdiv.d=0x024
        double d1 = get_fregd(static_cast<FRegister>(rj));
        double d2 = get_fregd(static_cast<FRegister>(rk));
        double result = 0.0;
        if (op10_fp == 0x014)
          result = d1 + d2;
        else if (op10_fp == 0x00c)
          result = d1 - d2;
        else if (op10_fp == 0x01c)
          result = d1 * d2;
        else if (op10_fp == 0x024)
          result = d1 / d2;
        else
          Fault("Unknown FP double op");
        set_fregd(static_cast<FRegister>(rd), result);
      } else if ((op10_fp & 0x3f0) == 0x040) {
        // fcmp: 0x040 (single) or 0x044 (double)
        bool is_double = (op10_fp & 0x004) != 0;
        uint32_t cond = (instr >> 5) & 0x1f;
        fcrcmp_result_ = 0;
        if (is_double) {
          double d1 = get_fregd(static_cast<FRegister>(rj));
          double d2 = get_fregd(static_cast<FRegister>(rk));
          switch (cond) {
            case 0x0:
              fcrcmp_result_ =
                  (d1 < d2) ? 1 : (d1 > d2 ? 2 : (d1 == d2 ? 0x40 : 0x10));
              break;  // CAF/SAF/CMP
            case 0x1:
              fcrcmp_result_ = (d1 < d2) ? 1 : (d1 > d2 ? 2 : 0x10);
              break;  // CUN/SAF
            case 0x2:
              fcrcmp_result_ = (d1 < d2) ? 1 : (d1 > d2 ? 2 : 0);
              break;  // CUEQ
            case 0x8:
              fcrcmp_result_ = (d1 <= d2) ? (d1 < d2 ? 1 : 0x40) : 2;
              break;  // CLE
            case 0xc:
              fcrcmp_result_ = (d1 == d2) ? 0x40 : ((d1 < d2) ? 1 : 2);
              break;  // CEQ
            case 0xe:
              fcrcmp_result_ =
                  (d1 == d2 || d1 < d2) ? ((d1 == d2) ? 0x40 : 1) : 2;
              break;  // CLE (UN)
            default:
              break;
          }
        } else {
          float f1 = get_fregs(static_cast<FRegister>(rj));
          float f2 = get_fregs(static_cast<FRegister>(rk));
          switch (cond) {
            case 0x0:
              fcrcmp_result_ =
                  (f1 < f2) ? 1 : (f1 > f2 ? 2 : (f1 == f2 ? 0x40 : 0x10));
              break;
            case 0x1:
              fcrcmp_result_ = (f1 < f2) ? 1 : (f1 > f2 ? 2 : 0x10);
              break;
            case 0x2:
              fcrcmp_result_ = (f1 < f2) ? 1 : (f1 > f2 ? 2 : 0);
              break;
            case 0x8:
              fcrcmp_result_ = (f1 <= f2) ? (f1 < f2 ? 1 : 0x40) : 2;
              break;
            case 0xc:
              fcrcmp_result_ = (f1 == f2) ? 0x40 : ((f1 < f2) ? 1 : 2);
              break;
            case 0xe:
              fcrcmp_result_ =
                  (f1 == f2 || f1 < f2) ? ((f1 == f2) ? 0x40 : 1) : 2;
              break;
            default:
              break;
          }
        }
      } else if ((op10_fp & 0x3f8) == 0x088) {
        // fcvts: s2d=0x088, d2s=0x08c
        if (op10_fp == 0x088) {
          set_fregd(static_cast<FRegister>(rd),
                    static_cast<double>(get_fregs(static_cast<FRegister>(rj))));
        } else {
          set_fregs(static_cast<FRegister>(rd),
                    static_cast<float>(get_fregd(static_cast<FRegister>(rj))));
        }
      } else if ((op10_fp & 0x3f8) == 0x090) {
        // ftintrz: w.s=0x090, w.d=0x094, l.s=0x098, l.d=0x09c
        if (op10_fp == 0x090)
          set_fregs(static_cast<FRegister>(rd),
                    static_cast<float>(static_cast<int32_t>(
                        get_fregs(static_cast<FRegister>(rj)))));
        else if (op10_fp == 0x094)
          set_fregs(static_cast<FRegister>(rd),
                    static_cast<float>(static_cast<int32_t>(
                        get_fregd(static_cast<FRegister>(rj)))));
        else if (op10_fp == 0x098)
          set_fregd(static_cast<FRegister>(rd),
                    static_cast<double>(static_cast<int64_t>(
                        get_fregs(static_cast<FRegister>(rj)))));
        else
          set_fregd(static_cast<FRegister>(rd),
                    static_cast<double>(static_cast<int64_t>(
                        get_fregd(static_cast<FRegister>(rj)))));
      } else if ((op10_fp & 0x3f8) == 0x080) {
        // ffint: w.s=0x084, l.s=0x086, w.d=0x085, l.d=0x087
        if (op10_fp == 0x084)
          set_fregs(
              static_cast<FRegister>(rd),
              static_cast<float>(static_cast<int32_t>(static_cast<int32_t>(
                  get_fregs(static_cast<FRegister>(rj))))));
        else if (op10_fp == 0x086)
          set_fregd(
              static_cast<FRegister>(rd),
              static_cast<double>(static_cast<int64_t>(static_cast<int32_t>(
                  get_fregs(static_cast<FRegister>(rj))))));
        else if (op10_fp == 0x085)
          set_fregs(
              static_cast<FRegister>(rd),
              static_cast<float>(static_cast<int32_t>(static_cast<int64_t>(
                  get_fregd(static_cast<FRegister>(rj))))));
        else
          set_fregd(
              static_cast<FRegister>(rd),
              static_cast<double>(static_cast<int64_t>(static_cast<int64_t>(
                  get_fregd(static_cast<FRegister>(rj))))));
      } else {
        Fault("Unknown FP instruction");
      }
    } else if ((instr & 0xffff8000) == 0x386b8000) {
      // amand_db.d
      const uint64_t addr = static_cast<uint64_t>(val_rj);
      const int64_t old = MemoryRead<int64_t>(addr, static_cast<Register>(rj));
      MemoryWrite<int64_t>(addr, old & val_rk, static_cast<Register>(rj));
      set_xreg(static_cast<Register>(rd), old);
    } else if ((instr & 0xffff8000) == 0x386c8000) {
      // amor_db.d
      const uint64_t addr = static_cast<uint64_t>(val_rj);
      const int64_t old = MemoryRead<int64_t>(addr, static_cast<Register>(rj));
      MemoryWrite<int64_t>(addr, old | val_rk, static_cast<Register>(rj));
      set_xreg(static_cast<Register>(rd), old);
    } else if (instr == Instr::kBreakPointInstruction) {
      // break instruction -> stop simulator
      pc_ = kEndSimulatingPC;
      continue;
    } else if (op6 == 0x08) {
      // ll.w / ll.d / sc.w / sc.d: atomic load-linked/store-conditional
      // Assembler encoding: 0x20000000 (ll.w), 0x21000000 (sc.w), 0x22000000 (ll.d), 0x23000000 (sc.d)
      int32_t si14 = LA_Si14(instr);
      uint64_t addr = static_cast<uint64_t>(val_rj) + si14;
      uint32_t sub = (instr >> 22) & 0x3;
      if (sub == 0x0) {
        // ll.w
        set_xreg(static_cast<Register>(rd), static_cast<int64_t>(MemoryRead<int32_t>(addr, static_cast<Register>(rj))));
      } else if (sub == 0x1) {
        // sc.w
        int64_t val_rd = get_xreg(static_cast<Register>(rd));
        MemoryWrite<int32_t>(addr, static_cast<int32_t>(val_rd), static_cast<Register>(rj));
        set_xreg(static_cast<Register>(rd), 0);
      } else if (sub == 0x2) {
        // ll.d
        set_xreg(static_cast<Register>(rd), MemoryRead<int64_t>(addr, static_cast<Register>(rj)));
      } else {
        // sc.d
        int64_t val_rd = get_xreg(static_cast<Register>(rd));
        MemoryWrite<int64_t>(addr, val_rd, static_cast<Register>(rj));
        set_xreg(static_cast<Register>(rd), 0);
      }
    } else if (op6 == 0x30) {
      // AMO instructions: amswap/add/xor/and/or/min/max/maxu
      // Format: bits[31:26]=0x30, bits[25:22]=func, bits[21:10]=si12 offset, bits[9:5]=rj, bits[4:0]=rd
      // rd serves as both source (value to operate) and destination (old value returned)
      int32_t offset = LA_Si12(instr);
      uint32_t amo_func = (instr >> 22) & 0xf;
      uint64_t addr = static_cast<uint64_t>(val_rj) + offset;
      int64_t val_rd = get_xreg(static_cast<Register>(rd));
      // Simulator is single-threaded, implement as non-atomic RMW
      switch (amo_func) {
        case 0x0: {  // amswap.w
          int32_t old = MemoryRead<int32_t>(addr, static_cast<Register>(rj));
          MemoryWrite<int32_t>(addr, static_cast<int32_t>(val_rd), static_cast<Register>(rj));
          set_xreg(static_cast<Register>(rd), static_cast<int64_t>(old));
          break;
        }
        case 0x1: {  // amadd.w
          int32_t old = MemoryRead<int32_t>(addr, static_cast<Register>(rj));
          MemoryWrite<int32_t>(addr, old + static_cast<int32_t>(val_rd), static_cast<Register>(rj));
          set_xreg(static_cast<Register>(rd), static_cast<int64_t>(old));
          break;
        }
        default:
          Fault("Unimplemented AMO instruction");
      }
    } else if ((instr & 0xff000000) == 0x10000000) {
      // addu16i.d
      int32_t si16 = LA_Si16(instr);
      set_xreg(static_cast<Register>(rd), val_rj + (static_cast<int64_t>(si16) << 16));
    } else {
      FATAL("Unimplemented LoongArch instruction: 0x%08x", instr);
    }

    pc_ += 4;
    instret_++;
  }
}


void Simulator::Fault(const char* message) {
  FATAL("%s", message);  // intentional
}
void Simulator::ExecuteTrace() {
  Disassembler::Disassemble(pc_, pc_ + Instr::kInstrSize);
  ExecuteNoTrace();
}

intx_t Simulator::CSRRead(uint16_t csr) {
  switch (csr) {
    case 0x001:  // fflags
      return fcsr_ & 0x1f;
    case 0x002:  // frm
      return (fcsr_ >> 5) & 0x7;
    case 0x003:  // fcsr
      return fcsr_;
    case 0xC00:  // cycle
    case 0xC02:  // instret
      return instret_;
    default:
      return 0;
  }
}

void Simulator::CSRWrite(uint16_t csr, intx_t value) {
  switch (csr) {
    case 0x001:  // fflags
      fcsr_ = (fcsr_ & ~0x1f) | (value & 0x1f);
      break;
    case 0x002:  // frm
      fcsr_ = (fcsr_ & ~(0x7 << 5)) | ((value & 0x7) << 5);
      break;
    case 0x003:  // fcsr
      fcsr_ = value & 0xff;
      break;
  }
}

uint64_t Simulator::ReadMem(uintx_t address, int size) {
  return memory_.Load<uint64_t>(address);
}

void Simulator::WriteMem(uintx_t address, uint64_t value, int size) {
  memory_.Store<uint64_t>(address, value);
}

template <typename type>
type Simulator::MemoryRead(uintx_t address, Register base) {
  // Fault on null pointer dereferences
  if (IsIllegalAddress(address)) {
    FATAL("Null pointer read at 0x%" Px64 ", pc=0x%" Px, address, pc_);
  }
  return memory_.Load<type>(address);
}

template <typename type>
void Simulator::MemoryWrite(uintx_t address, type value, Register base) {
  if (IsIllegalAddress(address)) {
    FATAL("Null pointer write at 0x%" Px64 ", pc=0x%" Px, address, pc_);
  }
  memory_.Store<type>(address, value);
}


Simulator* Simulator::Current() {
  Isolate* isolate = Isolate::Current();
  Simulator* simulator = isolate->simulator();
  if (simulator == nullptr) {
    NoSafepointScope no_safepoint;
    simulator = new Simulator();
    isolate->set_simulator(simulator);
  }
  return simulator;
}

int64_t Simulator::Call(intptr_t entry,
                        intptr_t parameter0,
                        intptr_t parameter1,
                        intptr_t parameter2,
                        intptr_t parameter3,
                        bool fp_return,
                        bool fp_args) {
  const intptr_t sp_before_call = get_register(SPREG);
  if (fp_args) {
    set_fregd(FA0, static_cast<double>(bit_cast<int64_t, double>(parameter0)));
    set_fregd(FA1, static_cast<double>(bit_cast<int64_t, double>(parameter1)));
    set_fregd(FA2, static_cast<double>(bit_cast<int64_t, double>(parameter2)));
    set_fregd(FA3, static_cast<double>(bit_cast<int64_t, double>(parameter3)));
  } else {
    set_register(A0, parameter0);
    set_register(A1, parameter1);
    set_register(A2, parameter2);
    set_register(A3, parameter3);
  }
  intptr_t stack_pointer = sp_before_call;
  if (OS::ActivationFrameAlignment() > 1) {
    stack_pointer = Utils::RoundDown(stack_pointer, OS::ActivationFrameAlignment());
  }
  set_register(SPREG, stack_pointer);
  set_pc(entry);
  set_register(RA, kEndSimulatingPC);
  Execute();
  set_register(SPREG, sp_before_call);
  int64_t return_value;
  if (fp_return) {
    return_value = bit_cast<double, int64_t>(get_fregd(FA0));
  } else {
    return_value = get_register(A0);
  }
  return return_value;
}

void Simulator::JumpToFrame(uword pc, uword sp, uword fp, Thread* thread) {
  SimulatorSetjmpBuffer* buf = last_setjmp_buffer();
  while (buf->link() != nullptr && buf->link()->sp() <= sp) {
    buf = buf->link();
  }
  ASSERT(buf != nullptr);
  StackResource::Unwind(thread);
  set_pc(static_cast<int64_t>(pc));
  set_register(SPREG, static_cast<int64_t>(sp));
  set_register(FPREG, static_cast<int64_t>(fp));
  set_register(THR, reinterpret_cast<int64_t>(thread));
  set_register(RA, kEndSimulatingPC);
  Execute();
}
}  // namespace dart

#endif  // defined(TARGET_ARCH_LOONG64) && defined(DART_INCLUDE_SIMULATOR)
