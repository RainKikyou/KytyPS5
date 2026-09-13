#include "graphics/shader/recompiler/ir/ShaderIR.h"
#include "graphics/shader/recompiler/ir/passes/ResourceMaterialization.h"

#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

void Check(bool value, const char *text) {
  if (!value) {
    std::fprintf(stderr, "ResourceMaterializationTests: failed: %s\n", text);
    std::abort();
  }
}

bool RejectSpecializationRead(void *userdata, uint64_t, uint32_t *) {
  ++*static_cast<uint32_t *>(userdata);
  return false;
}

Libs::Graphics::ShaderRecompiler::IR::Block &
AddValueBlock(Libs::Graphics::ShaderRecompiler::IR::Program &program) {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  auto block = std::make_unique<Block>();
  auto *result = block.get();
  program.blocks.push_back(result);
  program.block_info.push_back({.id = 0});
  program.block_storage.push_back(std::move(block));
  return *result;
}

Libs::Graphics::ShaderRecompiler::IR::ResourcePlan SrtPlan(uint64_t address) {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  Program program;
  program.stage = Libs::Graphics::ShaderType::Compute;
  program.srt_plan_complete = true;
  program.resource_tracking_complete = true;
  auto &value_block = AddValueBlock(program);

  MemoryInfo memory;
  memory.kind = ResourceKind::ScalarAddress;
  memory.planning_only = true;
  program.memory_info.push_back(memory);
  const auto low = Value(static_cast<uint32_t>(address));
  const auto high = Value(static_cast<uint32_t>(address >> 32u));
  auto &handle =
      value_block.AppendNewInst(ValueOpcode::GetAddressResource, {low, high});
  auto &raw = value_block.AppendNewInst(
      ValueOpcode::LoadAddressU32,
      {Value(&handle), Value(0u), Value(0u), Value(true)});
  raw.SetFlags(MemoryFlags{.index = 0, .pc = 0x40});
  program.srt_reads.push_back({Value(&raw), 0});

  auto &srt = value_block.AppendNewInst(ValueOpcode::GetSrtResource);
  auto &flat = value_block.AppendNewInst(ValueOpcode::ReadConst,
                                         {Value(&srt), Value(0u)});
  DescriptorSource source;
  source.dwords[0] = Value(&flat);
  source.dwords[1] = Value(0u);
  source.dword_count = 2;
  program.descriptor_sources.push_back(source);
  return ExtractResourcePlan(program);
}

Libs::Graphics::ShaderRecompiler::IR::ResourcePlan UnbasedFlatPlan() {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  Program program;
  program.stage = Libs::Graphics::ShaderType::Compute;
  program.srt_plan_complete = true;
  program.resource_tracking_complete = true;
  AddValueBlock(program);
  program.info.uses_dma = true;
  return ExtractResourcePlan(program);
}

Libs::Graphics::ShaderRecompiler::IR::ResourcePlan UserDataBufferPlan() {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  Program program;
  program.stage = Libs::Graphics::ShaderType::Compute;
  program.srt_plan_complete = true;
  program.resource_tracking_complete = true;
  auto &value_block = AddValueBlock(program);

  auto &user_data = value_block.AppendNewInst(
      ValueOpcode::GetUserData, {Value(static_cast<ScalarReg>(0))});
  DescriptorSource source;
  source.dwords[0] = Value(&user_data);
  source.dwords[1] = Value(0u);
  source.dwords[2] = Value(0u);
  source.dwords[3] = Value(0u);
  source.dword_count = 4;
  program.descriptor_sources.push_back(source);
  program.info.buffers.push_back({.source = 0});
  return ExtractResourcePlan(program);
}

