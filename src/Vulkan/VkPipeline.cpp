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

#include "VkPipeline.hpp"

#include "VkDestroy.hpp"
#include "VkDevice.hpp"
#include "VkPipelineCache.hpp"
#include "VkPipelineLayout.hpp"
#include "VkRenderPass.hpp"
#include "VkShaderModule.hpp"
#include "VkStringify.hpp"
#include "Pipeline/ComputeProgram.hpp"
#include "Pipeline/SpirvShader.hpp"

#include "marl/defer.h"
#include "marl/trace.h"
#include "marl/waitgroup.h"

#include "spirv-tools/optimizer.hpp"

#include <iostream>

namespace {

// preprocessSpirv applies and freezes specializations into constants, and inlines all functions.
std::vector<uint32_t> preprocessSpirv(
    std::vector<uint32_t> const &code,
    VkSpecializationInfo const *specializationInfo,
    bool optimize)
{
	spvtools::Optimizer opt{ SPV_ENV_VULKAN_1_1 };

	opt.SetMessageConsumer([](spv_message_level_t level, const char *source, const spv_position_t &position, const char *message) {
		switch(level)
		{
			case SPV_MSG_FATAL: sw::warn("SPIR-V FATAL: %d:%d %s\n", int(position.line), int(position.column), message);
			case SPV_MSG_INTERNAL_ERROR: sw::warn("SPIR-V INTERNAL_ERROR: %d:%d %s\n", int(position.line), int(position.column), message);
			case SPV_MSG_ERROR: sw::warn("SPIR-V ERROR: %d:%d %s\n", int(position.line), int(position.column), message);
			case SPV_MSG_WARNING: sw::warn("SPIR-V WARNING: %d:%d %s\n", int(position.line), int(position.column), message);
			case SPV_MSG_INFO: sw::trace("SPIR-V INFO: %d:%d %s\n", int(position.line), int(position.column), message);
			case SPV_MSG_DEBUG: sw::trace("SPIR-V DEBUG: %d:%d %s\n", int(position.line), int(position.column), message);
			default: sw::trace("SPIR-V MESSAGE: %d:%d %s\n", int(position.line), int(position.column), message);
		}
	});

	// If the pipeline uses specialization, apply the specializations before freezing
	if(specializationInfo)
	{
		std::unordered_map<uint32_t, std::vector<uint32_t>> specializations;
		for(auto i = 0u; i < specializationInfo->mapEntryCount; ++i)
		{
			auto const &e = specializationInfo->pMapEntries[i];
			auto value_ptr =
			    static_cast<uint32_t const *>(specializationInfo->pData) + e.offset / sizeof(uint32_t);
			specializations.emplace(e.constantID,
			                        std::vector<uint32_t>{ value_ptr, value_ptr + e.size / sizeof(uint32_t) });
		}
		opt.RegisterPass(spvtools::CreateSetSpecConstantDefaultValuePass(specializations));
	}

	if(optimize)
	{
		// Full optimization list taken from spirv-opt.
		opt.RegisterPerformancePasses();
	}

	std::vector<uint32_t> optimized;
	opt.Run(code.data(), code.size(), &optimized);

	if(false)
	{
		spvtools::SpirvTools core(SPV_ENV_VULKAN_1_1);
		std::string preOpt;
		core.Disassemble(code, &preOpt, SPV_BINARY_TO_TEXT_OPTION_NONE);
		std::string postOpt;
		core.Disassemble(optimized, &postOpt, SPV_BINARY_TO_TEXT_OPTION_NONE);
		std::cout << "PRE-OPT: " << preOpt << std::endl
		          << "POST-OPT: " << postOpt << std::endl;
	}

	return optimized;
}

std::shared_ptr<sw::SpirvShader> createShader(
    const vk::PipelineCache::SpirvShaderKey &key,
    uint32_t moduleSerialID,
    bool robustBufferAccess,
    const std::shared_ptr<vk::dbg::Context> &dbgctx)
{
	// Do not optimize the shader if we have a debugger context.
	// Optimization passes are likely to damage debug information, and reorder
	// instructions.
	const bool optimize = !dbgctx;

	// TODO(b/147726513): Do not preprocess the shader if we have a debugger
	// context.
	// This is a work-around for the SPIR-V tools incorrectly reporting errors
	// when debug information is provided. This can be removed once the
	// following SPIR-V tools bugs are fixed:
	// https://github.com/KhronosGroup/SPIRV-Tools/issues/3102
	// https://github.com/KhronosGroup/SPIRV-Tools/issues/3103
	// https://github.com/KhronosGroup/SPIRV-Tools/issues/3118
	auto code = dbgctx ? key.getInsns() : preprocessSpirv(key.getInsns(), key.getSpecializationInfo(), optimize);
	ASSERT(code.size() > 0);

	// If the pipeline has specialization constants, assume they're unique and
	// use a new serial ID so the shader gets recompiled.
	uint32_t codeSerialID = (key.getSpecializationInfo() ? vk::ShaderModule::nextSerialID() : moduleSerialID);

	// TODO(b/119409619): use allocator.
	return std::make_shared<sw::SpirvShader>(codeSerialID, key.getPipelineStage(), key.getEntryPointName().c_str(),
	                                         code, key.getRenderPass(), key.getSubpassIndex(), robustBufferAccess, dbgctx);
}

std::shared_ptr<sw::ComputeProgram> createProgram(std::shared_ptr<sw::SpirvShader> shader, vk::PipelineLayout const *layout)
{
	MARL_SCOPED_EVENT("createProgram");

	vk::DescriptorSet::Bindings descriptorSets;
	// TODO(b/119409619): use allocator.
	auto program = std::make_shared<sw::ComputeProgram>(std::move(shader), layout, descriptorSets);
	program->generate();
	program->finalize();
	return program;
}

}  // anonymous namespace

