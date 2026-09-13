#include "graphics/host_gpu/renderer/demonsSouls.h"

#include "graphics/host_gpu/renderer/cache/bufferCache.h"
#include "graphics/shader/shader.h"
#include "graphics/shader/recompiler/ir/ShaderIR.h"
#include "kernel/memory.h"
#include "loader/systemContent.h"

#include <array>
#include <cstring>
#include <string>

namespace Libs::Graphics::DemonsSouls {
bool IsSupportedGame() {
	// Called by the dispatch path after game metadata has been loaded.
	static const bool supported = [] {
		std::string title, version;
		return Loader::SystemContentParamSfoGetString("TITLE_ID", &title) &&
		       Loader::SystemContentParamSfoGetString("APP_VER", &version) &&
		       IsSupportedVersion(title, version);
	}();
	return supported;
}

// Verified shader semantics: dst[i] = src[i % period], i < count. Only the
// linear subset is replaced. Other formats, workgroup shapes, overlaps and
// GPU-owned control words stay on the shader path; CopyBuffer owns coherence.
bool TryLinearCopy(const ShaderComputeInputInfo& input, BufferCache& cache, uint32_t x, uint32_t y,
                   uint32_t z, uint32_t mode) {
	const auto& program = *input.stage.program;
	const auto& data    = input.stage.resources.user_data;
	if (!IsSupportedGame() || program.shader_hash != 0xeb7456322124ecc7ULL ||
	    data.size() != 12 || program.user_data_base != 0 || input.dispatch_thread_dimensions ||
	    mode != 0x41 || input.threads_num[0] != 64 || input.threads_num[1] != 1 ||
	    input.threads_num[2] != 1 || !input.group_id[0] || input.group_id[1] || input.group_id[2] ||
	    input.thread_ids_num != 1 || input.workgroup_register != 12 || input.tg_size_en || y != 1 ||
	    z != 1 || x == 0)
		return false;
	ShaderBufferResource source, destination, parameters;
	std::memcpy(source.fields, data.data(), 16);
	std::memcpy(destination.fields, data.data() + 4, 16);
	std::memcpy(parameters.fields, data.data() + 8, 16);
	for (const auto* d: {&source, &destination}) {
		// Exact control bits observed for idxen format_x UInt32. Reject every
		// unanalysed format/swizzle/OOB/cache/addressing variation.
		if (d->fields[3] != 0x00014004 || (d->fields[1] & 0xffff0000u) != 0x00040000u ||
		    d->Base48() == 0 || (d->Base48() & 3) != 0)
			return false;
	}
	if (parameters.fields[3] != 0x0004dfac || (parameters.fields[1] & 0xffff0000u) != 0x00100000u ||
	    parameters.NumRecords() != 1 || (parameters.Base48() & 3) != 0)
		return false;
	std::array<uint32_t, 2> controls {};
	if (!Libs::LibKernel::Memory::TryReadGpuCleanBacking(parameters.Base48(), controls.data(),
	                                                     sizeof(controls)))
		return false;
	const uint64_t count = controls[0], period = controls[1], bytes = count * 4;
	if (count == 0 || period < count || count > source.NumRecords() ||
	    count > destination.NumRecords() || x != (count + 63) / 64 || bytes > 16 * 1024 * 1024)
		return false;
	const auto src = source.Base48(), dst = destination.Base48();
	if ((src < dst + bytes && dst < src + bytes) ||
	    (parameters.Base48() < dst + bytes && dst < parameters.Base48() + 16))
		return false;
	if (!LibKernel::Memory::IsUniqueGuestBackingRange(src, bytes) ||
	    !LibKernel::Memory::IsUniqueGuestBackingRange(dst, bytes) ||
	    !LibKernel::Memory::IsUniqueGuestBackingRange(parameters.Base48(), 16))
		return false;
	cache.CopyBuffer(dst, src, bytes, false, false);
	return true;
}
} // namespace Libs::Graphics::DemonsSouls
