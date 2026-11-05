// SPDX-License-Identifier: GPL-3.0-or-later
// Standalone Lichii shader compiler adapter. Private native inputs and outputs belong under build/.
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <vector>
#include "shader_recompiler/environment.h"
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
#include "shader_recompiler/frontend/ampere/program_capture.h"
#include "shader_recompiler/frontend/ampere/program_override.h"
#include "shader_recompiler/frontend/ampere/program_address_map.h"
#include "shader_recompiler/frontend/maxwell/translate_program.h"
#include "shader_recompiler/host_translate_info.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"

class MeshEnvironment final : public Shader::Environment {
public:
    std::vector<u64> code;
    explicit MeshEnvironment(const std::vector<unsigned char>& native, const Shader::Ampere::LoweredProgram& lowered,
                             const std::array<u32,3>& registers, bool task) {
        stage = task ? Shader::Stage::Task : Shader::Stage::Mesh;
        start_address = sizeof(sph);
        mesh_registers = registers;
        std::memcpy(&sph, native.data(), sizeof sph);
        code.resize(sizeof(sph) / 8);
        std::memcpy(code.data(), &sph, sizeof sph);
        for (u64 instruction : lowered.instructions) {
            if ((code.size() * 8) % 32 == 0) code.push_back(0);
            code.push_back(instruction);
        }
        code.push_back(0);
        Shader::Ampere::AppendNativeAddressMap(code, lowered.source_instruction_offsets);
    }
    u64 ReadInstruction(u32 address) override {
        if (address % 8 || address / 8 >= code.size()) throw std::runtime_error("instruction outside captured program");
        return code[address / 8];
    }
    bool IsNativeProgram() const override { return true; }
    // The lowering emits extended native register copies. This decoder flag is distinct from
    // IsNativeProgram's resource ABI; leaving it false silently discarded those copies as masked MOVs.
    bool HasNativeInstructions() const override { return true; }
    u32 TranslateProgramAddress(u32 address) const override {
        if (auto target=Shader::Ampere::NativeProgramAddress(code,address)) return *target;
        throw std::runtime_error("indirect branch target outside captured native program");
    }
    u32 ReadCbufValue(u32 bank, u32 offset) override {
        throw std::runtime_error(fmt::format("constant buffer answer required: bank {} offset {:#x}",bank,offset));
    }
    Shader::TextureType ReadTextureType(u32) override { throw std::runtime_error("texture type answer required"); }
    Shader::TexturePixelFormat ReadTexturePixelFormat(u32) override { throw std::runtime_error("texture format answer required"); }
    bool IsTexturePixelFormatInteger(u32) override { throw std::runtime_error("texture integer answer required"); }
    u32 ReadViewportTransformState() override { return 1; }
    u32 TextureBoundBuffer() const override { return 0; }
    u32 LocalMemorySize() const override { return static_cast<u32>(sph.LocalMemorySize()) + sph.common3.shader_local_memory_crs_size; }
    u32 SharedMemorySize() const override { return 0; }
    std::array<u32,3> WorkgroupSize() const override { return {1,1,1}; }
    bool HasHLEMacroState() const override { return false; }
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32,u32) override { return std::nullopt; }
    void Dump(u64,u64) override {}
};

