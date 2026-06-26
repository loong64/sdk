// Copyright (c) 2017, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // Needed here to get TARGET_ARCH_LOONG64.
#if defined(TARGET_ARCH_LOONG64)

#include "vm/compiler/assembler/disassembler.h"

#include "platform/assert.h"
#include "vm/instructions.h"

namespace dart {

#if !defined(PRODUCT) || defined(FORCE_INCLUDE_DISASSEMBLER)

class LOONG64Disassembler {
 public:
  explicit LOONG64Disassembler(char* buffer, size_t buffer_size)
      : buffer_(buffer), buffer_size_(buffer_size), buffer_pos_(0) {}

  intptr_t Disassemble(uword pc) {
    uint32_t instr_raw = LoadUnaligned(reinterpret_cast<uint32_t*>(pc));
    Instr instr(instr_raw);
    uint32_t op6 = instr.opcode();
    Register rd = instr.rd();
    Register rj = instr.rj();
    Register rk = instr.rk();

    // Decode based on opcode at bits[31:26].
    switch (op6) {
      case 0x00: case 0x01: {
        // ALU 3-register
        uint32_t op10 = (instr_raw >> 22) & 0x3ff;
        if (op10 <= 0x1f) {
          // add/sub/sll/srl/sra family
          uint32_t bits = (instr_raw >> 15) & 0x7;
          const char* names[] = {"add.w","add.d","sub.w","sub.d",
                                 "sll.w","srl.w","sra.w",
                                 "sll.d","srl.d","sra.d"};
          APPEND("%-8s r%d, r%d, r%d", names[bits % 10], rd, rj, rk);
        } else if ((op10 & 0x3e0) == 0x0e0) {
          uint32_t func = (instr_raw >> 10) & 0x1f;
          const char* names[] = {nullptr,nullptr,nullptr,nullptr,nullptr,
                                 nullptr,nullptr,nullptr,nullptr,nullptr,
                                 "and","or","xor",nullptr,nullptr,
                                 nullptr,nullptr,nullptr,nullptr,nullptr};
          if (func < 21 && names[func]) {
            APPEND("%-8s r%d, r%d, r%d", names[func], rd, rj, rk);
          } else {
            APPEND("unknown.3r");
          }
        } else if ((op10 & 0x3fc) == 0x070) {
          const char* names[] = {"mul.w","mul.d","mulh.w","mulh.wu",
                                 "mulh.d","mulh.du"};
          int idx = (op10 - 0x38) >> 1;
          APPEND("%-8s r%d, r%d, r%d", names[idx], rd, rj, rk);
        } else if ((op10 & 0x3f8) == 0x048) {
          const char* names[] = {"div.w","div.wu","div.d","div.du",
                                 "mod.w","mod.wu","mod.d","mod.du"};
          int idx = (op10 - 0x48) >> 1;
          if (idx < 8) APPEND("%-8s r%d, r%d, r%d", names[idx], rd, rj, rk);
          else APPEND("unknown.3r");
        } else {
          APPEND("unknown.3r");
        }
        break;
      }
      case 0x02: case 0x03: {
        // ALU immediate
        int32_t imm = instr.si12();
        uint32_t func = (instr_raw >> 22) & 0xf;
        const char* names[] = {"addi.w","addi.d","slti","sltui",
                               nullptr,"andi","ori","xori"};
        if (func < 8 && names[func] && (func != 4)) {
          APPEND("%-8s r%d, r%d, %d", names[func], rd, rj, imm);
        } else {
          APPEND("unknown.alu_imm");
        }
        break;
      }
      case 0x05: {
        // LU12IW/LU32ID/LU52ID
        int32_t si20 = instr.si20();
        uint32_t sub = (instr_raw >> 24) & 0x3;
        if (sub == 0) APPEND("lu12iw     r%d, %d", rd, si20 & 0xfffff);
        else if (sub == 1) APPEND("lu32id     r%d, %d", rd, si20 & 0xfffff);
        else APPEND("lu52id     r%d, r%d, %d", rd, rj, (instr_raw >> 10) & 0xfff);
        break;
      }
      case 0x06: {
        int32_t si20 = instr.si20();
        uint32_t sub = (instr_raw >> 24) & 0x3;
        if (sub == 0) APPEND("pcaddu12i  r%d, %d", rd, si20 & 0xfffff);
        else if (sub == 1) APPEND("pcalau12i  r%d, %d", rd, si20 & 0xfffff);
        else APPEND("pcaddu18i  r%d, %d", rd, si20 & 0xfffff);
        break;
      }
      case 0x0a: {
        // Load/store integer
        int32_t offset = instr.si12();
        uint32_t type = (instr_raw >> 22) & 0xf;
        const char* lnames[] = {"ld.b","ld.h","ld.w","ld.d",
                                "ld.bu","ld.hu","ld.wu",nullptr};
        const char* snames[] = {"st.b","st.h","st.w","st.d"};
        if (type < 7 && lnames[type]) {
          APPEND("%-8s r%d, r%d, %d", lnames[type], rd, rj, offset);
        } else if (type >= 8 && type < 12) {
          APPEND("%-8s r%d, r%d, %d", snames[type - 8], rd, rj, offset);
        } else {
          APPEND("unknown.mem");
        }
        break;
      }
      case 0x0b: {
        // FP load/store
        int32_t offset = instr.si12();
        if ((instr_raw & 0x04000000) == 0) {
          if (instr_raw & 0x00200000)
            APPEND("fld.d      f%d, r%d, %d", rd, rj, offset);
          else
            APPEND("fld.s      f%d, r%d, %d", rd, rj, offset);
        } else {
          if (instr_raw & 0x00200000)
            APPEND("fst.d      f%d, r%d, %d", rd, rj, offset);
          else
            APPEND("fst.s      f%d, r%d, %d", rd, rj, offset);
        }
        break;
      }
      case 0x10: {
        int32_t offset = DecodeBranchOffset21(instr_raw);
        APPEND("beqz       r%d, %+d", rj, offset);
        break;
      }
      case 0x11: {
        int32_t offset = DecodeBranchOffset21(instr_raw);
        APPEND("bnez       r%d, %+d", rj, offset);
        break;
      }
      case 0x13: {
        int32_t offset = instr.jirl_offset();
        APPEND("jirl       r%d, r%d, %d", rd, rj, offset);
        break;
      }
      case 0x14: APPEND("b          .%+d", DecodeBranchOffset26(instr_raw)); break;
      case 0x15: APPEND("bl         .%+d", DecodeBranchOffset26(instr_raw)); break;
      case 0x16: APPEND("beq        r%d, r%d, .%+d", rj, rd, instr.branch_offset16()); break;
      case 0x17: APPEND("bne        r%d, r%d, .%+d", rj, rd, instr.branch_offset16()); break;
      case 0x18: APPEND("blt        r%d, r%d, .%+d", rj, rd, instr.branch_offset16()); break;
      case 0x19: APPEND("bge        r%d, r%d, .%+d", rj, rd, instr.branch_offset16()); break;
      case 0x1a: APPEND("bltu       r%d, r%d, .%+d", rj, rd, instr.branch_offset16()); break;
      case 0x1b: APPEND("bgeu       r%d, r%d, .%+d", rj, rd, instr.branch_offset16()); break;
      case 0x04: {
        // Shift immediate
        uint32_t func = (instr_raw >> 22) & 0xf;
        if (func == 0) APPEND("slli.w     r%d, r%d, %d", rd, rj, (instr_raw >> 10) & 0x1f);
        else if (func == 1) APPEND("srli.w     r%d, r%d, %d", rd, rj, (instr_raw >> 10) & 0x1f);
        else if (func == 2) APPEND("srai.w     r%d, r%d, %d", rd, rj, (instr_raw >> 10) & 0x1f);
        else if (func == 4) APPEND("slli.d     r%d, r%d, %d", rd, rj, (instr_raw >> 10) & 0x3f);
        else if (func == 5) APPEND("srli.d     r%d, r%d, %d", rd, rj, (instr_raw >> 10) & 0x3f);
        else if (func == 6) APPEND("srai.d     r%d, r%d, %d", rd, rj, (instr_raw >> 10) & 0x3f);
        else APPEND("unknown.shift");
        break;
      }
      case 0x1c: APPEND("bceqz      f%d, .%+d", rj, DecodeBranchOffset21(instr_raw)); break;
      case 0x1d: APPEND("bcnez      f%d, .%+d", rj, DecodeBranchOffset21(instr_raw)); break;
      default: {
        // Check addu16i.d and other special encodings
        if ((instr_raw & 0xff000000) == 0x10000000) {
          int32_t si16 = static_cast<int16_t>((instr_raw >> 10) & 0xffff);
          APPEND("addu16i.d  r%d, r%d, %d", rd, rj, si16);
        } else if (instr_raw == Instr::kBreakPointInstruction) {
          APPEND("break      ");
        } else {
          APPEND("0x%08x", instr_raw);
        }
        break;
      }
    }
    return 4;
  }

