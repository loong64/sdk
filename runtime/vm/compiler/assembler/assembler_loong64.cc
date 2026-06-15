// Copyright (c) 2017, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(TARGET_ARCH_LOONG64)

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
  inline uint32_t Rd(Register r) { return static_cast<uint32_t>(r) << 0; }
  inline uint32_t Rj(Register r) { return static_cast<uint32_t>(r) << 5; }
  inline uint32_t Rk(Register r) { return static_cast<uint32_t>(r) << 10; }
  inline uint32_t FRd(FRegister r) { return static_cast<uint32_t>(r) << 0; }
  inline uint32_t FRj(FRegister r) { return static_cast<uint32_t>(r) << 5; }
  inline uint32_t FRk(FRegister r) { return static_cast<uint32_t>(r) << 10; }
  inline uint32_t Si12(intptr_t imm) { return (static_cast<uint32_t>(imm) & 0xfff) << 10; }
  inline uint32_t Ui12(intptr_t imm) { return (static_cast<uint32_t>(imm) & 0xfff) << 10; }
  inline uint32_t Si20(intptr_t imm) { return (static_cast<uint32_t>(imm) & 0xfffff) << 5; }
  inline uint32_t Si16(intptr_t imm) { return (static_cast<uint32_t>(imm) & 0xffff) << 10; }
  inline uint32_t B12(intptr_t imm) { return (static_cast<uint32_t>(imm) & 0xfff) << 10; }
  inline uint32_t Offs16(intptr_t imm) { return (static_cast<uint32_t>(imm) & 0xffff) << 10; }
  inline uint32_t Ui5(intptr_t imm) { return (static_cast<uint32_t>(imm) & 0x1f) << 10; }
  inline uint32_t Ui6(intptr_t imm) { return (static_cast<uint32_t>(imm) & 0x3f) << 10; }
}
using namespace loong_enc;

#define EMIT(enc) Emit(enc)

MicroAssembler::MicroAssembler(ObjectPoolBuilder* ob, intptr_t fl)
    : AssemblerBase(ob), far_branch_level_(fl) {}
MicroAssembler::~MicroAssembler() {}
void MicroAssembler::Emit(uint32_t encoding) {
  AssemblerBuffer::EnsureCapacity c(&buffer_);
  AssemblerBuffer::Emit<int32_t>(encoding);
}
void MicroAssembler::Bind(Label* label) { AssemblerBase::Bind(label); }

// ==== LoongArch PC-relative address generation ====
// opcode 000110 (6): pcaddu12i, pcalau12i
// opcode 000111 (7): pcaddu18i
// Format: |31:26=opc|25:5=si20|4:0=rd|
void MicroAssembler::pcaddu12i(Register rd, intptr_t si20) {
  EMIT(Si20(si20) | Rd(rd) | 0x18000000);
}
void MicroAssembler::pcalau12i(Register rd, intptr_t si20) {
  EMIT(Si20(si20) | Rd(rd) | 0x1A000000);
}
void MicroAssembler::pcaddu18i(Register rd, intptr_t si20) {
  EMIT(Si20(si20) | Rd(rd) | 0x1C000000);
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
  EMIT(Si12(si12) | Rj(rj) | Rd(rd) | 0x32000000);
}

// ==== Jumps ====
// JIRL: |31:26=010011|25:10=offs16|9:5=rj|4:0=rd|
void MicroAssembler::jirl(Register rd, Register rj, intptr_t si12) {
  EMIT(Offs16(si12) | Rj(rj) | Rd(rd) | 0x4C000000);
}
void MicroAssembler::jirl_fixed(Register rd, Register rj, intptr_t si12) { jirl(rd, rj, si12); }

