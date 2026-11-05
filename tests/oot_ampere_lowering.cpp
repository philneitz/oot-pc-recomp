// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic instruction tests for Ocarina's lowering fixes. No captured game code.
// Build: clang++ -std=c++20 -O1 -I runtime/third_party/eglantine tests/oot_ampere_lowering.cpp -o build/oot-fix/lowering-check.exe
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
#include "shader_recompiler/frontend/ampere/register_pressure.h"

using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
static unsigned checks;
static void require(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void field(Instruction& insn, unsigned start, unsigned count, u32 value) {
    for (unsigned i = 0; i < count; ++i) {
        const u32 mask = 1U << ((start + i) % 32);
        auto& word = insn.words[(start + i) / 32];
        word = (word & ~mask) | (((value >> i) & 1U) ? mask : 0U);
    }
}
static Instruction insn(u32 opcode, u32 guard = 7, u32 dest = 0, u32 source = 0) {
    Instruction out{};
    field(out, 0, 12, opcode); field(out, 12, 4, guard);
    field(out, 16, 8, dest); field(out, 24, 8, source);
    return out;
}
static Instruction exit_insn() {
    auto out = insn(0x94d); field(out, 87, 4, 7); return out;
}
static Instruction move(u32 reg, u32 literal, u32 guard = 7) {
    auto out = insn(0x802, guard, reg);
    out.words[1] = literal; field(out, 72, 4, 15); return out;
}
static Instruction imad_move(u32 reg, u32 literal, u32 guard) {
    auto out = insn(0x424, guard, reg, 255);
    out.words[1] = literal; out.words[2] = 0x078e00ff; return out;
}
static Instruction gather(u32 guard, bool half, bool depth, bool per_tap, u32 offsets = 20) {
    auto out = insn(0xb63, guard, 4, 16);
    field(out, 32, 8, offsets); field(out, 40, 14, 7); field(out, 54, 5, 2);
    field(out, 60, 1, 1); field(out, 61, 3, 1); field(out, 64, 8, 8);
    field(out, 72, 4, 15); field(out, 76, 2, per_tap ? 2 : 0);
    field(out, 78, 1, depth); field(out, 79, 1, half); field(out, 81, 3, 7);
    return out;
}
static u64 copy(u32 guard, u32 dest, u32 source) {
    return 0x5c98000000000000ULL | (u64{guard} << 16) | dest | (15ULL << 39) | (u64{source} << 20);
}
int main() {
    for (u32 opcode : {0x809U, 0x803U, 0xb82U}) {
        auto high = insn(opcode, 7, 220, 224);
        high.words[1] = opcode == 0x803 ? 1 : 0x3727c5ac;
        if (opcode == 0xb82) field(high, 73, 3, 5);
        const auto pressure = Shader::Ampere::PrepareRegisterPressure(high, 216);
        require(pressure.valid, "known scalar/constant instruction supports high-register shadowing");
        require(pressure.instruction.Bits(16, 8) < 216, "high destination is moved out of the uniform bank");
        require(pressure.instruction.Bits(24, 8) < 216, "high source is moved out of the uniform bank");
        require(pressure.instruction.words[1] == high.words[1], "immediate and constant-buffer fields are unchanged");
        require(pressure.prefix.size() == 16 && pressure.suffix.size() == 16,
                "both borrowed register quartets are preserved and restored");
        if (opcode == 0xb82) {
            field(high, 16, 8, 223);
            require(!Shader::Ampere::PrepareRegisterPressure(high, 216).valid,
                    "constant-load pair spanning register quartets is rejected");
        }
    }
    for (u32 width : {0U, 2U}) {
        for (u32 guard : {1U, 7U, 9U}) {
            auto load = insn(0xabb, guard, 4, 6);
            field(load, 38, 16, width == 0 ? 0x83 : 0x82);
            field(load, 54, 5, 4);
            load.words[2] = 0x08000000 | (width << 9);
            auto result = LowerToMaxwell(std::array{load, exit_insn()});
            require(bool(result), "indexed unsigned subword ULDC lowers");
            const auto found = std::find_if(result.instructions.begin(), result.instructions.end(),
                                            [](u64 word) { return (word >> 52) == 0xef9; });
            require(found != result.instructions.end(), "indexed ULDC emits Maxwell LDC");
            const auto ldc = *found;
            require((ldc >> 48 & 7) == width, "indexed ULDC retains unsigned subword width");
            require((ldc >> 20 & 0xffff) == (width == 0 ? 0x83U : 0x82U), "indexed ULDC retains byte offset");
            require((ldc >> 36 & 31) == 4, "indexed ULDC retains constant bank");
            require((ldc >> 8 & 255) != 255, "indexed ULDC retains a nonzero index register");
            const u32 mapped_guard = guard == 7 ? 7 : (guard & 8) | (6 - (guard & 7));
            require((ldc >> 16 & 15) == mapped_guard, "indexed ULDC uses the mapped uniform predicate");
            field(load, 38, 16, 0x8000);
            require(!LowerToMaxwell(std::array{load, exit_insn()}), "unsigned offset outside signed Maxwell range is rejected");
        }
    }
    for (u32 guard : {1U, 7U, 9U}) {
        auto depth = gather(guard, false, true, false);
        const auto lowered = LowerToMaxwell(std::array{depth, exit_insn()});
        require(bool(lowered), "depth gather lowers");
        require(lowered.required_texture_buffer == 2, "depth gather retains descriptor bank");
        require(lowered.instructions.size() == 9, "depth gather result has four scalar destinations");
        require(lowered.instructions[0] == copy(guard, 250, 16), "depth gather preserves x");
        require(lowered.instructions[1] == copy(guard, 251, 17), "depth gather preserves y");
        require(lowered.instructions[2] == copy(guard, 246, 20), "depth gather preserves comparison reference");
        require((lowered.instructions[3] & (1ULL << 50)) != 0, "Maxwell gather enables depth comparison");
        field(depth, 79, 1, 1);
        require(!LowerToMaxwell(std::array{depth, exit_insn()}), "unsupported half depth mode is rejected");
    }
    for (bool half : {false, true}) {
        for (u32 guard : {1U, 7U, 9U}) {
            const u32 first = 0x00ff0001, second = 0x010000ff;
            std::vector<Instruction> code{imad_move(20, first, guard), imad_move(21, second, guard),
                                         gather(guard, half, false, true), exit_insn()};
            auto lowered = LowerToMaxwell(code);
            require(bool(lowered), "same-guard literal IMAD offsets lower");
            const auto start = lowered.source_instruction_offsets[2];
            require(lowered.instructions[start] == (0x010000000007f000ULL | 248 | (u64{first} << 20)),
                    "offset literal is unconditional for SPIR-V constant propagation");
            require(lowered.instructions[start + 1] == (0x010000000007f000ULL | 249 | (u64{second} << 20)),
                    "second offset literal retains every signed byte");
            field(code[0], 12, 4, guard == 9 ? 1 : 9);
            require(!LowerToMaxwell(code), "different-guard offset definition is rejected");
            code[0] = imad_move(20, 0x00000040, guard);
            require(!LowerToMaxwell(code), "out-of-range signed offset is rejected");
        }
    }
    {
        auto branch = insn(0x947, 1); branch.words[1] = 16; field(branch, 87, 4, 7);
        std::vector<Instruction> code{move(0, 0x010000ff), move(1, 0x00010000), branch,
                                     gather(9, false, false, true, 0), exit_insn()};
        require(bool(LowerToMaxwell(code)), "outgoing branch does not overwrite offset GPR zero");
        code[2].words[1] = 0;
        require(!LowerToMaxwell(code), "incoming control edge blocks backward offset proof");
    }
    {
        auto uniform = insn(0x8a4, 7, 20);
        uniform.words[1] = 63; uniform.words[2] = 0x0f8e023f;
        std::vector<Instruction> code{move(20, 0x010000ff), move(21, 0x00010000), uniform,
                                     gather(7, false, false, true), exit_insn()};
        require(bool(LowerToMaxwell(code)), "UR20 does not overwrite the independent offset GPR20");
        auto scalar = insn(0x202, 7, 20);
        scalar.words[1] = 4; scalar.words[2] = 0xf00;
        code[2] = scalar;
        require(!LowerToMaxwell(code), "a nonconstant GPR write invalidates the offset proof");
    }
    {
        auto clobber = insn(0x212, 7, 10, 12);
        clobber.words[1] = 255; clobber.words[2] = 0x0784a003;
        field(clobber, 81, 3, 1);
        const std::array code{imad_move(20, 0x010000ff, 9), imad_move(21, 0x00010000, 9),
                              clobber, gather(9, false, false, true), exit_insn()};
        const auto lowered = LowerToMaxwell(code);
        require(!lowered && lowered.error.find("native per-tap gather offset proof") == 0,
                "changing the guard predicate invalidates conditional offsets");
    }
    for (u32 guard : {1U, 7U, 9U}) {
        auto address = insn(0x211, guard, 1, 2);
        address.words[1] = 0x80000000; address.words[2] = 0x078ff0ff;
        const auto lowered = LowerToMaxwell(std::array{address, exit_insn()});
        require(bool(lowered), "LEA.HI with a negated register base lowers");
        require(lowered.instructions[0] == (0x38280000002002faULL | (u64{guard} << 16)),
                "LEA.HI preserves the shifted source register");
        require(lowered.instructions[1] == (0x5c1100000000fa01ULL | (u64{guard} << 16)),
                "LEA.HI subtracts the base rather than adding it");
        field(address, 62, 1, 1);
        require(!LowerToMaxwell(std::array{address, exit_insn()}), "unsupported LEA modifier is rejected");
    }
    for (u32 mode : {0x0021e100U, 0x00212800U, 0x0021f100U}) {
        for (u32 modifiers : {1U, 2U, 3U}) {
            auto conversion = insn(0xb05, 7, 21);
            field(conversion, 40, 14, 8); field(conversion, 54, 5, 2);
            field(conversion, 62, 2, modifiers); conversion.words[2] = mode;
            const auto lowered = LowerToMaxwell(std::array{conversion, exit_insn()});
            require(bool(lowered), "constant-buffer F2I modifiers lower");
            const auto has = [&](u64 word) {
                return std::find(lowered.instructions.begin(), lowered.instructions.end(), word) != lowered.instructions.end();
            };
            require(has(0x0400000000070000ULL | 252 | (252ULL << 8) | (0x7fffffffULL << 20)) == bool(modifiers & 1),
                    "absolute value clears the floating-point sign bit");
            require(has(0x0400000000070000ULL | 252 | (252ULL << 8) | (0x80000000ULL << 20) | (2ULL << 53)) == bool(modifiers & 2),
                    "negation toggles the floating-point sign bit after absolute value");
        }
    }
    for (u32 operation = 0; operation < 8; ++operation) {
        auto reduction = insn(0x59f, 7, 0, 4);
        field(reduction, 40, 14, 5); field(reduction, 61, 3, 3);
        reduction.words[2] = 0x0010e000; field(reduction, 87, 3, operation);
        const auto lowered = LowerToMaxwell(std::array{reduction, exit_insn()});
        require(bool(lowered), "U32 surface reduction lowers");
        require(((lowered.instructions[0] >> 24) & 7) == operation, "surface atomic operation is preserved");
        field(reduction, 73, 1, 1);
        require(!LowerToMaxwell(std::array{reduction, exit_insn()}), "unsupported reduction format is rejected");
    }
    std::printf("Ocarina lowering: %u checks passed\n", checks);
}
