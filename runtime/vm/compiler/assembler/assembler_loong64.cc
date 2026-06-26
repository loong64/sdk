// Copyright (c) 2017, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(TARGET_ARCH_LOONG64)

#define SHOULD_NOT_INCLUDE_RUNTIME

#include "vm/compiler/assembler/assembler.h"
#include "vm/compiler/backend/locations.h"
#include "vm/cpu.h"
#include "vm/instructions.h"
#include "vm/simulator.h"
#include "vm/tags.h"

namespace dart {

DECLARE_FLAG(bool, check_overflow);

namespace compiler {

namespace loong_enc {
  // LoongArch register field positions:
  // rd = bits[4:0], rj = bits[9:5], rk = bits[14:10]
  inline uint32_t Rd(Register r) { return (static_cast<uint32_t>(r) & 0x1F) << 0; }
  inline uint32_t Rj(Register r) { return (static_cast<uint32_t>(r) & 0x1F) << 5; }
  inline uint32_t Rk(Register r) { return (static_cast<uint32_t>(r) & 0x1F) << 10; }
  inline uint32_t FRd(FRegister r) { return (static_cast<uint32_t>(r) & 0x1F) << 0; }
  inline uint32_t FRj(FRegister r) { return (static_cast<uint32_t>(r) & 0x1F) << 5; }
  inline uint32_t FRk(FRegister r) { return (static_cast<uint32_t>(r) & 0x1F) << 10; }
  inline uint32_t Si12(intptr_t imm) { return (static_cast<uint32_t>(imm) & 0xfff) << 10; }
  inline uint32_t Ui12(intptr_t imm) { return (static_cast<uint32_t>(imm) & 0xfff) << 10; }
  inline uint32_t Si20(intptr_t imm) { return (static_cast<uint32_t>(imm) & 0xfffff) << 5; }
  inline uint32_t Si16(intptr_t imm) { return (static_cast<uint32_t>(imm) & 0xffff) << 10; }
  inline uint32_t B12(intptr_t imm) { return (static_cast<uint32_t>(imm) & 0xfff) << 10; }
  inline uint32_t Offs16(intptr_t imm) { return (static_cast<uint32_t>(imm) & 0xffff) << 10; }
  inline uint32_t Ui5(intptr_t imm) { return (static_cast<uint32_t>(imm) & 0x1f) << 10; }
  inline uint32_t Ui6(intptr_t imm) { return (static_cast<uint32_t>(imm) & 0x3f) << 10; }
  inline uint32_t Si14(intptr_t imm) { return (static_cast<uint32_t>(imm) & 0x3fff) << 10; }
}
using namespace loong_enc;

#define EMIT(enc) Emit(enc)

static intptr_t BranchBytesToWords(intptr_t offset) {
  ASSERT(Utils::IsAligned(offset, 4));
  return offset >> 2;
}

static intptr_t BranchWordsToBytes(intptr_t offset) {
  return offset << 2;
}

MicroAssembler::MicroAssembler(ObjectPoolBuilder* ob, intptr_t fl)
    : AssemblerBase(ob), far_branch_level_(fl) {}
MicroAssembler::~MicroAssembler() {}
void MicroAssembler::Emit(uint32_t encoding) {
  AssemblerBuffer::EnsureCapacity c(&buffer_);
  buffer_.Emit<int32_t>(encoding);
  if (encoding == 0xffffffff || ((encoding >> 26) & 0x3f) == 0x3f) {
    intptr_t sz = buffer_.Size();
    char buf[512]; int off = 0;
    for (intptr_t p = 0; p < sz && off < 500; p += 4) {
      off += snprintf(buf+off, sizeof(buf)-off, "%s0x%08x", p>0?" ":"", buffer_.Load<int32_t>(p));
    }
    FATAL("LoongArch: EMIT invalid enc=0x%x sz=%" Pd " buf=[%s]", encoding, sz, buf);
  }
}
void MicroAssembler::Bind(Label* label) {
  // Binding is handled in Assembler::Bind which patches all unresolved
  // forward branches before calling label->BindTo().
}

// ==== LoongArch PC-relative address generation ====
// opcode 000110 (6): pcaddu12i, pcalau12i
// opcode 000111 (7): pcaddu18i
// Format: |31:26=opc|25:5=si20|4:0=rd|
void MicroAssembler::pcaddu12i(Register rd, intptr_t si20) {
  EMIT(Si20(si20) | Rd(rd) | 0x1C000000);
}
void MicroAssembler::pcalau12i(Register rd, intptr_t si20) {
  EMIT(Si20(si20) | Rd(rd) | 0x1A000000);
}
void MicroAssembler::pcaddu18i(Register rd, intptr_t si20) {
  EMIT(Si20(si20) | Rd(rd) | 0x1E000000);
}

// ==== Immediate load ====
// LU12IW: |31:26=000110|25:5=si20|4:0=rd| (same as pcaddu12i, differentiated by bit[24])
// Actually: LU12IW has its own separate encoding
// From LA manual: lu12iw rd, si20
void MicroAssembler::lu12iw(Register rd, intptr_t si20) {
  EMIT(Si20(si20) | Rd(rd) | 0x14000000);
}
void MicroAssembler::lu32id(Register rd, intptr_t si20) {
  EMIT(Si20(si20) | Rd(rd) | 0x16000000);
}
void MicroAssembler::lu52id(Register rd, Register rj, intptr_t si12) {
  ASSERT(Utils::IsInt(12, si12));
  EMIT(Si12(si12) | Rj(rj) | Rd(rd) | 0x03000000);
}

// ==== Jumps ====
// JIRL: |31:26=010011|25:10=offs16|9:5=rj|4:0=rd|
void MicroAssembler::jirl(Register rd, Register rj, intptr_t si12) {
  EMIT(Offs16(BranchBytesToWords(si12)) | Rj(rj) | Rd(rd) | 0x4C000000);
}
void MicroAssembler::jirl_fixed(Register rd, Register rj, intptr_t si12) { jirl(rd, rj, si12); }

void MicroAssembler::trap() {
  Emit(Instr::kBreakPointInstruction);
}


// Branch encoding helpers for LoongArch.
// BEQZ:    |31:26=010000|25:10=offs[15:0]|9:5=rj|4:0=offs[20:16]|
// BNEZ:    |31:26=010001|25:10=offs[15:0]|9:5=rj|4:0=offs[20:16]|
// BC.EQZ:  |31:26=011100|25:10=offs[15:0]|9:5=rj|4:0=offs[20:16]|
// BC.NEZ:  |31:26=011101|25:10=offs[15:0]|9:5=rj|4:0=offs[20:16]|
// BEQ/BNE/BLT/BGE/BLTU/BGEU: |31:26=010110|25:10=offs16|9:5=rj|4:0=rd|
// B:    |31:26=010100|25:0=offs26|
// BL:   |31:26=010101|25:0=offs26|

// Encode a 21-bit signed offset for beqz/bnez/bceqz/bcnez into the
// split format: offs[15:0]@bits[25:10], offs[20:16]@bits[4:0].
static uint32_t EncodeBranchOffset21(intptr_t offs) {
  offs = BranchBytesToWords(offs);
  uint32_t lo = (offs & 0xffff) << 10;
  uint32_t hi = ((offs >> 16) & 0x1f) << 0;
  return lo | hi;
}

// Decode a 21-bit signed offset from the split format.
static int32_t DecodeBranchOffset21(uint32_t instr) {
  int32_t lo = static_cast<int16_t>((instr >> 10) & 0xffff);
  int32_t hi = (instr & 0x1f);
  int32_t offs = (hi << 16) | (lo & 0xffff);
  if (offs & (1 << 20)) {
    offs |= ~((1 << 21) - 1);
  }
  return BranchWordsToBytes(offs);
}

// Encode a 26-bit signed offset for b/bl: offs[25:0]@bits[25:0].
static uint32_t EncodeBranchOffset26(intptr_t offs) {
  offs = BranchBytesToWords(offs);
  return (offs & 0x3ffffff) << 0;
}

// Decode a 26-bit signed offset.
static int32_t DecodeBranchOffset26(uint32_t instr) {
  return BranchWordsToBytes(static_cast<int32_t>(instr << 6) >> 6);
}

static uint32_t EncodeBranchOffset16(intptr_t offs) {
  return Offs16(BranchBytesToWords(offs));
}

static int32_t DecodeBranchOffset16(uint32_t instr) {
  return BranchWordsToBytes(static_cast<int16_t>((instr >> 10) & 0xffff));
}

void MicroAssembler::b(Label* l, JumpDistance d) { beqz(ZR, l, d); }

void MicroAssembler::bl(Label* l, JumpDistance d) {
  if (l->IsBound()) {
    intptr_t offs = l->Position() - buffer_.Size();
    ASSERT(Utils::IsInt(28, offs));  // 26-bit offset, 4-byte aligned => 28-bit range
    EMIT(EncodeBranchOffset26(offs) | 0x54000000);
  } else {
    // Chain: encode distance to previous link in offset field.
    intptr_t chain_delta = 0;
    if (l->IsLinked()) {
      chain_delta = buffer_.Size() - l->Position();
      ASSERT(chain_delta > 0 && Utils::IsInt(28, chain_delta));
    }
    l->LinkTo(buffer_.Size());
    EMIT(EncodeBranchOffset26(chain_delta) | 0x54000000);
  }
}

void MicroAssembler::beqz(Register rj, Label* l, JumpDistance d) {
  if (l->IsBound()) {
    intptr_t offs = l->Position() - buffer_.Size();
    ASSERT(Utils::IsInt(23, offs));  // 21-bit offset, 4-byte aligned => 23-bit range
    EMIT(EncodeBranchOffset21(offs) | Rj(rj) | 0x40000000);
  } else {
    intptr_t chain_delta = 0;
    if (l->IsLinked()) {
      chain_delta = buffer_.Size() - l->Position();
      ASSERT(chain_delta > 0 && Utils::IsInt(23, chain_delta));
    }
    l->LinkTo(buffer_.Size());
    EMIT(EncodeBranchOffset21(chain_delta) | Rj(rj) | 0x40000000);
  }
}

void MicroAssembler::bnez(Register rj, Label* l, JumpDistance d) {
  if (l->IsBound()) {
    intptr_t offs = l->Position() - buffer_.Size();
    ASSERT(Utils::IsInt(23, offs));
    EMIT(EncodeBranchOffset21(offs) | Rj(rj) | 0x44000000);
  } else {
    intptr_t chain_delta = 0;
    if (l->IsLinked()) {
      chain_delta = buffer_.Size() - l->Position();
      ASSERT(chain_delta > 0 && Utils::IsInt(23, chain_delta));
    }
    l->LinkTo(buffer_.Size());
    EMIT(EncodeBranchOffset21(chain_delta) | Rj(rj) | 0x44000000);
  }
}

void MicroAssembler::beq(Register rj, Register rd, Label* l, JumpDistance d) {
  if (l->IsBound()) {
    intptr_t offs = l->Position() - buffer_.Size();
    ASSERT(Utils::IsInt(18, offs));
    EMIT(EncodeBranchOffset16(offs) | Rd(rd) | Rj(rj) | 0x58000000);
  } else {
    intptr_t chain_delta = 0;
    if (l->IsLinked()) {
      chain_delta = buffer_.Size() - l->Position();
      ASSERT(chain_delta > 0 && Utils::IsInt(18, chain_delta));
    }
    l->LinkTo(buffer_.Size());
    EMIT(EncodeBranchOffset16(chain_delta) | Rd(rd) | Rj(rj) | 0x58000000);
  }
}
void MicroAssembler::bne(Register rj, Register rd, Label* l, JumpDistance d) {
  if (l->IsBound()) {
    intptr_t offs = l->Position() - buffer_.Size();
    ASSERT(Utils::IsInt(18, offs));
    EMIT(EncodeBranchOffset16(offs) | Rd(rd) | Rj(rj) | 0x5C000000);
  } else {
    intptr_t chain_delta = 0;
    if (l->IsLinked()) {
      chain_delta = buffer_.Size() - l->Position();
      ASSERT(chain_delta > 0 && Utils::IsInt(18, chain_delta));
    }
    l->LinkTo(buffer_.Size());
    EMIT(EncodeBranchOffset16(chain_delta) | Rd(rd) | Rj(rj) | 0x5C000000);
  }
}
void MicroAssembler::blt(Register rj, Register rd, Label* l, JumpDistance d) {
  if (l->IsBound()) {
    intptr_t offs = l->Position() - buffer_.Size();
    ASSERT(Utils::IsInt(18, offs));
    EMIT(EncodeBranchOffset16(offs) | Rd(rd) | Rj(rj) | 0x60000000);
  } else {
    intptr_t chain_delta = 0;
    if (l->IsLinked()) {
      chain_delta = buffer_.Size() - l->Position();
      ASSERT(chain_delta > 0 && Utils::IsInt(18, chain_delta));
    }
    l->LinkTo(buffer_.Size());
    EMIT(EncodeBranchOffset16(chain_delta) | Rd(rd) | Rj(rj) | 0x60000000);
  }
}
void MicroAssembler::bge(Register rj, Register rd, Label* l, JumpDistance d) {
  if (l->IsBound()) {
    intptr_t offs = l->Position() - buffer_.Size();
    ASSERT(Utils::IsInt(18, offs));
    EMIT(EncodeBranchOffset16(offs) | Rd(rd) | Rj(rj) | 0x64000000);
  } else {
    intptr_t chain_delta = 0;
    if (l->IsLinked()) {
      chain_delta = buffer_.Size() - l->Position();
      ASSERT(chain_delta > 0 && Utils::IsInt(18, chain_delta));
    }
    l->LinkTo(buffer_.Size());
    EMIT(EncodeBranchOffset16(chain_delta) | Rd(rd) | Rj(rj) | 0x64000000);
  }
}
void MicroAssembler::bltu(Register rj, Register rd, Label* l, JumpDistance d) {
  if (l->IsBound()) {
    intptr_t offs = l->Position() - buffer_.Size();
    ASSERT(Utils::IsInt(18, offs));
    EMIT(EncodeBranchOffset16(offs) | Rd(rd) | Rj(rj) | 0x68000000);
  } else {
    intptr_t chain_delta = 0;
    if (l->IsLinked()) {
      chain_delta = buffer_.Size() - l->Position();
      ASSERT(chain_delta > 0 && Utils::IsInt(18, chain_delta));
    }
    l->LinkTo(buffer_.Size());
    EMIT(EncodeBranchOffset16(chain_delta) | Rd(rd) | Rj(rj) | 0x68000000);
  }
}
void MicroAssembler::bgeu(Register rj, Register rd, Label* l, JumpDistance d) {
  if (l->IsBound()) {
    intptr_t offs = l->Position() - buffer_.Size();
    ASSERT(Utils::IsInt(18, offs));
    EMIT(EncodeBranchOffset16(offs) | Rd(rd) | Rj(rj) | 0x6C000000);
  } else {
    intptr_t chain_delta = 0;
    if (l->IsLinked()) {
      chain_delta = buffer_.Size() - l->Position();
      ASSERT(chain_delta > 0 && Utils::IsInt(18, chain_delta));
    }
    l->LinkTo(buffer_.Size());
    EMIT(EncodeBranchOffset16(chain_delta) | Rd(rd) | Rj(rj) | 0x6C000000);
  }
}

void MicroAssembler::bceqz(FRegister fcj, Label* l, JumpDistance d) {
  if (l->IsBound()) {
    intptr_t offs = l->Position() - buffer_.Size();
    ASSERT(Utils::IsInt(23, offs));
    EMIT(EncodeBranchOffset21(offs) | FRj(fcj) | 0x70000000);
  } else {
    intptr_t chain_delta = 0;
    if (l->IsLinked()) {
      chain_delta = buffer_.Size() - l->Position();
      ASSERT(chain_delta > 0 && Utils::IsInt(23, chain_delta));
    }
    l->LinkTo(buffer_.Size());
    EMIT(EncodeBranchOffset21(chain_delta) | FRj(fcj) | 0x70000000);
  }
}

void MicroAssembler::bcnez(FRegister fcj, Label* l, JumpDistance d) {
  if (l->IsBound()) {
    intptr_t offs = l->Position() - buffer_.Size();
    ASSERT(Utils::IsInt(23, offs));
    EMIT(EncodeBranchOffset21(offs) | FRj(fcj) | 0x74000000);
  } else {
    intptr_t chain_delta = 0;
    if (l->IsLinked()) {
      chain_delta = buffer_.Size() - l->Position();
      ASSERT(chain_delta > 0 && Utils::IsInt(23, chain_delta));
    }
    l->LinkTo(buffer_.Size());
    EMIT(EncodeBranchOffset21(chain_delta) | FRj(fcj) | 0x74000000);
  }
}

// ==== Loads (opcode 001010 at bits[31:26]) ====
// Format: |31:26=001010|25:22=type|21:10=si12|9:5=rj|4:0=rd|
// type[3]=0: load; type[2]=0: signed, 1: unsigned; type[1:0]=size: 00=byte,01=half,10=word,11=dword
// Signed: type = 0, size@bits[23:22]; Unsigned: bits[24]=1, size@bits[23:22]
// LD.B:  type=0000    LD.BU: type=0100    ST.B: type=1000
// LD.H:  type=0001    LD.HU: type=0101    ST.H: type=1001
// LD.W:  type=0010    LD.WU: type=0110    ST.W: type=1010
// LD.D:  type=0011                        ST.D: type=1011

void MicroAssembler::ld_b(Register rd, Register rj, intptr_t si12) {
  EMIT(Si12(si12) | Rj(rj) | Rd(rd) | 0x28000000);
}
void MicroAssembler::ld_h(Register rd, Register rj, intptr_t si12) {
  EMIT(Si12(si12) | Rj(rj) | Rd(rd) | 0x28400000);
}
void MicroAssembler::ld_w(Register rd, Register rj, intptr_t si12) {
  EMIT(Si12(si12) | Rj(rj) | Rd(rd) | 0x28800000);
}
void MicroAssembler::ld_d(Register rd, Register rj, intptr_t si12) {
  EMIT(Si12(si12) | Rj(rj) | Rd(rd) | 0x28C00000);
}
void MicroAssembler::ld_bu(Register rd, Register rj, intptr_t si12) {
  EMIT(Si12(si12) | Rj(rj) | Rd(rd) | 0x2A000000);
}

void MicroAssembler::ll_w(Register rd, Register rj, intptr_t si14) {
  EMIT(Si14(si14) | Rj(rj) | Rd(rd) | 0x20000000);
}

void MicroAssembler::sc_w(Register rd, Register rj, intptr_t si14) {
  EMIT(Si14(si14) | Rj(rj) | Rd(rd) | 0x21000000);
}

void MicroAssembler::ll_d(Register rd, Register rj, intptr_t si14) {
  EMIT(Si14(si14) | Rj(rj) | Rd(rd) | 0x22000000);
}

void MicroAssembler::sc_d(Register rd, Register rj, intptr_t si14) {
  EMIT(Si14(si14) | Rj(rj) | Rd(rd) | 0x23000000);
}
void MicroAssembler::ld_hu(Register rd, Register rj, intptr_t si12) {
  EMIT(Si12(si12) | Rj(rj) | Rd(rd) | 0x2A400000);
}
void MicroAssembler::ld_wu(Register rd, Register rj, intptr_t si12) {
  EMIT(Si12(si12) | Rj(rj) | Rd(rd) | 0x2A800000);
}

// ==== Stores (opcode 001010 at bits[31:26], type>=1000 for stores) ====
void MicroAssembler::st_b(Register rd, Register rj, intptr_t si12) {
  EMIT(Si12(si12) | Rd(rd) | Rj(rj) | 0x29000000);
}
void MicroAssembler::st_h(Register rd, Register rj, intptr_t si12) {
  EMIT(Si12(si12) | Rd(rd) | Rj(rj) | 0x29400000);
}
void MicroAssembler::st_w(Register rd, Register rj, intptr_t si12) {
  EMIT(Si12(si12) | Rd(rd) | Rj(rj) | 0x29800000);
}
void MicroAssembler::st_d(Register rd, Register rj, intptr_t si12) {
  EMIT(Si12(si12) | Rd(rd) | Rj(rj) | 0x29C00000);
}

// ==== ALU Immediate (opcode 000000 at bits[31:26]) ====
// Format: |31:26=000000|25:22=func4|21:10=si12/ui12|9:5=rj|4:0=rd|
// func4: 1010=ADDI.W, 1011=ADDI.D, 1000=SLTI, 1001=SLTUI,
//        1101=ANDI, 1110=ORI, 1111=XORI

void MicroAssembler::addi_w(Register rd, Register rj, intptr_t si12) {
  EMIT(Si12(si12) | Rj(rj) | Rd(rd) | 0x02800000);
}
void MicroAssembler::addi_d(Register rd, Register rj, intptr_t si12) {
  EMIT(Si12(si12) | Rj(rj) | Rd(rd) | 0x02C00000);
}

void MicroAssembler::slti(Register rd, Register rj, intptr_t si12) {
  EMIT(Si12(si12) | Rj(rj) | Rd(rd) | 0x02000000);
}
void MicroAssembler::sltui(Register rd, Register rj, intptr_t si12) {
  EMIT(Si12(si12) | Rj(rj) | Rd(rd) | 0x02400000);
}

void MicroAssembler::andi(Register rd, Register rj, intptr_t ui12) {
  EMIT(Ui12(ui12) | Rj(rj) | Rd(rd) | 0x03400000);
}
void MicroAssembler::ori(Register rd, Register rj, intptr_t ui12) {
  EMIT(Ui12(ui12) | Rj(rj) | Rd(rd) | 0x03800000);
}
void MicroAssembler::xori(Register rd, Register rj, intptr_t ui12) {
  EMIT(Ui12(ui12) | Rj(rj) | Rd(rd) | 0x03C00000);
}

// ==== ALU Register 3-operand ====
// Format: |31:22|21:17=rk|16:10=func7|9:5=rj|4:0=rd|
// or: |31:22=op10|21:17=rk|16:10=func7|9:5=rj|4:0=rd|

void MicroAssembler::add_w(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00100000);
}
void MicroAssembler::add_d(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00108000);
}
void MicroAssembler::sub_w(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00110000);
}
void MicroAssembler::sub_d(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00118000);
}
void MicroAssembler::and_l(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00148000);
}
void MicroAssembler::or_l(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00150000);
}
void MicroAssembler::xor_l(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00158000);
}

