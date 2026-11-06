// Synthetic shaders only. Optional local probes never embed or load guest data in the test suite.
namespace PixelInputContractTest {
namespace IR = ShaderRecompiler::IR;

ShaderRecompiler::CompileResult Compile(std::span<const uint32_t> code,
                                       const ShaderPixelInputInfo& input) {
  auto options = MakeCompileOptions(ShaderType::Pixel);
  options.input_info.pixel = &input;
  auto translated = ShaderRecompiler::TranslateProgram(code, options);
  Check(!translated.unsupported, "pixel input fixture did not translate");
  auto plan = IR::ExtractResourcePlan(translated.program);
  IR::ResourceSnapshot resources;
  IR::ResourceSpecialization specialization;
  const IR::SrtRuntime runtime{.user_data = options.user_data};
  Check(IR::MaterializeResources(plan, runtime, resources, specialization),
        "resource-free pixel input fixture did not materialize");
  return ShaderRecompiler::CompileProgram(std::move(translated), options, specialization);
}

ShaderPixelInputInfo Input(uint32_t count) {
  ShaderPixelInputInfo input{};
  input.input_num = count;
  input.ps_system_input_base = 2;
  input.ps_perspective_center_vgpr = 0;
  SetIdentityInterpolatorSettings(&input);
  return input;
}

void CheckRejected(const ShaderRecompiler::CompileResult& result) {
  Check(result.unsupported != nullptr && result.spirv.empty(),
        "missing runtime pixel input did not return unsupported without SPIR-V");
  Check(std::strstr(result.unsupported, "runtime input") != nullptr,
        "missing pixel input rejection did not describe the runtime contract");
}

void CheckAccepted(const ShaderRecompiler::CompileResult& result) {
  Check(result.unsupported == nullptr && !result.spirv.empty(),
        "valid runtime pixel input contract was rejected");
  CheckSpirvBinaryValidates(result.spirv);
}

void TestMalformedReferencesRemainInvalid() {
  IR::Program program;
  program.stage = ShaderType::Pixel;
  program.block_storage.push_back(std::make_unique<IR::Block>());
  auto* block = program.block_storage.back().get();
  program.blocks.push_back(block);
  auto& attribute = block->AppendNewInst(IR::ValueOpcode::GetAttribute,
                                        {IR::Value(0u), IR::Value(0u)});
  const auto pixel = Input(0);
  ShaderStageInputInfo input;
  input.pixel = &pixel;
  const auto rejected = [&] { return IR::UnsupportedPixelInputReferences(program, input); };
  Check(rejected() != nullptr, "well-formed missing input was not recognized");

  attribute.SetArg(0, IR::Value::F32(0.0f));
  Check(rejected() == nullptr, "invalid attribute type was reclassified as runtime unsupported");
  attribute.SetArg(0, IR::Value(0u));
  attribute.SetArg(1, IR::Value(4u));
  Check(rejected() == nullptr, "invalid attribute component bypassed IR validation");
  attribute.SetArg(1, IR::Value(0u));

  // A later malformed reference must not be hidden by an earlier missing runtime input.
  auto& interpolation = block->AppendNewInst(IR::ValueOpcode::GetInterpolationParameter,
                                             {IR::Value(0u), IR::Value(0u), IR::Value(3u)});
  Check(rejected() == nullptr, "invalid interpolation vertex bypassed IR validation");
  interpolation.SetArg(2, IR::Value(2u));
  Check(rejected() != nullptr, "well-formed interpolation input was not recognized");
  interpolation.SetArg(1, IR::Value::F32(0.0f));
  Check(rejected() == nullptr, "invalid interpolation component type bypassed IR validation");
  interpolation.SetArg(1, IR::Value(0u));

  input.pixel = nullptr;
  Check(rejected() == nullptr, "absent input metadata bypassed IR validation");
  auto invalid_input = Input(33);
  input.pixel = &invalid_input;
  Check(rejected() == nullptr, "invalid input count bypassed IR validation");
  input.pixel = &pixel;
  program.stage = ShaderType::Vertex;
  Check(rejected() == nullptr, "pixel input rejection affected a different shader stage");
}
} // namespace PixelInputContractTest