// ==== Branches ====
// BEQZ: |31:26=010000|25:10=offs[15:0]|9:5=rj|4:0=offs[20:16]
// BNEZ: |31:26=010001|...
// B:    |31:26=010100|25:0=offs26
// BL:   |31:26=010101|25:0=offs26
// BEQ:  |31:26=010110|25:10=offs16|9:5=rj|4:0=rd
// BNE:  |31:26=010111...
// BLT:  |31:26=011000...
// BGE:  |31:26=011001...
// BLTU: |31:26=011010...
// BGEU: |31:26=011011...
// BC.EQZ: |31:26=011100...
// BC.NEZ: |31:26=011101...

void MicroAssembler::b(Label* l, JumpDistance d) { beqz(ZR, l, d); }
void MicroAssembler::bl(Label* l, JumpDistance d) {
  intptr_t offs = l->IsBound() ? (l->Position() - Position()) : 0;
  if (l->IsBound()) {
    EMIT(Offs16(offs) | Rd(RA) | Rj(ZR) | 0x54000000);
  } else {
    l->LinkTo(Position(), Label::kExpr2);
    EMIT(Rd(RA) | Rj(ZR) | 0x54000000);
  }
}

void MicroAssembler::beqz(Register rj, Label* l, JumpDistance d) {
  if (l->IsBound()) {
    EMIT(Si16(l->Position() - Position()) | Rj(rj) | 0x40000000);
  } else {
    l->LinkTo(Position(), Label::kExpr1);
    EMIT(Rj(rj) | 0x40000000);
  }
}

void MicroAssembler::bnez(Register rj, Label* l, JumpDistance d) {
  if (l->IsBound()) {
    EMIT(Si16(l->Position() - Position()) | Rj(rj) | 0x44000000);
  } else {
    l->LinkTo(Position(), Label::kExpr1);
    EMIT(Rj(rj) | 0x44000000);
  }
}

void MicroAssembler::beq(Register rj, Register rd, Label* l, JumpDistance d) {
  EMIT(Offs16(l->Position() - Position()) | Rd(rd) | Rj(rj) | 0x58000000);
}
void MicroAssembler::bne(Register rj, Register rd, Label* l, JumpDistance d) {
  EMIT(Offs16(l->Position() - Position()) | Rd(rd) | Rj(rj) | 0x5C000000);
}
void MicroAssembler::blt(Register rj, Register rd, Label* l, JumpDistance d) {
  EMIT(Offs16(l->Position() - Position()) | Rd(rd) | Rj(rj) | 0x60000000);
}
void MicroAssembler::bge(Register rj, Register rd, Label* l, JumpDistance d) {
  EMIT(Offs16(l->Position() - Position()) | Rd(rd) | Rj(rj) | 0x64000000);
}
void MicroAssembler::bltu(Register rj, Register rd, Label* l, JumpDistance d) {
  EMIT(Offs16(l->Position() - Position()) | Rd(rd) | Rj(rj) | 0x68000000);
}
void MicroAssembler::bgeu(Register rj, Register rd, Label* l, JumpDistance d) {
  EMIT(Offs16(l->Position() - Position()) | Rd(rd) | Rj(rj) | 0x6C000000);
}

void MicroAssembler::bceqz(FRegister fcj, Label* l, JumpDistance d) {
  if (l->IsBound()) {
    EMIT(Si16(l->Position() - Position()) | FRj(fcj) | 0x70000000);
  } else {
    l->LinkTo(Position(), Label::kExpr1);
    EMIT(FRj(fcj) | 0x70000000);
  }
}