Libs::Graphics::ShaderRecompiler::IR::ResourcePlan MixedSamplerPlan() {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  Program program;
  program.stage = Libs::Graphics::ShaderType::Compute;
  program.srt_plan_complete = true;
  program.resource_tracking_complete = true;
  AddValueBlock(program);

  const auto AddSource = [&program](uint32_t dword_count, uint32_t first) {
    DescriptorSource source;
    source.dword_count = dword_count;
    source.dwords[0] = Value(first);
    for (uint32_t i = 1; i < dword_count; i++) {
      source.dwords[i] = Value(0u);
    }
    program.descriptor_sources.push_back(source);
    return static_cast<uint32_t>(program.descriptor_sources.size() - 1u);
  };

  const auto image0 = AddSource(8, 0);
  const auto image1 = AddSource(8, 0);
  const auto sampler0 = AddSource(4, 0x11111111u);
  const auto sampler1 = AddSource(4, 0x22222222u);
  program.info.images.push_back(
      {.source = image0,
       .resource_class = ImageResourceClass::Sampled,
       .numeric_class = Libs::Graphics::Prospero::TextureNumericClass::Float,
       .dimension =
           Libs::Graphics::ShaderRecompiler::Decoder::ImageDimension::Dim2D});
  program.info.images.push_back(
      {.source = image1,
       .resource_class = ImageResourceClass::Sampled,
       .numeric_class = Libs::Graphics::Prospero::TextureNumericClass::Float,
       .dimension =
           Libs::Graphics::ShaderRecompiler::Decoder::ImageDimension::Dim2D,
       .conversion_format =
           Libs::Graphics::Prospero::BufferFormat::k8_8_8_8UNorm});
  program.info.samplers.push_back({.source = sampler0});
  program.info.samplers.push_back({.source = sampler1});
  program.info.sampled_pairs.push_back({.image = 0, .sampler = 0});
  program.info.sampled_pairs.push_back({.image = 0, .sampler = 1});
  program.info.sampled_pairs.push_back({.image = 1, .sampler = 1});
  return ExtractResourcePlan(program);
}

void TestMappedSrtUsesDirectReaderByDefault() {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  const uint32_t dword = 0x12345678;
  auto plan = SrtPlan(reinterpret_cast<uint64_t>(&dword));
  uint32_t specialization_reads = 0;
  const SrtRuntime runtime{.userdata = &specialization_reads,
                           .read_specialization_memory =
                               RejectSpecializationRead};
  ResourceSnapshot snapshot;
  ResourceSpecialization specialization;
  Check(MaterializeResources(plan, runtime, snapshot, specialization),
        "mapped SRT stage materialization failed");
  Check(specialization_reads == 0,
        "ordinary SRT read used the specialization reader");
  Check(snapshot.flattened_srt.size() == 1 &&
            snapshot.flattened_srt[0] == dword,
        "cache rematerialization did not use the direct reader by default");
}

void TestIntegerRuntimeValueFollowsSrtReads() {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  auto plan = SrtPlan(0x10000);
  const auto root = plan.descriptor_sources.front().dwords[0];
  Check(ValidateRuntimeValue(plan, root, RuntimeValueType::Integer),
        "integer SRT read was rejected");

  Block values;
  auto &comparison = values.AppendNewInst(ValueOpcode::FPOrdLessThanEqual32,
                                          {Value::F32(1.f), Value::F32(0.f)});
  auto &selection = values.AppendNewInst(
      ValueOpcode::SelectU32, {Value(&comparison), Value(1u), Value(0u)});
  plan.srt_reads[0].value = Value(&selection);
  Check(ValidateRuntimeValue(plan, root),
        "ordinary SRT validation rejected a floating-point dependency");
  Check(!ValidateRuntimeValue(plan, root, RuntimeValueType::Integer),
        "integer SRT validation missed a hidden floating-point dependency");

  auto &first =
      values.AppendNewInst(ValueOpcode::ReadFirstLane, {root, Value(true)});
  Check(!ValidateRuntimeValue(plan, Value(&first), RuntimeValueType::Integer),
        "read-first-lane lost integer-only SRT validation");

  auto &active = values.AppendNewInst(ValueOpcode::ReadFirstLane,
                                      {Value(&selection), Value(&comparison)});
  Check(!ValidateRuntimeValue(plan, Value(&active), RuntimeValueType::Integer),
        "floating-point execution mask was accepted as integer-only");

  auto &lane = values.AppendNewInst(
      ValueOpcode::GetBuiltin,
      {Value(static_cast<uint32_t>(StageInputKind::LocalInvocationId)),
       Value(0u)});
  auto &mask =
      values.AppendNewInst(ValueOpcode::INotEqual32, {Value(&lane), Value(0u)});
  selection.SetArg(0, Value(&mask));
  active.SetArg(1, Value(&mask));
  Check(ValidateRuntimeValue(plan, Value(&active), RuntimeValueType::Integer),
        "nonuniform integer execution mask was rejected");
  auto &float_value =
      values.AppendNewInst(ValueOpcode::BitCastU32F32, {Value::F32(1.f)});
  selection.SetArg(2, Value(&float_value));
  Check(!ValidateRuntimeValue(plan, Value(&active), RuntimeValueType::Integer),
        "floating-point inactive arm was accepted as integer-only");

  plan.srt_reads[0].value = Value(&first);
  Check(!ValidateRuntimeValue(plan, root, RuntimeValueType::Integer),
        "cyclic SRT read-first-lane dependency was accepted");
}

