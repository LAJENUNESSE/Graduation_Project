#include "engpch.h"
#include "Platform/Vulkan/VulkanSceneDrawDispatcher.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "Platform/Vulkan/VulkanAllocator.h"

#include <vma/vk_mem_alloc.h>
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanDescriptor.h"
#include "Platform/Vulkan/VulkanGraphicsPipelineBuilder.h"
#include "Platform/Vulkan/VulkanSceneState.h"
#include "Platform/Vulkan/VulkanVertexArray.h"

#include <glm/glm.hpp>

namespace Engine
{

    namespace
    {
        // ---- std140 打包结构：与 PBR.glsl Vulkan 分支布局逐字节对齐 ----

        struct GlobalUboStd140
        {
            glm::mat4 ViewProjection;               //   0
            glm::mat4 LightSpaceMatrix;             //  64
            glm::mat4 ViewMatrix;                   // 128
            glm::vec3 ViewPos;                      // 192
            float     _Pad0;                        // 204
            glm::mat4 CascadeLightSpaceMatrices[4]; // 208
            // std140 数组 stride=16B：C++ float[4]=16B，各补 48B 到 64B 槽位
            float   CascadeSplitDepths[4]; // 464
            float   _Pad1[12];
            float   CascadeTexelWorldSize[4]; // 528
            float   _Pad2[12];
            int32_t NumDirLights; // 592
            int32_t NumPointLights;
            int32_t NumSpotLights;
            int32_t CascadeCount;
            int32_t ShadowEnabled; // 608
            int32_t CSMEnabled;
            int32_t IBLEnabled;
            int32_t IBLDebugMode;
            int32_t SSAOEnabled; // 624
            float   AmbientStrength;
            float   IBLIntensity;
            float   ShadowBias; // 636
        };
        static_assert(sizeof(GlobalUboStd140) == 640, "GlobalUBO std140 layout mismatch");

        struct DirLightStd140
        {
            glm::vec3 Direction;
            float     _Pad0;
            glm::vec3 Color;
            float     _Pad1;
            float     Intensity;
            float     _Pad2[3];
        };
        static_assert(sizeof(DirLightStd140) == 48, "DirLight std140 size");

        struct PointLightStd140
        {
            glm::vec3 Position;
            float     _Pad0;
            glm::vec3 Color;
            float     _Pad1;
            float     Intensity, Constant, Linear, Quadratic;
        };
        static_assert(sizeof(PointLightStd140) == 48, "PointLight std140 size");

        struct SpotLightStd140
        {
            glm::vec3 Position;
            float     _Pad0;
            glm::vec3 Direction;
            float     _Pad1;
            glm::vec3 Color;
            float     _Pad2;
            float     Intensity, Constant, Linear, Quadratic, InnerCutoff, OuterCutoff; // @48..72
            float     _Pad3[2]; // struct 向上取整到 16B 倍数 → 80
        };
        static_assert(sizeof(SpotLightStd140) == 80, "SpotLight std140 size");

        struct LightsUboStd140
        {
            DirLightStd140   DirLights[2];   //   0
            PointLightStd140 PointLights[8]; //  96
            SpotLightStd140  SpotLights[4];  // 480
        };
        static_assert(sizeof(LightsUboStd140) == 800, "LightsUBO std140 size");

        struct MaterialUboStd140
        {
            glm::vec4 Color;  //  0
            glm::vec2 Tiling; // 16
            float     _Pad0[2];
            float     Metallic;   // 24
            float     Roughness;  // 28
            int32_t   HasTexture; // 32
            int32_t   HasNormalMap;
            int32_t   HasMetallicMap;
            int32_t   HasRoughnessMap;
            int32_t   HasAOMap;
            int32_t   EntityID; // 52
        };
        static_assert(sizeof(MaterialUboStd140) == 64, "MaterialUBO std140 size");