void MicroAssembler::bcnez(FRegister fcj, Label* l, JumpDistance d) {
  if (l->IsBound()) {
    EMIT(Si16(l->Position() - Position()) | FRj(fcj) | 0x74000000);
  } else {
    l->LinkTo(Position(), Label::kExpr1);
    EMIT(FRj(fcj) | 0x74000000);
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
  EMIT(Si12(si12) | Rj(rj) | Rd(rd) | 0x29000000);
}
void MicroAssembler::ld_hu(Register rd, Register rj, intptr_t si12) {
  EMIT(Si12(si12) | Rj(rj) | Rd(rd) | 0x29400000);
}
void MicroAssembler::ld_wu(Register rd, Register rj, intptr_t si12) {
  EMIT(Si12(si12) | Rj(rj) | Rd(rd) | 0x29800000);
}

// ==== Stores (opcode 001010 at bits[31:26], type>=1000 for stores) ====
void MicroAssembler::st_b(Register rd, Register rj, intptr_t si12) {
  EMIT(Si12(si12) | Rd(rd) | Rj(rj) | 0x2A000000);
}
void MicroAssembler::st_h(Register rd, Register rj, intptr_t si12) {
  EMIT(Si12(si12) | Rd(rd) | Rj(rj) | 0x2A400000);
}
void MicroAssembler::st_w(Register rd, Register rj, intptr_t si12) {
  EMIT(Si12(si12) | Rd(rd) | Rj(rj) | 0x2A800000);
}
void MicroAssembler::st_d(Register rd, Register rj, intptr_t si12) {
  EMIT(Si12(si12) | Rd(rd) | Rj(rj) | 0x2AC00000);
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
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00000000);
}
void MicroAssembler::add_d(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00200000);
}
void MicroAssembler::sub_w(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00400000);
}
void MicroAssembler::sub_d(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00600000);
}
void MicroAssembler::and_l(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | (0x02000000 | (1 << 26)));
}
void MicroAssembler::or_l(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | (0x02400000 | (1 << 26)));
}
void MicroAssembler::xor_l(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | (0x02800000 | (1 << 26)));
}

void MicroAssembler::mul_w(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x03800000);
}
void MicroAssembler::mul_d(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x03A00000);
}
void MicroAssembler::mulh_w(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x03C00000);
}
void MicroAssembler::mulh_wu(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x03E00000);
}
void MicroAssembler::mulh_d(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x04000000);
}
void MicroAssembler::mulh_du(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x04200000);
}

void MicroAssembler::div_w(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x04800000);
}
void MicroAssembler::div_wu(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x04A00000);
}
void MicroAssembler::div_d(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x04C00000);
}
void MicroAssembler::div_du(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x04E00000);
}

void MicroAssembler::mod_w(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x05000000);
}
void MicroAssembler::mod_wu(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x05200000);
}
void MicroAssembler::mod_d(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x05400000);
}
void MicroAssembler::mod_du(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x05600000);
}

// ==== Shift instructions ====
void MicroAssembler::sll_w(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00800000);
}
void MicroAssembler::srl_w(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00A00000);
}
void MicroAssembler::sra_w(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00C00000);
}
void MicroAssembler::sll_d(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x00E00000);
}
void MicroAssembler::srl_d(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x01000000);
}
void MicroAssembler::sra_d(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x01200000);
}

// ==== Shift immediate ====
void MicroAssembler::slli_w(Register rd, Register rj, intptr_t ui5) {
  ASSERT(Utils::IsUint(5, ui5));
  EMIT(Ui5(ui5) | Rj(rj) | Rd(rd) | 0x00840000);
}
void MicroAssembler::srli_w(Register rd, Register rj, intptr_t ui5) {
  ASSERT(Utils::IsUint(5, ui5));
  EMIT(Ui5(ui5) | Rj(rj) | Rd(rd) | 0x00A40000);
}
void MicroAssembler::srai_w(Register rd, Register rj, intptr_t ui5) {
  ASSERT(Utils::IsUint(5, ui5));
  EMIT(Ui5(ui5) | Rj(rj) | Rd(rd) | 0x00C40000);
}
void MicroAssembler::slli_d(Register rd, Register rj, intptr_t ui6) {
  ASSERT(Utils::IsUint(6, ui6));
  EMIT(Ui6(ui6) | Rj(rj) | Rd(rd) | 0x00E40000);
}
void MicroAssembler::srli_d(Register rd, Register rj, intptr_t ui6) {
  ASSERT(Utils::IsUint(6, ui6));
  EMIT(Ui6(ui6) | Rj(rj) | Rd(rd) | 0x01040000);
}
void MicroAssembler::srai_d(Register rd, Register rj, intptr_t ui6) {
  ASSERT(Utils::IsUint(6, ui6));
  EMIT(Ui6(ui6) | Rj(rj) | Rd(rd) | 0x01240000);
}

