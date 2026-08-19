/*
* Copyright 2023 NVIDIA Corporation.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*    http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include <iostream>
#include <stdio.h>
#include <string.h>
#include "VulkanComputePipeline.h"

// Create Graphics Pipeline
VkResult VulkanComputePipeline::CreatePipeline(const VulkanDeviceContext* vkDevCtx,
                                               VulkanShaderCompiler& shaderCompiler,
                                               const char *shaderCode, size_t shaderSize,
                                               const char* pEntryName, // "main"
                                               uint32_t workgroupSizeX, // usually 16
                                               uint32_t workgroupSizeY, // usually 16
                                               const VulkanDescriptorSetLayout* pDescriptorSetLayout)
{
    m_vkDevCtx = vkDevCtx;

    if (m_pipelineCache == VkPipelineCache(0)) {
        // Create the pipeline cache
        VkPipelineCacheCreateInfo pipelineCacheInfo = VkPipelineCacheCreateInfo();
        pipelineCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        pipelineCacheInfo.pNext = nullptr;
        pipelineCacheInfo.initialDataSize = 0;
        pipelineCacheInfo.pInitialData = nullptr;
        pipelineCacheInfo.flags = 0;  // reserved, must be 0
        VkResult result = m_vkDevCtx->CreatePipelineCache(*m_vkDevCtx, &pipelineCacheInfo, nullptr, &m_pipelineCache);
        if (result != VK_SUCCESS) {
            return result;
        }
    }

    const bool verbose = false;

    if (verbose) printf("\nCompute shader code:\n %s", shaderCode);

    DestroyShaderModule();
    m_shaderModule = shaderCompiler.BuildGlslShader(shaderCode,
                                                    shaderSize,
                                                    VK_SHADER_STAGE_COMPUTE_BIT,
                                                    m_vkDevCtx);
    if (m_shaderModule == VK_NULL_HANDLE) {
        // BuildGlslShader() returns VK_NULL_HANDLE when the GLSL fails to compile, and
        // that return was being ignored: the null module went straight into
        // VkComputePipelineCreateInfo::stage.module and the process SEGFAULTED inside
        // pipeline creation. A generator that emits invalid GLSL is a bug worth seeing,
        // but it has to surface as an error rather than a crash -- the crash hides both
        // the compiler diagnostics printed just above and which shader was at fault.
        // Hit for real by the YCBCR2RGBA generator, whose main() assumes a 2-plane input
        // and so emits code referencing an undeclared sampler for 3-plane formats.
        std::cerr << "VulkanComputePipeline: shader failed to compile; "
                     "see the compiler diagnostics above. Pipeline not created."
                  << std::endl;
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Create the pipeline
    VkComputePipelineCreateInfo computePipelineCreateInfo {};
    computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;


    const uint32_t specValues[] = {workgroupSizeX, workgroupSizeY};

                                               // constantID,     offset,                  size
    const VkSpecializationMapEntry specMapEntries[] {{ 0,            0,               sizeof(specValues[0])},
                                                     { 1,      sizeof(specValues[0]), sizeof(specValues[1])}};

    const VkSpecializationInfo specializationInfo { (uint32_t)(sizeof(specMapEntries)/sizeof(specMapEntries[0])), specMapEntries,
                                                     sizeof(specValues), specValues};

    // Specify the compute shader module and entry point
    computePipelineCreateInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computePipelineCreateInfo.stage.flags = 0;
    computePipelineCreateInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computePipelineCreateInfo.stage.module = m_shaderModule;
    computePipelineCreateInfo.stage.pName = pEntryName;
    computePipelineCreateInfo.stage.pSpecializationInfo = &specializationInfo;

    computePipelineCreateInfo.layout = pDescriptorSetLayout->GetPipelineLayout();
    computePipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
    computePipelineCreateInfo.basePipelineIndex = 0;

    // Make sure we destroy the existing pipeline, if it were to exist.
    DestroyPipeline();
    VkResult pipelineResult = m_vkDevCtx->CreateComputePipelines(*m_vkDevCtx, m_pipelineCache, 1,
                                                                  &computePipelineCreateInfo,
                                                                  nullptr, &m_pipeline);

    return pipelineResult;
}