void MicroAssembler::mul_w(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x001C0000);
}
void MicroAssembler::mul_d(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x001D8000);
}
void MicroAssembler::mulh_w(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x001C8000);
}
void MicroAssembler::mulh_wu(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x001D0000);
}
void MicroAssembler::mulh_d(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x001E0000);
}
void MicroAssembler::mulh_du(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x001E8000);
}

void MicroAssembler::div_w(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00200000);
}
void MicroAssembler::div_wu(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00210000);
}
void MicroAssembler::div_d(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00220000);
}
void MicroAssembler::div_du(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00230000);
}

void MicroAssembler::mod_w(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00208000);
}
void MicroAssembler::mod_wu(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00218000);
}
void MicroAssembler::mod_d(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00228000);
}
void MicroAssembler::mod_du(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00238000);
}

// ==== Shift instructions ====
void MicroAssembler::sll_w(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00170000);
}
void MicroAssembler::srl_w(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00178000);
}
void MicroAssembler::sra_w(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00180000);
}
void MicroAssembler::sll_d(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00188000);
}
void MicroAssembler::srl_d(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00190000);
}
void MicroAssembler::sra_d(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00198000);
}

// ==== Shift immediate ====
void MicroAssembler::slli_w(Register rd, Register rj, intptr_t ui5) {
  ASSERT(Utils::IsUint(5, ui5));
  EMIT(Ui5(ui5) | Rj(rj) | Rd(rd) | 0x00408000);
}
void MicroAssembler::srli_w(Register rd, Register rj, intptr_t ui5) {
  ASSERT(Utils::IsUint(5, ui5));
  EMIT(Ui5(ui5) | Rj(rj) | Rd(rd) | 0x00448000);
}
void MicroAssembler::srai_w(Register rd, Register rj, intptr_t ui5) {
  ASSERT(Utils::IsUint(5, ui5));
  EMIT(Ui5(ui5) | Rj(rj) | Rd(rd) | 0x00488000);
}
void MicroAssembler::slli_d(Register rd, Register rj, intptr_t ui6) {
  ASSERT(Utils::IsUint(6, ui6));
  EMIT(Ui6(ui6) | Rj(rj) | Rd(rd) | 0x00410000);
}
void MicroAssembler::srli_d(Register rd, Register rj, intptr_t ui6) {
  ASSERT(Utils::IsUint(6, ui6));
  EMIT(Ui6(ui6) | Rj(rj) | Rd(rd) | 0x00450000);
}
void MicroAssembler::srai_d(Register rd, Register rj, intptr_t ui6) {
  ASSERT(Utils::IsUint(6, ui6));
  EMIT(Ui6(ui6) | Rj(rj) | Rd(rd) | 0x00490000);
}

// ==== Compare/set ====
void MicroAssembler::slt(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00120000);
}
void MicroAssembler::sltu(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00128000);
}

// ==== Special ====
void MicroAssembler::break_(uint32_t code) {
  ASSERT(Utils::IsUint(15, code));
  EMIT((code << 5) | 0x002a0000);  // LoongArch break: |31:26=0|25:15=code[14:5]|14:5=code[4:0]|4:0=0|
}
void MicroAssembler::dbar(intptr_t hint) {
  EMIT((static_cast<uint32_t>(hint) << 15) | 0x38720000);
}
void MicroAssembler::ibar(intptr_t hint) {
  EMIT((static_cast<uint32_t>(hint) << 15) | 0x38728000);
}
void MicroAssembler::syscall(uint32_t code) {
  EMIT((code << 15) | 0x00000040);
}

// ==== 16-bit immediate addition ====
void MicroAssembler::addu16i_d(Register rd, Register rj, intptr_t si16) {
  EMIT((static_cast<uint32_t>(si16) & 0xffff) << 5 | Rj(rj) | Rd(rd) | 0x10000000);
}

// ==== Floating point ====
void MicroAssembler::fadd_s(FRegister fd, FRegister fj, FRegister fk) {
  EMIT(FRk(fk) | FRj(fj) | FRd(fd) | 0x01000000);
}
void MicroAssembler::fadd_d(FRegister fd, FRegister fj, FRegister fk) {
  EMIT(FRk(fk) | FRj(fj) | FRd(fd) | 0x01400000);
}
void MicroAssembler::fsub_s(FRegister fd, FRegister fj, FRegister fk) {
  EMIT(FRk(fk) | FRj(fj) | FRd(fd) | 0x00800000);
}
void MicroAssembler::fsub_d(FRegister fd, FRegister fj, FRegister fk) {
  EMIT(FRk(fk) | FRj(fj) | FRd(fd) | 0x00C00000);
}
void MicroAssembler::fmul_s(FRegister fd, FRegister fj, FRegister fk) {
  EMIT(FRk(fk) | FRj(fj) | FRd(fd) | 0x01800000);
}
void MicroAssembler::fmul_d(FRegister fd, FRegister fj, FRegister fk) {
  EMIT(FRk(fk) | FRj(fj) | FRd(fd) | 0x01C00000);
}
void MicroAssembler::fdiv_s(FRegister fd, FRegister fj, FRegister fk) {
  EMIT(FRk(fk) | FRj(fj) | FRd(fd) | 0x02000000);
}
void MicroAssembler::fdiv_d(FRegister fd, FRegister fj, FRegister fk) {
  EMIT(FRk(fk) | FRj(fj) | FRd(fd) | 0x02400000);
}

