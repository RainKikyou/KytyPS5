#include "graphics/shader/recompiler/backend/spirv/spirvEmitterInternal.h"

#include <algorithm>
#include <bit>

namespace Libs::Graphics::ShaderRecompiler::Spirv::Emitter {

const IR::DescriptorBinding* DescriptorBinding(const EmitterState&       state,
                                               IR::DescriptorBindingKind kind);
uint32_t DescriptorCount(const EmitterState& state, IR::DescriptorBindingKind kind);

uint32_t TypeVoid(EmitterState& state) {
	return state.builder.Type(OpTypeVoid);
}

uint32_t TypeBool(EmitterState& state) {
	return state.builder.Type(OpTypeBool);
}

uint32_t TypeBoolVector(EmitterState& state, uint32_t components) {
	return state.builder.Type(OpTypeVector, {TypeBool(state), components});
}

uint32_t TypeU32(EmitterState& state) {
	return state.builder.Type(OpTypeInt, {32, 0});
}

uint32_t TypeU64(EmitterState& state) {
	return TypeU32Vector(state, 2);
}

uint32_t TypeDeviceAddress(EmitterState& state) {
	return TypeScalarU64(state);
}

uint32_t TypeScalarU64(EmitterState& state) {
	return state.builder.Type(OpTypeInt, {64, 0});
}

uint32_t TypeU32Pair(EmitterState& state) {
	const auto element = TypeU32(state);
	return state.builder.Type(OpTypeStruct, {element, element});
}

uint32_t TypeI32(EmitterState& state) {
	return state.builder.Type(OpTypeInt, {32, 1});
}

uint32_t TypeI32Pair(EmitterState& state) {
	const auto element = TypeI32(state);
	return state.builder.Type(OpTypeStruct, {element, element});
}

uint32_t TypeF32(EmitterState& state) {
	return state.builder.Type(OpTypeFloat, {32});
}

uint32_t TypeF64(EmitterState& state) {
	return state.builder.Type(spv::OpTypeFloat, 64);
}

uint32_t TypeU32Vector(EmitterState& state, uint32_t components) {
	return state.builder.Type(OpTypeVector, {TypeU32(state), components});
}

uint32_t TypeU32Composite(EmitterState& state, uint32_t components) {
	EXIT_IF(components < 2u || components > 4u);
	return components == 2u ? TypeU32Pair(state) : TypeU32Vector(state, components);
}

uint32_t TypeI32Vector(EmitterState& state, uint32_t components) {
	return state.builder.Type(OpTypeVector, {TypeI32(state), components});
}

uint32_t TypeF32Vector(EmitterState& state, uint32_t components) {
	return state.builder.Type(OpTypeVector, {TypeF32(state), components});
}

uint32_t TypePointer(EmitterState& state, uint32_t storage_class, uint32_t pointee) {
	return state.builder.Type(OpTypePointer, {storage_class, pointee});
}

uint32_t TypeFunction(EmitterState& state) {
	return state.builder.Type(OpTypeFunction, {TypeVoid(state)});
}

uint32_t StorageRuntimeArrayType(EmitterState& state) {
	return state.builder.DecoratedType(OpTypeRuntimeArray, {TypeU32(state)},
	                                   {{OpDecorate, {DecorationArrayStride, sizeof(uint32_t)}}});
}

uint32_t StorageBufferType(EmitterState& state) {
	return state.builder.DecoratedType(
	    OpTypeStruct, {StorageRuntimeArrayType(state)},
	    {{OpMemberDecorate, {0, DecorationOffset, 0}}, {OpDecorate, {DecorationBlock}}});
}

uint32_t TypeStorageBufferPointer(EmitterState& state) {
	return TypePointer(state, StorageClassStorageBuffer, StorageBufferType(state));
}

uint32_t TypeStorageBufferElementPointer(EmitterState& state) {
	return TypePointer(state, StorageClassStorageBuffer, TypeU32(state));
}

uint32_t StorageU64RuntimeArrayType(EmitterState& state) {
	return state.builder.DecoratedType(OpTypeRuntimeArray, {TypeScalarU64(state)},
	                                   {{OpDecorate, {DecorationArrayStride, sizeof(uint64_t)}}});
}

uint32_t StorageBufferU64Type(EmitterState& state) {
	return state.builder.DecoratedType(
	    OpTypeStruct, {StorageU64RuntimeArrayType(state)},
	    {{OpMemberDecorate, {0, DecorationOffset, 0}}, {OpDecorate, {DecorationBlock}}});
}

uint32_t TypeStorageBufferU64Pointer(EmitterState& state) {
	return TypePointer(state, StorageClassStorageBuffer, StorageBufferU64Type(state));
}

uint32_t TypeStorageBufferU64ElementPointer(EmitterState& state) {
	return TypePointer(state, StorageClassStorageBuffer, TypeScalarU64(state));
}

uint32_t TypeDeviceAddressStoragePointer(EmitterState& state) {
	return TypePointer(state, StorageClassStorageBuffer, TypeDeviceAddress(state));
}

uint32_t TypePhysicalU32Pointer(EmitterState& state) {
	return TypePointer(state, StorageClassPhysicalStorageBuffer, TypeU32(state));
}

uint32_t TypePushConstantElementPointer(EmitterState& state) {
	return TypePointer(state, StorageClassPushConstant, TypeU32(state));
}

uint32_t TypeU32ArrayPointer(EmitterState& state, uint32_t storage_class, uint32_t dwords) {
	const auto count = ConstantU32(state, std::max(dwords, 1u));
	const auto array = state.builder.Type(OpTypeArray, {TypeU32(state), count});
	return TypePointer(state, storage_class, array);
}

uint32_t TypeU32ElementPointer(EmitterState& state, uint32_t storage_class) {
	return TypePointer(state, storage_class, TypeU32(state));
}

namespace {

uint32_t PushConstantArrayType(EmitterState& state) {
	const auto count = ConstantU32(state, IR::PushData::DwordCount);
	return state.builder.DecoratedType(OpTypeArray, {TypeU32(state), count},
	                                   {{OpDecorate, {DecorationArrayStride, sizeof(uint32_t)}}});
}

uint32_t PushConstantBlockType(EmitterState& state) {
	return state.builder.DecoratedType(
	    OpTypeStruct, {PushConstantArrayType(state)},
	    {{OpMemberDecorate, {0, DecorationOffset, 0}},
	     {OpDecorate, {DecorationBlock}}});
}

uint32_t PerVertexType(EmitterState& state) {
	return state.builder.DecoratedType(OpTypeStruct, {TypeF32Vector(state, 4)},
	                                   {{OpMemberDecorate, {0, DecorationBuiltIn, BuiltInPosition}},
	                                    {OpDecorate, {DecorationBlock}}});
}

uint32_t SampleMaskArrayType(EmitterState& state) {
	return state.builder.Type(OpTypeArray, {TypeI32(state), ConstantU32(state, 1)});
}

uint32_t BdaPagetableType(EmitterState& state) {
	const auto array = state.builder.DecoratedType(
	    OpTypeRuntimeArray, {TypeDeviceAddress(state)},
	    {{OpDecorate, {DecorationArrayStride, sizeof(uint64_t)}}});
	return state.builder.DecoratedType(
	    OpTypeStruct, {array},
	    {{OpMemberDecorate, {0, DecorationOffset, 0}}, {OpDecorate, {DecorationBlock}}});
}

uint32_t F32ArrayType(EmitterState& state, uint32_t count) {
	return state.builder.Type(OpTypeArray, {TypeF32(state), ConstantU32(state, count)});
}

void DefineDescriptorVariables(EmitterState& state) {
	if (DescriptorBinding(state, IR::DescriptorBindingKind::Buffers) != nullptr) {
		const auto count =
		    ConstantU32(state, DescriptorCount(state, IR::DescriptorBindingKind::Buffers));
		const auto array_type = state.builder.Type(OpTypeArray, {StorageBufferType(state), count});
		const auto pointer_type = TypePointer(state, StorageClassStorageBuffer, array_type);
		state.storage_buffer_variable =
		    state.builder.DefineGlobalVariable(pointer_type, StorageClassStorageBuffer);
		if (state.requirements.buffer_int64_atomics) {
			const auto u64_array_type =
			    state.builder.Type(OpTypeArray, {StorageBufferU64Type(state), count});
			state.storage_buffer_u64_variable = state.builder.DefineGlobalVariable(
			    TypePointer(state, StorageClassStorageBuffer, u64_array_type),
			    StorageClassStorageBuffer);
		}
	}
	if (DescriptorBinding(state, IR::DescriptorBindingKind::BdaPagetable) != nullptr) {
		state.bda_pagetable_variable = state.builder.DefineGlobalVariable(
		    TypePointer(state, StorageClassStorageBuffer, BdaPagetableType(state)),
		    StorageClassStorageBuffer);
	}
	if (DescriptorBinding(state, IR::DescriptorBindingKind::FaultBuffer) != nullptr) {
		state.fault_buffer_variable = state.builder.DefineGlobalVariable(
		    TypeStorageBufferPointer(state), StorageClassStorageBuffer);
	}
	if (state.program.bindings.UsesPushData() || state.stage == ShaderType::Mesh) {
		const auto pointer_type =
		    TypePointer(state, StorageClassPushConstant, PushConstantBlockType(state));
		state.push_constant_variable =
		    state.builder.DefineGlobalVariable(pointer_type, StorageClassPushConstant);
	}
	if (DescriptorBinding(state, IR::DescriptorBindingKind::ShaderData) != nullptr) {
		state.shader_data_storage_variable = state.builder.DefineGlobalVariable(
		    TypeStorageBufferPointer(state), StorageClassStorageBuffer);
	}
	if (DescriptorBinding(state, IR::DescriptorBindingKind::FlattenedSrt) != nullptr) {
		state.flattened_srt_variable = state.builder.DefineGlobalVariable(
		    TypeStorageBufferPointer(state), StorageClassStorageBuffer);
	}
	for (const auto& binding: state.program.bindings.descriptors) {
		const auto Define = [&](uint32_t type, const char* name,
		                        spv::StorageClass storage = spv::StorageClassStorageBuffer) {
			const auto variable =
			    state.builder.DefineGlobalVariable(TypePointer(state, storage, type), storage);
			state.builder.AddName(variable, name);
			state.builder.AddAnnotation(spv::OpDecorate, variable, spv::DecorationDescriptorSet, 0);
			state.builder.AddAnnotation(spv::OpDecorate, variable, spv::DecorationBinding,
			                            IR::NativeBinding(state.program.stage, binding.kind));
			return variable;
		};
		const auto ArrayType = [&](uint32_t type) {
			return state.builder.Type(
			    spv::OpTypeArray, type,
			    ConstantU32(state, static_cast<uint32_t>(binding.resources.size())));
		};
		switch (binding.kind) {
			case IR::DescriptorBindingKind::Buffers:
				state.storage_buffer_variable =
				    Define(ArrayType(StorageBufferType(state)), "buffers");
				if (state.requirements.buffer_int64_atomics) {
					state.storage_buffer_u64_variable =
					    Define(ArrayType(StorageBufferU64Type(state)), "buffers_u64");
					state.builder.AddAnnotation(spv::OpDecorate, state.storage_buffer_variable,
					                            spv::DecorationAliased);
					state.builder.AddAnnotation(spv::OpDecorate, state.storage_buffer_u64_variable,
					                            spv::DecorationAliased);
				}
				if (state.requirements.coherent_buffers) {
					// RDNA2 stores publish to L2 even without GLC; every alias of the buffer
					// must participate in visibility for cache-bypassing polling loads.
					state.builder.AddAnnotation(spv::OpDecorate, state.storage_buffer_variable,
					                            spv::DecorationCoherent);
					if (state.storage_buffer_u64_variable != 0) {
						state.builder.AddAnnotation(spv::OpDecorate, state.storage_buffer_u64_variable,
						                            spv::DecorationCoherent);
					}
				}
				break;
			case IR::DescriptorBindingKind::BdaPagetable:
				state.bda_pagetable_variable = Define(StorageBufferU64Type(state), "bda_pagetable");
				break;
			case IR::DescriptorBindingKind::FaultBuffer:
				state.fault_buffer_variable = Define(StorageBufferType(state), "fault_buffer");
				break;
			case IR::DescriptorBindingKind::ShaderData:
				state.shader_data_storage_variable =
				    Define(StorageBufferType(state), "shader_data");
				break;
			case IR::DescriptorBindingKind::FlattenedSrt:
				state.flattened_srt_variable = Define(StorageBufferType(state), "flattened_srt");
				break;
			case IR::DescriptorBindingKind::Samplers:
				state.sampler_variable = Define(ArrayType(state.builder.Type(spv::OpTypeSampler)),
				                                "samplers", spv::StorageClassUniformConstant);
				break;
			case IR::DescriptorBindingKind::Gds:
				state.gds_variable = Define(StorageBufferType(state), "gds");
				break;
			default: {
				EXIT_IF(IR::ImageBindingResourceClass(binding.kind) ==
				        IR::ImageResourceClass::None);
				const auto& image = state.program.info.images.at(binding.resources.front());
				const auto  name  = "image_" + std::to_string(static_cast<uint32_t>(binding.kind));
				state.image_variables[IR::ImageBindingIndex(binding.kind)] =
				    Define(ArrayType(ImageType(state, image)), name.c_str(),
				           spv::StorageClassUniformConstant);
				if (image.dimension == ImageDimension::Dim1D ||
				    image.dimension == ImageDimension::Dim1DArray) {
					state.builder.RequireCapability(image.resource_class ==
					                                        IR::ImageResourceClass::Sampled
					                                    ? spv::CapabilitySampled1D
					                                    : spv::CapabilityImage1D);
				}
				break;
			}
		}
		const auto& image        = state.program.info.images.at(binding.resources.front());
		const auto  count        = ConstantU32(state, DescriptorCount(state, binding.kind));
		const auto  image_type   = ImageType(state, image);
		const auto  array_type   = state.builder.Type(OpTypeArray, {image_type, count});
		const auto  pointer_type = TypePointer(state, StorageClassUniformConstant, array_type);
		state.image_variables[IR::ImageBindingIndex(binding.kind)] =
		    state.builder.DefineGlobalVariable(pointer_type, StorageClassUniformConstant);
		if (image.dimension == ImageDimension::Dim1D ||
		    image.dimension == ImageDimension::Dim1DArray) {
			const auto capability = image.resource_class == IR::ImageResourceClass::Sampled
			                            ? CapabilitySampled1D
			                            : CapabilityImage1D;
			state.builder.RequireCapability(capability);
		}
	}
	if (DescriptorBinding(state, IR::DescriptorBindingKind::Samplers) != nullptr) {
		const auto sampler_type = state.builder.Type(OpTypeSampler);
		const auto count =
		    ConstantU32(state, DescriptorCount(state, IR::DescriptorBindingKind::Samplers));
		const auto array_type   = state.builder.Type(OpTypeArray, {sampler_type, count});
		const auto pointer_type = TypePointer(state, StorageClassUniformConstant, array_type);
		state.sampler_variable =
		    state.builder.DefineGlobalVariable(pointer_type, StorageClassUniformConstant);
	}
	if (DescriptorBinding(state, IR::DescriptorBindingKind::LodStats) != nullptr) {
		state.lod_stats_variable = state.builder.DefineGlobalVariable(TypeStorageBufferPointer(state),
		                                                        StorageClassStorageBuffer);
		state.builder.RequireCapability(CapabilityImageQuery);
	}
	if (DescriptorBinding(state, IR::DescriptorBindingKind::Gds) != nullptr) {
		state.gds_variable = state.builder.DefineGlobalVariable(TypeStorageBufferPointer(state),
		                                                        StorageClassStorageBuffer);
	}
}

} // namespace

const IR::DescriptorBinding* DescriptorBinding(const EmitterState&       state,
                                               IR::DescriptorBindingKind kind) {
	return IR::FindBinding(state.program.bindings, kind);
}

uint32_t DescriptorCount(const EmitterState& state, IR::DescriptorBindingKind kind) {
	const auto* binding = DescriptorBinding(state, kind);
	return binding != nullptr ? static_cast<uint32_t>(binding->resources.size()) : 0;
}

uint32_t ConstantU32(EmitterState& state, uint32_t value) {
	return state.builder.Constant(OpConstant, TypeU32(state), {value});
}

uint32_t ConstantI32(EmitterState& state, int32_t value) {
	return state.builder.Constant(OpConstant, TypeI32(state), {static_cast<uint32_t>(value)});
}

uint32_t ConstantF32(EmitterState& state, uint32_t bits) {
	return state.builder.Constant(OpConstant, TypeF32(state), {bits});
}

uint32_t ConstantF32Value(EmitterState& state, float value) {
	return ConstantF32(state, std::bit_cast<uint32_t>(value));
}

uint32_t ConstantBool(EmitterState& state, bool value) {
	return state.builder.Constant(value ? OpConstantTrue : OpConstantFalse, TypeBool(state));
}

uint32_t ConstantU64(EmitterState& state, uint64_t value) {
	return state.builder.Constant(OpConstantComposite, TypeU64(state),
	                              {ConstantU32(state, static_cast<uint32_t>(value)),
	                               ConstantU32(state, static_cast<uint32_t>(value >> 32u))});
}

uint32_t ConstantU32CompositeZero(EmitterState& state, uint32_t components) {
	EXIT_IF(components < 2u || components > 4u);
	const auto            zero = ConstantU32(state, 0);
	std::vector<uint32_t> values(components, zero);
	return state.builder.Constant(OpConstantComposite, TypeU32Composite(state, components), values);
}

uint32_t GlslStd450(EmitterState& state) {
	return state.builder.Import("GLSL.std.450");
}

VertexInputScalarKind VertexParameterScalarKind(const EmitterState& state, uint32_t location) {
	if ((state.program.stage != ShaderType::Vertex && state.program.stage != ShaderType::Local) ||
	    location >= ShaderVertexInputInfo::RES_MAX ||
	    location >= static_cast<uint32_t>(state.input_info.vertex->resources_num)) {
		return VertexInputScalarKind::Float;
	}

	switch (state.input_info.vertex->resources[location].Format()) {
		case Prospero::BufferFormat::k8UInt:
		case Prospero::BufferFormat::k16UInt:
		case Prospero::BufferFormat::k8_8UInt:
		case Prospero::BufferFormat::k32UInt:
		case Prospero::BufferFormat::k16_16UInt:
		case Prospero::BufferFormat::k8_8_8_8UInt:
		case Prospero::BufferFormat::k32_32UInt:
		case Prospero::BufferFormat::k16_16_16_16UInt:
		case Prospero::BufferFormat::k32_32_32UInt:
		case Prospero::BufferFormat::k32_32_32_32UInt: return VertexInputScalarKind::Uint;
		case Prospero::BufferFormat::k8SInt:
		case Prospero::BufferFormat::k16SInt:
		case Prospero::BufferFormat::k8_8SInt:
		case Prospero::BufferFormat::k32SInt:
		case Prospero::BufferFormat::k16_16SInt:
		case Prospero::BufferFormat::k8_8_8_8SInt:
		case Prospero::BufferFormat::k32_32SInt:
		case Prospero::BufferFormat::k16_16_16_16SInt:
		case Prospero::BufferFormat::k32_32_32SInt:
		case Prospero::BufferFormat::k32_32_32_32SInt: return VertexInputScalarKind::Sint;
		default: return VertexInputScalarKind::Float;
	}
}

uint32_t VertexParameterComponentCount(const InputBinding& input) {
	return std::clamp(input.component_count, 1u, 4u);
}

uint32_t VertexParameterScalarType(EmitterState& state, VertexInputScalarKind kind) {
	switch (kind) {
		case VertexInputScalarKind::Sint: return TypeI32(state);
		case VertexInputScalarKind::Uint: return TypeU32(state);
		case VertexInputScalarKind::Float:
		default: return TypeF32(state);
	}
}

uint32_t DefineInterfaceVariable(EmitterState& state, uint32_t type, spv::StorageClass storage,
                                 const char* name) {
	const auto variable =
	    state.builder.DefineGlobalVariable(TypePointer(state, storage, type), storage);
	state.interface_variables.push_back(variable);
	state.builder.AddName(variable, name);
	return variable;
}

namespace {

uint32_t BuiltInForInput(IR::StageInputKind kind) {
	switch (kind) {
		case IR::StageInputKind::VertexIndex: return spv::BuiltInVertexIndex;
		case IR::StageInputKind::InvocationId: return spv::BuiltInInvocationId;
		case IR::StageInputKind::PrimitiveId: return spv::BuiltInPrimitiveId;
		case IR::StageInputKind::TessCoord: return spv::BuiltInTessCoord;
		case IR::StageInputKind::InstanceIndex: return spv::BuiltInInstanceIndex;
		case IR::StageInputKind::FragCoord: return spv::BuiltInFragCoord;
		case IR::StageInputKind::FrontFacing: return spv::BuiltInFrontFacing;
		case IR::StageInputKind::Layer: return spv::BuiltInLayer;
		case IR::StageInputKind::SampleId: return spv::BuiltInSampleId;
		case IR::StageInputKind::BaryCoordSmooth: return spv::BuiltInBaryCoordKHR;
		case IR::StageInputKind::BaryCoordNoPerspective: return spv::BuiltInBaryCoordNoPerspKHR;
		case IR::StageInputKind::WorkgroupId: return spv::BuiltInWorkgroupId;
		case IR::StageInputKind::LocalInvocationId: return spv::BuiltInLocalInvocationId;
		case IR::StageInputKind::LocalInvocationIndex: return spv::BuiltInLocalInvocationIndex;
		case IR::StageInputKind::GlobalInvocationId: return spv::BuiltInGlobalInvocationId;
		default: return UINT32_MAX;
	}
}

static bool MrtUsesUintOutput(const EmitterState& state, uint32_t index) {
	return state.stage == ShaderType::Pixel &&
	       index < std::size(state.input_info.pixel->target_output_mode) &&
	       state.input_info.pixel->target_output_mode[index] == 7u;
}

void AllocateInputVariables(EmitterState& state) {
	if (state.lod_stats_subgroup) {
		state.lod_helper_variable = state.builder.AllocateId();
		state.interface_variables.push_back(state.lod_helper_variable);
	}

	if (state.lane_count == 2) {
		const auto add_builtin = [&](IR::StageInputKind kind, uint32_t components,
		                             const char* name) {
			if (std::ranges::none_of(state.inputs, [kind](const InputBinding& input) {
				    return input.kind == kind;
			    })) {
				state.inputs.push_back({kind, 0, components, 0, name});
			}
		};
		add_builtin(IR::StageInputKind::LocalInvocationIndex, 1, "gl_LocalInvocationIndex");
		if (std::ranges::any_of(state.inputs, [](const InputBinding& input) {
			    return input.kind == IR::StageInputKind::GlobalInvocationId;
		    })) {
			add_builtin(IR::StageInputKind::WorkgroupId, 3, "gl_WorkGroupID");
		}
	}
	for (auto& input: state.inputs) {
		if (state.program.stage == ShaderType::Pixel &&
		    input.kind == IR::StageInputKind::Parameter) {
			const auto location = PixelParameterLocation(state, input.location);
			const auto alias = std::ranges::find_if(state.inputs, [&](const InputBinding& other) {
				return other.kind == IR::StageInputKind::Parameter && other.variable_id != 0 &&
				       PixelParameterLocation(state, other.location) == location;
			});
			if (alias != state.inputs.end()) {
				EXIT_IF(alias->per_vertex != input.per_vertex);
				input.variable_id = alias->variable_id;
				continue;
			}
		}
		uint32_t type = TypeU32(state);
		switch (input.kind) {
			case IR::StageInputKind::VertexIndex:
			case IR::StageInputKind::InvocationId:
			case IR::StageInputKind::PrimitiveId:
			case IR::StageInputKind::InstanceIndex:
			case IR::StageInputKind::Layer:
			case IR::StageInputKind::SampleId: type = TypeI32(state); break;
			case IR::StageInputKind::WorkgroupId:
			case IR::StageInputKind::LocalInvocationId:
			case IR::StageInputKind::GlobalInvocationId: type = TypeU32Vector(state, 3); break;
			case IR::StageInputKind::FragCoord: type = TypeF32Vector(state, 4); break;
			case IR::StageInputKind::TessCoord:
			case IR::StageInputKind::BaryCoordSmooth:
			case IR::StageInputKind::BaryCoordNoPerspective: type = TypeF32Vector(state, 3); break;
			case IR::StageInputKind::FrontFacing: type = TypeBool(state); break;
			case IR::StageInputKind::Parameter:
				if (state.program.stage == ShaderType::Vertex ||
				    state.program.stage == ShaderType::Local) {
					type = VertexParameterScalarType(
					    state, VertexParameterScalarKind(state, input.location));
					const auto components = VertexParameterComponentCount(input);
					if (components > 1u) {
						type = state.builder.Type(spv::OpTypeVector, type, components);
					}
				} else if (input.per_vertex) {
					type = state.builder.Type(spv::OpTypeArray, TypeF32Vector(state, 4),
					                          ConstantU32(state, 3));
				} else {
					type = TypeF32Vector(state, 4);
				}
				break;
		}
	}
}

uint32_t BuiltInForInput(IR::StageInputKind kind) {
	switch (kind) {
		case IR::StageInputKind::VertexIndex: return BuiltInVertexIndex;
		case IR::StageInputKind::InstanceIndex: return BuiltInInstanceIndex;
		case IR::StageInputKind::FragCoord: return BuiltInFragCoord;
		case IR::StageInputKind::FrontFacing: return BuiltInFrontFacing;
		case IR::StageInputKind::Layer: return BuiltInLayer;
		case IR::StageInputKind::SampleId: return BuiltInSampleId;
		case IR::StageInputKind::BaryCoordSmooth: return BuiltInBaryCoordKHR;
		case IR::StageInputKind::BaryCoordNoPerspective: return BuiltInBaryCoordNoPerspKHR;
		case IR::StageInputKind::WorkgroupId: return BuiltInWorkgroupId;
		case IR::StageInputKind::LocalInvocationId: return BuiltInLocalInvocationId;
		case IR::StageInputKind::LocalInvocationIndex: return BuiltInLocalInvocationIndex;
		case IR::StageInputKind::GlobalInvocationId: return BuiltInGlobalInvocationId;
		default: return UINT32_MAX;
	}
}

void AddInputAnnotationsAndNames(EmitterState& state) {
	if (state.lod_helper_variable != 0) {
		state.builder.AddName(state.lod_helper_variable, "gl_HelperInvocation");
		state.builder.AddAnnotation({OpDecorate, state.lod_helper_variable,
		                             DecorationBuiltIn, BuiltInHelperInvocation});
	}

	if (state.subgroup_local_invocation_id_variable != 0) {
		state.builder.AddName(state.subgroup_local_invocation_id_variable,
		                      "gl_SubgroupInvocationID");
		state.builder.AddAnnotation({OpDecorate, state.subgroup_local_invocation_id_variable,
		                             DecorationBuiltIn, BuiltInSubgroupLocalInvocationId});
		if (state.stage == ShaderType::Pixel) {
			state.builder.AddAnnotation(
			    {OpDecorate, state.subgroup_local_invocation_id_variable, DecorationFlat});
		}
	}
	for (const auto& input: state.inputs) {
		state.builder.AddName(input.variable_id, input.debug_name.c_str());
		if (input.kind == IR::StageInputKind::Layer || input.kind == IR::StageInputKind::SampleId) {
			state.builder.AddAnnotation({OpDecorate, input.variable_id, DecorationFlat});
		}
		if (input.kind == IR::StageInputKind::Parameter) {
			const auto flat = PixelParameterIsFlat(state, input.location);
			if (input.per_vertex) {
				state.builder.AddAnnotation(
				    {OpDecorate, input.variable_id, DecorationPerVertexKHR});
			} else if (flat) {
				state.builder.AddAnnotation({OpDecorate, input.variable_id, DecorationFlat});
			}
			if (state.stage == ShaderType::Pixel && state.input_info.pixel->ps_no_perspective &&
			    !flat && !input.per_vertex) {
				state.builder.AddAnnotation(
				    {OpDecorate, input.variable_id, DecorationNoPerspective});
			}
			const auto location = PixelParameterLocation(state, input.location);
			state.builder.AddAnnotation(
			    {OpDecorate, input.variable_id, DecorationLocation, location});
			continue;
		}
		const auto builtin = BuiltInForInput(input.kind);
		if (builtin != UINT32_MAX) {
			state.builder.AddAnnotation(
			    {OpDecorate, input.variable_id, DecorationBuiltIn, builtin});
		}
	}
}

void AddOutputAnnotationsAndNames(EmitterState& state) {
	if (state.stage == ShaderType::Mesh) {
		return;
	}
	if (state.program.stage == ShaderType::Vertex && clip_distance_count + cull_distance_count < 8u &&
	    std::ranges::any_of(state.outputs, [](const OutputBinding& output) {
		    return output.kind == IR::StageOutputKind::Position;
	    })) {
		// Reserve one plane for the enabled PA_CL_CLIP_CNTL clipping-error cull.
		state.invalid_position_clip_distance = clip_distance_count++;
		state.outputs.push_back({{IR::StageOutputKind::ClipDistance,
		                          state.invalid_position_clip_distance, 0, "gl_ClipDistance"}});
	}
	const auto BuiltIn = [&](uint32_t& variable, uint32_t type, const char* name,
	                         spv::BuiltIn builtin) {
		if (variable == 0) {
			variable = DefineInterfaceVariable(state, type, spv::StorageClassOutput, name);
			state.builder.AddAnnotation(spv::OpDecorate, variable, spv::DecorationBuiltIn, builtin);
		}
	};
	for (auto& binding: state.outputs) {
		switch (binding.kind) {
			case IR::StageOutputKind::Position:
				if (state.per_vertex_variable == 0) {
					const auto type = PerVertexType(state);
					state.builder.AddName(type, "gl_PerVertex");
					state.per_vertex_variable = DefineInterfaceVariable(
					    state, type, spv::StorageClassOutput, "outPerVertex");
				}
				binding.variable_id = state.per_vertex_variable;
				break;
			case IR::StageOutputKind::PointSize:
				binding.variable_id = BuiltIn(state.point_size_variable, TypeF32(state),
				                              "gl_PointSize", spv::BuiltInPointSize);
				break;
			case IR::StageOutputKind::ClipDistance:
				binding.variable_id =
				    BuiltIn(state.clip_distance_variable, F32ArrayType(state, clip_distance_count),
				            "gl_ClipDistance", spv::BuiltInClipDistance);
				break;
			case IR::StageOutputKind::CullDistance:
				binding.variable_id =
				    BuiltIn(state.cull_distance_variable, F32ArrayType(state, cull_distance_count),
				            "gl_CullDistance", spv::BuiltInCullDistance);
				break;
			case IR::StageOutputKind::Layer:
				binding.variable_id =
				    BuiltIn(state.layer_variable, TypeU32(state), "gl_Layer", spv::BuiltInLayer);
				break;
			case IR::StageOutputKind::ViewportIndex:
				binding.variable_id = BuiltIn(state.viewport_index_variable, TypeU32(state),
				                              "gl_ViewportIndex", spv::BuiltInViewportIndex);
				break;
			case IR::StageOutputKind::Depth:
				binding.variable_id = BuiltIn(state.depth_variable, TypeF32(state), "gl_FragDepth",
				                              spv::BuiltInFragDepth);
				break;
			case IR::StageOutputKind::SampleMask:
				binding.variable_id =
				    BuiltIn(state.sample_mask_variable, SampleMaskArrayType(state), "gl_SampleMask",
				            spv::BuiltInSampleMask);
				break;
			case IR::StageOutputKind::Parameter:
			case IR::StageOutputKind::Mrt: {
				const bool uint_output =
				    binding.kind == IR::StageOutputKind::Mrt &&
				    state.program.stage == ShaderType::Pixel &&
				    binding.index < std::size(state.input_info.pixel->target_output_mode) &&
				    state.input_info.pixel->target_output_mode[binding.index] == 7u;
				const auto type = uint_output ? TypeU32Vector(state, 4) : TypeF32Vector(state, 4);
				binding.variable_id = DefineInterfaceVariable(state, type, spv::StorageClassOutput,
				                                              binding.debug_name.c_str());
				const bool dual_source = binding.kind == IR::StageOutputKind::Mrt &&
				                         state.program.stage == ShaderType::Pixel &&
				                         state.input_info.pixel->dual_source_blending;
				EXIT_NOT_IMPLEMENTED(dual_source && binding.index > 1);
				state.builder.AddAnnotation(spv::OpDecorate, binding.variable_id,
				                            spv::DecorationLocation,
				                            dual_source ? 0u : binding.location);
				if (dual_source) {
					state.builder.AddAnnotation(spv::OpDecorate, binding.variable_id,
					                            spv::DecorationIndex, binding.index);
				}
				break;
			}
		}
	}
}

void DecorateDescriptor(EmitterState& state, uint32_t variable, const char* name,
                        IR::DescriptorBindingKind kind) {
	if (variable == 0) {
		return;
	}
	state.builder.AddName(variable, name);
	state.builder.AddAnnotation({OpDecorate, variable, DecorationDescriptorSet, 0});
	state.builder.AddAnnotation(
	    {OpDecorate, variable, DecorationBinding, IR::NativeBinding(state.program.stage, kind)});
}

void AddDescriptorAnnotationsAndNames(EmitterState& state) {
	auto Decorate = [&](uint32_t variable, const char* name, IR::DescriptorBindingKind kind) {
		DecorateDescriptor(state, variable, name, kind);
	};
	if (state.storage_buffer_variable != 0) {
		Decorate(state.storage_buffer_variable, "buffers", IR::DescriptorBindingKind::Buffers);
	}
	if (state.storage_buffer_u64_variable != 0) {
		Decorate(state.storage_buffer_u64_variable, "buffers_u64",
		         IR::DescriptorBindingKind::Buffers);
		state.builder.AddAnnotation(
		    {OpDecorate, state.storage_buffer_variable, DecorationAliased});
		state.builder.AddAnnotation(
		    {OpDecorate, state.storage_buffer_u64_variable, DecorationAliased});
	}
	if (state.bda_pagetable_variable != 0) {
		Decorate(state.bda_pagetable_variable, "bda_pagetable",
		         IR::DescriptorBindingKind::BdaPagetable);
	}
	if (state.fault_buffer_variable != 0) {
		Decorate(state.fault_buffer_variable, "fault_buffer",
		         IR::DescriptorBindingKind::FaultBuffer);
	}
	for (const auto& binding: state.program.bindings.descriptors) {
		if (IR::ImageBindingResourceClass(binding.kind) == IR::ImageResourceClass::None) {
			continue;
		}
		const auto name = "image_" + std::to_string(static_cast<uint32_t>(binding.kind));
		Decorate(state.image_variables[IR::ImageBindingIndex(binding.kind)], name.c_str(),
		         binding.kind);
	}
	if (state.sampler_variable != 0) {
		Decorate(state.sampler_variable, "samplers", IR::DescriptorBindingKind::Samplers);
	}
	if (state.lod_stats_variable != 0) {
		Decorate(state.lod_stats_variable, "lod_stats", IR::DescriptorBindingKind::LodStats);
	}
	if (state.gds_variable != 0) {
		Decorate(state.gds_variable, "gds", IR::DescriptorBindingKind::Gds);
	}
	if (state.flattened_srt_variable != 0) {
		Decorate(state.flattened_srt_variable, "flattened_srt",
		         IR::DescriptorBindingKind::FlattenedSrt);
	}
}

void AddVsharpAnnotationsAndNames(EmitterState& state) {
	if (state.push_constant_variable != 0) {
		state.builder.AddName(PushConstantBlockType(state), "BufferResource");
		state.builder.AddName(state.push_constant_variable, "vsharp");
	}
	if (state.shader_data_storage_variable != 0) {
		DecorateDescriptor(state, state.shader_data_storage_variable, "shader_data",
		                   IR::DescriptorBindingKind::ShaderData);
	}
}

void DefineModule(EmitterState& state) {
	state.interface_variables.reserve(state.program.info.inputs.size() +
	                                  state.program.info.outputs.size());
	DefineInputs(state);
	DefineOutputs(state);
	DefineTessellationInterfaces(state);
	DefineDescriptors(state);
	if (state.requirements.function_lds) {
		state.lds_variable = state.builder.AllocateId();
	}
	if (state.requirements.function_scratch) {
		for (uint32_t half = 0; half < state.lane_count; half++) {
			state.scratch_variable[half] = state.builder.AllocateId();
		}
	}
	state.main_func   = state.builder.AllocateId();
	if (state.stage == ShaderType::Mesh) {
		state.mesh_guest_func = state.builder.AllocateId();
		state.builder.RequireCapability(5283u); // MeshShadingEXT
		state.builder.RequireExtension("SPV_EXT_mesh_shader");
		state.builder.AddExecutionMode({state.main_func, 5298u}); // OutputTrianglesEXT
		state.builder.AddExecutionMode(
		    {state.main_func, 26u, state.input_info.vertex->mesh.max_vertices});
		state.builder.AddExecutionMode(
		    {state.main_func, 5270u, state.input_info.vertex->mesh.max_primitives});
	}
	if (state.program.stage == ShaderType::TessellationControl ||
	    state.program.stage == ShaderType::TessellationEvaluation) {
		DefineTessellationExecutionModes(state);
	}
	state.entry_label = state.builder.AllocateId();

	state.builder.RequireCapability(CapabilityShader);
	state.builder.RequireCapability(CapabilitySignedZeroInfNanPreserve);
	if (state.program.info.uses_dma) {
		state.builder.RequireCapability(CapabilityInt64);
		state.builder.RequireCapability(CapabilityPhysicalStorageBufferAddresses);
		state.builder.RequireExtension("SPV_KHR_physical_storage_buffer");
	}
	if (state.requirements.buffer_int64_atomics || state.requirements.shared_int64_atomics) {
		state.builder.RequireCapability(spv::CapabilityInt64);
		state.builder.RequireCapability(spv::CapabilityInt64Atomics);
	}
	if (state.requirements.shared_int64_atomics) {
		state.builder.RequireVersion(0x00010400u);
		state.builder.RequireExtension("SPV_KHR_workgroup_memory_explicit_layout");
		state.builder.RequireCapability(spv::CapabilityWorkgroupMemoryExplicitLayoutKHR);
	}
	if (state.clip_distance_variable != 0) {
		state.builder.RequireCapability(CapabilityClipDistance);
	}
	if (state.cull_distance_variable != 0) {
		state.builder.RequireCapability(CapabilityCullDistance);
	}
	if (state.layer_variable != 0 || InputVariableForKind(state, IR::StageInputKind::Layer) != 0) {
		state.builder.RequireVersion(0x00010500u);
		state.builder.RequireCapability(CapabilityShaderLayer);
	}
	if (state.viewport_index_variable != 0) {
		state.builder.RequireVersion(0x00010500u);
		state.builder.RequireCapability(CapabilityShaderViewportIndex);
	}
	if (InputVariableForKind(state, IR::StageInputKind::SampleId) != 0) {
		state.builder.RequireCapability(CapabilitySampleRateShading);
	}
	if (state.requirements.image_gather_extended) {
		state.builder.RequireCapability(CapabilityImageGatherExtended);
	}
	if (state.lod_stats_subgroup) {
		state.builder.RequireCapability(CapabilityGroupNonUniform);
		state.builder.RequireCapability(CapabilityGroupNonUniformVote);
		state.builder.RequireCapability(CapabilityGroupNonUniformArithmetic);
	}
	if (state.lane_count == 2 || state.requirements.subgroup_ballot ||
	    state.requirements.subgroup_shuffle || state.requirements.subgroup_local_invocation_id) {
		state.builder.RequireCapability(CapabilityGroupNonUniform);
	}
	if (state.lane_count == 2 || state.requirements.subgroup_ballot) {
		state.builder.RequireCapability(CapabilityGroupNonUniformBallot);
	}
	if (state.requirements.subgroup_shuffle) {
		state.builder.RequireCapability(CapabilityGroupNonUniformShuffle);
	}
	if (state.requirements.compute_derivatives && state.stage == ShaderType::Compute) {
		state.builder.RequireCapability(CapabilityComputeDerivativeGroupQuadsKHR);
		state.builder.RequireExtension("SPV_KHR_compute_shader_derivatives");
	}
	const bool fragment_barycentric =
	    state.stage == ShaderType::Pixel &&
	    std::any_of(state.inputs.begin(), state.inputs.end(), [](const InputBinding& input) {
		    return input.per_vertex || input.kind == IR::StageInputKind::BaryCoordSmooth ||
		           input.kind == IR::StageInputKind::BaryCoordNoPerspective;
	    });
	if (fragment_barycentric) {
		state.builder.RequireCapability(CapabilityFragmentBarycentricKHR);
		state.builder.RequireExtension("SPV_KHR_fragment_shader_barycentric");
	}
	state.builder.RequireExtension("SPV_KHR_float_controls");
	state.builder.AddMemoryModel(
	    {state.program.info.uses_dma ? AddressingModelPhysicalStorageBuffer64
	                                 : AddressingModelLogical,
	     MemoryModelGLSL450});
	// GCN/RDNA arithmetic preserves 32-bit signed zero, infinity, and NaN. Declaring that
	// contract prevents host compilers from treating synthesized IEEE values as finite.
	state.builder.AddExecutionMode(state.main_func, spv::ExecutionModeSignedZeroInfNanPreserve,
	                               32u);
	if (state.requirements.float64) {
		EXIT_NOT_IMPLEMENTED(state.program.stage == ShaderType::Compute &&
		                     state.input_info.compute->float_mode != 0xc0);
		// MODE=0xc0 uses round-to-nearest-even and preserves FP64 input/output denormals.
		state.builder.RequireCapability(spv::CapabilityFloat64);
		state.builder.RequireCapability(spv::CapabilityRoundingModeRTE);
		state.builder.AddExecutionMode(state.main_func, spv::ExecutionModeSignedZeroInfNanPreserve,
		                               64u);
		// FP64 denormal preservation is temporarily disabled.
		// state.builder.RequireCapability(spv::CapabilityDenormPreserve);
		// state.builder.AddExecutionMode(state.main_func, spv::ExecutionModeDenormPreserve, 64u);
		state.builder.AddExecutionMode(state.main_func, spv::ExecutionModeRoundingModeRTE, 32u);
	}
	if (const auto* cs = ShaderWorkgroupInput(state.program.stage, state.input_info)) {
		uint32_t    local_x = state.requirements.compute_derivatives ? 2u : 1u;
		uint32_t    local_y = state.requirements.compute_derivatives ? 2u : 1u;
		uint32_t    local_z = 1u;
		local_x             = cs->threads_num[0] != 0u ? cs->threads_num[0] : local_x;
		local_y             = cs->threads_num[1] != 0u ? cs->threads_num[1] : local_y;
		local_z             = cs->threads_num[2] != 0u ? cs->threads_num[2] : local_z;
		if (state.lane_count == 2) {
			local_x = ((local_x * local_y * local_z + 63u) / 64u) * 32u;
			local_y = local_z = 1u;
			if (state.requirements.compute_derivatives) {
				local_y = local_x / 2u;
				local_x = 2u;
			}
		}
		state.builder.AddExecutionMode(
		    {state.main_func, ExecutionModeLocalSize, local_x, local_y, local_z});
	}
	if (state.stage == ShaderType::Pixel) {
		state.builder.AddExecutionMode({state.main_func, ExecutionModeOriginUpperLeft});
		if (state.depth_variable != 0) {
			state.builder.AddExecutionMode({state.main_func, ExecutionModeDepthReplacing});
		}
		if (state.input_info.pixel->ps_early_z && !state.input_info.pixel->ps_pixel_kill_enable &&
		    !state.input_info.pixel->ps_depth_export_enable &&
		    !state.input_info.pixel->ps_sample_mask_export_enable) {
			state.builder.AddExecutionMode({state.main_func, ExecutionModeEarlyFragmentTests});
		}
	}
	if (state.requirements.compute_derivatives && state.stage == ShaderType::Compute) {
		state.builder.AddExecutionMode({state.main_func, ExecutionModeDerivativeGroupQuadsKHR});
	}
	state.builder.AddName(state.main_func, "main");
	if (state.requirements.function_lds) {
		state.builder.AddName(state.lds_variable, "lds_dwords");
	}
	if (state.requirements.function_scratch) {
		for (uint32_t half = 0; half < state.lane_count; half++) {
			state.builder.AddName(state.scratch_variable[half], "scratch_dwords");
		}
	}
	if (state.lod_helper_variable != 0) {
		state.builder.DefineGlobalVariable(state.lod_helper_variable,
		    TypePointer(state, StorageClassInput, TypeBool(state)), StorageClassInput);
	}
	AddInputAnnotationsAndNames(state);
	AddOutputAnnotationsAndNames(state);
	AddDescriptorAnnotationsAndNames(state);
	AddVsharpAnnotationsAndNames(state);

	if (state.subgroup_local_invocation_id_variable != 0) {
		state.builder.DefineGlobalVariable(state.subgroup_local_invocation_id_variable,
		                                   TypePointer(state, StorageClassInput, TypeU32(state)),
		                                   StorageClassInput);
	}
	for (const auto& input: state.inputs) {
		uint32_t ptr_type = TypePointer(state, StorageClassInput, TypeU32(state));
		switch (input.kind) {
			case IR::StageInputKind::VertexIndex:
			case IR::StageInputKind::InstanceIndex:
			case IR::StageInputKind::Layer:
			case IR::StageInputKind::SampleId:
				ptr_type = TypePointer(state, StorageClassInput, TypeI32(state));
				break;
			case IR::StageInputKind::WorkgroupId:
			case IR::StageInputKind::LocalInvocationId:
			case IR::StageInputKind::GlobalInvocationId:
				ptr_type = TypePointer(state, StorageClassInput, TypeU32Vector(state, 3));
				break;
			case IR::StageInputKind::FragCoord:
				ptr_type = TypePointer(state, StorageClassInput, TypeF32Vector(state, 4));
				break;
			case IR::StageInputKind::BaryCoordSmooth:
			case IR::StageInputKind::BaryCoordNoPerspective:
				ptr_type = TypePointer(state, StorageClassInput, TypeF32Vector(state, 3));
				break;
			case IR::StageInputKind::FrontFacing:
				ptr_type = TypePointer(state, StorageClassInput, TypeBool(state));
				break;
			case IR::StageInputKind::Parameter:
				if (state.stage == ShaderType::Vertex) {
					const auto kind       = VertexParameterScalarKind(state, input.location);
					const auto components = VertexParameterComponentCount(input);
					ptr_type = VertexParameterInputPointerType(state, kind, components);
				} else if (input.per_vertex) {
					const auto array_type = state.builder.Type(
					    OpTypeArray, {TypeF32Vector(state, 4), ConstantU32(state, 3)});
					ptr_type = TypePointer(state, StorageClassInput, array_type);
				} else {
					ptr_type = TypePointer(state, StorageClassInput, TypeF32Vector(state, 4));
				}
				break;
			default: break;
		}
		state.builder.DefineGlobalVariable(input.variable_id, ptr_type, StorageClassInput);
	}
	if (state.stage == ShaderType::Mesh) {
		return;
	}
	if (state.per_vertex_variable != 0) {
		state.builder.DefineGlobalVariable(
		    state.per_vertex_variable, TypePointer(state, StorageClassOutput, PerVertexType(state)),
		    StorageClassOutput);
	}
	if (state.point_size_variable != 0) {
		state.builder.DefineGlobalVariable(
		    state.point_size_variable, TypePointer(state, StorageClassOutput, TypeF32(state)),
		    StorageClassOutput);
	}
	if (state.clip_distance_variable != 0) {
		state.builder.DefineGlobalVariable(
		    state.clip_distance_variable,
		    TypePointer(state, StorageClassOutput,
		                F32ArrayType(state, state.clip_distance_count)),
		    StorageClassOutput);
	}
	if (state.cull_distance_variable != 0) {
		state.builder.DefineGlobalVariable(
		    state.cull_distance_variable,
		    TypePointer(state, StorageClassOutput,
		                F32ArrayType(state, state.cull_distance_count)),
		    StorageClassOutput);
	}
	if (state.layer_variable != 0) {
		state.builder.DefineGlobalVariable(
		    state.layer_variable, TypePointer(state, StorageClassOutput, TypeU32(state)),
		    StorageClassOutput);
	}
	if (state.viewport_index_variable != 0) {
		state.builder.DefineGlobalVariable(
		    state.viewport_index_variable, TypePointer(state, StorageClassOutput, TypeU32(state)),
		    StorageClassOutput);
	}
	for (const auto& binding: state.outputs) {
		if (binding.kind == IR::StageOutputKind::Parameter ||
		    binding.kind == IR::StageOutputKind::Mrt) {
			const auto pointer_type =
			    binding.kind == IR::StageOutputKind::Mrt && MrtUsesUintOutput(state, binding.index)
			        ? TypePointer(state, StorageClassOutput, TypeU32Vector(state, 4))
			        : TypePointer(state, StorageClassOutput, TypeF32Vector(state, 4));
			state.builder.DefineGlobalVariable(binding.variable_id, pointer_type,
			                                   StorageClassOutput);
		}
	}
	if (state.depth_variable != 0) {
		state.builder.DefineGlobalVariable(state.depth_variable,
		                                   TypePointer(state, StorageClassOutput, TypeF32(state)),
		                                   StorageClassOutput);
	}
	if (state.sample_mask_variable != 0) {
		state.builder.DefineGlobalVariable(
		    state.sample_mask_variable,
		    TypePointer(state, StorageClassOutput, SampleMaskArrayType(state)), StorageClassOutput);
	}
}

} // namespace Libs::Graphics::ShaderRecompiler::Spirv::Emitter