        struct ScenePCStd140
        {
            glm::mat4 Transform;       //  0
            glm::vec4 NormalMatrix[3]; // 64：std430 mat3 列各占 16B
        };
        static_assert(sizeof(ScenePCStd140) == 112, "Scene push constant size");

        // GLSL sampler uniform 名 → OpenGL texture unit（与 PBR.glsl / Skybox.glsl 约定一致）
        uint32_t SlotForSamplerName(std::string name)
        {
            // 去掉数组元素后缀（反射名可能带 "[0]"）
            const size_t bracket = name.find('[');
            if (bracket != std::string::npos)
                name.resize(bracket);

            static const std::unordered_map<std::string, uint32_t> table = {
                {"u_DiffuseTexture", 0}, {"u_ShadowMap", 1},     {"u_NormalMap", 2},     {"u_MetallicMap", 3},
                {"u_RoughnessMap", 4},   {"u_AOMap", 5},         {"u_IrradianceMap", 6}, {"u_PrefilterMap", 7},
                {"u_BRDF_LUT", 8},       {"u_SSAOTexture", 9},   {"u_Skybox", 0},        {"u_HDRBuffer", 14},
                {"u_BloomBlur", 15},     {"u_GrassTexture", 12}, {"u_Splatmap", 6},
            };
            auto it = table.find(name);
            return it != table.end() ? it->second : UINT32_MAX;
        }

    } // namespace

    void VulkanSceneDrawDispatcher::Init(VkDevice device)
    {
        ENGINE_CORE_RELEASE_ASSERT(device != VK_NULL_HANDLE, "VulkanSceneDrawDispatcher requires a device");
        m_Device = device;

        for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
            CreateFrameResources(i);

        m_Initialized = true;
        ENGINE_CORE_INFO("[Vulkan] Scene draw dispatcher initialized");
    }

    void VulkanSceneDrawDispatcher::CreateFrameResources(uint32_t frameIndex)
    {
        FrameResources& fr = m_Frames[frameIndex];

        // ---- global buffer（Global + Lights 两段，host-visible persistent mapped）----
        {
            const VkDeviceSize totalSize = kLightsUboOffset + kLightsUboSize;

            VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bufferInfo.size  = totalSize;
            bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

            VmaAllocationInfo outInfo{};
            const VkResult    result = vmaCreateBuffer(VulkanAllocator::GetAllocator(), &bufferInfo, &allocInfo,
                                                       &fr.GlobalBuffer, &fr.GlobalAllocation, &outInfo);
            ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create scene global UBO buffer");
            fr.GlobalMapped = outInfo.pMappedData;
        }

        // ---- material ring ----
        {
            const VkDeviceSize totalSize = static_cast<VkDeviceSize>(kMaterialUboSize) * kMaxMaterialAllocsPerFrame;

            VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bufferInfo.size  = totalSize;
            bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

            VmaAllocationInfo outInfo{};
            const VkResult    result = vmaCreateBuffer(VulkanAllocator::GetAllocator(), &bufferInfo, &allocInfo,
                                                       &fr.MaterialBuffer, &fr.MaterialAllocation, &outInfo);
            ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create scene material ring buffer");
            fr.MaterialMapped = outInfo.pMappedData;
        }

        // ---- per-frame descriptor pool（P-15 惯例：帧首 Reset）----
        {
            std::vector<VkDescriptorPoolSize> sizes = {
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxMaterialAllocsPerFrame * 3},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxMaterialAllocsPerFrame * 8},
            };

            VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            poolInfo.maxSets       = kMaxMaterialAllocsPerFrame * 2;
            poolInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
            poolInfo.pPoolSizes    = sizes.data();