int main(int argc, char** argv) try {
    if (argc != 3 && argc != 7) {
        std::cerr << "Usage: oot_mesh_xlate inspect native.bin | compile[-task] native.bin output.spv mesh_A mesh_B mesh_init\n";
        return 2;
    }
    std::ifstream input(argv[2],std::ios::binary);
    if (!input) throw std::runtime_error("cannot read native input");
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),{});
    if (bytes.size() < 0x90) throw std::runtime_error("native graphics input is too short");
    u32 w0; std::memcpy(&w0,bytes.data(),4);
    if (((w0 & 31) != 1 && (w0 & 31) != 2) || ((w0 >> 5) & 31) != 4) throw std::runtime_error("expected native SPH version 4");
    std::vector<Shader::Ampere::Instruction> native;
    Shader::Ampere::ProgramCaptureState capture;
    bool complete=false;
    for (size_t off=0x80;off+16<=bytes.size();off+=16) {
        Shader::Ampere::Instruction instruction;
        std::memcpy(instruction.words.data(),bytes.data()+off,16);
        native.push_back(instruction);
        auto result=capture.Observe(instruction,static_cast<u32>(native.size()-1));
        if (result==Shader::Ampere::ProgramCaptureState::Result::InvalidTarget) throw std::runtime_error("invalid native branch target");
        if (result==Shader::Ampere::ProgramCaptureState::Result::Complete) { complete=true; break; }
    }
    if (!complete) throw std::runtime_error("native input ends before the complete program was captured");
    auto lowered=Shader::Ampere::LowerToMaxwell(native);
    fmt::print("native {:016x}, SPH type {}, {} instructions: {}\n",Shader::Ampere::NativeProgramKey(native),
               (w0>>10)&15,native.size(),lowered ? "lowered with mesh context" : "lowering rejected");
    if (!lowered) throw std::runtime_error(fmt::format("at {:#x}: {}",lowered.error_offset,lowered.error));
    if (std::string_view(argv[1])=="inspect" && argc==3) return 0;
    bool task=std::string_view(argv[1])=="compile-task";
    if ((!task && std::string_view(argv[1])!="compile") || argc!=7) return 2;
    std::array<u32,3> registers;
    for(size_t i=0;i<3;i++) registers[i]=static_cast<u32>(std::stoul(argv[4+i],nullptr,0));
    MeshEnvironment env(bytes,lowered,registers,task);
    Shader::ObjectPool<Shader::IR::Inst> instructions;
    Shader::ObjectPool<Shader::IR::Block> blocks;
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow;
    Shader::Maxwell::Flow::CFG cfg(env,flow,env.StartAddress());
    Shader::HostTranslateInfo host;
    host.min_ssbo_alignment=16; host.support_float64=true; host.support_int64=true;
    auto program=Shader::Maxwell::TranslateProgram(instructions,blocks,env,cfg,host);
    fmt::print("mesh output lines: vertex {}, primitive {}, payload words {}\n", program.mesh.vertex_lines.size(),
               program.mesh.primitive_lines.size(), program.mesh.payload_words);
    std::set<u32> driver_offsets;
    for (const auto* block : program.post_order_blocks)
        for (const auto& instruction : block->Instructions()) {
            const auto op = instruction.GetOpcode();
            if (op != Shader::IR::Opcode::GetCbufU32 && op != Shader::IR::Opcode::GetCbufF32 &&
                op != Shader::IR::Opcode::GetCbufU32x2) continue;
            if (instruction.Arg(0).IsImmediate() && instruction.Arg(0).U32() == 0 && instruction.Arg(1).IsImmediate())
                driver_offsets.insert(instruction.Arg(1).U32());
        }
    fmt::print("driver-bank immediate reads:");
    for (u32 offset : driver_offsets) fmt::print(" {:#x}", offset);
    fmt::print("\n");
    Shader::Profile profile;
    profile.supported_spirv=0x00010500;
    profile.unified_descriptor_binding=true;
    profile.support_int64=true;
    profile.min_ssbo_alignment=16;
    auto spirv=Shader::Backend::SPIRV::EmitSPIRV(profile,program);
    if (!program.info.texture_descriptors.empty() || !program.info.image_descriptors.empty() ||
        !program.info.texture_buffer_descriptors.empty() || !program.info.image_buffer_descriptors.empty())
        throw std::runtime_error("standalone binding export does not yet support image resources");
    std::ofstream output(argv[3],std::ios::binary);
    output.write(reinterpret_cast<const char*>(spirv.data()),spirv.size()*4);
    if (!output) throw std::runtime_error("cannot write SPIR-V output");
    std::ofstream metadata(std::string(argv[3])+".bindings");
    metadata << "MSH2 " << (task ? 6 : 7) << ' ' << std::hex << Shader::Ampere::NativeProgramKey(native) << std::dec;
    for (u32 reg : registers) metadata << ' ' << reg;
    metadata << ' ' << program.info.constant_buffer_descriptors.size()+program.info.storage_buffers_descriptors.size() << '\n';
    u32 binding=0;
    for (const auto& resource : program.info.constant_buffer_descriptors) {
        metadata << "U " << binding << ' ' << resource.index << ' ' << resource.count << '\n';
        binding+=resource.count;
    }
    for (const auto& resource : program.info.storage_buffers_descriptors) {
        metadata << "S " << binding << ' ' << resource.cbuf_index << ' ' << resource.cbuf_offset << ' '
                 << resource.count << ' ' << resource.is_written << ' ' << resource.indirect_offset << '\n';
        binding+=resource.count;
    }
    if (!metadata) throw std::runtime_error("cannot write binding metadata");
    fmt::print("{} SPIR-V words, {} vertices, {} primitives\n",spirv.size(),program.mesh.max_vertices,program.mesh.max_primitives);
    return 0;
} catch(const std::exception& error) {
    std::cerr << "mesh-xlate: " << error.what() << '\n';
    return 1;
}
