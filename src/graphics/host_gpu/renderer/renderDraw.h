#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_RENDERDRAW_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_RENDERDRAW_H_

#include <cstdint>
#include "graphics/host_gpu/renderer/render.h"

namespace Libs::Graphics {

struct DrawIndexBufferSource {
	uint64_t      address   = 0;
	const void*   host_data = nullptr;
	uint64_t      size      = 0;
	vk::IndexType type      = vk::IndexType::eUint16;
	uint32_t      guest_element_size = 0;
};

struct DrawIndexRun {
	static constexpr size_t MaxDraws = 64;
	std::array<vk::DrawIndexedIndirectCommand, MaxDraws> commands {};
	DrawIndexBufferSource indices;
};
[[nodiscard]] bool BuildDrawIndexRun(std::span<const DrawIndexArgs> draws, DrawIndexRun& run);

struct ShaderVertexInputInfo;

[[nodiscard]] std::pair<int32_t, uint32_t>
ResolveDrawOffsets(uint32_t index_offset, const ShaderVertexInputInfo& vs_input_info);

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_RENDERDRAW_H_
