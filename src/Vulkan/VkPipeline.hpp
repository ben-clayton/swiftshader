// Copyright 2018 The SwiftShader Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef VK_PIPELINE_HPP_
#define VK_PIPELINE_HPP_

#include "VkObject.hpp"

#include "VkDescriptorSet.hpp"
#include "VkPipelineCache.hpp"
#include "VkSpecializationInfo.hpp"

#include "Device/Renderer.hpp"

#include "marl/waitgroup.h"

#include <memory>

namespace sw {

class ComputeProgram;
class SpirvShader;

}  // namespace sw

namespace vk {

namespace dbg {
class Context;
}  // namespace dbg

class PipelineCache;
class PipelineLayout;
class ShaderModule;
class Device;

class Pipeline
{
public:
	Pipeline(PipelineLayout *layout, const Device *device);
	virtual ~Pipeline() = default;

	operator VkPipeline()
	{
		return vk::TtoVkT<Pipeline, VkPipeline>(this);
	}

	static inline Pipeline *Cast(VkPipeline object)
	{
		return vk::VkTtoT<Pipeline, VkPipeline>(object);
	}

	void destroy(const VkAllocationCallbacks *pAllocator);

	virtual void destroyPipeline(const VkAllocationCallbacks *pAllocator) = 0;
#ifndef NDEBUG
	virtual VkPipelineBindPoint bindPoint() const = 0;
#endif

	PipelineLayout *getLayout() const
	{
		return layout;
	}

protected:
	struct CompileOptions
	{
		// TODO(bclayton): add compilation options here. Examples: optimization
		// level, reactor backend, debugger features.
		bool debuggerEnabled = false;

		bool operator==(const CompileOptions &other) const
		{
			return debuggerEnabled == other.debuggerEnabled;
		}
	};

	struct CompileOptionsHash
	{
		uint64_t operator()(const CompileOptions &options) const { return options.debuggerEnabled ? 1 : 0; }
	};

	template<typename T>
	using CompileCache = sw::SyncCache<std::unordered_map<CompileOptions, T, CompileOptionsHash>>;

	CompileOptions getCompileOptions() const;

	PipelineLayout *const layout;
	Device const *const device;
	Acquirable acquirable;

	const bool robustBufferAccess = true;
};

class GraphicsPipeline : public Pipeline, public ObjectBase<GraphicsPipeline, VkPipeline>
{
public:
	GraphicsPipeline(const VkGraphicsPipelineCreateInfo *pCreateInfo,
	                 void *mem,
	                 const Device *device);
	virtual ~GraphicsPipeline() = default;

	void destroyPipeline(const VkAllocationCallbacks *pAllocator) override;

#ifndef NDEBUG
	VkPipelineBindPoint bindPoint() const override
	{
		return VK_PIPELINE_BIND_POINT_GRAPHICS;
	}
#endif

	static size_t ComputeRequiredAllocationSize(const VkGraphicsPipelineCreateInfo *pCreateInfo);

	void compileShaders(const VkAllocationCallbacks *pAllocator, const VkGraphicsPipelineCreateInfo *pCreateInfo, PipelineCache *pipelineCache);

	uint32_t computePrimitiveCount(uint32_t vertexCount) const;
	sw::Context getContext();
	const VkRect2D &getScissor() const;
	const VkViewport &getViewport() const;
	const sw::float4 &getBlendConstants() const;
	bool hasDynamicState(VkDynamicState dynamicState) const;
	bool hasPrimitiveRestartEnable() const { return primitiveRestartEnable; }

private:
	struct Environment
	{
		struct Stage
		{
			VkShaderStageFlagBits stage;
			std::string name;
			std::vector<uint32_t> code;
			uint32_t moduleSerialID;
			vk::SpecializationInfo specializationInfo;
		};
		std::vector<Stage> stages;
		vk::PipelineCache *pipelineCache;
		vk::RenderPass *renderPass;
		uint32_t subpassIndex;
	};

	struct Shaders
	{
		std::shared_ptr<sw::SpirvShader> vertex;
		std::shared_ptr<sw::SpirvShader> fragment;
	};

	Shaders getOrBuild(const CompileOptions &);

	std::unique_ptr<const Environment> env;
	CompileCache<Shaders> shaders;

	uint32_t dynamicStateFlags = 0;
	bool primitiveRestartEnable = false;
	sw::Context context;
	VkRect2D scissor;
	VkViewport viewport;
	sw::float4 blendConstants;
};

class ComputePipeline : public Pipeline, public ObjectBase<ComputePipeline, VkPipeline>
{
public:
	ComputePipeline(const VkComputePipelineCreateInfo *pCreateInfo, void *mem, const Device *device);
	virtual ~ComputePipeline() = default;

	void destroyPipeline(const VkAllocationCallbacks *pAllocator) override;

#ifndef NDEBUG
	VkPipelineBindPoint bindPoint() const override
	{
		return VK_PIPELINE_BIND_POINT_COMPUTE;
	}
#endif

	static size_t ComputeRequiredAllocationSize(const VkComputePipelineCreateInfo *pCreateInfo);

	void compileShaders(const VkAllocationCallbacks *pAllocator, const VkComputePipelineCreateInfo *pCreateInfo, PipelineCache *pipelineCache);

	void run(uint32_t baseGroupX, uint32_t baseGroupY, uint32_t baseGroupZ,
	         uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ,
	         vk::DescriptorSet::Array const &descriptorSetObjects,
	         vk::DescriptorSet::Bindings const &descriptorSets,
	         vk::DescriptorSet::DynamicOffsets const &descriptorDynamicOffsets,
	         sw::PushConstantStorage const &pushConstants);

protected:
	struct Environment
	{
		VkShaderStageFlagBits stage;
		std::string name;
		std::vector<uint32_t> code;
		uint32_t moduleSerialID;
		vk::SpecializationInfo specializationInfo;
		vk::PipelineCache *pipelineCache;
	};
	std::shared_ptr<sw::ComputeProgram> getOrBuild(const CompileOptions &);

	std::unique_ptr<const Environment> env;
	CompileCache<std::shared_ptr<sw::ComputeProgram>> programs;
	marl::WaitGroup numPending;
};

static inline Pipeline *Cast(VkPipeline object)
{
	return Pipeline::Cast(object);
}

}  // namespace vk

#endif  // VK_PIPELINE_HPP_