void MicroAssembler::fmadd_s(FRegister fd, FRegister fj, FRegister fk, FRegister fa) {
  EMIT(FRk(fk) | FRj(fj) | FRd(fd) | (static_cast<uint32_t>(fa) << 15) | 0x10000040);
}
void MicroAssembler::fmadd_d(FRegister fd, FRegister fj, FRegister fk, FRegister fa) {
  EMIT(FRk(fk) | FRj(fj) | FRd(fd) | (static_cast<uint32_t>(fa) << 15) | 0x10400040);
}
void MicroAssembler::fmsub_s(FRegister fd, FRegister fj, FRegister fk, FRegister fa) {
  EMIT(FRk(fk) | FRj(fj) | FRd(fd) | (static_cast<uint32_t>(fa) << 15) | 0x10800040);
}
void MicroAssembler::fmsub_d(FRegister fd, FRegister fj, FRegister fk, FRegister fa) {
  EMIT(FRk(fk) | FRj(fj) | FRd(fd) | (static_cast<uint32_t>(fa) << 15) | 0x10C00040);
}
void MicroAssembler::fnmadd_s(FRegister fd, FRegister fj, FRegister fk, FRegister fa) {
  EMIT(FRk(fk) | FRj(fj) | FRd(fd) | (static_cast<uint32_t>(fa) << 15) | 0x11000040);
}
void MicroAssembler::fnmadd_d(FRegister fd, FRegister fj, FRegister fk, FRegister fa) {
  EMIT(FRk(fk) | FRj(fj) | FRd(fd) | (static_cast<uint32_t>(fa) << 15) | 0x11400040);
}
void MicroAssembler::fnmsub_s(FRegister fd, FRegister fj, FRegister fk, FRegister fa) {
  EMIT(FRk(fk) | FRj(fj) | FRd(fd) | (static_cast<uint32_t>(fa) << 15) | 0x11800040);
}
void MicroAssembler::fnmsub_d(FRegister fd, FRegister fj, FRegister fk, FRegister fa) {
  EMIT(FRk(fk) | FRj(fj) | FRd(fd) | (static_cast<uint32_t>(fa) << 15) | 0x11C00040);
}

void MicroAssembler::fcmp_cond_s(FRegister fj, FRegister fk, int cond) {
  EMIT(FRk(fk) | FRj(fj) | (cond << 5) | 0x04000040);
}
void MicroAssembler::fcmp_cond_d(FRegister fj, FRegister fk, int cond) {
  EMIT(FRk(fk) | FRj(fj) | (cond << 5) | 0x04400040);
}

void MicroAssembler::fcvts_d2s(FRegister fd, FRegister fj) {
  EMIT(FRj(fj) | FRd(fd) | 0x08C00040);
}
void MicroAssembler::fcvts_s2d(FRegister fd, FRegister fj) {
  EMIT(FRj(fj) | FRd(fd) | 0x08800040);
}

void MicroAssembler::ftintrm_l_d(FRegister fd, FRegister fj) {
  EMIT(FRj(fj) | FRd(fd) | 0x09C10040);
}
void MicroAssembler::ftintrp_l_d(FRegister fd, FRegister fj) {
  EMIT(FRj(fj) | FRd(fd) | 0x09C08040);
}

void MicroAssembler::ftintrz_w_s(FRegister fd, FRegister fj) {
  EMIT(FRj(fj) | FRd(fd) | 0x09000040);
}
void MicroAssembler::ftintrz_w_d(FRegister fd, FRegister fj) {
  EMIT(FRj(fj) | FRd(fd) | 0x09400040);
}
void MicroAssembler::ftintrz_l_s(FRegister fd, FRegister fj) {
  EMIT(FRj(fj) | FRd(fd) | 0x09800040);
}
void MicroAssembler::ftintrz_l_d(FRegister fd, FRegister fj) {
  EMIT(FRj(fj) | FRd(fd) | 0x09C00040);
}

void MicroAssembler::ffint_s_w(FRegister fd, FRegister fj) {
  EMIT(FRj(fj) | FRd(fd) | 0x08400040);
}
void MicroAssembler::ffint_s_l(FRegister fd, FRegister fj) {
  EMIT(FRj(fj) | FRd(fd) | 0x08400040 | (1<<10));
}
void MicroAssembler::ffint_d_w(FRegister fd, FRegister fj) {
  EMIT(FRj(fj) | FRd(fd) | 0x08400040 | (1<<11));
}
void MicroAssembler::ffint_d_l(FRegister fd, FRegister fj) {
  EMIT(FRj(fj) | FRd(fd) | 0x08400040 | (1<<11) | (1<<10));
}

// ==== FP load/store ====
// FP loads/stores use opcode 001011 at bits[31:26]
// type[3]=0: load, 1: store; type[0]=0: single, 1: double
// fld_s: type=0000, fld_d: type=0001, fst_s: type=1000, fst_d: type=1001

void MicroAssembler::fld_s(FRegister fd, Register rj, intptr_t si12) {
  ASSERT(Utils::IsInt(12, si12));
  EMIT(Si12(si12) | Rj(rj) | FRd(fd) | 0x2B000000);
}
void MicroAssembler::fld_d(FRegister fd, Register rj, intptr_t si12) {
  ASSERT(Utils::IsInt(12, si12));
  EMIT(Si12(si12) | Rj(rj) | FRd(fd) | 0x2B800000);
}
void MicroAssembler::fst_s(FRegister fd, Register rj, intptr_t si12) {
  ASSERT(Utils::IsInt(12, si12));
  EMIT(Si12(si12) | FRd(fd) | Rj(rj) | 0x2B400000);
}
void MicroAssembler::fst_d(FRegister fd, Register rj, intptr_t si12) {
  ASSERT(Utils::IsInt(12, si12));
  EMIT(Si12(si12) | FRd(fd) | Rj(rj) | 0x2BC00000);
}

// ==== FP move ====
void MicroAssembler::fmv_s(FRegister fd, FRegister fj) {
  EMIT(FRj(fj) | FRd(fd) | 0x06400040);
}
void MicroAssembler::fmv_d(FRegister fd, FRegister fj) {
  EMIT(FRj(fj) | FRd(fd) | 0x06600040);
}
void MicroAssembler::fabs_d(FRegister fd, FRegister fj) {
  EMIT(FRj(fj) | FRd(fd) | 0x06C00040);
}
void MicroAssembler::fneg_d(FRegister fd, FRegister fj) {
  EMIT(FRj(fj) | FRd(fd) | 0x06E00040);
}

// ==== GR <-> FR moves ====
void MicroAssembler::movgr2fr_w(FRegister fd, Register rj) {
  EMIT(Rj(rj) | FRd(fd) | 0x01200040);
}
void MicroAssembler::movgr2fr_d(FRegister fd, Register rj) {
  EMIT(Rj(rj) | FRd(fd) | 0x01300040);
}
void MicroAssembler::movfr2gr_s(Register rd, FRegister fj) {
  EMIT(FRj(fj) | Rd(rd) | 0x00E00040);
}
void MicroAssembler::movfr2gr_d(Register rd, FRegister fj) {
  EMIT(FRj(fj) | Rd(rd) | 0x00F00040);
}
void MicroAssembler::movgr2frh_w(FRegister fd, Register rj) {
  EMIT(Rj(rj) | FRd(fd) | 0x01600040);
}
void MicroAssembler::movfrh2gr_s(Register rd, FRegister fj) {
  EMIT(FRj(fj) | Rd(rd) | 0x00E00040 | (1 << 15));
}
void MicroAssembler::fsel(FRegister fd, FRegister fj, FRegister fk, FRegister fa) {
  EMIT(FRk(fk) | FRj(fj) | FRd(fd) | (static_cast<uint32_t>(fa) << 15) | 0x12000040);
}

// ============================================================================
// Assembler (higher-level)
// ============================================================================

Assembler::Assembler(ObjectPoolBuilder* ob, intptr_t fl)
    : MicroAssembler(ob, fl), constant_pool_allowed_(false) {}
Assembler::~Assembler() {}
void Assembler::Bind(Label* l) {
  ASSERT(!l->IsBound());
  const intptr_t bound_pc = buffer_.Size();

  // Resolve all forward branches linked to this label.
  while (l->IsLinked()) {
    const intptr_t link_pos = l->Position();
    const intptr_t dest = bound_pc - link_pos;
    int32_t instr = buffer_.Load<int32_t>(link_pos);
    uint32_t opcode = (instr >> 26) & 0x3f;

    if (opcode == 0x10 || opcode == 0x11) {
      // beqz (0x10) or bnez (0x11): 21-bit offset.
      ASSERT(Utils::IsInt(23, dest));
      int32_t chain = DecodeBranchOffset21(instr);
      buffer_.Store<int32_t>(
          link_pos,
          EncodeBranchOffset21(dest) | (instr & 0xfc000fe0) | (opcode << 26));
      l->position_ = (chain == 0) ? 0 : (link_pos - chain + 4);
    } else if (opcode == 0x15) {
      // bl (0x15): 26-bit offset.
      ASSERT(Utils::IsInt(28, dest));
      int32_t chain = DecodeBranchOffset26(instr);
      buffer_.Store<int32_t>(
          link_pos,
          EncodeBranchOffset26(dest) | 0x54000000);
      l->position_ = (chain == 0) ? 0 : (link_pos - chain + 4);
    } else if (opcode == 0x16 || opcode == 0x17 || opcode == 0x18 ||
               opcode == 0x19 || opcode == 0x1a || opcode == 0x1b) {
      // beq(0x16)/bne(0x17)/blt(0x18)/bge(0x19)/bltu(0x1a)/bgeu(0x1b): 16-bit offset.
      ASSERT(Utils::IsInt(18, dest));
      int32_t chain = DecodeBranchOffset16(instr);
      buffer_.Store<int32_t>(
          link_pos,
          EncodeBranchOffset16(dest) | (instr & 0xfc0003ff) | (opcode << 26));
      l->position_ = (chain == 0) ? 0 : (link_pos - chain + 4);
    } else if (opcode == 0x1c || opcode == 0x1d) {
      // bceqz (0x1c) or bcnez (0x1d): 21-bit offset.
      ASSERT(Utils::IsInt(23, dest));
      int32_t chain = DecodeBranchOffset21(instr);
      buffer_.Store<int32_t>(
          link_pos,
          EncodeBranchOffset21(dest) | (instr & 0xfc000fe0) | (opcode << 26));
      l->position_ = (chain == 0) ? 0 : (link_pos - chain + 4);
    } else if ((instr & 0xfc000000) == 0x4c000000) {
      // jirl: 16-bit offset. Used by Call(Label*).
      ASSERT(Utils::IsInt(18, dest));
      int32_t chain = DecodeBranchOffset16(instr);
      buffer_.Store<int32_t>(
          link_pos,
          EncodeBranchOffset16(dest) | (instr & 0xfc0003ff));
      l->position_ = (chain == 0) ? 0 : (link_pos - chain + 4);
    } else {
      // Unknown branch type.
      FATAL("LoongArch: unknown branch instruction at link position");
    }
  }

  // Resolve near links.
  while (l->HasNear()) {
    const intptr_t near_pos = l->NearPosition();
    intptr_t dest = bound_pc - near_pos;
    int32_t instr = buffer_.Load<int32_t>(near_pos);
    uint32_t opcode = (instr >> 26) & 0x3f;

    if (opcode == 0x10 || opcode == 0x11) {
      ASSERT(Utils::IsInt(23, dest));
      buffer_.Store<int32_t>(
          near_pos,
          EncodeBranchOffset21(dest) | (instr & 0xfc000fe0) | (opcode << 26));
    } else if (opcode == 0x16 || opcode == 0x17 || opcode == 0x18 ||
               opcode == 0x19 || opcode == 0x1a || opcode == 0x1b) {
      ASSERT(Utils::IsInt(18, dest));
      buffer_.Store<int32_t>(
          near_pos,
          EncodeBranchOffset16(dest) | (instr & 0xfc0003ff) | (opcode << 26));
    } else if (opcode == 0x1c || opcode == 0x1d) {
      ASSERT(Utils::IsInt(23, dest));
      buffer_.Store<int32_t>(
          near_pos,
          EncodeBranchOffset21(dest) | (instr & 0xfc000fe0) | (opcode << 26));
    } else {
      FATAL("LoongArch: unknown near branch instruction");
    }
  }

  l->BindTo(bound_pc);
}
intptr_t Assembler::CodeSize() const { return buffer_.Size(); }

// Frame management
void Assembler::EnterFrame(intptr_t frame_size) {
  addi_d(SP, SP, -(frame_size + 2 * target::kWordSize));
  st_d(RA, SP, frame_size + 1 * target::kWordSize);
  st_d(FP, SP, frame_size + 0 * target::kWordSize);
  addi_d(FP, SP, frame_size + 2 * target::kWordSize);
}
void Assembler::LeaveFrame() {
  addi_d(SP, FP, -2 * target::kWordSize);
  ld_d(FP, SP, 0 * target::kWordSize);
  ld_d(RA, SP, 1 * target::kWordSize);
  addi_d(SP, SP, 2 * target::kWordSize);
}
void Assembler::EnterStubFrame() { EnterDartFrame(0); }
void Assembler::LeaveStubFrame() { LeaveDartFrame(); }

void Assembler::EnterDartFrame(intptr_t frame_size) {
  ASSERT(!constant_pool_allowed());

  if (FLAG_precompiled_mode) {
    EnterFrame(frame_size);
  } else {
    const intptr_t kFixedFrame =
        target::kWordSize * target::frame_layout.dart_fixed_frame_size;
    if (Utils::IsInt(12, -(frame_size + kFixedFrame))) {
      addi_d(SP, SP, -(frame_size + kFixedFrame));
    } else {
      addi_d(SP, SP, -kFixedFrame);
      addi_d(FP, SP, kFixedFrame);
      AddImmediate(SP, SP, -frame_size);
      LoadPoolPointer();
      set_constant_pool_allowed(true);
      return;
    }
    st_d(RA, SP, frame_size + 3 * target::kWordSize);
    st_d(FP, SP, frame_size + 2 * target::kWordSize);
    st_d(CODE_REG, SP, frame_size + 1 * target::kWordSize);
    addi_d(TMP, PP, kHeapObjectTag);
    st_d(TMP, SP, frame_size + 0 * target::kWordSize);
    addi_d(FP, SP, frame_size + kFixedFrame);
    LoadPoolPointer();
  }
  set_constant_pool_allowed(true);
}