namespace vk {

Pipeline::Pipeline(PipelineLayout *layout, const Device *device)
    : layout(layout)
    , device(device)
    , robustBufferAccess(device->getEnabledFeatures().robustBufferAccess)
{
	layout->incRefCount();
}

void Pipeline::destroy(const VkAllocationCallbacks *pAllocator)
{
	destroyPipeline(pAllocator);

	vk::release(static_cast<VkPipelineLayout>(*layout), pAllocator);
}

Pipeline::CompileOptions Pipeline::getCompileOptions() const
{
	CompileOptions options;
	options.debuggerEnabled = device->hasDebuggerConnection();
	return options;
}

GraphicsPipeline::GraphicsPipeline(const VkGraphicsPipelineCreateInfo *pCreateInfo, void *mem, const Device *device)
    : Pipeline(vk::Cast(pCreateInfo->layout), device)
{
	context.robustBufferAccess = robustBufferAccess;

	if((pCreateInfo->flags &
	    ~(VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT |
	      VK_PIPELINE_CREATE_DERIVATIVE_BIT |
	      VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT)) != 0)
	{
		UNSUPPORTED("pCreateInfo->flags %d", int(pCreateInfo->flags));
	}

	if(pCreateInfo->pDynamicState)
	{
		if(pCreateInfo->pDynamicState->flags != 0)
		{
			// Vulkan 1.2: "flags is reserved for future use." "flags must be 0"
			UNSUPPORTED("pCreateInfo->pDynamicState->flags %d", int(pCreateInfo->pDynamicState->flags));
		}

		for(uint32_t i = 0; i < pCreateInfo->pDynamicState->dynamicStateCount; i++)
		{
			VkDynamicState dynamicState = pCreateInfo->pDynamicState->pDynamicStates[i];
			switch(dynamicState)
			{
				case VK_DYNAMIC_STATE_VIEWPORT:
				case VK_DYNAMIC_STATE_SCISSOR:
				case VK_DYNAMIC_STATE_LINE_WIDTH:
				case VK_DYNAMIC_STATE_DEPTH_BIAS:
				case VK_DYNAMIC_STATE_BLEND_CONSTANTS:
				case VK_DYNAMIC_STATE_DEPTH_BOUNDS:
				case VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK:
				case VK_DYNAMIC_STATE_STENCIL_WRITE_MASK:
				case VK_DYNAMIC_STATE_STENCIL_REFERENCE:
					ASSERT(dynamicState < (sizeof(dynamicStateFlags) * 8));
					dynamicStateFlags |= (1 << dynamicState);
					break;
				default:
					UNSUPPORTED("VkDynamicState %d", int(dynamicState));
			}
		}
	}

	const VkPipelineVertexInputStateCreateInfo *vertexInputState = pCreateInfo->pVertexInputState;

	if(vertexInputState->flags != 0)
	{
		// Vulkan 1.2: "flags is reserved for future use." "flags must be 0"
		UNSUPPORTED("vertexInputState->flags");
	}

	// Context must always have a PipelineLayout set.
	context.pipelineLayout = layout;

	// Temporary in-binding-order representation of buffer strides, to be consumed below
	// when considering attributes. TODO: unfuse buffers from attributes in backend, is old GL model.
	uint32_t vertexStrides[MAX_VERTEX_INPUT_BINDINGS];
	uint32_t instanceStrides[MAX_VERTEX_INPUT_BINDINGS];
	for(uint32_t i = 0; i < vertexInputState->vertexBindingDescriptionCount; i++)
	{
		auto const &desc = vertexInputState->pVertexBindingDescriptions[i];
		vertexStrides[desc.binding] = desc.inputRate == VK_VERTEX_INPUT_RATE_VERTEX ? desc.stride : 0;
		instanceStrides[desc.binding] = desc.inputRate == VK_VERTEX_INPUT_RATE_INSTANCE ? desc.stride : 0;
	}

	for(uint32_t i = 0; i < vertexInputState->vertexAttributeDescriptionCount; i++)
	{
		auto const &desc = vertexInputState->pVertexAttributeDescriptions[i];
		sw::Stream &input = context.input[desc.location];
		input.format = desc.format;
		input.offset = desc.offset;
		input.binding = desc.binding;
		input.vertexStride = vertexStrides[desc.binding];
		input.instanceStride = instanceStrides[desc.binding];
	}

	const VkPipelineInputAssemblyStateCreateInfo *inputAssemblyState = pCreateInfo->pInputAssemblyState;

	if(inputAssemblyState->flags != 0)
	{
		// Vulkan 1.2: "flags is reserved for future use." "flags must be 0"
		UNSUPPORTED("pCreateInfo->pInputAssemblyState->flags %d", int(pCreateInfo->pInputAssemblyState->flags));
	}

	primitiveRestartEnable = (inputAssemblyState->primitiveRestartEnable != VK_FALSE);
	context.topology = inputAssemblyState->topology;

	const VkPipelineRasterizationStateCreateInfo *rasterizationState = pCreateInfo->pRasterizationState;

	if(rasterizationState->flags != 0)
	{
		// Vulkan 1.2: "flags is reserved for future use." "flags must be 0"
		UNSUPPORTED("pCreateInfo->pRasterizationState->flags %d", int(pCreateInfo->pRasterizationState->flags));
	}

	if(rasterizationState->depthClampEnable != VK_FALSE)
	{
		UNSUPPORTED("VkPhysicalDeviceFeatures::depthClamp");
	}

	context.rasterizerDiscard = (rasterizationState->rasterizerDiscardEnable != VK_FALSE);
	context.cullMode = rasterizationState->cullMode;
	context.frontFace = rasterizationState->frontFace;
	context.polygonMode = rasterizationState->polygonMode;
	context.depthBias = (rasterizationState->depthBiasEnable != VK_FALSE) ? rasterizationState->depthBiasConstantFactor : 0.0f;
	context.slopeDepthBias = (rasterizationState->depthBiasEnable != VK_FALSE) ? rasterizationState->depthBiasSlopeFactor : 0.0f;
	context.lineWidth = rasterizationState->lineWidth;

	const VkBaseInStructure *extensionCreateInfo = reinterpret_cast<const VkBaseInStructure *>(rasterizationState->pNext);
	while(extensionCreateInfo)
	{
		// Casting to a long since some structures, such as
		// VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_FEATURES_EXT
		// are not enumerated in the official Vulkan header
		switch((long)(extensionCreateInfo->sType))
		{
			case VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT:
			{
				const VkPipelineRasterizationLineStateCreateInfoEXT *lineStateCreateInfo = reinterpret_cast<const VkPipelineRasterizationLineStateCreateInfoEXT *>(extensionCreateInfo);
				context.lineRasterizationMode = lineStateCreateInfo->lineRasterizationMode;
			}
			break;
			case VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_PROVOKING_VERTEX_STATE_CREATE_INFO_EXT:
			{
				const VkPipelineRasterizationProvokingVertexStateCreateInfoEXT *provokingVertexModeCreateInfo =
				    reinterpret_cast<const VkPipelineRasterizationProvokingVertexStateCreateInfoEXT *>(extensionCreateInfo);
				context.provokingVertexMode = provokingVertexModeCreateInfo->provokingVertexMode;
			}
			break;
			default:
				WARN("pCreateInfo->pRasterizationState->pNext sType = %s", vk::Stringify(extensionCreateInfo->sType).c_str());
				break;
		}

		extensionCreateInfo = extensionCreateInfo->pNext;
	}

	// The sample count affects the batch size, so it needs initialization even if rasterization is disabled.
	// TODO(b/147812380): Eliminate the dependency between multisampling and batch size.
	context.sampleCount = 1;

	// Only access rasterization state if rasterization is not disabled.
	if(rasterizationState->rasterizerDiscardEnable == VK_FALSE)
	{
		const VkPipelineViewportStateCreateInfo *viewportState = pCreateInfo->pViewportState;
		const VkPipelineMultisampleStateCreateInfo *multisampleState = pCreateInfo->pMultisampleState;
		const VkPipelineDepthStencilStateCreateInfo *depthStencilState = pCreateInfo->pDepthStencilState;
		const VkPipelineColorBlendStateCreateInfo *colorBlendState = pCreateInfo->pColorBlendState;

		if(viewportState->flags != 0)
		{
			// Vulkan 1.2: "flags is reserved for future use." "flags must be 0"
			UNSUPPORTED("pCreateInfo->pViewportState->flags %d", int(pCreateInfo->pViewportState->flags));
		}

		if((viewportState->viewportCount != 1) ||
		   (viewportState->scissorCount != 1))
		{
			UNSUPPORTED("VkPhysicalDeviceFeatures::multiViewport");
		}

		if(!hasDynamicState(VK_DYNAMIC_STATE_SCISSOR))
		{
			scissor = viewportState->pScissors[0];
		}

		if(!hasDynamicState(VK_DYNAMIC_STATE_VIEWPORT))
		{
			viewport = viewportState->pViewports[0];
		}

		if(multisampleState->flags != 0)
		{
			// Vulkan 1.2: "flags is reserved for future use." "flags must be 0"
			UNSUPPORTED("pCreateInfo->pMultisampleState->flags %d", int(pCreateInfo->pMultisampleState->flags));
		}

		if(multisampleState->sampleShadingEnable != VK_FALSE)
		{
			UNSUPPORTED("VkPhysicalDeviceFeatures::sampleRateShading");
		}

		if(multisampleState->alphaToOneEnable != VK_FALSE)
		{
			UNSUPPORTED("VkPhysicalDeviceFeatures::alphaToOne");
		}

		switch(multisampleState->rasterizationSamples)
		{
			case VK_SAMPLE_COUNT_1_BIT:
				context.sampleCount = 1;
				break;
			case VK_SAMPLE_COUNT_4_BIT:
				context.sampleCount = 4;
				break;
			default:
				UNSUPPORTED("Unsupported sample count");
		}

		if(multisampleState->pSampleMask)
		{
			context.sampleMask = multisampleState->pSampleMask[0];
		}
		else  // "If pSampleMask is NULL, it is treated as if the mask has all bits set to 1."
		{
			context.sampleMask = ~0;
		}

		context.alphaToCoverage = (multisampleState->alphaToCoverageEnable != VK_FALSE);
		context.multiSampleMask = context.sampleMask & ((unsigned)0xFFFFFFFF >> (32 - context.sampleCount));

		const vk::RenderPass *renderPass = vk::Cast(pCreateInfo->renderPass);
		const VkSubpassDescription &subpass = renderPass->getSubpass(pCreateInfo->subpass);

		//  Ignore pDepthStencilState when "the subpass of the render pass the pipeline is created against does not use a depth/stencil attachment"
		if(subpass.pDepthStencilAttachment && subpass.pDepthStencilAttachment->attachment != VK_ATTACHMENT_UNUSED)
		{
			if(depthStencilState->flags != 0)
			{
				// Vulkan 1.2: "flags is reserved for future use." "flags must be 0"
				UNSUPPORTED("pCreateInfo->pDepthStencilState->flags %d", int(pCreateInfo->pDepthStencilState->flags));
			}

			if(depthStencilState->depthBoundsTestEnable != VK_FALSE)
			{
				UNSUPPORTED("VkPhysicalDeviceFeatures::depthBounds");
			}

			context.depthBoundsTestEnable = (depthStencilState->depthBoundsTestEnable != VK_FALSE);
			context.depthBufferEnable = (depthStencilState->depthTestEnable != VK_FALSE);
			context.depthWriteEnable = (depthStencilState->depthWriteEnable != VK_FALSE);
			context.depthCompareMode = depthStencilState->depthCompareOp;

			context.stencilEnable = (depthStencilState->stencilTestEnable != VK_FALSE);
			if(context.stencilEnable)
			{
				context.frontStencil = depthStencilState->front;
				context.backStencil = depthStencilState->back;
			}
		}

		bool colorAttachmentUsed = false;
		for(uint32_t i = 0; i < subpass.colorAttachmentCount; i++)
		{
			if(subpass.pColorAttachments[i].attachment != VK_ATTACHMENT_UNUSED)
			{
				colorAttachmentUsed = true;
				break;
			}
		}

		// Ignore pColorBlendState when "the subpass of the render pass the pipeline is created against does not use any color attachments"
		if(colorAttachmentUsed)
		{
			if(colorBlendState->flags != 0)
			{
				// Vulkan 1.2: "flags is reserved for future use." "flags must be 0"
				UNSUPPORTED("pCreateInfo->pColorBlendState->flags %d", int(pCreateInfo->pColorBlendState->flags));
			}

			if(colorBlendState->logicOpEnable != VK_FALSE)
			{
				UNSUPPORTED("VkPhysicalDeviceFeatures::logicOp");
			}

			if(!hasDynamicState(VK_DYNAMIC_STATE_BLEND_CONSTANTS))
			{
				blendConstants.x = colorBlendState->blendConstants[0];
				blendConstants.y = colorBlendState->blendConstants[1];
				blendConstants.z = colorBlendState->blendConstants[2];
				blendConstants.w = colorBlendState->blendConstants[3];
			}

			for(auto i = 0u; i < colorBlendState->attachmentCount; i++)
			{
				const VkPipelineColorBlendAttachmentState &attachment = colorBlendState->pAttachments[i];
				context.colorWriteMask[i] = attachment.colorWriteMask;

				context.setBlendState(i, { (attachment.blendEnable != VK_FALSE),
				                           attachment.srcColorBlendFactor, attachment.dstColorBlendFactor, attachment.colorBlendOp,
				                           attachment.srcAlphaBlendFactor, attachment.dstAlphaBlendFactor, attachment.alphaBlendOp });
			}
		}
	}
}

void GraphicsPipeline::destroyPipeline(const VkAllocationCallbacks *pAllocator)
{
	acquirable.destroy();
}

size_t GraphicsPipeline::ComputeRequiredAllocationSize(const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
	return 0;
}

void GraphicsPipeline::compileShaders(const VkAllocationCallbacks *pAllocator, const VkGraphicsPipelineCreateInfo *pCreateInfo, PipelineCache *pPipelineCache)
{
	std::vector<Environment::Stage> stages;

	for(auto pStage = pCreateInfo->pStages; pStage != pCreateInfo->pStages + pCreateInfo->stageCount; pStage++)
	{
		if(pStage->flags != 0)
		{
			// Vulkan 1.2: "flags must be 0"
			UNSUPPORTED("pStage->flags %d", int(pStage->flags));
		}

		auto module = vk::Cast(pStage->module);

		stages.emplace_back(Environment::Stage{
		    /* stage */ pStage->stage,
		    /* name */ pStage->pName,
		    /* code */ module->getCode(),
		    /* moduleSerialID */ module->getSerialID(),
		    /* specializationInfo */ pStage->pSpecializationInfo,
		});
	}

	ASSERT(!env);
	env = std::make_unique<Environment>(Environment{
	    /* stages */ std::move(stages),
	    /* pipelineCache */ pPipelineCache,
	    /* renderPass */ vk::Cast(pCreateInfo->renderPass),
	    /* subpassIndex */ pCreateInfo->subpass,
	});

	// Start building the shaders now.
	acquirable.acquire();
	layout->incRefCount();
	if(env->pipelineCache) { env->pipelineCache->acquire(); }

	marl::schedule([=] {
		defer(acquirable.release());
		defer(layout->decRefCount());
		defer(if(env->pipelineCache) { env->pipelineCache->release(); });
		getOrBuild(getCompileOptions());
	});
}

GraphicsPipeline::Shaders GraphicsPipeline::getOrBuild(const CompileOptions &options)
{
	return shaders.getOrCreate(options, [&] {
		auto dbgCtx = options.debuggerEnabled ? device->getDebuggerContext() : nullptr;

		Shaders shaders;
		marl::WaitGroup wg(static_cast<int>(env->stages.size()));
		for(auto &stage : env->stages)
		{
			marl::schedule([=, &shaders] {
				defer(wg.done());

				auto const key = PipelineCache::SpirvShaderKey{
					/* pipelineStage */ stage.stage,
					/* entryPointName */ stage.name,
					/* insns */ stage.code,
					/* renderPass */ env->renderPass,
					/* subpassIndex */ env->subpassIndex,
					/* specializationInfo */ stage.specializationInfo,
					/* debuggerEnabled */ options.debuggerEnabled,
				};

				std::shared_ptr<sw::SpirvShader> shader;
				if(env->pipelineCache)
				{
					shader = env->pipelineCache->getOrCreateShader(key, [&] {
						return createShader(key, stage.moduleSerialID, robustBufferAccess, dbgCtx);
					});
				}
				else
				{
					shader = createShader(key, stage.moduleSerialID, robustBufferAccess, dbgCtx);
				}

				switch(stage.stage)
				{
					case VK_SHADER_STAGE_VERTEX_BIT:
						shaders.vertex = shader;
						break;
					case VK_SHADER_STAGE_FRAGMENT_BIT:
						shaders.fragment = shader;
						break;
					default:
						UNSUPPORTED("Unsupported stage");
						break;
				}
			});
		}
		wg.wait();
		return shaders;
	});
}

uint32_t GraphicsPipeline::computePrimitiveCount(uint32_t vertexCount) const
{
	switch(context.topology)
	{
		case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
			return vertexCount;
		case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
			return vertexCount / 2;
		case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
			return std::max<uint32_t>(vertexCount, 1) - 1;
		case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
			return vertexCount / 3;
		case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
			return std::max<uint32_t>(vertexCount, 2) - 2;
		case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
			return std::max<uint32_t>(vertexCount, 2) - 2;
		default:
			UNSUPPORTED("VkPrimitiveTopology %d", int(context.topology));
	}

	return 0;
}

sw::Context GraphicsPipeline::getContext()
{
	auto shaders = getOrBuild(getCompileOptions());

	auto out = context;
	out.vertexShader = shaders.vertex.get();
	out.pixelShader = shaders.fragment.get();
	return out;
}

const VkRect2D &GraphicsPipeline::getScissor() const
{
	return scissor;
}

const VkViewport &GraphicsPipeline::getViewport() const
{
	return viewport;
}

const sw::float4 &GraphicsPipeline::getBlendConstants() const
{
	return blendConstants;
}

bool GraphicsPipeline::hasDynamicState(VkDynamicState dynamicState) const
{
	return (dynamicStateFlags & (1 << dynamicState)) != 0;
}

ComputePipeline::ComputePipeline(const VkComputePipelineCreateInfo *pCreateInfo, void *mem, const Device *device)
    : Pipeline(vk::Cast(pCreateInfo->layout), device)
{
}

void ComputePipeline::destroyPipeline(const VkAllocationCallbacks *pAllocator)
{
	acquirable.destroy();
}

size_t ComputePipeline::ComputeRequiredAllocationSize(const VkComputePipelineCreateInfo *pCreateInfo)
{
	return 0;
}

void ComputePipeline::compileShaders(const VkAllocationCallbacks *pAllocator, const VkComputePipelineCreateInfo *pCreateInfo, PipelineCache *pPipelineCache)
{
	auto &stage = pCreateInfo->stage;

	auto module = vk::Cast(pCreateInfo->stage.module);

	ASSERT(!env);
	env = std::make_unique<Environment>(Environment{
	    /* stage */ stage.stage,
	    /* name */ stage.pName,
	    /* code */ module->getCode(),
	    /* moduleSerialID */ module->getSerialID(),
	    /* specializationInfo */ stage.pSpecializationInfo,
	    /* pipelineCache */ pPipelineCache,
	});

	// Start building the program now.
	acquirable.acquire();
	layout->incRefCount();
	if(env->pipelineCache) { env->pipelineCache->acquire(); }

	marl::schedule([=] {
		defer(acquirable.release());
		defer(layout->decRefCount());
		defer(if(env->pipelineCache) { env->pipelineCache->release(); });
		getOrBuild(getCompileOptions());
	});
}

std::shared_ptr<sw::ComputeProgram> ComputePipeline::getOrBuild(const CompileOptions &options)
{
	return programs.getOrCreate(options, [&] {
		auto dbgCtx = options.debuggerEnabled ? device->getDebuggerContext() : nullptr;

		auto const key = PipelineCache::SpirvShaderKey{
			/* pipelineStage */ env->stage,
			/* entryPointName */ env->name,
			/* insns */ env->code,
			/* renderPass */ nullptr,
			/* subpassIndex */ 0,
			/* specializationInfo */ env->specializationInfo,
			/* debuggerEnabled */ options.debuggerEnabled,
		};

		auto const moduleSerialID = env->moduleSerialID;

		std::shared_ptr<sw::SpirvShader> shader;
		if(env->pipelineCache)
		{
			shader = env->pipelineCache->getOrCreateShader(key, [&] {
				return createShader(key, moduleSerialID, robustBufferAccess, dbgCtx);
			});
		}
		else
		{
			shader = createShader(key, moduleSerialID, robustBufferAccess, dbgCtx);
		}

		const PipelineCache::ComputeProgramKey programKey(shader.get(), layout);
		if(env->pipelineCache)
		{
			return env->pipelineCache->getOrCreateComputeProgram(programKey, [&] {
				return createProgram(std::move(shader), layout);
			});
		}
		else
		{
			return createProgram(std::move(shader), layout);
		}
	});
}

void ComputePipeline::run(uint32_t baseGroupX, uint32_t baseGroupY, uint32_t baseGroupZ,
                          uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ,
                          vk::DescriptorSet::Array const &descriptorSetObjects,
                          vk::DescriptorSet::Bindings const &descriptorSets,
                          vk::DescriptorSet::DynamicOffsets const &descriptorDynamicOffsets,
                          sw::PushConstantStorage const &pushConstants)
{
	ASSERT(env);

	auto program = getOrBuild(getCompileOptions());
	ASSERT_OR_RETURN(program != nullptr);

	program->run(
	    descriptorSetObjects, descriptorSets, descriptorDynamicOffsets, pushConstants,
	    baseGroupX, baseGroupY, baseGroupZ,
	    groupCountX, groupCountY, groupCountZ);
}

}  // namespace vk