void TestUnbasedFlatCacheHitMaterializes() {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  auto plan = UnbasedFlatPlan();
  ResourceSnapshot snapshot;
  ResourceSpecialization specialization;
  Check(MaterializeResources(plan, {}, snapshot, specialization),
        "unbased FLAT stage materialization failed");
  Check(snapshot.buffers.empty() && snapshot.images.empty(),
        "unbased FLAT plan produced unexpected descriptors");
}

void TestFailedMaterializationPreservesPriorStage() {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  auto plan = UserDataBufferPlan();
  ResourceSnapshot snapshot;
  snapshot.user_data.push_back(0xfeedbeefu);
  ResourceSpecialization specialization;
  specialization.buffers.push_back({.packed_stride = 7});
  Check(!MaterializeResources(plan, {}, snapshot, specialization),
        "missing runtime user data did not reject the cached stage");
  Check(snapshot.user_data == std::vector<uint32_t>{0xfeedbeefu} &&
            specialization.buffers.size() == 1 &&
            specialization.buffers[0].packed_stride == 7,
        "failed cache materialization changed its destinations");
}

void TestMixedSamplerDuplicatesTheCorrectSnapshot() {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  auto plan = MixedSamplerPlan();
  ResourceSnapshot snapshot;
  ResourceSpecialization specialization;
  Check(MaterializeResources(plan, {}, snapshot, specialization),
        "mixed sampler materialization failed");
  Check(snapshot.samplers.size() == 3,
        "mixed sampler materialization appended unrelated samplers");
  Check(snapshot.samplers[2] == snapshot.samplers[1] &&
            snapshot.samplers[2] != snapshot.samplers[0],
        "point sampler variant duplicated the wrong runtime descriptor");
}