void Assembler::LeaveDartFrame() {
  if (!FLAG_precompiled_mode) {
    ld_d(PP, FP, target::frame_layout.saved_caller_pp_from_fp *
                       target::kWordSize);
    addi_d(PP, PP, -kHeapObjectTag);
  }
  set_constant_pool_allowed(false);
  LeaveFrame();
}

void Assembler::EnterCFrame(intptr_t frame_space) {
  // Callee-saved registers already saved by compiler: THR, NULL_REG, etc.
  COMPILE_ASSERT(IsCalleeSavedRegister(THR));
  COMPILE_ASSERT(IsCalleeSavedRegister(NULL_REG));

  addi_d(SP, SP, -(frame_space + 4 * target::kWordSize));
  st_d(RA, SP, frame_space + 3 * target::kWordSize);
  st_d(FP, SP, frame_space + 2 * target::kWordSize);
  st_d(PP, SP, frame_space + 1 * target::kWordSize);
  st_d(ZR, SP, frame_space + 0 * target::kWordSize);  // 0-terminated frame link
  addi_d(FP, SP, frame_space + 4 * target::kWordSize);
  // andi is 32-bit on LoongArch; use 64-bit shift pair for SP alignment.
  srli_d(SP, SP, 4);
  slli_d(SP, SP, 4);
}

void Assembler::LeaveCFrame() {
  addi_d(SP, FP, -4 * target::kWordSize);
  ld_d(PP, SP, 1 * target::kWordSize);
  ld_d(FP, SP, 2 * target::kWordSize);
  ld_d(RA, SP, 3 * target::kWordSize);
  addi_d(SP, SP, 4 * target::kWordSize);
}

void Assembler::SetReturnAddress(Register value) {
  Move(RA, value);
}

void Assembler::PushRegister(Register r) {
  addi_d(SP, SP, -8);
  st_d(r, SP, 0);
}
void Assembler::PopRegister(Register r) {
  ld_d(r, SP, 0);
  addi_d(SP, SP, 8);
}
void Assembler::PushRegisterPair(Register r0, Register r1) {
  addi_d(SP, SP, -16);
  st_d(r0, SP, 0);
  st_d(r1, SP, 8);
}
void Assembler::PopRegisterPair(Register r0, Register r1) {
  ld_d(r0, SP, 0);
  ld_d(r1, SP, 8);
  addi_d(SP, SP, 16);
}
void Assembler::PushRegisters(const RegisterSet& regs) {
  intptr_t size = regs.SpillSize();
  if (size == 0) return;
  addi_d(SP, SP, -size);
  intptr_t offset = size;
  // Push FPU registers first (highest number at lowest address).
  for (intptr_t i = kNumberOfFpuRegisters - 1; i >= 0; i--) {
    FRegister reg = static_cast<FRegister>(i);
    if (regs.ContainsFpuRegister(reg)) {
      offset -= kFpuRegisterSize;
      fst_d(reg, SP, offset);
    }
  }
  // Then push CPU registers.
  for (intptr_t i = kNumberOfCpuRegisters - 1; i >= 0; i--) {
    Register reg = static_cast<Register>(i);
    if (regs.ContainsRegister(reg)) {
      offset -= target::kWordSize;
      st_d(reg, SP, offset);
    }
  }
  ASSERT(offset == 0);
}

void Assembler::PopRegisters(const RegisterSet& regs) {
  intptr_t size = regs.SpillSize();
  if (size == 0) return;
  intptr_t offset = 0;
  // Pop CPU registers first.
  for (intptr_t i = 0; i < kNumberOfCpuRegisters; i++) {
    Register reg = static_cast<Register>(i);
    if (regs.ContainsRegister(reg)) {
      ld_d(reg, SP, offset);
      offset += target::kWordSize;
    }
  }
  // Then pop FPU registers.
  for (intptr_t i = 0; i < kNumberOfFpuRegisters; i++) {
    FRegister reg = static_cast<FRegister>(i);
    if (regs.ContainsFpuRegister(reg)) {
      fld_d(reg, SP, offset);
      offset += kFpuRegisterSize;
    }
  }
  ASSERT(offset == size);
  addi_d(SP, SP, size);
}

void Assembler::PushNativeCalleeSavedRegisters() {
  RegisterSet regs(kAbiPreservedCpuRegs, kAbiPreservedFpuRegs);
  intptr_t cpu_count = 0;
  intptr_t fpu_count = 0;
  for (intptr_t i = 0; i < kNumberOfCpuRegisters; i++) {
    if (regs.ContainsRegister(static_cast<Register>(i))) cpu_count++;
  }
  for (intptr_t i = 0; i < kNumberOfFpuRegisters; i++) {
    if (regs.ContainsFpuRegister(static_cast<FRegister>(i))) fpu_count++;
  }
  intptr_t size = cpu_count * target::kWordSize + fpu_count * kFpuRegisterSize;
  addi_d(SP, SP, -size);
  intptr_t offset = 0;
  for (intptr_t i = 0; i < kNumberOfFpuRegisters; i++) {
    FRegister reg = static_cast<FRegister>(i);
    if (regs.ContainsFpuRegister(reg)) {
      fst_d(reg, SP, offset);
      offset += kFpuRegisterSize;
    }
  }
  for (intptr_t i = 0; i < kNumberOfCpuRegisters; i++) {
    Register reg = static_cast<Register>(i);
    if (regs.ContainsRegister(reg)) {
      st_d(reg, SP, offset);
      offset += target::kWordSize;
    }
  }
  ASSERT(offset == size);
}

void Assembler::PopNativeCalleeSavedRegisters() {
  RegisterSet regs(kAbiPreservedCpuRegs, kAbiPreservedFpuRegs);
  intptr_t cpu_count = 0;
  intptr_t fpu_count = 0;
  for (intptr_t i = 0; i < kNumberOfCpuRegisters; i++) {
    if (regs.ContainsRegister(static_cast<Register>(i))) cpu_count++;
  }
  for (intptr_t i = 0; i < kNumberOfFpuRegisters; i++) {
    if (regs.ContainsFpuRegister(static_cast<FRegister>(i))) fpu_count++;
  }
  intptr_t size = cpu_count * target::kWordSize + fpu_count * kFpuRegisterSize;
  intptr_t offset = 0;
  for (intptr_t i = 0; i < kNumberOfFpuRegisters; i++) {
    FRegister reg = static_cast<FRegister>(i);
    if (regs.ContainsFpuRegister(reg)) {
      fld_d(reg, SP, offset);
      offset += kFpuRegisterSize;
    }
  }
  for (intptr_t i = 0; i < kNumberOfCpuRegisters; i++) {
    Register reg = static_cast<Register>(i);
    if (regs.ContainsRegister(reg)) {
      ld_d(reg, SP, offset);
      offset += target::kWordSize;
    }
  }
  ASSERT(offset == size);
  addi_d(SP, SP, size);
}

void Assembler::Call(Register target) { jirl(RA, target, 0); }
void Assembler::Call(Label* label) { bl(label); }
void Assembler::Call(intptr_t target_code_pool_index, CodeEntryKind entry_kind) {
  ASSERT(constant_pool_allowed());
  // Load target from pool and call.
  LoadWordFromPoolIndex(TMP, target_code_pool_index);
  ld_d(TMP, TMP, 0);
  jirl(RA, TMP, 0);
}
void Assembler::JumpAndLink(const Code& code, ObjectPoolBuilderEntry::Patchability patchable, CodeEntryKind entry_kind, ObjectPoolBuilderEntry::SnapshotBehavior snapshot_behavior) { BranchLink(code, patchable, entry_kind, snapshot_behavior); }

void Assembler::BranchLink(
    const Code& target,
    ObjectPoolBuilderEntry::Patchability patchable,
    CodeEntryKind entry_kind,
    ObjectPoolBuilderEntry::SnapshotBehavior snapshot_behavior) {
  const intptr_t index = object_pool_builder().FindObject(
      ToObject(target), patchable, snapshot_behavior);
  JumpAndLink(index, entry_kind);
}

void Assembler::Call(Address address) {
  ld_d(TMP, address.base(), address.offset());
  jirl(RA, TMP, 0);
}

void Assembler::Jump(const Address& address) {
  ld_d(TMP2, address.base(), address.offset());
  jr(TMP2);
}

void Assembler::LoadIsolate(Register dst) {
  ld_d(dst, THR, target::Thread::isolate_offset());
}

void Assembler::LoadIsolateGroup(Register dst) {
  ld_d(dst, THR, target::Thread::isolate_group_offset());
}

void Assembler::LoadNativeEntry(Register dst, const ExternalLabel* label, ObjectPoolBuilderEntry::Patchability patchable) {
  const intptr_t index = object_pool_builder().FindNativeFunction(label, patchable);
  LoadWordFromPoolIndex(dst, index);
}

void Assembler::LoadPoolPointer(Register pp) {
  // CODE_REG holds a tagged Code pointer. Compensate for the tag in the
  // offset so we load from the correct field address.
  ld_d(pp, CODE_REG,
       target::Code::object_pool_offset() - kHeapObjectTag);
  // The ObjectPoolPtr stored in the Code object is tagged. Untag so that PP
  // points to the raw ObjectPool, making pool-relative loads single-
  // instruction for the first 4096 entries.
  addi_d(pp, pp, -kHeapObjectTag);
  set_constant_pool_allowed(pp == PP);
}

void Assembler::PushRegistersInOrder(std::initializer_list<Register> regs) {
  intptr_t offset = regs.size() * target::kWordSize;
  addi_d(SP, SP, -offset);
  for (Register reg : regs) {
    ASSERT(reg != SP);
    offset -= target::kWordSize;
    st_d(reg, SP, offset);
  }
}

void Assembler::CallRuntime(const RuntimeEntry& entry,
                         intptr_t argument_count,
                         bool tsan_enter_exit) {
  ASSERT(!entry.is_leaf());
  if (FLAG_target_thread_sanitizer && tsan_enter_exit) {
    TsanFuncEntry(false);
  }
  ld_d(T2, THR, entry.OffsetFromThread());
  LoadImmediate(T3, argument_count);
  Comment("Runtime call: %s", entry.name());
  Call(Address(THR, target::Thread::call_to_runtime_entry_point_offset()));
  if (FLAG_target_thread_sanitizer && tsan_enter_exit) {
    TsanFuncExit(false);
  }
}

void Assembler::Move(Register dst, Register src) { or_l(dst, src, ZR); }

void Assembler::MoveRegister(Register dst, Register src) { Move(dst, src); }

void Assembler::EnterOsrFrame(intptr_t extra_size, Register new_pp) {
  ASSERT(!constant_pool_allowed());
  Comment("EnterOsrFrame");
  RestoreCodePointer();
  LoadPoolPointer();
  if (extra_size > 0) {
    AddImmediate(SP, -extra_size);
  }
}

void Assembler::RestoreCodePointer() {
  // Load the code object reference from the frame.
  ld_d(CODE_REG, FP,
       target::frame_layout.code_from_fp * target::kWordSize);
}

void Assembler::TestImmediate(Register rn, intx_t imm, OperandSize sz) {
  andi(TMP2, rn, static_cast<uint32_t>(imm));
  CompareRegisters(TMP2, ZR);
}

void Assembler::CompareRegisters(Register rn, Register rm) {
  deferred_compare_ = kCompareReg;
  deferred_left_ = rn;
  deferred_reg_ = rm;
}
void Assembler::CompareObjectRegisters(Register rn, Register rm) {
  CompareRegisters(rn, rm);
}

void Assembler::CompareObject(Register reg, const Object& object) {
  ASSERT(IsOriginalObject(object));
  if (IsSameObject(compiler::NullObject(), object)) {
    CompareObjectRegisters(reg, NULL_REG);
  } else if (target::IsSmi(object)) {
    CompareImmediate(reg, target::ToRawSmi(object), kObjectBytes);
  } else {
    LoadObject(TMP, object);
    CompareObjectRegisters(reg, TMP);
  }
}
void Assembler::TestRegisters(Register rn, Register rm) {
  deferred_compare_ = kTestReg;
  deferred_left_ = rn;
  deferred_reg_ = rm;
}
void Assembler::BranchIfZero(Register rn, Label* label, JumpDistance d) {
  beqz(rn, label, d);
}
void Assembler::CompareWords(Register r1, Register r2, Label* not_equal) {
  bne(r1, r2, not_equal);
}

void Assembler::BranchIf(Condition condition, Label* label, JumpDistance d) {
  if (deferred_compare_ == kNone) { FATAL("BranchIf called without prior compare"); }
  Register left = deferred_left_;
  Register right;
  if (deferred_compare_ == kCompareImm) {
    if (deferred_imm_ == 0) {
      right = ZR;
    } else {
      LoadImmediate(TMP2, deferred_imm_);
      right = TMP2;
    }
  } else {
    right = deferred_reg_;
  }
  if (left == kNoRegister || right == kNoRegister) { FATAL("BranchIf: invalid register left=%d right=%d", left, right); }
  switch (condition) {
    case EQUAL: beq(left, right, label, d); break;
    case NOT_EQUAL: bne(left, right, label, d); break;
    case LESS: blt(left, right, label, d); break;
    case LESS_EQUAL: bge(right, left, label, d); break;
    case GREATER: blt(right, left, label, d); break;
    case GREATER_EQUAL: bge(left, right, label, d); break;
    case UNSIGNED_LESS: bltu(left, right, label, d); break;
    case UNSIGNED_LESS_EQUAL: bgeu(right, left, label, d); break;
    case UNSIGNED_GREATER: bltu(right, left, label, d); break;
    case UNSIGNED_GREATER_EQUAL: bgeu(left, right, label, d); break;
    default: UNREACHABLE();
  }
  deferred_compare_ = kNone;
}

void Assembler::BranchIfBit(Register rn, intptr_t bit_number, Condition condition, Label* label,
                            JumpDistance distance) {
  ASSERT(rn != TMP2);
  andi(TMP2, rn, 1 << bit_number);
  if (condition == ZERO) {
    beqz(TMP2, label, distance);
  } else if (condition == NOT_ZERO) {
    bnez(TMP2, label, distance);
  } else {
    UNREACHABLE();
  }
}

