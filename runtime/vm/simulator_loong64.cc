// Copyright (c) 2017, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(TARGET_ARCH_LOONG64) && defined(DART_INCLUDE_SIMULATOR)

#include "vm/simulator.h"
#include "vm/simulator_loong64.h"

#include "platform/atomic.h"
#include "vm/compiler/assembler/disassembler.h"
#include "vm/compiler/runtime_api.h"
#include "vm/cpu.h"
#include "vm/dart_entry.h"
#include "vm/isolate.h"
#include "vm/isolate_group.h"
#include "vm/longjump.h"
#include "vm/os_thread.h"
#include "vm/random.h"
#include "vm/stub_code.h"
#include "vm/symbols.h"
#include "vm/thread.h"
#include "vm/thread_state.h"

namespace dart {

DECLARE_FLAG(bool, trace_simulator);
DEFINE_FLAG(int, sim_stack_size, 128, "Simulator stack size (in kB)");
DEFINE_FLAG(int, sim_stack_hard_limit, 0,
            "If set, the simulated stack hard limit in kB");

static constexpr intptr_t kSimulatorStackSize =
    64 * KB + OS::page_size();

struct PreservedRegisters {
  uint64_t xregs[kAbiPreservedCpuRegCount];
  uint64_t fregs[kAbiPreservedFpuRegCount];
};

void Simulator::PrepareCall(PreservedRegisters* preserved) {
  SavePreservedRegisters(preserved);
}

void Simulator::SavePreservedRegisters(PreservedRegisters* preserved) {
  // S0-S8 are preserved (excluding S0=NULL_REG, S1=THR, S4=ARGS_DESC_REG, S5=IC_DATA_REG)
  // FP is preserved
  // FS0-FS7 are preserved
  preserved->xregs[0] = get_xreg(FP);
  preserved->xregs[1] = get_xreg(S0);
  preserved->xregs[2] = get_xreg(S2);
  preserved->xregs[3] = get_xreg(S3);
  preserved->xregs[4] = get_xreg(S6);
  preserved->xregs[5] = get_xreg(S7);
  preserved->xregs[6] = get_xreg(S8);
  preserved->fregs[0] = bit_cast<uint64_t>(get_fregd(FS0));
  preserved->fregs[1] = bit_cast<uint64_t>(get_fregd(FS1));
  preserved->fregs[2] = bit_cast<uint64_t>(get_fregd(FS2));
  preserved->fregs[3] = bit_cast<uint64_t>(get_fregd(FS3));
  preserved->fregs[4] = bit_cast<uint64_t>(get_fregd(FS4));
  preserved->fregs[5] = bit_cast<uint64_t>(get_fregd(FS5));
  preserved->fregs[6] = bit_cast<uint64_t>(get_fregd(FS6));
  preserved->fregs[7] = bit_cast<uint64_t>(get_fregd(FS7));
}

void Simulator::RestorePreservedRegisters(PreservedRegisters* preserved) {
  set_xreg(FP, preserved->xregs[0]);
  set_xreg(S0, preserved->xregs[1]);
  set_xreg(S2, preserved->xregs[2]);
  set_xreg(S3, preserved->xregs[3]);
  set_xreg(S6, preserved->xregs[4]);
  set_xreg(S7, preserved->xregs[5]);
  set_xreg(S8, preserved->xregs[6]);
  set_fregd(FS0, bit_cast<double>(preserved->fregs[0]));
  set_fregd(FS1, bit_cast<double>(preserved->fregs[1]));
  set_fregd(FS2, bit_cast<double>(preserved->fregs[2]));
  set_fregd(FS3, bit_cast<double>(preserved->fregs[3]));
  set_fregd(FS4, bit_cast<double>(preserved->fregs[4]));
  set_fregd(FS5, bit_cast<double>(preserved->fregs[5]));
  set_fregd(FS6, bit_cast<double>(preserved->fregs[6]));
  set_fregd(FS7, bit_cast<double>(preserved->fregs[7]));
}

#if defined(DART_HOST_OS_LINUX) || defined(DART_HOST_OS_ANDROID) || \
    defined(DART_HOST_OS_MACOS) || defined(DART_HOST_OS_IOS) ||     \
    defined(DART_HOST_OS_FUCHSIA)
extern "C" {
intptr_t SimulatorEntryPoint(intptr_t function,
                             intptr_t arg0,
                             intptr_t arg1,
                             intptr_t arg2,
                             intptr_t arg3,
                             intptr_t arg4,
                             intptr_t arg5) {
  Simulator* sim = Simulator::Current();
  sim->set_pc(function);
  sim->set_xreg(A0, arg0);
  sim->set_xreg(A1, arg1);
  sim->set_xreg(A2, arg2);
  sim->set_xreg(A3, arg3);
  sim->set_xreg(A4, arg4);
  sim->set_xreg(A5, arg5);
  sim->Execute();
  return sim->get_xreg(A0);
}
}
#endif

void Simulator::RunCall(intx_t function, PreservedRegisters* preserved) {
  set_pc(function);
  Execute();
  RestorePreservedRegisters(preserved);
}

// Set the threshold for detecting stack overflow.
static constexpr intptr_t kSimStackLimitMargin =
    4 * kWordSize + Simulator::kSimulatorStackUnderflowSize;

Simulator::Simulator()
    : random_(),
      shadow_stack_(nullptr),
      memory_(/*growable=*/true) {
  // Allocate C stack as the simulator stack.
  const intptr_t kStackSize = (FLAG_sim_stack_size * 1024);
  stack_ = reinterpret_cast<char*>(malloc(kStackSize));
  stack_base_ = reinterpret_cast<uword>(stack_) + kStackSize;
  stack_limit_ = reinterpret_cast<uword>(stack_) + kSimStackLimitMargin;
  overflow_stack_limit_ = stack_limit_ + (FLAG_sim_stack_hard_limit * 1024);

  // Initialize all registers to 0.
  for (int i = 0; i < kNumberOfCpuRegisters; i++) {
    xregs_[i] = 0;
  }
  for (int i = 0; i < kNumberOfFpuRegisters; i++) {
    fregs_[i] = 0.0;
  }
  fcsr_ = 0;
  instret_ = 0;
}

Simulator::~Simulator() {
  free(stack_);
  free(shadow_stack_);
}

static thread_local Simulator* current_simulator = nullptr;

Simulator* Simulator::Current() {
  return current_simulator;
}

void Simulator::set_current_simulator(Simulator* simulator) {
  current_simulator = simulator;
}

void Simulator::JumpToFrame(uword pc, uword sp, uword fp, Thread* thread) {
  PreservedRegisters preserved;
  set_xreg(SP, sp);
  set_xreg(FP, fp);
  set_lr(pc);
  set_pc(pc);
  Execute();
}

DART_NORETURN void Simulator::Fault(const char* message) {
  uint64_t sp = get_xreg(SP);
  OS::PrintErr("Simulator fault: %s\n", message);
  OS::PrintErr("pc: 0x%" Px " sp: 0x%" Px " fp: 0x%" Px "\n",
               pc_, sp, get_xreg(FP));
  for (int i = 0; i < kNumberOfCpuRegisters; i++) {
    OS::PrintErr("x%d: 0x%" Px "\n", i, xregs_[i]);
  }
  UNREACHABLE();
}

DART_NORETURN void Simulator::IllegalInstruction(Instr instr) {
  Fault("Illegal instruction");
}

bool Simulator::IsTracingExecution() const {
  return FLAG_trace_simulator;
}

// Main execution loop
void Simulator::Execute() {
  while (pc_ != kEndSimulatingPC) {
    if (IsTracingExecution()) {
      ExecuteTrace();
    } else {
      ExecuteNoTrace();
    }
  }
}

void Simulator::ExecuteNoTrace() {
  // Simplified execution - in a real implementation this would decode and interpret
  // all RISC-V instructions. For now, provide a basic dispatch loop.
  while (pc_ != kEndSimulatingPC) {
    Instr instr(LoadUnaligned(reinterpret_cast<uint32_t*>(pc_)));
    uint32_t opcode = instr.opcode();

    switch (opcode) {
      case OPIMM: {
        Register rd = instr.rd();
        Register rs1 = instr.rs1();
        intx_t imm = instr.itype_imm();
        uint32_t funct3 = instr.funct3();
        switch (funct3) {
          case ADDI:
            set_xreg(rd, get_xreg(rs1) + imm);
            break;
          case SLTI:
            set_xreg(rd, get_xreg(rs1) < imm ? 1 : 0);
            break;
          case SLTIU:
            set_xreg(rd, static_cast<uintx_t>(get_xreg(rs1)) < static_cast<uintx_t>(imm) ? 1 : 0);
            break;
          case XORI:
            set_xreg(rd, get_xreg(rs1) ^ imm);
            break;
          case ORI:
            set_xreg(rd, get_xreg(rs1) | imm);
            break;
          case ANDI:
            set_xreg(rd, get_xreg(rs1) & imm);
            break;
          case SRI:
            if (instr.funct7() == SRA) {
              set_xreg(rd, static_cast<intx_t>(get_xreg(rs1)) >> (imm & 0x3f));
            } else {
              set_xreg(rd, static_cast<uintx_t>(get_xreg(rs1)) >> (imm & 0x3f));
            }
            break;
          case SLLI:
            set_xreg(rd, get_xreg(rs1) << (imm & 0x3f));
            break;
        }
        pc_ += 4;
        break;
      }

      case OP: {
        Register rd = instr.rd();
        Register rs1 = instr.rs1();
        Register rs2 = instr.rs2();
        uint32_t funct3 = instr.funct3();
        uint32_t funct7 = instr.funct7();
        switch (funct3) {
          case ADD:
            if (funct7 == SUB) {
              set_xreg(rd, get_xreg(rs1) - get_xreg(rs2));
            } else {
              set_xreg(rd, get_xreg(rs1) + get_xreg(rs2));
            }
            break;
          case SLL:
            set_xreg(rd, get_xreg(rs1) << (get_xreg(rs2) & 0x3f));
            break;
          case SLT:
            set_xreg(rd, get_xreg(rs1) < get_xreg(rs2) ? 1 : 0);
            break;
          case SLTU:
            set_xreg(rd, static_cast<uintx_t>(get_xreg(rs1)) < static_cast<uintx_t>(get_xreg(rs2)) ? 1 : 0);
            break;
          case XOR:
            set_xreg(rd, get_xreg(rs1) ^ get_xreg(rs2));
            break;
          case SR:
            if (funct7 == SRA) {
              set_xreg(rd, static_cast<intx_t>(get_xreg(rs1)) >> (get_xreg(rs2) & 0x3f));
            } else {
              set_xreg(rd, static_cast<uintx_t>(get_xreg(rs1)) >> (get_xreg(rs2) & 0x3f));
            }
            break;
          case OR:
            set_xreg(rd, get_xreg(rs1) | get_xreg(rs2));
            break;
          case AND:
            set_xreg(rd, get_xreg(rs1) & get_xreg(rs2));
            break;
          case MUL:
            if (funct7 == MULDIV) {
              set_xreg(rd, get_xreg(rs1) * get_xreg(rs2));
            }
            break;
        }
        pc_ += 4;
        break;
      }

      case LUI:
        set_xreg(instr.rd(), static_cast<uintx_t>(static_cast<intx_t>(instr.utype_imm())));
        pc_ += 4;
        break;

      case AUIPC:
        set_xreg(instr.rd(), pc_ + static_cast<intx_t>(instr.utype_imm()));
        pc_ += 4;
        break;

      case JAL: {
        Register rd = instr.rd();
        set_xreg(rd, pc_ + 4);
        pc_ += static_cast<intx_t>(instr.jtype_imm());
        break;
      }

      case JALR: {
        Register rd = instr.rd();
        Register rs1 = instr.rs1();
        intx_t imm = instr.itype_imm();
        set_xreg(rd, pc_ + 4);
        uintx_t target = (get_xreg(rs1) + imm) & ~1;
        // If target == 0 or rd == ZR with rs1 == RA and imm == 0, this is a ret
        if (target == 0) {
          pc_ = kEndSimulatingPC;
        } else {
          pc_ = static_cast<uword>(target);
        }
        break;
      }

      case BRANCH: {
        Register rs1 = instr.rs1();
        Register rs2 = instr.rs2();
        intx_t imm = instr.btype_imm();
        bool taken = false;
        switch (instr.funct3()) {
          case BEQ:  taken = (get_xreg(rs1) == get_xreg(rs2)); break;
          case BNE:  taken = (get_xreg(rs1) != get_xreg(rs2)); break;
          case BLT:  taken = (get_xreg(rs1) < get_xreg(rs2)); break;
          case BGE:  taken = (get_xreg(rs1) >= get_xreg(rs2)); break;
          case BLTU: taken = (static_cast<uintx_t>(get_xreg(rs1)) < static_cast<uintx_t>(get_xreg(rs2))); break;
          case BGEU: taken = (static_cast<uintx_t>(get_xreg(rs1)) >= static_cast<uintx_t>(get_xreg(rs2))); break;
        }
        if (taken) {
          pc_ += imm;
        } else {
          pc_ += 4;
        }
        break;
      }

      case LOAD: {
        Register rd = instr.rd();
        Register rs1 = instr.rs1();
        intx_t imm = instr.itype_imm();
        uintx_t addr = get_xreg(rs1) + imm;
        switch (instr.funct3()) {
          case LB:  set_xreg(rd, MemoryRead<int8_t>(addr, rs1)); break;
          case LH:  set_xreg(rd, MemoryRead<int16_t>(addr, rs1)); break;
          case LW:  set_xreg(rd, MemoryRead<int32_t>(addr, rs1)); break;
          case LBU: set_xreg(rd, MemoryRead<uint8_t>(addr, rs1)); break;
          case LHU: set_xreg(rd, MemoryRead<uint16_t>(addr, rs1)); break;
          case LD:  set_xreg(rd, MemoryRead<int64_t>(addr, rs1)); break;
          case LWU: set_xreg(rd, MemoryRead<uint32_t>(addr, rs1)); break;
        }
        pc_ += 4;
        break;
      }

      case STORE: {
        Register rs2 = instr.rs2();
        Register rs1 = instr.rs1();
        intx_t imm = instr.stype_imm();
        uintx_t addr = get_xreg(rs1) + imm;
        switch (instr.funct3()) {
          case SB: MemoryWrite<uint8_t>(addr, static_cast<uint8_t>(get_xreg(rs2)), rs1); break;
          case SH: MemoryWrite<uint16_t>(addr, static_cast<uint16_t>(get_xreg(rs2)), rs1); break;
          case SW: MemoryWrite<uint32_t>(addr, static_cast<uint32_t>(get_xreg(rs2)), rs1); break;
          case SD: MemoryWrite<uint64_t>(addr, static_cast<uint64_t>(get_xreg(rs2)), rs1); break;
        }
        pc_ += 4;
        break;
      }

      case LOADFP: {
        FRegister rd = instr.frd();
        Register rs1 = instr.rs1();
        intx_t imm = instr.itype_imm();
        uintx_t addr = get_xreg(rs1) + imm;
        switch (instr.funct3()) {
          case S:  // flw
            set_fregs(rd, MemoryRead<float>(addr, rs1));
            break;
          case D:  // fld
            set_fregd(rd, MemoryRead<double>(addr, rs1));
            break;
        }
        pc_ += 4;
        break;
      }

      case STOREFP: {
        FRegister rs2 = instr.frs2();
        Register rs1 = instr.rs1();
        intx_t imm = instr.stype_imm();
        uintx_t addr = get_xreg(rs1) + imm;
        switch (instr.funct3()) {
          case S:
            MemoryWrite<float>(addr, get_fregs(rs2), rs1);
            break;
          case D:
            MemoryWrite<double>(addr, get_fregd(rs2), rs1);
            break;
        }
        pc_ += 4;
        break;
      }

      case SYSTEM: {
        uint32_t funct3 = instr.funct3();
        if (funct3 == PRIV) {
          switch (instr.funct12()) {
            case ECALL:
              Fault("ECALL");
              break;
            case EBREAK:
              // Stop execution
              pc_ = kEndSimulatingPC;
              break;
          }
        } else if (funct3 == CSRRS) {
          // CSR read
          uint16_t csr = static_cast<uint16_t>(instr.itype_imm());
          Register rd = instr.rd();
          set_xreg(rd, CSRRead(csr));
          pc_ += 4;
        } else if (funct3 == CSRRW) {
          // CSR write
          uint16_t csr = static_cast<uint16_t>(instr.itype_imm());
          Register rd = instr.rd();
          Register rs1 = instr.rs1();
          set_xreg(rd, CSRRead(csr));
          CSRWrite(csr, get_xreg(rs1));
          pc_ += 4;
        } else {
          pc_ += 4;
        }
        break;
      }

      case MISCMEM: {
        // FENCE instructions
        pc_ += 4;
        break;
      }

      case OP32: {
        Register rd = instr.rd();
        Register rs1 = instr.rs1();
        Register rs2 = instr.rs2();
        uint32_t funct3 = instr.funct3();
        uint32_t funct7 = instr.funct7();
        int32_t vrs1 = static_cast<int32_t>(get_xreg(rs1) & 0xFFFFFFFF);
        int32_t vrs2 = static_cast<int32_t>(get_xreg(rs2) & 0xFFFFFFFF);
        switch (funct3) {
          case ADD:
            if (funct7 == SUB) {
              set_xreg(rd, static_cast<int32_t>(vrs1 - vrs2));
            } else {
              set_xreg(rd, static_cast<int32_t>(vrs1 + vrs2));
            }
            break;
          case MULW:
            if (funct7 == MULDIV) {
              set_xreg(rd, static_cast<int32_t>(vrs1 * vrs2));
            }
            break;
          case DIVW:
            if (funct7 == MULDIV) {
              if (vrs2 == 0) {
                set_xreg(rd, -1);
              } else {
                set_xreg(rd, vrs1 / vrs2);
              }
            }
            break;
          case DIVUW: {
            uint32_t u1 = static_cast<uint32_t>(get_xreg(rs1));
            uint32_t u2 = static_cast<uint32_t>(get_xreg(rs2));
            if (u2 == 0) {
              set_xreg(rd, static_cast<int64_t>(0xFFFFFFFFFFFFFFFF));
            } else {
              set_xreg(rd, static_cast<uint32_t>(u1 / u2));
            }
            break;
          }
          case REMW:
            if (funct7 == MULDIV) {
              if (vrs2 == 0) {
                set_xreg(rd, vrs1);
              } else {
                set_xreg(rd, vrs1 % vrs2);
              }
            }
            break;
          case REMUW: {
            uint32_t u1 = static_cast<uint32_t>(get_xreg(rs1));
            uint32_t u2 = static_cast<uint32_t>(get_xreg(rs2));
            if (u2 == 0) {
              set_xreg(rd, static_cast<int64_t>(u1));
            } else {
              set_xreg(rd, static_cast<uint32_t>(u1 % u2));
            }
            break;
          }
        }
        pc_ += 4;
        break;
      }

      case OPIMM32: {
        Register rd = instr.rd();
        Register rs1 = instr.rs1();
        int32_t imm = static_cast<int32_t>(instr.itype_imm());
        uint32_t funct3 = instr.funct3();
        if (funct3 == ADD) {
          int32_t vrs1 = static_cast<int32_t>(get_xreg(rs1) & 0xFFFFFFFF);
          set_xreg(rd, static_cast<int32_t>(vrs1 + imm));
        }
        pc_ += 4;
        break;
      }

      case AMO: {
        Register rd = instr.rd();
        Register rs2 = instr.rs2();
        Register rs1 = instr.rs1();
        uintx_t addr = get_xreg(rs1);
        uint32_t funct5 = instr.funct5();
        uint64_t loaded = MemoryRead<uint64_t>(addr, rs1);
        uint64_t val = get_xreg(rs2);

        switch (funct5) {
          case AMOADD:
            MemoryWrite<uint64_t>(addr, loaded + val, rs1);
            break;
          case AMOSWAP:
            MemoryWrite<uint64_t>(addr, val, rs1);
            break;
          case AMOXOR:
            MemoryWrite<uint64_t>(addr, loaded ^ val, rs1);
            break;
          case AMOAND:
            MemoryWrite<uint64_t>(addr, loaded & val, rs1);
            break;
          case AMOOR:
            MemoryWrite<uint64_t>(addr, loaded | val, rs1);
            break;
          case AMOMIN:
            MemoryWrite<uint64_t>(addr,
                static_cast<int64_t>(loaded) < static_cast<int64_t>(val) ? loaded : val, rs1);
            break;
          case AMOMAX:
            MemoryWrite<uint64_t>(addr,
                static_cast<int64_t>(loaded) > static_cast<int64_t>(val) ? loaded : val, rs1);
            break;
          case AMOMINU:
            MemoryWrite<uint64_t>(addr, loaded < val ? loaded : val, rs1);
            break;
          case AMOMAXU:
            MemoryWrite<uint64_t>(addr, loaded > val ? loaded : val, rs1);
            break;
          case LR:
            reserved_address_ = addr;
            reserved_value_ = get_xreg(rs2);
            break;
          case SC:
            if (reserved_address_ == addr) {
              MemoryWrite<uint64_t>(addr, val, rs1);
              set_xreg(rd, 0);
              reserved_address_ = 0;
            } else {
              set_xreg(rd, 1);
            }
            break;
        }
        if (funct5 != SC) {
          set_xreg(rd, loaded);
        }
        pc_ += 4;
        break;
      }

      case OPFP: {
        uint32_t funct7 = instr.funct7();
        uint32_t funct3 = instr.funct3();
        if ((funct7 & 1) == 0) {
          // Single precision
          float rs1_val = get_fregs(instr.frs1());
          float rs2_val = get_fregs(instr.frs2());
          float result = 0.0f;
          switch (funct7) {
            case FADDS: result = rs1_val + rs2_val; break;
            case FSUBS: result = rs1_val - rs2_val; break;
            case FMULS: result = rs1_val * rs2_val; break;
            case FDIVS: result = rs1_val / rs2_val; break;
            case FSGNJS:
              result = (funct3 == 0) ? rs1_val : (funct3 == 1) ? -fabsf(rs1_val) : fabsf(rs1_val);
              break;
          }
          if (funct7 != FCLASSS && funct7 != FCMPS && funct7 != FCVTintS && funct7 != FCVTSint) {
            set_fregs(instr.frd(), result);
          }
        } else {
          // Double precision
          double rs1_val = get_fregd(instr.frs1());
          double rs2_val = get_fregd(instr.frs2());
          double result = 0.0;
          switch (funct7) {
            case FADDD: result = rs1_val + rs2_val; break;
            case FSUBD: result = rs1_val - rs2_val; break;
            case FMULD: result = rs1_val * rs2_val; break;
            case FDIVD: result = rs1_val / rs2_val; break;
            case FSQRTD: result = sqrt(rs1_val); break;
            case FSGNJD:
              result = (funct3 == 0) ? rs1_val : (funct3 == 1) ? -fabs(rs1_val) : fabs(rs1_val);
              break;
          }
          if (funct7 != FCLASSD && funct7 != FCMPD && funct7 != FCVTintD && funct7 != FCVTDint) {
            set_fregd(instr.frd(), result);
          }
        }
        pc_ += 4;
        break;
      }

      case FMADD:
      case FMSUB:
      case FNMSUB:
      case FNMADD: {
        // FMA instructions
        double a = get_fregd(instr.frs1());
        double b = get_fregd(instr.frs2());
        double c = get_fregd(FRegister(instr.frs3()));
        double result;
        switch (opcode) {
          case FMADD:  result = a * b + c; break;
          case FMSUB:  result = a * b - c; break;
          case FNMSUB: result = -(a * b) + c; break;
          case FNMADD: result = -(a * b) - c; break;
        }
        set_fregd(instr.frd(), result);
        pc_ += 4;
        break;
      }

      default:
        IllegalInstruction(instr);
        break;
    }

    instret_++;

    // Check for stack overflow
    uintx_t sp = get_xreg(SP);
    if (sp < stack_limit_) {
      Fault("Stack overflow");
    }
  }
}

void Simulator::ExecuteTrace() {
  if (IsTracingExecution()) {
    // Get disassembly
    char buffer[256];
    buffer[0] = '\0';
    Disassembler::Decode(pc_, buffer, 256);
    OS::Print("  0x%" Px ": %s\n", pc_, buffer);
  }
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
  return memory_.Read<uint64_t>(address, size);
}

void Simulator::WriteMem(uintx_t address, uint64_t value, int size) {
  memory_.Write<uint64_t>(address, size, value);
}

template <typename type>
type Simulator::MemoryRead(uintx_t address, Register base) {
  // Fault on null pointer dereferences
  if (IsIllegalAddress(address)) {
    Fault("Null pointer read");
  }
  return memory_.Read<type>(address);
}

template <typename type>
void Simulator::MemoryWrite(uintx_t address, type value, Register base) {
  if (IsIllegalAddress(address)) {
    Fault("Null pointer write");
  }
  memory_.Write<type>(address, value);
}

}  // namespace dart

#endif  // defined(TARGET_ARCH_LOONG64) && defined(DART_INCLUDE_SIMULATOR)