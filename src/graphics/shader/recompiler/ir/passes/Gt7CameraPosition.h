#pragma once

#include "graphics/shader/recompiler/ir/ShaderIR.h"

#include <array>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace Libs::Graphics::ShaderRecompiler::IR {

namespace Gt7CameraPositionDetail {

// This is a deliberately narrow proof, not a search for a familiar constant-buffer layout.
// Run after SSA/identity simplification, before resource tracking replaces scalar reads by
// snapshots. The four final clip coordinates must be one row vector times s[10:11]+80.
inline Value Bits(Value value) {
    for (unsigned count = 0; count != 16; ++count) {
        const auto* inst = value.TryInstruction();
        if (inst == nullptr) break;
        const auto op = inst->GetOpcode();
        if (op != ValueOpcode::Identity && op != ValueOpcode::BitCastF32U32 &&
            op != ValueOpcode::BitCastU32F32) break;
        if (inst->NumArgs() != 1) return {};
        value = inst->Arg(0);
    }
    if (value.IsImmediate() && (value.GetType() == Type::F32 || value.GetType() == Type::U32))
        return Value(static_cast<uint32_t>(value.ImmediateBits()));
    return value;
}

inline bool Immediate(Value value, Type type, uint64_t expected) {
    return value.IsImmediate() && value.GetType() == type && value.ImmediateBits() == expected;
}

inline bool UserRegister(Value value, uint32_t reg) {
    const auto* inst = Bits(value).TryInstruction();
    return inst != nullptr && inst->GetOpcode() == ValueOpcode::GetUserData &&
           inst->NumArgs() == 1 && inst->Arg(0).GetType() == Type::ScalarReg &&
           RegIndex(inst->Arg(0).ScalarRegister()) == reg;
}

inline uint32_t MatrixIndex(const Program& program, Value value) {
    constexpr auto Missing = std::numeric_limits<uint32_t>::max();
    const auto* load = Bits(value).TryInstruction();
    if (load == nullptr || load->GetOpcode() != ValueOpcode::LoadAddressU32 ||
        load->NumArgs() != 4 || !Immediate(load->Arg(2), Type::U32, 0) ||
        !Immediate(load->Arg(3), Type::U1, 1)) return Missing;
    const auto index = load->Flags<MemoryFlags>().index;
    if (index >= program.memory_info.size()) return Missing;
    const auto& memory = program.memory_info[index];
    if (memory.kind != ResourceKind::ScalarAddress || memory.data_bits != 32 ||
        memory.data_dwords != 1 || !load->Arg(1).IsImmediate() ||
        load->Arg(1).GetType() != Type::U32) return Missing;
    const auto* address = Bits(load->Arg(0)).TryInstruction();
    if (address == nullptr || address->GetOpcode() != ValueOpcode::GetAddressResource ||
        address->NumArgs() != 2 || !UserRegister(address->Arg(0), 10) ||
        !UserRegister(address->Arg(1), 11)) return Missing;
    const uint64_t offset = uint64_t(memory.offset) + load->Arg(1).U32();
    if (offset < 80 || offset > 140 || (offset & 3u) != 0) return Missing;
    return static_cast<uint32_t>((offset - 80) / 4);
}

// The compiler can emit vertex * (model * camera), so checking only a final four-term dot
// misses otherwise identical camera producers. Expand just the arithmetic surrounding camera
// coefficients. Each final column must have the same multiset of source products for each row.
// Opaque source expressions are compared by SSA identity; no numerical sampling is involved.
struct Term {
    uint32_t matrix = 0;
    uint32_t factor_count = 0;
    std::array<Value, 4> factors {};
};

struct Expression {
    bool valid = false;
    Value opaque;
    std::vector<Term> terms;
};

inline bool IsZero(const Expression& expr) {
    return expr.valid && expr.terms.empty() && Immediate(expr.opaque, Type::U32, 0);
}

inline Expression Multiply(const Expression& a, const Expression& b, Value opaque) {
    if (!a.valid || !b.valid) return {};
    if (a.terms.empty() && b.terms.empty()) return {true, opaque, {}};
    if (!a.terms.empty() && !b.terms.empty()) return {}; // Quadratic camera coefficients.
    auto result = a.terms.empty() ? b : a;
    const auto factor = a.terms.empty() ? a.opaque : b.opaque;
    if (factor.IsEmpty()) return {};
    if (Immediate(factor, Type::U32, 0x3f800000u)) return result;
    for (auto& term : result.terms) {
        if (term.factor_count == term.factors.size()) return {};
        term.factors[term.factor_count++] = factor;
    }
    return result;
}

inline Expression Add(const Expression& a, const Expression& b, Value opaque) {
    if (!a.valid || !b.valid) return {};
    if (a.terms.empty() && b.terms.empty()) return {true, opaque, {}};
    if (a.terms.empty()) return IsZero(a) ? b : Expression {};
    if (b.terms.empty()) return IsZero(b) ? a : Expression {};
    if (a.terms.size() + b.terms.size() > 64) return {};
    auto result = a;
    result.terms.insert(result.terms.end(), b.terms.begin(), b.terms.end());
    return result;
}

class Expand {
public:
    explicit Expand(const Program& program) : program(program) {}