void TestBdaReadPlanIntervals() {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  Program program;
  program.info.uses_dma = true;
  program.user_data_base = 12;
  program.user_data_count = 2;
  auto& block = AddValueBlock(program);
  const auto emit = [&](ValueOpcode op, std::initializer_list<Value> args) {
    return Value(&block.AppendNewInst(op, args));
  };
  const auto low = emit(ValueOpcode::GetUserData, {Value(ScalarReg{12})});
  const auto high = emit(ValueOpcode::GetUserData, {Value(ScalarReg{13})});
  const auto handle = emit(ValueOpcode::GetAddressResource, {low, high});
  const auto x = emit(ValueOpcode::UndefU32, {});
  const auto condition = emit(ValueOpcode::ULessThan32, {x, Value(4u)});
  const auto active = emit(ValueOpcode::LogicalAnd, {condition, Value(true)});
  const auto sum = emit(ValueOpcode::IAdd32, {x, Value(33u)});
  const auto shift = emit(ValueOpcode::ShiftLeftLogical32, {sum, Value(6u)});
  const auto selected = emit(ValueOpcode::SelectU32, {condition, shift, Value(UINT32_MAX)});
  auto& load = block.AppendNewInst(ValueOpcode::LoadAddressU32,
      {handle, selected, Value(0u), active});
  load.SetFlags(MemoryFlags{0, 0});
  program.memory_info.push_back({.kind = ResourceKind::Global, .offset = 4});
  ResourceSnapshot snapshot;
  snapshot.user_data = {0x10000u, 9u};
  std::vector<Libs::Graphics::GuestRange> ranges;
  auto plan = BuildBdaReadPlan(program);
  Check(plan.complete && plan.spans.size() == 1 && plan.spans[0].begin == 33 * 64 + 4 &&
        plan.spans[0].end == 36 * 64 + 8 && EvaluateBdaReadPlan(plan, snapshot, ranges),
        "guarded dynamic address did not retain its proven unsigned bounds");
  Check(ranges.size() == 1 && ranges[0].address == 0x900010844ull && ranges[0].size == 196,
        "bounded plan did not resolve runtime user data or preserve its last word");

  // A guard does not constrain a load that executes outside it, including a
  // selected inactive address. The broad range must include U32 wraparound.
  load.SetArg(3, Value(true));
  plan = BuildBdaReadPlan(program);
  Check(plan.complete && plan.spans[0].begin == 4 && plan.spans[0].end == int64_t(UINT32_MAX) + 8,
        "unguarded selection incorrectly borrowed a condition from its true arm");
  const auto wrapping = emit(ValueOpcode::IAdd32, {x, Value(UINT32_MAX)});
  load.SetArg(1, wrapping);
  plan = BuildBdaReadPlan(program);
  Check(plan.spans[0].begin == 4 && plan.spans[0].end == int64_t(UINT32_MAX) + 8,
        "wrapping addition narrowed an unknown address");
  const auto aligned = emit(ValueOpcode::ShiftLeftLogical32, {x, Value(4u)});
  load.SetArg(1, aligned);
  plan = BuildBdaReadPlan(program);
  Check(plan.spans[0].begin == 4 && plan.spans[0].end == int64_t(0xfffffff0u) + 8,
        "wrapping left shift lost its conservative alignment bound");
  const auto masked = emit(ValueOpcode::BitwiseAnd32, {x, Value(255u)});
  const auto product = emit(ValueOpcode::IMul32, {masked, Value(12u)});
  load.SetArg(1, product);
  plan = BuildBdaReadPlan(program);
  Check(plan.spans[0].begin == 4 && plan.spans[0].end == 255 * 12 + 8,
        "masked multiplication did not cover all possible indices");

  // Shared expression DAGs must terminate without treating incomplete analysis
  // as an empty footprint or relying on any observed runtime value.
  auto dag = x;
  for (unsigned i = 0; i < 80; ++i) dag = emit(ValueOpcode::IAdd32, {dag, dag});
  load.SetArg(1, dag);
  plan = BuildBdaReadPlan(program);
  Check(plan.complete && plan.spans[0].end == int64_t(UINT32_MAX) + 8,
        "bounded analysis budget lost an unknown address dependency");

  program.memory_info[0].kind = ResourceKind::ScalarAddress;
  program.memory_info[0].offset = uint32_t(-1);
  load.SetArg(1, Value(7u));
  snapshot.user_data = {0x10003u, 0xabcd0009u};
  plan = BuildBdaReadPlan(program);
  Check(EvaluateBdaReadPlan(plan, snapshot, ranges) && ranges.size() == 1 &&
        ranges[0].address == 0x900010000ull && ranges[0].size == 4,
        "SMEM plan did not separately mask its 48-bit base, offset and immediate");

  program.memory_info[0].kind = ResourceKind::Global;
  program.memory_info[0].offset = uint32_t(-4);
  program.memory_info[0].address_is_full = true;
  load.SetArg(1, Value(0x10001u));
  load.SetArg(2, Value(9u));
  plan = BuildBdaReadPlan(program);
  Check(EvaluateBdaReadPlan(plan, snapshot, ranges) && ranges.size() == 1 &&
        ranges[0].address == 0x90000fffCull && ranges[0].size == 8,
        "full unaligned address did not include both physical dword loads");
  load.SetArg(1, x);
  Check(!BuildBdaReadPlan(program).complete, "dynamic full-address base was accepted");
  program.memory_info[0].address_is_full = false;
  load.SetArg(1, Value(0u));
  auto& store = block.AppendNewInst(ValueOpcode::StoreAddressU32,
      {handle, Value(0u), Value(0u), Value(1u), Value(true)});
  store.SetFlags(MemoryFlags{0, 0});
  Check(!BuildBdaReadPlan(program).complete, "address store was accepted as a read-only footprint");
}