// ==== Compare/set ====
void MicroAssembler::slt(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x01400000);
}
void MicroAssembler::sltu(Register rd, Register rj, Register rk) {
  EMIT(Rk(rk) | Rj(rj) | Rd(rd) | 0x01600000);
}

// ==== Special ====
void MicroAssembler::break_(uint32_t code) {
  EMIT((code << 15) | 0x00000080);
}
void MicroAssembler::dbar(intptr_t hint) {
  EMIT((static_cast<uint32_t>(hint) << 15) | 0x00280080);
}
void MicroAssembler::ibar(intptr_t hint) {
  EMIT((static_cast<uint32_t>(hint) << 15) | 0x00200080);
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
  EMIT(Si12(si12) | Rj(rj) | FRd(fd) | 0x2C000000);
}
void MicroAssembler::fld_d(FRegister fd, Register rj, intptr_t si12) {
  ASSERT(Utils::IsInt(12, si12));
  EMIT(Si12(si12) | Rj(rj) | FRd(fd) | 0x2C400000);
}
void MicroAssembler::fst_s(FRegister fd, Register rj, intptr_t si12) {
  ASSERT(Utils::IsInt(12, si12));
  EMIT(Si12(si12) | FRd(fd) | Rj(rj) | 0x2E000000);
}
void MicroAssembler::fst_d(FRegister fd, Register rj, intptr_t si12) {
  ASSERT(Utils::IsInt(12, si12));
  EMIT(Si12(si12) | FRd(fd) | Rj(rj) | 0x2E400000);
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
void Assembler::Bind(Label* l) { MicroAssembler::Bind(l); }
intptr_t Assembler::CodeSize() const { return buffer_.Size(); }

// Frame management
void Assembler::EnterFrame(intptr_t frame_size) {
  addi_d(SP, SP, -(frame_size + 16));
  st_d(FP, SP, frame_size);
  st_d(RA, SP, frame_size + 8);
  addi_d(FP, SP, frame_size + 8);
}
void Assembler::LeaveFrame() {
  addi_d(SP, FP, -8);
  ld_d(RA, SP, 8);
  ld_d(FP, SP, 0);
  addi_d(SP, SP, 16);
}
void Assembler::EnterStubFrame() { EnterFrame(0); }
void Assembler::LeaveStubFrame() { LeaveFrame(); }

void Assembler::EnterDartFrame(intptr_t frame_size) {
  UNIMPLEMENTED();
}
void Assembler::LeaveDartFrame() {
  UNIMPLEMENTED();
}
void Assembler::EnterCFrame() { UNIMPLEMENTED(); }
void Assembler::LeaveCFrame() { UNIMPLEMENTED(); }

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
void Assembler::PushRegisters(const RegisterSet& regs) { UNIMPLEMENTED(); }
void Assembler::PopRegisters(const RegisterSet& regs) { UNIMPLEMENTED(); }
void Assembler::PushNativeCalleeSavedRegisters() { UNIMPLEMENTED(); }
void Assembler::PopNativeCalleeSavedRegisters() { UNIMPLEMENTED(); }

void Assembler::Call(Register target) { jirl(RA, target, 0); }
void Assembler::Call(Label* label) { bl(label); }
void Assembler::Call(const StubEntry& stub) { Call(stub.entry_point()); }
void Assembler::Call(const Address& address) { UNIMPLEMENTED(); }
void Assembler::Call(intptr_t target_code_pool_index, CodeEntryKind entry_kind) {
  UNIMPLEMENTED();
}
void Assembler::Call(const Code& target) { UNIMPLEMENTED(); }
void Assembler::Jump(const Address& address) { UNIMPLEMENTED(); }

void Assembler::Move(Register dst, Register src) { or_l(dst, src, ZR); }

void Assembler::CompareRegisters(Register rn, Register rm) {
  deferred_compare_ = kCompareReg;
  deferred_left_ = rn;
  deferred_reg_ = rm;
}
void Assembler::CompareObjectRegisters(Register rn, Register rm) {
  CompareRegisters(rn, rm);
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
  ASSERT(deferred_compare_ != kNone);
  switch (condition) {
    case EQUAL: beq(deferred_left_, deferred_reg_, label, d); break;
    case NOT_EQUAL: bne(deferred_left_, deferred_reg_, label, d); break;
    case LESS: blt(deferred_left_, deferred_reg_, label, d); break;
    case LESS_EQUAL: bge(deferred_reg_, deferred_left_, label, d); break;
    case GREATER: blt(deferred_reg_, deferred_left_, label, d); break;
    case GREATER_EQUAL: bge(deferred_left_, deferred_reg_, label, d); break;
    case BELOW: bltu(deferred_left_, deferred_reg_, label, d); break;
    case BELOW_EQUAL: bgeu(deferred_reg_, deferred_left_, label, d); break;
    case ABOVE: bltu(deferred_reg_, deferred_left_, label, d); break;
    case ABOVE_EQUAL: bgeu(deferred_left_, deferred_reg_, label, d); break;
    default: UNREACHABLE();
  }
  deferred_compare_ = kNone;
}

void Assembler::BranchIfBit(Register rn, int bit, Label* label,
                            JumpDistance distance) {
  UNIMPLEMENTED();
}

void Assembler::BranchIfNotSmi(Register reg, Label* label,
                               JumpDistance distance) {
  UNIMPLEMENTED();
}

void Assembler::BranchIfSmi(Register reg, Label* label, JumpDistance distance) {
  UNIMPLEMENTED();
}

void Assembler::ArithmeticShiftRightImmediate(Register dst, Register src,
                                              intptr_t shift, OperandSize sz) {
  UNIMPLEMENTED();
}

void Assembler::Load(Register dst, const Address& address, OperandSize sz) {
  switch (sz) {
    case kDoubleWord:
    case kWordSize:
      ld_d(dst, address.base(), address.offset());
      break;
    case kUnsignedWord:
      ld_wu(dst, address.base(), address.offset());
      break;
    case kWord:
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
    case kDoubleWord:
    case kWordSize:
      st_d(src, address.base(), address.offset());
      break;
    case kUnsignedWord:
    case kWord:
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
                                   int32_t offset, OperandSize sz) {
  UNIMPLEMENTED();
}

void Assembler::LoadSFromOffset(FRegister dst, Register base, int32_t offset) {
  fld_s(dst, base, offset);
}
void Assembler::LoadDFromOffset(FRegister dst, Register base, int32_t offset) {
  fld_d(dst, base, offset);
}
void Assembler::LoadFromStack(Register dst, intptr_t depth) {
  ld_d(dst, SP, depth);
}
void Assembler::StoreToStack(Register src, intptr_t depth) {
  st_d(src, SP, depth);
}

void Assembler::SmiTag(Register dst, Register src) {
  UNIMPLEMENTED();
}
void Assembler::SmiUnTag(Register dst, Register src) {
  UNIMPLEMENTED();
}

void Assembler::ExtendValue(Register rd, Register rn, OperandSize sz) {
  UNIMPLEMENTED();
}
void Assembler::ExtendAndSmiTagValue(Register rd, Register rn, OperandSize sz) {
  UNIMPLEMENTED();
}

void Assembler::LoadAcquire(Register dst, const Address& address) {
  UNIMPLEMENTED();
}
void Assembler::StoreRelease(Register src, const Address& address) {
  UNIMPLEMENTED();
}
void Assembler::TsanLoadAcquire(Register dst, const Address& address) {
  UNIMPLEMENTED();
}
void Assembler::TsanStoreRelease(Register src, const Address& address) {
  UNIMPLEMENTED();
}
void Assembler::TsanFuncEntry(bool preserve_registers) { UNIMPLEMENTED(); }
void Assembler::TsanFuncExit(bool preserve_registers) { UNIMPLEMENTED(); }
void Assembler::ReserveAlignedFrameSpace(intptr_t frame_space) {
  UNIMPLEMENTED();
}
void Assembler::EmitEntryFrameVerification() { UNIMPLEMENTED(); }

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
  // First: load bits[31:12]
  uint32_t lo12 = uv & 0xfff;
  uint32_t upper = (uv >> 12) & 0xfffff;
  int64_t adjusted = uv;
  if (lo12 >= 0x800) {
    // Adjust for sign extension: add 1 to upper 52 bits
    adjusted = uv + 0x1000;
    lo12 = adjusted & 0xfff;
  }
  lu12iw(rd, static_cast<intptr_t>((adjusted >> 12) & 0xfffff));
  // Next: load bits[51:32]
  if (uv >> 32) {
    lu32id(rd, static_cast<intptr_t>((uv >> 32) & 0xfffff));
  }
  // Finally: load bits[63:52] with sign extension from bit 51
  if (uv >> 52) {
    lu52id(rd, rd, static_cast<intptr_t>((uv >> 52) & 0xfff));
  } else if (uv & 0x800000000000ULL) {
    // Sign extend from bit 51 explicitly
    lu52id(rd, rd, 0);
  }
  const intptr_t low12 = static_cast<intptr_t>(adjusted & 0xfff);
  if (low12 != 0) {
    ori(rd, rd, low12);
  }
}

void Assembler::AddImmediateBranchOverflow(Register rd, Register rs1,
                                           intx_t imm, Label* overflow) {
  UNIMPLEMENTED();
}
void Assembler::SubtractImmediateBranchOverflow(Register rd, Register rs1,
                                                intx_t imm, Label* overflow) {
  UNIMPLEMENTED();
}
void Assembler::MultiplyImmediateBranchOverflow(Register rd, Register rs1,
                                                intx_t imm, Label* overflow) {
  UNIMPLEMENTED();
}
void Assembler::AddBranchOverflow(Register rd, Register rs1, Register rs2,
                                  Label* overflow) {
  UNIMPLEMENTED();
}
void Assembler::SubtractBranchOverflow(Register rd, Register rs1, Register rs2,
                                       Label* overflow) {
  UNIMPLEMENTED();
}
void Assembler::MultiplyBranchOverflow(Register rd, Register rs1, Register rs2,
                                       Label* overflow) {
  UNIMPLEMENTED();
}
void Assembler::CountLeadingZeroes(Register rd, Register rs) {
  UNIMPLEMENTED();
}
void Assembler::CompareWithMemoryValue(Register value, const Address& address,
                                       int8_t expected_cid) {
  UNIMPLEMENTED();
}

void Assembler::LoadWordFromPool(Register dst, int32_t offset) {
  ASSERT(constant_pool_allowed_);
  pcalau12i(dst, (offset >> 12) + ((offset >> 11) & 1));
  ld_d(dst, dst, offset & 0xfff);
}
void Assembler::LoadWordFromPoolIndex(Register dst, int32_t index) {
  LoadWordFromPool(dst, ObjectPool::OffsetFromIndex(index));
}
void Assembler::LoadObject(Register dst, const Object& obj) {
  if (obj.IsSmi() && obj.ptr() == 0) { Move(dst, ZR); return; }
  LoadWordFromPoolIndex(dst, object_pool_builder().FindObject(obj, false));
}
void Assembler::LoadUniqueObject(Register dst, const Object& obj) {
  LoadWordFromPoolIndex(dst, object_pool_builder().FindObject(obj, true));
}

void Assembler::LoadIntoObject(Register dst, Register obj, int32_t offset) {
  UNIMPLEMENTED();
}
void Assembler::LoadField(Register dst, Register instance, int32_t offset) {
  UNIMPLEMENTED();
}
void Assembler::StoreIntoObject(Register object, const Address& address,
                                Register value) {
  UNIMPLEMENTED();
}
void Assembler::StoreIntoObjectNoBarrier(Register object, const Address& address,
                                         Register value) {
  st_d(value, address.base(), address.offset());
}
void Assembler::StoreIntoObjectNoBarrier(Register object, Register value,
                                         int32_t offset) {
  st_d(value, object, offset - kHeapObjectTag);
}
void Assembler::LoadFieldAddressForOffset(Register address, Register instance,
                                          int32_t offset) {
  AddImmediate(address, instance, offset - kHeapObjectTag);
}

bool Assembler::AddressCanHoldConstantIndex(const Object& constant,
                                            bool is_external, intptr_t cid,
                                            intptr_t index_scale) {
  UNREACHABLE();
  return false;
}

Address Assembler::ElementAddressForIntIndex(bool is_external, intptr_t cid,
                                             intptr_t index_scale,
                                             Register array,
                                             intptr_t index) const {
  UNREACHABLE();
  return Address(SP);
}
void Assembler::ComputeElementAddressForIntIndex(Register address,
                                                 bool is_external, intptr_t cid,
                                                 intptr_t index_scale,
                                                 Register array,
                                                 intptr_t index) {
  UNREACHABLE();
}
Address Assembler::ElementAddressForRegIndex(bool is_external, intptr_t cid,
                                             intptr_t index_scale,
                                             bool index_unboxed,
                                             Register array, Register index,
                                             Register temp) {
  UNREACHABLE();
  return Address(SP);
}
void Assembler::ComputeElementAddressForRegIndex(
    Register address, bool is_external, intptr_t cid, intptr_t index_scale,
    bool index_unboxed, Register array, Register index) {
  UNREACHABLE();
}
void Assembler::LoadStaticFieldAddress(Register address, Register field,
                                       Register scratch, bool is_shared) {
  UNREACHABLE();
}
void Assembler::LoadFieldAddressForRegOffset(Register address,
                                             Register instance,
                                             Register offset_in_words_as_smi) {
  UNIMPLEMENTED();
}
int32_t Assembler::HeapDataOffset(bool is_external, intptr_t cid) {
  return is_external
             ? 0
             : (target::Instance::DataOffsetFor(cid) - kHeapObjectTag);
}
void Assembler::SmiUntagOrCheck(Register tmp, Label* label) { UNIMPLEMENTED(); }
void Assembler::MaybePatchCodeStart(Register tmp) { UNIMPLEMENTED(); }
void Assembler::EmitPcRelativeCall(Label* label) { UNIMPLEMENTED(); }
void Assembler::EmitPcRelativeTailCall(intptr_t offset_into_target) {
  UNIMPLEMENTED();
}
void Assembler::GenerateUnRelocatedPcRelativeCall(intptr_t offset_into_target) {
  UNIMPLEMENTED();
}
void Assembler::GenerateUnRelocatedPcRelativeTailCall(
    intptr_t offset_into_target) {
  UNIMPLEMENTED();
}
void Assembler::LoadObjectHelper(Register dst, const Object& obj,
                                 bool is_unique) {
  UNIMPLEMENTED();
}
void Assembler::JumpAndLink(intptr_t target_code_pool_index,
                            CodeEntryKind entry_kind) {
  UNIMPLEMENTED();
}

}  // namespace compiler
}  // namespace dart

#endif  // defined(TARGET_ARCH_LOONG64)