 private:
  char* buffer_;
  size_t buffer_size_;
  size_t buffer_pos_;

  void APPEND(const char* format, ...) {
    va_list args;
    va_start(args, format);
    intptr_t available = static_cast<intptr_t>(buffer_size_) - buffer_pos_;
    if (available > 0) {
      intptr_t offset =
          vsnprintf(buffer_ + buffer_pos_, static_cast<size_t>(available),
                    format, args);
      if (offset > 0) buffer_pos_ += offset;
    }
    va_end(args);
  }

  // Decode helpers for split branch offsets
  static int32_t DecodeBranchOffset21(uint32_t instr_raw) {
    int32_t lo = static_cast<int16_t>((instr_raw >> 10) & 0xffff);
    int32_t hi = instr_raw & 0x1f;
    return (hi << 16) | (lo & 0xffff);
  }

  static int32_t DecodeBranchOffset26(uint32_t instr_raw) {
    return static_cast<int32_t>(instr_raw << 6) >> 6;
  }

  DISALLOW_COPY_AND_ASSIGN(LOONG64Disassembler);
};

void Disassembler::DecodeInstruction(char* hex_buffer,
                                     intptr_t hex_size,
                                     char* human_buffer,
                                     intptr_t human_size,
                                     int* out_instr_len,
                                     const Code& code,
                                     Object** object,
                                     uword pc) {
  LOONG64Disassembler disasm(human_buffer, human_size);
  intptr_t instr_len = disasm.Disassemble(pc);
  uint32_t parcel = LoadUnaligned(reinterpret_cast<uint32_t*>(pc));
  Utils::SNPrint(hex_buffer, hex_size, "0x%08x", parcel);
  if (out_instr_len != nullptr) {
    *out_instr_len = instr_len;
  }
  *object = nullptr;
}

#endif  // !defined(PRODUCT) || defined(FORCE_INCLUDE_DISASSEMBLER)

}  // namespace dart

#endif  // defined TARGET_ARCH_LOONG64