void TestBdaReadPlanTransactions() {
  using namespace Libs::Graphics;
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  using Kind = BdaReadWord::Kind;
  BdaReadPlan plan;
  plan.complete = true;
  plan.spans = {{{Kind::Flat, 0}, {Kind::Flat, 1}, 1, 7, false},
                {{Kind::Immediate, 0x10004}, {Kind::Immediate, 9}, 0, 8, false}};
  ResourceSnapshot snapshot;
  snapshot.flattened_srt = {0x10000, 9};
  std::vector<GuestRange> ranges;
  Check(EvaluateBdaReadPlan(plan, snapshot, ranges) &&
        ranges == std::vector<GuestRange>{{0x900010000ull, 12}},
        "flat pointer, unaligned spans or overlapping bases were not merged correctly");
  const auto saved = ranges;
  snapshot.flattened_srt.pop_back();
  Check(!EvaluateBdaReadPlan(plan, snapshot, ranges) && ranges == saved,
        "missing flat word partially replaced a prior valid footprint");
  snapshot.flattened_srt = {0x10000, 9};
  plan.spans[0].begin = INT64_MIN;
  Check(!EvaluateBdaReadPlan(plan, snapshot, ranges) && ranges == saved,
        "signed underflow produced an address or changed the prior footprint");
  plan.spans[0].begin = 0;
  plan.spans[0].end = INT64_MAX;
  Check(!EvaluateBdaReadPlan(plan, snapshot, ranges) && ranges == saved,
        "out-of-tracker read was accepted");
  snapshot.flattened_srt = {UINT32_MAX, UINT32_MAX};
  plan.spans[0].end = 4;
  Check(!EvaluateBdaReadPlan(plan, snapshot, ranges) && ranges == saved,
        "overflowing 64-bit address was accepted");
  snapshot.flattened_srt = {0xfffffffcu, 0xffu};
  plan.spans.resize(1);
  Check(EvaluateBdaReadPlan(plan, snapshot, ranges) &&
        ranges == std::vector<GuestRange>{{TRACKER_ADDRESS_SIZE - 4, 4}},
        "last valid tracker dword was rejected");
}

} // namespace

namespace Common {

int DbgExitHandler(const char *, int, std::string_view) { std::abort(); }

int DbgExitHandler(const char *, int, fmt::text_style, std::string_view) {
  std::abort();
}

int DbgExitIfHandler(const char *, const char *, int) { return 1; }

void DbgExit(int) { std::abort(); }

} // namespace Common

int main() {
  TestBdaReadPlanIntervals();
  TestBdaReadPlanTransactions();
  TestMappedSrtUsesDirectReaderByDefault();
  TestIntegerRuntimeValueFollowsSrtReads();
  TestUnbasedFlatCacheHitMaterializes();
  TestFailedMaterializationPreservesPriorStage();
  TestMixedSamplerDuplicatesTheCorrectSnapshot();
  std::puts("ResourceMaterializationTests: all cases passed");
  return 0;
}

// Keep this focused standalone target self-contained by amalgamating its small
// typed-IR implementation set.
#include "graphics/shader/recompiler/ir/Block.cpp"
#include "graphics/shader/recompiler/ir/Program.cpp"
#include "graphics/shader/recompiler/ir/Type.cpp"
#include "graphics/shader/recompiler/ir/Value.cpp"
#include "graphics/shader/recompiler/ir/opcodes/ValueOpcodes.cpp"