void TestPixelInputContract() {
  using namespace PixelInputContractTest;
  const uint32_t four_attributes[] = {
      EncodeVintrp(0, 8, 0, 0, 0), EncodeVintrp(1, 8, 0, 0, 1),
      EncodeVintrp(0, 9, 1, 1, 0), EncodeVintrp(1, 9, 1, 1, 1),
      EncodeVintrp(0, 10, 2, 2, 0), EncodeVintrp(1, 10, 2, 2, 1),
      EncodeVintrp(0, 11, 3, 3, 0), EncodeVintrp(1, 11, 3, 3, 1),
      EncodeExp0(0x00, 0xf), EncodeExp1(8, 9, 10, 11), 0xbf810000u,
  };
  for (const uint32_t count : {0u, 3u, 4u}) {
    const auto result = Compile(four_attributes, Input(count));
    if (count < 4) {
      CheckRejected(result);
      if (count == 0) {
        Check(std::strstr(result.unsupported, "input_num=0") != nullptr,
              "zero-input runtime contract was missing from the rejection diagnostic");
      }
    } else {
      CheckAccepted(result);
      Check(ProgramInputCount(result.program, IR::StageInputKind::Parameter) == 4,
            "four live exported inputs were not preserved");
    }
  }
  Check(MakeStageStaticKey(Input(0)) != MakeStageStaticKey(Input(4)) &&
            MakeStageStaticKey(Input(3)) != MakeStageStaticKey(Input(4)),
        "unsupported pixel input variant aliases a valid variant in the program cache");

  for (const uint32_t opcode : {1u, 2u}) {
    const uint32_t dead_attribute[] = {
        EncodeVintrp(opcode, 8, 31, 0, 2),
        EncodeVop1(0x01, 8, 128), // Overwrite with zero before exporting.
        EncodeExp0(0x00, 0xf), EncodeExp1(8, 8, 8, 8), 0xbf810000u,
    };
    const auto result = Compile(dead_attribute, Input(0));
    CheckAccepted(result);
    Check(ProgramInputCount(result.program, IR::StageInputKind::Parameter) == 0,
          "dead out-of-range interpolation input survived dead-code elimination");
  }

  const uint32_t mov_attribute[] = {
      EncodeVintrp(2, 8, 0, 3, 2),
      EncodeExp0(0x00, 0xf), EncodeExp1(8, 8, 8, 8), 0xbf810000u,
  };
  for (const bool custom : {false, true}) {
    for (const uint32_t count : {0u, 1u}) {
      auto input = Input(count);
      input.interpolator_settings[0] = custom ? 0x420u : 0x400u;
      input.custom_interpolation_mask = custom ? 1u : 0u;
      const auto result = Compile(mov_attribute, input);
      if (count == 0) {
        CheckRejected(result);
      } else {
        CheckAccepted(result);
        Check(ProgramInputCount(result.program, IR::StageInputKind::Parameter) == 1,
              "valid MOV interpolation input was lost");
      }
    }
  }
  TestMalformedReferencesRemainInvalid();
}

// Offline frontend replay only: no GPU, descriptor materialization, or host-memory reads.
int ProbePixelInputShader(int argc, char** argv) {
  const auto number = [](const char* text, uint32_t limit, uint32_t& out) {
    char* end = nullptr;
    const auto value = std::strtoul(text, &end, 10);
    if (*text == '\0' || *end != '\0' || value > limit) return false;
    out = static_cast<uint32_t>(value);
    return true;
  };
  uint32_t count = 0, system_base = 0, no_perspective = 0;
  uint32_t user_base = 0, user_count = 16, wave = 64;
  if (!number(argv[3], 32, count) || !number(argv[4], 256, system_base) ||
      !number(argv[5], 1, no_perspective) ||
      (argc == 9 && (!number(argv[6], 127, user_base) ||
                     !number(argv[7], 128 - user_base, user_count) ||
                     !number(argv[8], 64, wave))) || (wave != 32 && wave != 64)) return 2;
  std::ifstream file(argv[2], std::ios::binary | std::ios::ate);
  if (!file) return 2;
  const auto size = file.tellg();
  if (size <= 0 || size > (16 << 20) || (size % 4) != 0) return 2;
  std::vector<uint32_t> code(static_cast<size_t>(size) / 4);
  file.seekg(0);
  if (!file.read(reinterpret_cast<char*>(code.data()), size)) return 2;

  auto input = PixelInputContractTest::Input(count);
  input.ps_system_input_base = system_base;
  input.ps_no_perspective = no_perspective != 0;
  std::vector<uint32_t> user_data(user_count);
  auto options = MakeCompileOptions(ShaderType::Pixel);
  options.input_info.pixel = &input;
  options.non_fatal = true;
  options.bindless_images = true;
  options.user_data_base = user_base;
  options.user_data = user_data;
  options.wave_size = wave;
  auto translated = ShaderRecompiler::TranslateProgram(code, options);
  if (translated.unsupported) {
    std::puts("PS_INPUT_PROBE translation_unsupported=1");
    return 3;
  }
  auto& program = translated.program;
  ShaderRecompiler::IR::RemoveIdentities(program.blocks);
  ShaderRecompiler::IR::EliminateDeadCode(program.blocks);
  const auto* reason = ShaderRecompiler::IR::UnsupportedPixelInputReferences(program, options.input_info);
  uint32_t attributes = 0;
  for (const auto* block : program.blocks) {
    for (const auto& inst : *block) {
      if (inst.GetOpcode() != ShaderRecompiler::IR::ValueOpcode::GetAttribute &&
          inst.GetOpcode() != ShaderRecompiler::IR::ValueOpcode::GetInterpolationParameter) continue;
      if (inst.Arg(0).IsImmediate() && inst.Arg(0).GetType() == ShaderRecompiler::IR::Type::U32 &&
          inst.Arg(0).U32() < 32) attributes |= 1u << inst.Arg(0).U32();
    }
  }
  std::printf("PS_INPUT_PROBE input_num=%u system_base=%u no_perspective=%u "
              "live_attributes=0x%08x contract_rejected=%u reason=%s\n",
              count, system_base, no_perspective, attributes, reason != nullptr ? 1u : 0u,
              reason != nullptr ? reason : "none");
  return 0;
}