void Assembler::BranchIfNotSmi(Register reg, Label* label,
                               JumpDistance distance) {
  ASSERT(kSmiTagSize == 1);
  andi(TMP2, reg, kSmiTagMask);
  bnez(TMP2, label, distance);
}

void Assembler::BranchIfSmi(Register reg, Label* label, JumpDistance distance) {
  ASSERT(kSmiTagSize == 1);
  andi(TMP2, reg, kSmiTagMask);
  beqz(TMP2, label, distance);
}


void Assembler::Load(Register dst, const Address& address, OperandSize sz) {
  switch (sz) {
    case kEightBytes:
      ld_d(dst, address.base(), address.offset());
      break;
    case kUnsignedFourBytes:
      ld_wu(dst, address.base(), address.offset());
      break;
    case kFourBytes:
      ld_w(dst, address.base(), address.offset());
      break;
    case kUnsignedTwoBytes:
      ld_hu(dst, address.base(), address.offset());
      break;
    case kTwoBytes:
      ld_h(dst, address.base(), address.offset());
      break;
    case kUnsignedByte:
      ld_bu(dst, address.base(), address.offset());
      break;
    case kByte:
      ld_b(dst, address.base(), address.offset());
      break;
    default:
      UNREACHABLE();
  }
}
void Assembler::Store(Register src, const Address& address, OperandSize sz) {
  switch (sz) {
    case kEightBytes:
      st_d(src, address.base(), address.offset());
      break;
    case kUnsignedFourBytes:
    case kFourBytes:
      st_w(src, address.base(), address.offset());
      break;
    case kUnsignedTwoBytes:
    case kTwoBytes:
      st_h(src, address.base(), address.offset());
      break;
    case kUnsignedByte:
    case kByte:
      st_b(src, address.base(), address.offset());
      break;
    default:
      UNREACHABLE();
  }
}

void Assembler::LoadIndexedPayload(Register dest, Register base,
                                   int32_t offset, Register index,
                                   ScaleFactor scale, OperandSize sz) {
  AddScaled(TMP, base, index, scale, 0);
  Load(dest, Address(TMP, offset - kHeapObjectTag), sz);
}

void Assembler::LoadSFromOffset(FRegister dst, Register base, int32_t offset) {
  if (Utils::IsInt(12, offset)) {
    fld_s(dst, base, offset);
  } else {
    AddImmediate(TMP, base, offset);
    fld_s(dst, TMP, 0);
  }
}
void Assembler::LoadDFromOffset(FRegister dst, Register base, int32_t offset) {
  if (Utils::IsInt(12, offset)) {
    fld_d(dst, base, offset);
  } else {
    AddImmediate(TMP, base, offset);
    fld_d(dst, TMP, 0);
  }
}

void Assembler::StoreDToOffset(FRegister src, Register base, int32_t offset) {
  if (Utils::IsInt(12, offset)) {
    fst_d(src, base, offset);
  } else {
    AddImmediate(TMP, base, offset);
    fst_d(src, TMP, 0);
  }
}

void Assembler::LoadSImmediate(FRegister reg, float imms) {
  uint32_t imm = bit_cast<uint32_t, float>(imms);
  if (imm == 0) {
    movgr2fr_w(reg, ZR);
  } else if (constant_pool_allowed()) {
    intptr_t index = object_pool_builder().FindImmediate64(imm);
    intptr_t offset = target::ObjectPool::element_offset(index);
    LoadSFromOffset(reg, PP, offset);
  } else {
    LoadImmediate(TMP, static_cast<int32_t>(imm));
    movgr2fr_w(reg, TMP);
  }
}

void Assembler::LoadDImmediate(FRegister reg, double immd) {
  uint64_t imm = bit_cast<uint64_t, double>(immd);
  if (imm == 0) {
    movgr2fr_d(reg, ZR);
  } else if (constant_pool_allowed()) {
    intptr_t index = object_pool_builder().FindImmediate64(imm);
    intptr_t offset = target::ObjectPool::element_offset(index);
    LoadDFromOffset(reg, PP, offset);
  } else {
    LoadImmediate(TMP, static_cast<int64_t>(imm));
    movgr2fr_d(reg, TMP);
  }
}

void Assembler::LoadFromStack(Register dst, intptr_t depth) {
  ld_d(dst, SP, depth);
}
void Assembler::StoreToStack(Register src, intptr_t depth) {
  st_d(src, SP, depth);
}

void Assembler::CompareToStack(Register src, intptr_t depth) {
  LoadFromStack(TMP, depth);
  CompareRegisters(src, TMP);
}

void Assembler::CopyMemoryWords(Register src, Register dst, Register size, Register temp) {
  Label loop, done;
  beqz(size, &done, Assembler::kNearJump);
  Bind(&loop);
  ld_d(temp, src, 0);
  addi_d(src, src, target::kWordSize);
  st_d(temp, dst, 0);
  addi_d(dst, dst, target::kWordSize);
  addi_d(size, size, -target::kWordSize);
  bnez(size, &loop, Assembler::kNearJump);
  Bind(&done);
}

void Assembler::SmiTag(Register r) {
  ASSERT(kSmiTagShift == 1);
  slli_d(r, r, kSmiTagShift);
}


void Assembler::SmiUnTag(Register dst, Register src) {
  ASSERT(kSmiTagShift == 1);
  srai_d(dst, src, kSmiTagShift);
}

void Assembler::ExtendValue(Register rd, Register rn, OperandSize sz) {
  switch (sz) {
    case kEightBytes:
      if (rd != rn) Move(rd, rn);
      break;
    case kUnsignedFourBytes:
    case kFourBytes:
      slli_w(rd, rn, 0);  // sign-extend 32-bit to 64-bit
      break;
    default:
      UNIMPLEMENTED();
  }
}

void Assembler::ExtendAndSmiTagValue(Register rd, Register rn, OperandSize sz) {
  switch (sz) {
    case kEightBytes:
      slli_d(rd, rn, kSmiTagShift);
      return;
    case kUnsignedFourBytes:
      slli_d(rd, rn, 32);
      srli_d(rd, rd, 32 - kSmiTagShift);
      return;
    case kFourBytes:
      slli_d(rd, rn, 32);
      srai_d(rd, rd, 32 - kSmiTagShift);
      return;
    case kUnsignedTwoBytes:
      slli_d(rd, rn, 48);
      srli_d(rd, rd, 48 - kSmiTagShift);
      return;
    case kTwoBytes:
      slli_d(rd, rn, 48);
      srai_d(rd, rd, 48 - kSmiTagShift);
      return;
    default:
      UNIMPLEMENTED();
  }
}

void Assembler::LoadAcquire(Register dst, const Address& address, OperandSize size) {
  ASSERT(dst != address.base());
  Load(dst, address, size);
  // On LoongArch, dbar 0x14 provides a load-acquire semantic.
  dbar(0x14);
}

void Assembler::StoreRelease(Register src, const Address& address, OperandSize size) {
  ASSERT(src != address.base());
  // On LoongArch, dbar 0x12 provides a store-release semantic before the store.
  dbar(0x12);
  Store(src, address, size);
}

void Assembler::TsanLoadAcquire(Register dst, const Address& address) {
  Load(dst, address, kEightBytes);
}

void Assembler::TsanStoreRelease(Register src, const Address& address) {
  Store(src, address, kEightBytes);
}
void Assembler::TsanFuncEntry(bool preserve_registers) {
  // No-op when ThreadSanitizer is disabled.
  if (!FLAG_target_thread_sanitizer) return;
  UNIMPLEMENTED();
}

void Assembler::TsanFuncExit(bool preserve_registers) {
  if (!FLAG_target_thread_sanitizer) return;
  UNIMPLEMENTED();
}

void Assembler::ReserveAlignedFrameSpace(intptr_t frame_space) {
  if (frame_space != 0) {
    addi_d(SP, SP, -frame_space);
  }
  // andi is 32-bit on LoongArch; use 64-bit shift pair for SP alignment.
  srli_d(SP, SP, 4);
  slli_d(SP, SP, 4);
}

void Assembler::EmitEntryFrameVerification() {
  // Entry frame verification not implemented for LoongArch yet.
}

void Assembler::AddImmediate(Register rd, Register rs, intx_t value) {
  if (value == 0) {
    if (rd != rs) Move(rd, rs);
  } else if (Utils::IsInt(12, value)) {
    addi_d(rd, rs, value);
  } else {
    LoadImmediate(TMP, value);
    add_d(rd, rs, TMP);
  }
}

void Assembler::LoadImmediate(Register rd, intx_t value) {
  if (value == 0) { Move(rd, ZR); return; }
  uint64_t uv = static_cast<uint64_t>(value);
  // Use lu12iw + lu32id + lu52id sequence to load arbitrary 64-bit value
  int64_t adjusted = uv;
  intptr_t si20 = static_cast<intptr_t>((adjusted >> 12) & 0xfffff);
  // lu12i.w takes a signed 20-bit immediate.
  if (si20 >= (1 << 19)) si20 -= (1 << 20);
  lu12iw(rd, si20);
  // Set low 12 bits now, before lu32id/lu52id, because ori is a 32-bit
  // operation on LoongArch that zero-extends its result to 64 bits.
  const intptr_t low12 = static_cast<intptr_t>(adjusted & 0xfff);
  if (low12 != 0) {
    ori(rd, rd, low12);
  }
  // Next: load bits[51:32]
  if (uv >> 32) {
    si20 = static_cast<intptr_t>((uv >> 32) & 0xfffff);
    // lu32i.d takes a signed 20-bit immediate.
    if (si20 >= (1 << 19)) si20 -= (1 << 20);
    lu32id(rd, si20);
  } else if (adjusted & 0x80000000) {
    // lu12i.w sign-extends the loaded 20-bit value (bits [31:12])
    // into bits [63:32]. If the value should be zero-extended to 64 bits
    // but bit 31 is set, clear the upper 32 bits with lu32i.d.
    lu32id(rd, 0);
  }
  // Finally: load bits[63:52] with sign extension from bit 51
  if (uv >> 52) {
    intptr_t si12 = static_cast<intptr_t>((uv >> 52) & 0xfff);
    // lu52i.d takes a signed 12-bit immediate.
    if (si12 >= (1 << 11)) si12 -= (1 << 12);
    lu52id(rd, rd, si12);
  } else if (uv & 0x800000000000ULL) {
    // Sign extend from bit 51 explicitly
    lu52id(rd, rd, 0);
  }
}

void Assembler::AddImmediateBranchOverflow(Register rd, Register rs1,
                                           intx_t imm, Label* overflow) {
  ASSERT(rd != TMP2);
  if (rd == rs1) {
    Move(TMP2, rs1);
    AddImmediate(rd, rs1, imm);
    if (imm > 0) {
      blt(rd, TMP2, overflow);
    } else if (imm < 0) {
      bge(TMP2, rd, overflow);
    }
  } else {
    AddImmediate(rd, rs1, imm);
    if (imm > 0) {
      blt(rd, rs1, overflow);
    } else if (imm < 0) {
      bge(rs1, rd, overflow);
    }
  }
}

void Assembler::SubtractImmediateBranchOverflow(Register rd, Register rs1,
                                                intx_t imm, Label* overflow) {
  AddImmediateBranchOverflow(rd, rs1, -imm, overflow);
}

void Assembler::MultiplyImmediateBranchOverflow(Register rd, Register rs1,
                                                intx_t imm, Label* overflow) {
  ASSERT(rd != TMP);
  ASSERT(rd != TMP2);
  ASSERT(rs1 != TMP);
  ASSERT(rs1 != TMP2);
  LoadImmediate(TMP2, imm);
  mulh_d(TMP, rs1, TMP2);
  mul_d(rd, rs1, TMP2);
  srai_d(TMP2, rd, 63);
  bne(TMP, TMP2, overflow);
}

void Assembler::AddBranchOverflow(Register rd, Register rs1, Register rs2,
                                  Label* overflow) {
  ASSERT(rd != TMP);
  ASSERT(rd != TMP2);
  ASSERT(rs1 != TMP);
  ASSERT(rs1 != TMP2);
  ASSERT(rs2 != TMP);
  ASSERT(rs2 != TMP2);

  if (rd != rs1 && rd != rs2) {
    add_d(rd, rs1, rs2);
    slti(TMP, rs2, 0);
    slt(TMP2, rd, rs1);
    bne(TMP, TMP2, overflow);
  } else if (rd == rs1 && rd != rs2) {
    slti(TMP, rs2, 0);
    add_d(rd, rs1, rs2);
    slt(TMP2, rd, rs1);
    bne(TMP, TMP2, overflow);
  } else if (rd == rs2 && rd != rs1) {
    slti(TMP, rs2, 0);
    add_d(rd, rs1, rs2);
    slt(TMP2, rd, rs1);
    bne(TMP, TMP2, overflow);
  } else {
    ASSERT(rd == rs1 && rd == rs2);
    Move(TMP, rs1);
    add_d(rd, rs1, rs2);
    xor_l(TMP2, TMP, rd);
    srai_d(TMP2, TMP2, 63);
    bnez(TMP2, overflow);
  }
}

void Assembler::SubtractBranchOverflow(Register rd, Register rs1, Register rs2,
                                       Label* overflow) {
  ASSERT(rd != TMP);
  ASSERT(rd != TMP2);
  ASSERT(rs1 != TMP);
  ASSERT(rs1 != TMP2);
  ASSERT(rs2 != TMP);
  ASSERT(rs2 != TMP2);

  if (rd != rs1 && rd != rs2) {
    sub_d(rd, rs1, rs2);
    slti(TMP, rs2, 0);
    slt(TMP2, rs1, rd);
    bne(TMP, TMP2, overflow);
  } else {
    Move(TMP2, rs1);
    sub_d(rd, rs1, rs2);
    slti(TMP, rs2, 0);
    slt(TMP2, rs1, rd);
    bne(TMP, TMP2, overflow);
  }
}