            const VkResult result = vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &fr.Pool);
            ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create scene descriptor pool");
        }
    }

    void VulkanSceneDrawDispatcher::Shutdown(VkDevice device)
    {
        if (!m_Initialized)
            return;

        vkDeviceWaitIdle(device);
        for (auto& fr : m_Frames)
        {
            if (fr.Pool != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(device, fr.Pool, nullptr);
                fr.Pool = VK_NULL_HANDLE;
            }
            if (fr.GlobalAllocation)
            {
                vmaDestroyBuffer(VulkanAllocator::GetAllocator(), fr.GlobalBuffer, fr.GlobalAllocation);
                fr.GlobalBuffer     = VK_NULL_HANDLE;
                fr.GlobalAllocation = nullptr;
            }
            if (fr.MaterialAllocation)
            {
                vmaDestroyBuffer(VulkanAllocator::GetAllocator(), fr.MaterialBuffer, fr.MaterialAllocation);
                fr.MaterialBuffer     = VK_NULL_HANDLE;
                fr.MaterialAllocation = nullptr;
            }
        }
        m_Initialized = false;
    }

    void VulkanSceneDrawDispatcher::OnBeginFrame(uint32_t frameIndex)
    {
        FrameResources& fr = m_Frames[frameIndex];
        fr.MaterialOffset  = 0;
        if (fr.Pool)
            vkResetDescriptorPool(m_Device, fr.Pool, 0);
    }

    void VulkanSceneDrawDispatcher::PackAndUploadGlobals(VulkanShader* shader, uint32_t frameIndex)
    {
        FrameResources& fr = m_Frames[frameIndex];
        auto*           g  = static_cast<GlobalUboStd140*>(fr.GlobalMapped);
        auto* lts = reinterpret_cast<LightsUboStd140*>(static_cast<uint8_t*>(fr.GlobalMapped) + kLightsUboOffset);

        const auto& mat4s = shader->GetMat4Uniforms();
        const auto& vec3s = shader->GetFloat3Uniforms();
        const auto& ints  = shader->GetIntUniforms();
        const auto& flts  = shader->GetFloatUniforms();

        auto readMat4 = [&](const char* name, glm::mat4& out)
        {
            auto it = mat4s.find(name);
            if (it != mat4s.end())
                out = it->second;
        };
        auto readVec3 = [&](const char* name, glm::vec3& out)
        {
            auto it = vec3s.find(name);
            if (it != vec3s.end())
                out = it->second;
        };
        auto readInt = [&](const char* name, int32_t& out)
        {
            auto it = ints.find(name);
            if (it != ints.end())
                out = it->second;
        };
        auto readFloat = [&](const char* name, float& out)
        {
            auto it = flts.find(name);
            if (it != flts.end())
                out = it->second;
        };

        // ---- globals ----
        readMat4("u_ViewProjection", g->ViewProjection);
        readMat4("u_LightSpaceMatrix", g->LightSpaceMatrix);
        readMat4("u_ViewMatrix", g->ViewMatrix);
        readVec3("u_ViewPos", g->ViewPos);
        for (int i = 0; i < 4; ++i)
        {
            readMat4(("u_CascadeLightSpaceMatrices[" + std::to_string(i) + "]").c_str(),
                     g->CascadeLightSpaceMatrices[i]);
            readFloat(("u_CascadeSplitDepths[" + std::to_string(i) + "]").c_str(), g->CascadeSplitDepths[i]);
            readFloat(("u_CascadeTexelWorldSize[" + std::to_string(i) + "]").c_str(), g->CascadeTexelWorldSize[i]);
        }
        readInt("u_NumDirLights", g->NumDirLights);
        readInt("u_NumPointLights", g->NumPointLights);
        readInt("u_NumSpotLights", g->NumSpotLights);
        readInt("u_CascadeCount", g->CascadeCount);
        readInt("u_ShadowEnabled", g->ShadowEnabled);
        readInt("u_CSMEnabled", g->CSMEnabled);
        readInt("u_IBLEnabled", g->IBLEnabled);
        readInt("u_IBLDebugMode", g->IBLDebugMode);
        readInt("u_SSAOEnabled", g->SSAOEnabled);
        readFloat("u_AmbientStrength", g->AmbientStrength);
        readFloat("u_IBLIntensity", g->IBLIntensity);
        readFloat("u_ShadowBias", g->ShadowBias);

        // ---- lights（std140 结构体数组逐字段提取）----
        *lts = {};
        for (int i = 0; i < 2; ++i)
        {
            const std::string idx = "[" + std::to_string(i) + "]";
            auto&             dl  = lts->DirLights[i];
            readVec3(("u_DirLights" + idx + ".direction").c_str(), dl.Direction);
            readVec3(("u_DirLights" + idx + ".color").c_str(), dl.Color);
            readFloat(("u_DirLights" + idx + ".intensity").c_str(), dl.Intensity);
        }
        for (int i = 0; i < 8; ++i)
        {
            const std::string idx = "[" + std::to_string(i) + "]";
            auto&             pl  = lts->PointLights[i];
            readVec3(("u_PointLights" + idx + ".position").c_str(), pl.Position);
            readVec3(("u_PointLights" + idx + ".color").c_str(), pl.Color);
            readFloat(("u_PointLights" + idx + ".intensity").c_str(), pl.Intensity);
            readFloat(("u_PointLights" + idx + ".constant").c_str(), pl.Constant);
            readFloat(("u_PointLights" + idx + ".linear").c_str(), pl.Linear);
            readFloat(("u_PointLights" + idx + ".quadratic").c_str(), pl.Quadratic);
        }
        for (int i = 0; i < 4; ++i)
        {
            const std::string idx = "[" + std::to_string(i) + "]";
            auto&             sl  = lts->SpotLights[i];
            readVec3(("u_SpotLights" + idx + ".position").c_str(), sl.Position);
            readVec3(("u_SpotLights" + idx + ".direction").c_str(), sl.Direction);
            readVec3(("u_SpotLights" + idx + ".color").c_str(), sl.Color);
            readFloat(("u_SpotLights" + idx + ".intensity").c_str(), sl.Intensity);
            readFloat(("u_SpotLights" + idx + ".constant").c_str(), sl.Constant);
            readFloat(("u_SpotLights" + idx + ".linear").c_str(), sl.Linear);
            readFloat(("u_SpotLights" + idx + ".quadratic").c_str(), sl.Quadratic);
            readFloat(("u_SpotLights" + idx + ".innerCutoff").c_str(), sl.InnerCutoff);
            readFloat(("u_SpotLights" + idx + ".outerCutoff").c_str(), sl.OuterCutoff);
        }
    }

    uint32_t VulkanSceneDrawDispatcher::PackMaterial(VulkanShader* shader, uint32_t frameIndex)
    {
        FrameResources& fr = m_Frames[frameIndex];
        if (fr.MaterialOffset + kMaterialUboSize > kMaterialUboSize * kMaxMaterialAllocsPerFrame)
        {
            static bool warnedFull = false;
            if (!warnedFull)
            {
                warnedFull = true;
                ENGINE_CORE_WARN("[Vulkan] Material ring exhausted this frame; draws dropped");
            }
            return UINT32_MAX;
        }

        auto* m = reinterpret_cast<MaterialUboStd140*>(static_cast<uint8_t*>(fr.MaterialMapped) + fr.MaterialOffset);

        const auto& vec4s = shader->GetFloat4Uniforms();
        const auto& vec2s = shader->GetFloat2Uniforms();
        const auto& flts  = shader->GetFloatUniforms();
        const auto& ints  = shader->GetIntUniforms();

        if (auto it = vec4s.find("u_Color"); it != vec4s.end())
            m->Color = it->second;
        if (auto it = vec2s.find("u_Tiling"); it != vec2s.end())
            m->Tiling = it->second;
        if (auto it = flts.find("u_Metallic"); it != flts.end())
            m->Metallic = it->second;
        if (auto it = flts.find("u_Roughness"); it != flts.end())
            m->Roughness = it->second;
        if (auto it = ints.find("u_HasTexture"); it != ints.end())
            m->HasTexture = it->second;
        if (auto it = ints.find("u_HasNormalMap"); it != ints.end())
            m->HasNormalMap = it->second;
        if (auto it = ints.find("u_HasMetallicMap"); it != ints.end())
            m->HasMetallicMap = it->second;
        if (auto it = ints.find("u_HasRoughnessMap"); it != ints.end())
            m->HasRoughnessMap = it->second;
        if (auto it = ints.find("u_HasAOMap"); it != ints.end())
            m->HasAOMap = it->second;
        if (auto it = ints.find("u_EntityID"); it != ints.end())
            m->EntityID = it->second;

        const uint32_t offset = fr.MaterialOffset;
        fr.MaterialOffset += kMaterialUboSize;
        return offset;
    }

    bool VulkanSceneDrawDispatcher::DispatchDraw(const VertexArray* vertexArray,
                                                 VulkanShader*      shader,
                                                 const DrawParams&  params,
                                                 uint32_t           frameIndex)
    {
        if (!m_Initialized || !shader || !vertexArray || params.RenderPass == VK_NULL_HANDLE ||
            params.Cmd == VK_NULL_HANDLE)
            return false;

        auto* va = dynamic_cast<const VulkanVertexArray*>(vertexArray);
        if (!va || va->GetVertexBuffers().empty())
            return false;

        auto* context = VulkanContext::Get();
        if (!context)
            return false;

        FrameResources& fr = m_Frames[frameIndex];

        // ---- pipeline ----
        GraphicsPipelineDesc desc{};
        desc.Shader               = shader;
        desc.RenderPass           = params.RenderPass;
        desc.ColorAttachmentCount = params.ColorAttachmentCount;
        desc.Bindings             = va->BuildBindingDescriptions();
        desc.Attributes           = va->BuildAttributeDescriptions();
        desc.DepthTest            = params.DepthTest;
        desc.DepthWrite           = params.DepthWrite;
        desc.DepthLEqual          = params.DepthLEqual;
        desc.CullBack             = params.CullBack;

        VulkanPipelineCache::PipelineHandle handle = context->GetPipelineBuilder().GetOrCreate(m_Device, desc);
        if (handle.Pipeline == VK_NULL_HANDLE)
            return false;

        // ---- uniforms ----
        PackAndUploadGlobals(shader, frameIndex);
        const uint32_t materialOffset = PackMaterial(shader, frameIndex);
        if (materialOffset == UINT32_MAX)
            return false;

        // ---- descriptor sets：按反射 binding 分派写入（layout 复用 builder 缓存）----
        const auto& bindings = shader->GetReflectedBindings();

        uint32_t maxSet = 0;
        for (const auto& b : bindings)
            maxSet = std::max(maxSet, b.Set);

        const auto* cachedLayouts = context->GetPipelineBuilder().GetSetLayouts(shader);
        if (!cachedLayouts || cachedLayouts->size() <= maxSet)
            return false;

        std::array<VkDescriptorSet, 2> sets{};
        for (uint32_t set = 0; set <= maxSet && set < 2; ++set)
        {
            VkDescriptorSetLayout             layoutHandle = (*cachedLayouts)[set]->GetHandle();
            const VkDescriptorSetAllocateInfo setAllocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr,
                                                           fr.Pool, 1, &layoutHandle};
            VkDescriptorSet                   allocated = VK_NULL_HANDLE;
            const VkResult allocResult                  = vkAllocateDescriptorSets(m_Device, &setAllocInfo, &allocated);
            if (allocResult != VK_SUCCESS || allocated == VK_NULL_HANDLE)
                return false;
            sets[set] = allocated;
        }

        const VulkanSceneState& scene = context->GetSceneState();

        VulkanDescriptorWriter w0;
        VulkanDescriptorWriter w1;
        bool                   hasSet1 = (maxSet >= 1);

        for (const auto& b : bindings)
        {
            const std::string baseName = [&]
            {
                std::string  n          = b.Name;
                const size_t bracketPos = n.find('[');
                if (bracketPos != std::string::npos)
                    n.resize(bracketPos);
                return n;
            }();

            if (b.Type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            {
                if (baseName.find("Global") != std::string::npos)
                    (b.Set == 0 ? w0 : w1).WriteBuffer(b.Binding, fr.GlobalBuffer, 0, kGlobalUboSize, b.Type);
                else if (baseName.find("Lights") != std::string::npos)
                    (b.Set == 0 ? w0 : w1)
                        .WriteBuffer(b.Binding, fr.GlobalBuffer, kLightsUboOffset, kLightsUboSize, b.Type);
                else if (hasSet1 && b.Set == 1)
                    w1.WriteBuffer(b.Binding, fr.MaterialBuffer, materialOffset, kMaterialUboSize, b.Type);
            }
            else if (b.Type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            {
                const uint32_t slotBase = SlotForSamplerName(baseName);
                if (slotBase == UINT32_MAX)
                    continue;

                for (uint32_t elem = 0; elem < b.Count; ++elem)
                {
                    const auto& slot = scene.GetTextureSlot(slotBase + elem);
                    if (!slot.Valid)
                        continue;

                    if (b.Count > 1)
                        (b.Set == 0 ? w0 : w1)
                            .WriteImageElement(b.Binding, elem, slot.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                               b.Type, slot.Sampler);
                    else
                        (b.Set == 0 ? w0 : w1)
                            .WriteImage(b.Binding, slot.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, b.Type,
                                        slot.Sampler);
                }
            }
        }

        if (sets[0])
            w0.UpdateSet(m_Device, sets[0]);
        if (sets[1])
            w1.UpdateSet(m_Device, sets[1]);

        // ---- push constant（per-draw 变换；无反射 PC 的 shader 跳过）----
        const auto& pcs = shader->GetReflectedPushConstants();
        if (!pcs.empty())
        {
            ScenePCStd140 pcData{};
            if (auto it = shader->GetMat4Uniforms().find("u_Transform"); it != shader->GetMat4Uniforms().end())
                pcData.Transform = it->second;
            if (auto it = shader->GetMat3Uniforms().find("u_NormalMatrix"); it != shader->GetMat3Uniforms().end())
            {
                // glm 列主序 mat3 → 3×vec16B 槽位
                for (int c = 0; c < 3; ++c)
                    pcData.NormalMatrix[c] = glm::vec4(it->second[c], 0.0f);
            }

            for (const auto& range : pcs)
                VulkanCommandBuffer(params.Cmd)
                    .PushConstants(handle.Layout, range.Stages, range.Offset, range.Size,
                                   reinterpret_cast<const uint8_t*>(&pcData) + range.Offset);
        }

        // ---- 录制 ----
        VulkanCommandBuffer cmd(params.Cmd);
        cmd.BindGraphicsPipeline(handle.Pipeline);

        std::vector<VkDescriptorSet> bindSets;
        for (uint32_t set = 0; set <= maxSet && set < 2; ++set)
            if (sets[set])
                bindSets.push_back(sets[set]);
        if (!bindSets.empty())
            cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, handle.Layout, 0, bindSets);

        // 顶点缓冲绑定（binding 号 = VAO 内 vertex buffer 下标）
        {
            const auto&           vbos = va->GetVertexBuffers();
            std::vector<VkBuffer> buffers;
            buffers.reserve(vbos.size());
            for (const auto& vbo : vbos)
                buffers.push_back(static_cast<const VulkanVertexBuffer*>(vbo.get())->GetBuffer());
            cmd.BindVertexBuffers(0, buffers);
        }

        if (params.Indexed)
        {
            const Ref<IndexBuffer>& ibo = va->GetIndexBuffer();
            if (!ibo)
                return false;
            cmd.BindIndexBuffer(static_cast<const VulkanIndexBuffer*>(ibo.get())->GetBuffer());
            cmd.DrawIndexed(params.IndexCount, 1, params.FirstIndex, params.VertexOffset);
        }
        else
        {
            cmd.Draw(params.VertexCount, 1, params.FirstVertex);
        }

        return true;
    }

} // namespace Engine