    Expression Get(Value value, unsigned depth = 0) {
        value = Bits(value);
        if (value.IsEmpty() || depth > 80) return {};
        const auto matrix = MatrixIndex(program, value);
        if (matrix != std::numeric_limits<uint32_t>::max())
            return {true, {}, {{matrix, 0, {}}}};
        const auto* inst = value.TryInstruction();
        if (inst == nullptr) return {true, value, {}};
        if (const auto found = cache.find(inst); found != cache.end()) return found->second;
        if (remaining == 0) return {};
        --remaining;
        // Insert a failed sentinel first: malformed cyclic IR must not recurse indefinitely.
        cache.emplace(inst, Expression {});
        Expression result {true, value, {}};
        const auto op = inst->GetOpcode();
        if (op == ValueOpcode::FPMul32 || op == ValueOpcode::FPAdd32 ||
            op == ValueOpcode::FPFma32 || op == ValueOpcode::FPMad32) {
            const auto expected_args = op == ValueOpcode::FPMul32 || op == ValueOpcode::FPAdd32 ? 2u : 3u;
            if (inst->NumArgs() != expected_args) return {};
            const auto a = Get(inst->Arg(0), depth + 1);
            const auto b = Get(inst->Arg(1), depth + 1);
            if (op == ValueOpcode::FPAdd32) result = Add(a, b, value);
            else if (op == ValueOpcode::FPMul32) result = Multiply(a, b, value);
            else {
                const auto c = Get(inst->Arg(2), depth + 1);
                if (a.valid && b.valid && c.valid && a.terms.empty() &&
                    b.terms.empty() && c.terms.empty()) result = {true, value, {}};
                else result = Add(Multiply(a, b, {}), c, value);
            }
        }
        cache[inst] = result;
        return result;
    }

private:
    const Program& program;
    unsigned remaining = 8192;
    std::unordered_map<const Inst*, Expression> cache;
};

inline bool SameSourceProduct(const Term& a, const Term& b) {
    if (a.matrix / 4 != b.matrix / 4 || a.factor_count != b.factor_count) return false;
    std::array<bool, 4> used {};
    for (uint32_t i = 0; i != a.factor_count; ++i) {
        bool found = false;
        for (uint32_t j = 0; j != b.factor_count; ++j) {
            if (!used[j] && a.factors[i] == b.factors[j]) {
                used[j] = true;
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

inline bool SameSourceVector(const Expression& a, const Expression& b) {
    if (a.terms.size() != b.terms.size()) return false;
    std::array<bool, 64> used {};
    for (const auto& term : a.terms) {
        bool found = false;
        for (size_t i = 0; i != b.terms.size(); ++i) {
            if (!used[i] && SameSourceProduct(term, b.terms[i])) {
                used[i] = true;
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

inline bool StraightLine(const Program& program) {
    if (program.blocks.empty() || program.dispatcher_fallback ||
        program.cfg_failure_kind != CFG::FailureKind::None) return false;
    for (const auto& block : program.block_info) {
        if (block.terminator.loop_header ||
            (block.terminator.kind != CFG::TerminatorKind::Branch &&
             block.terminator.kind != CFG::TerminatorKind::Return)) return false;
    }
    const Block* current = program.blocks.front();
    for (size_t count = 0; count != program.blocks.size(); ++count) {
        if (current == nullptr || current->ImmPredecessors().size() != (count == 0 ? 0u : 1u) ||
            current->ImmSuccessors().size() > 1) return false;
        if (current->ImmSuccessors().empty()) return count + 1 == program.blocks.size();
        current = current->ImmSuccessors().front();
    }
    return false; // Cycle or a disconnected/unvisited block.
}

} // namespace Gt7CameraPositionDetail

// Metadata only. It never changes shader code or enables frame generation on its own.
// Renderer still has to validate the current camera values, raster state and exact TAA pair.
[[nodiscard]] inline bool HasGt7CameraPositionContract(const Program& program) {
    using namespace Gt7CameraPositionDetail;
    if (program.stage != ShaderType::Vertex || program.user_data_base > 10 ||
        uint64_t(program.user_data_base) + program.user_data_count < 12 ||
        !StraightLine(program)) return false;
    const Inst* position = nullptr;
    for (const auto* block : program.blocks) {
        for (const auto& inst : *block) {
            // Even a selected source coordinate needs a separate control-flow proof.
            if (inst.GetOpcode() == ValueOpcode::Phi) return false;
            if (inst.GetOpcode() != ValueOpcode::SetAttribute) continue;
            const auto index = inst.Flags<ExportFlags>().index;
            if (index >= program.export_info.size()) return false;
            const auto& exp = program.export_info[index];
            // POS1+ carry point/clip/cull/viewport/layer outputs and cannot overwrite POS0.
            // The renderer separately rejects viewport/layer routing for camera producers.
            if (exp.kind != ExportTargetKind::Position || exp.index != 0) continue;
            if (position != nullptr || exp.en != 0xf || exp.compr ||
                inst.NumArgs() != 2 || !Immediate(inst.Arg(1), Type::U1, 1)) return false;
            position = &inst;
        }
    }
    if (position == nullptr) return false;
    const auto* vector = Bits(position->Arg(0)).TryInstruction();
    if (vector == nullptr || vector->GetOpcode() != ValueOpcode::CompositeConstructU32x4 ||
        vector->NumArgs() != 4) return false;
    Expand expand(program);
    std::array<Expression, 4> dots {};
    for (uint32_t column = 0; column != 4; ++column) {
        dots[column] = expand.Get(vector->Arg(column));
        if (!dots[column].valid || dots[column].terms.empty()) return false;
        uint32_t rows = 0;
        for (const auto& term : dots[column].terms) {
            if (term.matrix % 4 != column) return false;
            rows |= 1u << (term.matrix / 4);
        }
        if (rows != 0xf || (column != 0 && !SameSourceVector(dots[0], dots[column]))) return false;
    }
    return true;
}

} // namespace Libs::Graphics::ShaderRecompiler::IR