void Assembler::MultiplyBranchOverflow(Register rd, Register rs1, Register rs2,
                                       Label* overflow) {
  ASSERT(rd != TMP);
  ASSERT(rd != TMP2);
  ASSERT(rs1 != TMP);
  ASSERT(rs1 != TMP2);
  ASSERT(rs2 != TMP);
  ASSERT(rs2 != TMP2);

  mulh_d(TMP, rs1, rs2);
  mul_d(rd, rs1, rs2);
  srai_d(TMP2, rd, 63);
  bne(TMP, TMP2, overflow);
}
void Assembler::CountLeadingZeroes(Register rd, Register rs) {
  // Use clz.w/clz.d if available, otherwise software implementation.
  // LoongArch does not have a native CLZ in the base ISA, so use software.
  // Algorithm: count leading zeros by binary search.
  Label done;
  LoadImmediate(rd, 64);
  Move(TMP, rs);
  // Check upper 32 bits.
  srli_d(TMP2, TMP, 32);
  beqz(TMP2, &done, kNearJump);
  addi_d(rd, rd, -32);
  Move(TMP, TMP2);
  Bind(&done);
  // Check upper 16 bits.
  srli_d(TMP2, TMP, 16);
  beqz(TMP2, &done, kNearJump);
  addi_d(rd, rd, -16);
  Move(TMP, TMP2);
  Bind(&done);
  // Check upper 8 bits.
  srli_d(TMP2, TMP, 8);
  beqz(TMP2, &done, kNearJump);
  addi_d(rd, rd, -8);
  Move(TMP, TMP2);
  Bind(&done);
  // Check upper 4 bits.
  srli_d(TMP2, TMP, 4);
  beqz(TMP2, &done, kNearJump);
  addi_d(rd, rd, -4);
  Move(TMP, TMP2);
  Bind(&done);
  // Check upper 2 bits.
  srli_d(TMP2, TMP, 2);
  beqz(TMP2, &done, kNearJump);
  addi_d(rd, rd, -2);
  Move(TMP, TMP2);
  Bind(&done);
  // Check top bit.
  srli_d(TMP2, TMP, 1);
  beqz(TMP2, &done, kNearJump);
  addi_d(rd, rd, -1);
  Bind(&done);
}
void Assembler::CompareWithMemoryValue(Register value,
                                       Address address, OperandSize size) {
  Load(TMP2, address, size);
  CompareRegisters(value, TMP2);
}

void Assembler::LoadWordFromPool(Register dst, int32_t offset) {
  ASSERT(constant_pool_allowed_);
  // Use PP-relative addressing (like ARM64 and RISC-V).
  // The PP register points to the global object pool.
  if (Utils::IsInt(12, offset)) {
    ld_d(dst, PP, offset);
  } else {
    AddImmediate(TMP, PP, offset);
    ld_d(dst, TMP, 0);
  }
}
void Assembler::LoadWordFromPoolIndex(Register dst, int32_t index) {
  LoadWordFromPool(dst, target::ObjectPool::element_offset(index));
}

void Assembler::StoreWordToPoolIndex(Register src,
                                     intptr_t index,
                                     Register pp) {
  ASSERT((pp != PP) || constant_pool_allowed());
  ASSERT(src != pp);
  // PP is untagged on LoongArch.
  const int32_t offset = target::ObjectPool::element_offset(index);
  if (Utils::IsInt(12, offset)) {
    st_d(src, pp, offset);
  } else {
    LoadImmediate(TMP, offset);
    add_d(TMP, TMP, pp);
    st_d(src, TMP, 0);
  }
}
void Assembler::LoadObject(Register dst, const Object& obj) {
  LoadWordFromPoolIndex(dst, object_pool_builder().FindObject(obj));
}
void Assembler::LoadUniqueObject(
    Register dst,
    const Object& obj,
    ObjectPoolBuilderEntry::SnapshotBehavior snapshot_behavior) {
  LoadWordFromPoolIndex(
      dst, object_pool_builder().AddObject(
               obj, ObjectPoolBuilderEntry::kPatchable, snapshot_behavior));
}

void Assembler::LoadIntoObject(Register dst, Register obj, int32_t offset) {
  ld_d(dst, obj, offset - kHeapObjectTag);
}

void Assembler::LoadField(Register dst, Register instance, int32_t offset) {
  ld_d(dst, instance, offset - kHeapObjectTag);
}

void Assembler::StoreIntoObject(Register object, const Address& address,
                                Register value) {
  StoreIntoObjectNoBarrier(object, address, value);
  StoreBarrier(object, value, kValueIsNotSmi, TMP);
}
void Assembler::StoreIntoObjectNoBarrier(Register object, const Address& address,
                                         Register value) {
  st_d(value, address.base(), address.offset());
}

void Assembler::LoadFieldAddressForOffset(Register address, Register instance,
                                          int32_t offset) {
  AddImmediate(address, instance, offset - kHeapObjectTag);
}

bool Assembler::AddressCanHoldConstantIndex(const Object& constant,
                                            bool is_external, intptr_t cid,
                                            intptr_t index_scale) {
  if (!IsSafeSmi(constant)) return false;
  const int64_t index = target::SmiValue(constant);
  const int64_t offset = index * index_scale + HeapDataOffset(is_external, cid);
  return Utils::IsInt(32, offset);
}
Address Assembler::ElementAddressForIntIndex(bool is_external, intptr_t cid,
                                             intptr_t index_scale,
                                             Register array,
                                             intptr_t index) const {
  const int64_t offset = index * index_scale + HeapDataOffset(is_external, cid);
  ASSERT(Utils::IsInt(32, offset));
  return Address(array, static_cast<int32_t>(offset));
}
void Assembler::ComputeElementAddressForIntIndex(Register address,
                                                 bool is_external, intptr_t cid,
                                                 intptr_t index_scale,
                                                 Register array,
                                                 intptr_t index) {
  const int64_t offset = index * index_scale + HeapDataOffset(is_external, cid);
  AddImmediate(address, array, offset);
}
Address Assembler::ElementAddressForRegIndex(bool is_external, intptr_t cid,
                                             intptr_t index_scale,
                                             bool index_unboxed,
                                             Register array, Register index,
                                             Register temp) {
  // If unboxed, index is expected smi-tagged, (i.e, LSL 1) for all arrays.
  const intptr_t boxing_shift = index_unboxed ? 0 : -kSmiTagShift;
  const intptr_t shift = Utils::ShiftForPowerOfTwo(index_scale) + boxing_shift;
  const int32_t offset = HeapDataOffset(is_external, cid);
  ASSERT(array != temp);
  ASSERT(index != temp);
  if (shift < 0) {
    ASSERT(shift == -1);
    srai_d(temp, index, 1);
    add_d(temp, array, temp);
  } else if (shift > 0) {
    slli_d(temp, index, shift);
    add_d(temp, array, temp);
  } else {
    add_d(temp, array, index);
  }
  return Address(temp, offset);
}
void Assembler::ComputeElementAddressForRegIndex(
    Register address, bool is_external, intptr_t cid, intptr_t index_scale,
    bool index_unboxed, Register array, Register index) {
  // If unboxed, index is expected smi-tagged, (i.e, LSL 1) for all arrays.
  const intptr_t boxing_shift = index_unboxed ? 0 : -kSmiTagShift;
  const intptr_t shift = Utils::ShiftForPowerOfTwo(index_scale) + boxing_shift;
  const int32_t offset = HeapDataOffset(is_external, cid);
  ASSERT(array != address);
  ASSERT(index != address);
  if (shift < 0) {
    ASSERT(shift == -1);
    srai_d(address, index, 1);
    add_d(address, array, address);
  } else if (shift > 0) {
    slli_d(address, index, shift);
    add_d(address, array, address);
  } else {
    add_d(address, array, index);
  }
  if (offset != 0) {
    AddImmediate(address, address, offset);
  }
}
void Assembler::LoadStaticFieldAddress(Register address, Register field,
                                       Register scratch, bool is_shared) {
  LoadCompressedSmiFieldFromOffset(
      scratch, field, target::Field::host_offset_or_field_id_offset());
  const intptr_t field_table_offset =
      is_shared ? compiler::target::Thread::shared_field_table_values_offset()
                : compiler::target::Thread::field_table_values_offset();
  LoadMemoryValue(address, THR, static_cast<int32_t>(field_table_offset));
  slli_d(scratch, scratch, target::kWordSizeLog2 - kSmiTagShift);
  add_d(address, address, scratch);
}
void Assembler::LoadFieldAddressForRegOffset(Register address,
                                             Register instance,
                                             Register offset_in_words_as_smi) {
  // offset_in_words_as_smi is Smi-tagged, so shift right to get word offset.
  ASSERT(kSmiTagShift == 1);
  srai_d(TMP, offset_in_words_as_smi, kSmiTagShift);
  slli_d(TMP, TMP, target::kWordSizeLog2);
  add_d(address, instance, TMP);
  addi_d(address, address, -kHeapObjectTag);
}
int32_t Assembler::HeapDataOffset(bool is_external, intptr_t cid) {
  return is_external
             ? 0
             : (target::Instance::DataOffsetFor(cid) - kHeapObjectTag);
}
void Assembler::SmiUntagOrCheck(Register tmp, Label* label) {
  Label ok;
  ASSERT(kSmiTagSize == 1);
  andi(TMP2, tmp, kSmiTagMask);
  beqz(TMP2, &ok, Assembler::kNearJump);
  b(label);
  Bind(&ok);
  srai_d(tmp, tmp, kSmiTagShift);
}
void Assembler::MaybePatchCodeStart(Register tmp) {
  // No-op in JIT mode. In precompiled mode, this adjusts the entry point.
  if (FLAG_precompiled_mode) {
    // For now, no patching needed on LoongArch.
  }
}

bool Assembler::CanLoadFromObjectPool(const Object& object) const {
  ASSERT(IsOriginalObject(object));
  if (!constant_pool_allowed()) {
    return false;
  }

  DEBUG_ASSERT(IsNotTemporaryScopedHandle(object));
  ASSERT(IsInOldSpace(object));
  return true;
}

void Assembler::LoadObjectHelper(Register dst, const Object& obj,
                                 bool is_unique) {
  ASSERT(IsOriginalObject(obj));
  // Optimized loads for common objects.
  if (!is_unique) {
    if (IsSameObject(compiler::NullObject(), obj)) {
      Move(dst, NULL_REG);
      return;
    }
    if (IsSameObject(CastHandle<Object>(compiler::TrueObject()), obj)) {
      addi_d(dst, NULL_REG, kTrueOffsetFromNull);
      return;
    }
    if (IsSameObject(CastHandle<Object>(compiler::FalseObject()), obj)) {
      addi_d(dst, NULL_REG, kFalseOffsetFromNull);
      return;
    }
    if (target::IsSmi(obj)) {
      LoadImmediate(dst, target::ToRawSmi(obj));
      return;
    }
    word offset = 0;
    if (target::CanLoadFromThread(obj, &offset)) {
      ld_d(dst, THR, offset);
      return;
    }
  }
  RELEASE_ASSERT(CanLoadFromObjectPool(obj));
  const intptr_t index = is_unique
      ? object_pool_builder().AddObject(obj)
      : object_pool_builder().FindObject(obj);
  LoadWordFromPoolIndex(dst, index);
}

void Assembler::JumpAndLink(intptr_t target_code_pool_index,
                            CodeEntryKind entry_kind) {
  // Avoid clobbering CODE_REG in precompiled mode.
  const Register code_reg = FLAG_precompiled_mode ? TMP : CODE_REG;
  LoadWordFromPoolIndex(code_reg, target_code_pool_index);
  ld_d(TMP, code_reg,
       target::Code::entry_point_offset(entry_kind));
  jirl(RA, TMP, 0);
}

}  // namespace compiler
}  // namespace dart

namespace dart {
namespace compiler {


void Assembler::MonomorphicCheckedEntryJIT() {
  has_monomorphic_entry_ = true;
  const intptr_t saved_far_branch_level = far_branch_level();
  set_far_branch_level(0);
  const intptr_t start = CodeSize();

  Label miss;
  Bind(&miss);
  ld_d(TMP, THR, target::Thread::switchable_call_miss_entry_offset());
  jr(TMP);

  Comment("MonomorphicCheckedEntry");
  ASSERT_EQUAL(CodeSize() - start,
               target::Instructions::kMonomorphicEntryOffsetJIT);

  Register entries_reg = IC_DATA_REG;  // Contains ICData::entries().
  const intptr_t cid_offset = target::Array::element_offset(0);
  const intptr_t count_offset = target::Array::element_offset(1);
  ASSERT(A1 != PP);
  ASSERT(A1 != entries_reg);
  ASSERT(A1 != CODE_REG);

  ld_d(TMP, entries_reg, cid_offset - kHeapObjectTag);
  LoadTaggedClassIdMayBeSmi(A1, A0);
  bne(TMP, A1, &miss, kNearJump);

  ld_d(TMP, entries_reg, count_offset - kHeapObjectTag);
  addi_d(TMP, TMP, target::ToRawSmi(1));
  st_d(TMP, entries_reg, count_offset - kHeapObjectTag);

  LoadImmediate(ARGS_DESC_REG, 0);  // GC-safe for OptimizeInvokedFunction

  // Fall through to unchecked entry.
  ASSERT_EQUAL(CodeSize() - start,
               target::Instructions::kPolymorphicEntryOffsetJIT);

  set_far_branch_level(saved_far_branch_level);
}

// A0 receiver, S5 guarded cid as Smi.
// Preserve S4 (ARGS_DESC_REG), not required today, but maybe later.
void Assembler::MonomorphicCheckedEntryAOT() {
  has_monomorphic_entry_ = true;
  intptr_t saved_far_branch_level = far_branch_level();
  set_far_branch_level(0);

  const intptr_t start = CodeSize();

  Label miss;
  Bind(&miss);
  ld_d(TMP, THR, target::Thread::switchable_call_miss_entry_offset());
  jr(TMP);

  Comment("MonomorphicCheckedEntry");
  ASSERT_EQUAL(CodeSize() - start,
               target::Instructions::kMonomorphicEntryOffsetAOT);
  LoadClassId(TMP, A0);
  SmiTag(TMP);
  bne(IC_DATA_REG, TMP, &miss, kNearJump);

  // Fall through to unchecked entry.
  ASSERT_EQUAL(CodeSize() - start,
               target::Instructions::kPolymorphicEntryOffsetAOT);

  set_far_branch_level(saved_far_branch_level);
}

void Assembler::BranchOnMonomorphicCheckedEntryJIT(Label* label) {
  has_monomorphic_entry_ = true;
  while (CodeSize() < target::Instructions::kMonomorphicEntryOffsetJIT) {
    break_();
  }
  b(label);
  while (CodeSize() < target::Instructions::kPolymorphicEntryOffsetJIT) {
    break_();
  }
}

void Assembler::LslImmediate(Register reg, int32_t shift, OperandSize sz) {
  ASSERT(sz == kEightBytes || sz == kFourBytes || sz == kUnsignedFourBytes);
  ASSERT((shift >= 0) && (shift < OperandSizeInBits(sz)));
  if (shift == 0) return;
  if (sz == kFourBytes || sz == kUnsignedFourBytes) {
    slli_w(reg, reg, shift);
  } else {
    slli_d(reg, reg, shift);
  }
}

void Assembler::LslImmediate(Register dst, Register src, int32_t shift, OperandSize sz) {
  ASSERT(sz == kEightBytes || sz == kFourBytes || sz == kUnsignedFourBytes);
  ASSERT((shift >= 0) && (shift < OperandSizeInBits(sz)));
  if (shift == 0) {
    if (dst != src) Move(dst, src);
    return;
  }
  if (sz == kFourBytes || sz == kUnsignedFourBytes) {
    slli_w(dst, src, shift);
  } else {
    slli_d(dst, src, shift);
  }
}

void Assembler::ArithmeticShiftRightImmediate(Register reg, int32_t shift, OperandSize sz) {
  ASSERT(sz == kEightBytes || sz == kFourBytes || sz == kUnsignedFourBytes);
  ASSERT((shift >= 0) && (shift < OperandSizeInBits(sz)));
  if (shift == 0) return;
  if (sz == kFourBytes || sz == kUnsignedFourBytes) {
    srai_w(reg, reg, shift);
  } else {
    srai_d(reg, reg, shift);
  }
}

void Assembler::ArithmeticShiftRightImmediate(Register dst, Register src, int32_t shift, OperandSize sz) {
  ASSERT(sz == kEightBytes || sz == kFourBytes || sz == kUnsignedFourBytes);
  ASSERT((shift >= 0) && (shift < OperandSizeInBits(sz)));
  if (shift == 0) {
    if (dst != src) Move(dst, src);
    return;
  }
  if (sz == kFourBytes || sz == kUnsignedFourBytes) {
    srai_w(dst, src, shift);
  } else {
    srai_d(dst, src, shift);
  }
}

void Assembler::LoadInt32FromBoxOrSmi(Register result, Register value) {
  Label done, is_smi;
  BranchIfSmi(value, &is_smi, Assembler::kNearJump);
  ld_w(result, value, target::Mint::value_offset() - kHeapObjectTag);
  b(&done);
  Bind(&is_smi);
  srai_d(result, value, kSmiTagShift);
  Bind(&done);
}

void Assembler::LoadInt64FromBoxOrSmi(Register result, Register value) {
  Label done, is_smi;
  BranchIfSmi(value, &is_smi, Assembler::kNearJump);
  ld_d(result, value, target::Mint::value_offset() - kHeapObjectTag);
  b(&done);
  Bind(&is_smi);
  srai_d(result, value, kSmiTagShift);
  Bind(&done);
}

void Assembler::AddScaled(Register dst, Register base, Register index, ScaleFactor scale, int32_t disp) {
  ASSERT(scale < TIMES_16);
  if (scale > TIMES_1) {
    int shift = 0;
    if (scale == TIMES_2) shift = 1;
    else if (scale == TIMES_4) shift = 2;
    else if (scale == TIMES_8) shift = 3;
    slli_d(TMP, index, shift);
    add_d(TMP, base, TMP);
  } else {
    add_d(TMP, base, index);
  }
  if (disp == 0) {
    if (dst != TMP) Move(dst, TMP);
  } else {
    AddImmediate(dst, TMP, disp);
  }
}

void Assembler::CompareImmediate(Register reg, target::word imm, OperandSize width) {
  ASSERT(deferred_compare_ == kNone);
  deferred_compare_ = kCompareImm;
  deferred_left_ = reg;
  deferred_imm_ = imm;
}

void Assembler::AndImmediate(Register reg, target::word imm, OperandSize sz) {
  ASSERT(sz == kEightBytes || sz == kUnsignedFourBytes || sz == kFourBytes);
  uintx_t uimm = (sz == kFourBytes || sz == kUnsignedFourBytes) ? static_cast<uint32_t>(imm) : imm;
  if (uimm == 0) {
    Move(reg, ZR);
  } else if (Utils::IsUint(12, uimm)) {
    andi(reg, reg, uimm);
  } else {
    LoadImmediate(TMP, uimm);
    and_l(reg, reg, TMP);
  }
}

void Assembler::OrImmediate(Register dst, Register src, target::word imm, OperandSize sz) {
  if (imm == 0) {
    Move(dst, src);
  } else if (sz != kEightBytes && Utils::IsUint(12, imm)) {
    // ori is a 32-bit operation on LoongArch that zero-extends to 64 bits.
    // For kEightBytes, always use or_l to preserve upper 32 bits.
    ori(dst, src, imm);
  } else {
    ASSERT(src != TMP2);
    LoadImmediate(TMP2, imm);
    or_l(dst, src, TMP2);
  }
}

void Assembler::AndImmediate(Register dst, Register src, target::word imm, OperandSize sz) {
  ASSERT(sz == kEightBytes || sz == kUnsignedFourBytes || sz == kFourBytes);
  uintx_t uimm = (sz == kFourBytes || sz == kUnsignedFourBytes) ? static_cast<uint32_t>(imm) : imm;
  if (uimm == 0) {
    Move(dst, ZR);
  } else if (Utils::IsUint(12, uimm)) {
    andi(dst, src, uimm);
  } else {
    LoadImmediate(TMP, uimm);
    and_l(dst, src, TMP);
  }
}

void Assembler::LsrImmediate(Register dst, int32_t shift) {
  ASSERT(shift >= 0);
  if (shift == 0) return;
  srli_d(dst, dst, shift);
}

void Assembler::MulImmediate(Register dst, target::word imm, OperandSize sz) {
  if (imm == 0) {
    Move(dst, ZR);
  } else if (Utils::IsPowerOfTwo(imm)) {
    const intx_t shift = Utils::ShiftForPowerOfTwo(imm);
    if (sz == kFourBytes) {
      slli_w(dst, dst, shift);
    } else {
      slli_d(dst, dst, shift);
    }
  } else {
    LoadImmediate(TMP, imm);
    if (sz == kFourBytes) {
      mul_w(dst, dst, TMP);
    } else {
      mul_d(dst, dst, TMP);
    }
  }
}

void Assembler::AndRegisters(Register dst, Register src1, Register src2) {
  if (src2 == kNoRegister) {
    and_l(dst, dst, src1);
  } else {
    and_l(dst, src1, src2);
  }
}

void Assembler::LslRegister(Register dst, Register shift) {
  sll_d(dst, dst, shift);
}

void Assembler::ExtractBitField(Register dst, Register src, intptr_t low_bit, intptr_t width) {
  ASSERT((0 <= low_bit) && (low_bit + width <= compiler::target::kBitsPerWord));
  if (width == 1 && low_bit == 0) {
    andi(dst, src, 1);
    return;
  }
  intptr_t left_shift = compiler::target::kBitsPerWord - low_bit - width;
  intptr_t right_shift = compiler::target::kBitsPerWord - width;
  if (left_shift == 0) {
    srli_d(dst, src, right_shift);
  } else {
    slli_d(dst, src, left_shift);
    srli_d(dst, dst, right_shift);
  }
}

void Assembler::CombineHashes(Register hash, Register other) {
  add_d(hash, hash, other);
  slli_d(other, hash, 10);
  add_d(hash, hash, other);
  srli_d(other, hash, 6);
  xor_l(hash, hash, other);
}

void Assembler::FinalizeHashForSize(intptr_t bit_size, Register hash, Register scratch) {
  ASSERT(bit_size > 0);
  ASSERT(bit_size <= kBitsPerInt32);
  ASSERT(scratch != kNoRegister);
  slli_d(scratch, hash, 3);
  add_d(hash, hash, scratch);
  srli_d(scratch, hash, 11);
  xor_l(hash, hash, scratch);
  slli_d(scratch, hash, 15);
  add_d(hash, hash, scratch);
  if (bit_size < kBitsPerInt32) {
    LslImmediate(hash, hash, kBitsPerInt32 - bit_size, kFourBytes);
    LsrImmediate(hash, kBitsPerInt32 - bit_size);
  }
  LslImmediate(hash, hash, kSmiTagShift, kFourBytes);
}


void Assembler::CompareClassId(Register object, intptr_t class_id, Register scratch) {
  ASSERT(scratch != kNoRegister);
  LoadClassId(scratch, object);
  CompareImmediate(scratch, class_id);
}


void Assembler::ExtractClassIdFromTags(Register result, Register tags) {
  ASSERT(target::UntaggedObject::kClassIdTagPos == 12);
  ASSERT(target::UntaggedObject::kClassIdTagSize == 20);
  srli_w(result, tags, target::UntaggedObject::kClassIdTagPos);
}

void Assembler::ExtractInstanceSizeFromTags(Register result, Register tags) {
  ASSERT(target::UntaggedObject::kSizeTagPos == 8);
  ASSERT(target::UntaggedObject::kSizeTagSize == 4);
  srli_d(result, tags, target::UntaggedObject::kSizeTagPos);
  andi(result, result, (1 << target::UntaggedObject::kSizeTagSize) - 1);
  slli_d(result, result, target::ObjectAlignment::kObjectAlignmentLog2);
}

void Assembler::LoadClassId(Register result, Register object) {
  ld_wu(result, object, target::Object::tags_offset() - kHeapObjectTag);
  srli_d(result, result, target::UntaggedObject::kClassIdTagPos);
}

void Assembler::LoadClassIdMayBeSmi(Register result, Register object) {
  ASSERT(result != object);
  LoadImmediate(result, kSmiCid);
  Label done;
  BranchIfSmi(object, &done, Assembler::kNearJump);
  LoadClassId(result, object);
  Bind(&done);
}

void Assembler::LoadClassById(Register result, Register class_id) {
  ASSERT(result != class_id);
  const intptr_t table_offset =
      target::IsolateGroup::cached_class_table_table_offset();
  LoadIsolateGroup(result);
  LoadFromOffset(result, result, table_offset);
  AddScaled(result, result, class_id, TIMES_8, 0);
  ld_d(result, result, 0);
}

void Assembler::LoadTaggedClassIdMayBeSmi(Register result, Register object) {
  LoadClassIdMayBeSmi(result, object);
  SmiTag(result);
}
void Assembler::EnsureHasClassIdInDEBUG(intptr_t cid, Register src, Register scratch, bool can_be_null) {
#if defined(DEBUG)
  Comment("Check that object in register has cid %" Pd "", cid);
  Label matches;
  LoadClassIdMayBeSmi(scratch, src);
  CompareImmediate(scratch, cid);
  BranchIf(EQUAL, &matches, Assembler::kNearJump);
  if (can_be_null) {
    CompareImmediate(scratch, kNullCid);
    BranchIf(EQUAL, &matches, Assembler::kNearJump);
  }
  Stop("Object did not have expected cid");
  Bind(&matches);
#endif
}

void Assembler::RangeCheck(Register value, Register temp, intptr_t low, intptr_t high, RangeCheckCondition condition, Label* target) {
  auto cc = condition == kIfInRange ? LS : HI;
  Register to_check = temp != kNoRegister ? temp : value;
  AddImmediate(to_check, value, -low);
  CompareImmediate(to_check, static_cast<target::word>(high - low));
  BranchIf(cc, target);
}

void Assembler::StoreBarrier(Register object, Register value, CanBeSmi can_value_be_smi, Register scratch) {
  ASSERT(object != value);
  Label done;
  if (can_value_be_smi == kValueCanBeSmi) {
    BranchIfSmi(value, &done);
  }
  LoadImmediate(scratch, target::Thread::write_barrier_mask_offset());
  add_d(scratch, THR, scratch);
  ld_d(scratch, scratch, 0);
  and_l(scratch, scratch, object);
  beqz(scratch, &done);
  Comment("Store barrier needed");
  const intptr_t store_buffer_offset = target::Thread::store_buffer_block_offset();
  addi_d(scratch, THR, store_buffer_offset);
  st_d(object, scratch, 0);
  Bind(&done);
}

void Assembler::ArrayStoreBarrier(Register object, Register slot, Register value, CanBeSmi can_value_be_smi, Register scratch) {
  StoreBarrier(object, value, can_value_be_smi, scratch);
}

void Assembler::VerifyStoreNeedsNoWriteBarrier(Register object, Register value) {
  Label done;
  BranchIfSmi(value, &done);
  LoadImmediate(TMP, target::Thread::write_barrier_mask_offset());
  add_d(TMP, THR, TMP);
  ld_d(TMP, TMP, 0);
  and_l(TMP, TMP, object);
  beqz(TMP, &done);
  Stop("Write barrier is needed but was assumed not needed");
  Bind(&done);
}
#if defined(DART_COMPRESSED_POINTERS)
void Assembler::LoadAcquireCompressed(Register dst, const Address& address) {
  ld_d(dst, address.base(), address.offset());
  // No acquire barrier needed on LOONG64
}

void Assembler::LoadCompressed(Register dst, const Address& address) {
  ld_wu(dst, address.base(), address.offset());
}
#endif  // defined(DART_COMPRESSED_POINTERS)
void Assembler::CompareWords(Register reg1,
                             Register reg2,
                             intptr_t offset,
                             Register count,
                             Register temp,
                             Label* equals) {
  Label loop;
  Bind(&loop);
  BranchIfZero(count, equals, Assembler::kNearJump);
  AddImmediate(count, -1);
  ld_d(temp, reg1, offset);
  ld_d(TMP, reg2, offset);
  addi_d(reg1, reg1, target::kWordSize);
  addi_d(reg2, reg2, target::kWordSize);
  beq(temp, TMP, &loop, Assembler::kNearJump);
}


#if !defined(PRODUCT)
void Assembler::MaybeTraceAllocation(intptr_t cid,
                                     Label* trace,
                                     Register temp_reg,
                                     JumpDistance distance) {
  ASSERT(cid > 0);
  LoadIsolateGroup(temp_reg);
  ld_d(temp_reg, temp_reg, target::IsolateGroup::class_table_offset());
  ld_d(temp_reg, temp_reg, target::ClassTable::allocation_tracing_state_table_offset());
  LoadFromOffset(temp_reg, temp_reg,
                 target::ClassTable::AllocationTracingStateSlotOffsetFor(cid),
                 kUnsignedByte);
  bnez(temp_reg, trace);
}

void Assembler::MaybeTraceAllocation(Register cid,
                                     Label* trace,
                                     Register temp_reg,
                                     JumpDistance distance) {
  LoadIsolateGroup(temp_reg);
  ld_d(temp_reg, temp_reg, target::IsolateGroup::class_table_offset());
  ld_d(temp_reg, temp_reg, target::ClassTable::allocation_tracing_state_table_offset());
  add_d(temp_reg, temp_reg, cid);
  LoadFromOffset(temp_reg, temp_reg,
                 target::ClassTable::AllocationTracingStateSlotOffsetFor(0),
                 kUnsignedByte);
  bnez(temp_reg, trace);
}
#endif  // !PRODUCT

void Assembler::TryAllocateObject(intptr_t cid,
                                  intptr_t instance_size,
                                  Label* failure,
                                  JumpDistance distance,
                                  Register instance_reg,
                                  Register temp_reg) {
  ASSERT(failure != nullptr);
  ASSERT(instance_size != 0);
  ASSERT(instance_reg != temp_reg);
  ASSERT(temp_reg != kNoRegister);
  ASSERT(Utils::IsAligned(instance_size,
                          target::ObjectAlignment::kObjectAlignment));
  if (FLAG_inline_alloc &&
      target::Heap::IsAllocatableInNewSpace(instance_size)) {
    // If this allocation is traced, program will jump to failure path
    // (i.e. the allocation stub) which will allocate the object and trace the
    // allocation call site.
    NOT_IN_PRODUCT(MaybeTraceAllocation(cid, failure, temp_reg));

    ld_d(instance_reg, THR, target::Thread::top_offset());
    ld_d(temp_reg, THR, target::Thread::end_offset());
    // instance_reg: current top (next object start).
    // temp_reg: heap end

    // TODO(koda): Protect against unsigned overflow here.
    AddImmediate(instance_reg, instance_size);
    // instance_reg: potential top (next object start).
    // fail if heap end unsigned less than or equal to new heap top.
    bgeu(instance_reg, temp_reg, failure, distance);
    CheckAllocationCanary(instance_reg, temp_reg);

    // Successfully allocated the object, now update temp to point to
    // next object start and store the class in the class field of object.
    st_d(instance_reg, THR, target::Thread::top_offset());
    // Move instance_reg back to the start of the object and tag it.
    AddImmediate(instance_reg, -instance_size + kHeapObjectTag);

    const uword tags = target::MakeTagWordForNewSpaceObject(cid, instance_size);
    LoadImmediate(temp_reg, tags);
    InitializeHeader(temp_reg, instance_reg);
  } else {
    Jump(failure, distance);
  }
}

void Assembler::TryAllocateArray(intptr_t cid,
                                 Label* failure,
                                 JumpDistance distance,
                                 Register instance,
                                 Register length_reg,
                                 Register type_args_reg,
                                 Register temp1,
                                 Register temp2) {
  if (FLAG_inline_alloc) {
    BranchIfNotSmi(length_reg, failure);
    const intptr_t max_len =
        target::ToRawSmi(target::Array::kMaxNewSpaceElements);
    CompareImmediate(length_reg, max_len);
    BranchIf(HI, failure, distance);
    SmiUnTag(temp1, length_reg);
    slli_d(temp1, temp1, target::kCompressedWordSizeLog2);
    const intptr_t fixed_size =
        target::Array::header_size() +
        target::ObjectAlignment::kObjectAlignment - 1;
    AddImmediate(temp1, temp1, fixed_size);
    AndImmediate(temp1, temp1, ~(target::ObjectAlignment::kObjectAlignment - 1));
    ld_d(instance, THR, target::Thread::top_offset());
    add_d(temp2, instance, temp1);
    bltu(temp2, instance, failure);
    ld_d(TMP, THR, target::Thread::end_offset());
    bgeu(temp2, TMP, failure);
    CheckAllocationCanary(instance);
    st_d(temp2, THR, target::Thread::top_offset());
    AddImmediate(instance, instance, kHeapObjectTag);
    {
      uword tags = target::MakeTagWordForNewSpaceObject(cid, 0);
      LoadImmediate(TMP, tags);
      InitializeHeader(TMP, instance);
    }
    ret();
  } else {
    b(failure);
  }
}


static const RegisterSet kRuntimeCallSavedRegisters(kDartVolatileCpuRegs,
                                                    kDartVolatileFpuRegs);

#define __ assembler_->

LeafRuntimeScope::LeafRuntimeScope(Assembler* assembler,
                                   intptr_t frame_size,
                                   bool preserve_registers)
    : assembler_(assembler), preserve_registers_(preserve_registers) {
  __ addi_d(SP, SP, -4 * target::kWordSize);
  __ st_d(RA, SP, 3 * target::kWordSize);
  __ st_d(FP, SP, 2 * target::kWordSize);
  __ st_d(CODE_REG, SP, 1 * target::kWordSize);
  __ st_d(PP, SP, 0 * target::kWordSize);
  __ addi_d(FP, SP, 4 * target::kWordSize);

  if (preserve_registers) {
    __ PushRegisters(kRuntimeCallSavedRegisters);
  } else {
    COMPILE_ASSERT(!IsAbiPreservedRegister(CODE_REG));
    COMPILE_ASSERT(!IsAbiPreservedRegister(PP));
    COMPILE_ASSERT(IsCalleeSavedRegister(THR));
    COMPILE_ASSERT(IsCalleeSavedRegister(NULL_REG));
    COMPILE_ASSERT(IsCalleeSavedRegister(HEAP_BITS));
  }

  __ ReserveAlignedFrameSpace(frame_size);
}

void LeafRuntimeScope::Call(const RuntimeEntry& entry,
                            intptr_t argument_count) {
  ASSERT(argument_count == entry.argument_count());
  __ ld_d(TMP2, THR, entry.OffsetFromThread());
  __ st_d(TMP2, THR, target::Thread::vm_tag_offset());
  __ Comment("Leaf runtime call: %s", entry.name());
  __ jirl(RA, TMP2, 0);
  __ LoadImmediate(TMP2, VMTag::kDartTagId);
  __ st_d(TMP2, THR, target::Thread::vm_tag_offset());
}

LeafRuntimeScope::~LeafRuntimeScope() {
  if (preserve_registers_) {
    __ addi_d(SP, FP,
              -kRuntimeCallSavedRegisters.SpillSize() - 4 * target::kWordSize);
    __ PopRegisters(kRuntimeCallSavedRegisters);
  }

  __ addi_d(SP, FP, -4 * target::kWordSize);
  __ ld_d(PP, SP, 0 * target::kWordSize);
  __ ld_d(CODE_REG, SP, 1 * target::kWordSize);
  __ ld_d(FP, SP, 2 * target::kWordSize);
  __ ld_d(RA, SP, 3 * target::kWordSize);
  __ addi_d(SP, SP, 4 * target::kWordSize);
}

#undef __

void Assembler::StoreObjectIntoObjectNoBarrier(Register object,
                                               const Address& address,
                                               const Object& value,
                                               MemoryOrder memory_order,
                                               OperandSize size) {
  ASSERT(IsOriginalObject(value));
  DEBUG_ASSERT(IsNotTemporaryScopedHandle(value));
  Register src = kNoRegister;
  if (IsSameObject(compiler::NullObject(), value)) {
    src = NULL_REG;
  } else if (target::IsSmi(value) && (target::ToRawSmi(value) == 0)) {
    src = ZR;
  } else {
    src = TMP;
    ASSERT(object != src);
    LoadObject(src, value);
  }
  if (memory_order == kRelease) {
    StoreRelease(src, address, size);
  } else {
    Store(src, address, size);
  }
}

void Assembler::GenerateUnRelocatedPcRelativeCall(intptr_t offset_into_target) {
  pcaddu12i(RA, 0);
  jirl_fixed(RA, RA, 0);

  PcRelativeCallPattern pattern(buffer_.contents() + buffer_.Size() -
                                PcRelativeCallPattern::kLengthInBytes);
  pattern.set_distance(offset_into_target);
}

void Assembler::GenerateUnRelocatedPcRelativeTailCall(
    intptr_t offset_into_target) {
  pcaddu12i(TMP, 0);
  jirl_fixed(ZR, TMP, 0);

  PcRelativeTailCallPattern pattern(buffer_.contents() + buffer_.Size() -
                                    PcRelativeTailCallPattern::kLengthInBytes);
  pattern.set_distance(offset_into_target);
}

void Assembler::StoreSToOffset(FRegister src, Register base, int32_t offset) {
  if (Utils::IsInt(12, offset)) {
    fst_s(src, base, offset);
  } else {
    AddImmediate(TMP, base, offset);
    fst_s(src, TMP, 0);
  }
}

void Assembler::RestorePoolPointer() {
  if (FLAG_precompiled_mode) {
    ld_d(PP, THR, target::Thread::global_object_pool_offset());
  } else {
    ld_d(PP, FP, target::frame_layout.code_from_fp * target::kWordSize);
    ld_d(PP, PP, target::Code::object_pool_offset());
  }
  AddImmediate(PP, PP, -kHeapObjectTag);  // Pool in PP is untagged!
}

void Assembler::SetupGlobalPoolAndDispatchTable() {
  ASSERT(FLAG_precompiled_mode);
  ld_d(PP, THR, target::Thread::global_object_pool_offset());
  AddImmediate(PP, PP, -kHeapObjectTag);  // Pool in PP is untagged!
  ld_d(DISPATCH_TABLE_REG, THR, target::Thread::dispatch_table_array_offset());
}

void Assembler::EnterFullSafepoint(Register state) {
  // Always use the slow path which is correct and avoids needing
  // LL/SC atomic instructions (not yet implemented for LoongArch).
  Register addr = TMP2;
  ASSERT(addr != state);
  ld_d(addr, THR, target::Thread::enter_safepoint_stub_offset());
  ld_d(addr, addr, target::Code::entry_point_offset());
  jirl(RA, addr, 0);
}

void Assembler::ExitFullSafepoint(Register state) {
  // Always use the slow path for consistency with EnterFullSafepoint.
  Register addr = TMP2;
  ASSERT(addr != state);
  ld_d(addr, THR, target::Thread::exit_safepoint_stub_offset());
  ld_d(addr, addr, target::Code::entry_point_offset());
  jirl(RA, addr, 0);
}

void Assembler::RestorePinnedRegisters() {
  ld_d(WRITE_BARRIER_STATE, THR,
       target::Thread::write_barrier_mask_offset());
  ld_d(NULL_REG, THR, target::Thread::object_null_offset());
  // Our write barrier uses mask-and-test; adjust WRITE_BARRIER_STATE
  // for the compare-and-branch instruction pattern.
  xori(WRITE_BARRIER_STATE, WRITE_BARRIER_STATE,
       (target::UntaggedObject::kGenerationalBarrierMask << 1) - 1);
  // Generational bit must be higher than incremental bit, with no other bits
  // between.
  ASSERT(target::UntaggedObject::kGenerationalBarrierMask ==
         (target::UntaggedObject::kIncrementalBarrierMask << 1));
  // Other header bits must be lower.
  ASSERT(target::UntaggedObject::kIncrementalBarrierMask >
         target::UntaggedObject::kCanonicalBit);
  ASSERT(target::UntaggedObject::kIncrementalBarrierMask >
         target::UntaggedObject::kCardRememberedBit);
}


void Assembler::TransitionGeneratedToNative(Register destination,
                                            Register new_exit_frame,
                                            Register new_exit_through_ffi,
                                            bool enter_safepoint) {
  // Save exit frame information to enable stack walking.
  StoreToOffset(new_exit_frame, THR,
                target::Thread::top_exit_frame_info_offset());
  StoreToOffset(new_exit_through_ffi, THR,
                target::Thread::exit_through_ffi_offset());
  Register tmp = new_exit_through_ffi;
  // Mark that the thread is executing native code.
  StoreToOffset(destination, THR, target::Thread::vm_tag_offset());
  LoadImmediate(tmp, target::Thread::native_execution_state());
  StoreToOffset(tmp, THR, target::Thread::execution_state_offset());
  if (enter_safepoint) {
    EnterFullSafepoint(tmp);
  }
}

void Assembler::TransitionNativeToGenerated(Register state,
                                            bool exit_safepoint,
                                            bool set_tag) {
  if (exit_safepoint) {
    ExitFullSafepoint(state);
  }
  // Mark that the thread is executing Dart code.
  if (set_tag) {
    LoadImmediate(state, target::Thread::vm_tag_dart_id());
    StoreToOffset(state, THR, target::Thread::vm_tag_offset());
  }
  LoadImmediate(state, target::Thread::generated_execution_state());
  StoreToOffset(state, THR, target::Thread::execution_state_offset());
  // Reset exit frame information in Isolate mutator thread structure.
  StoreToOffset(ZR, THR, target::Thread::top_exit_frame_info_offset());
  StoreToOffset(ZR, THR, target::Thread::exit_through_ffi_offset());
}

}  // namespace compiler
}  // namespace dart
#endif  // defined(TARGET_ARCH_LOONG64)
