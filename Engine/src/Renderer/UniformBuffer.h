#pragma once

#include <cstdint>

#include "Core/Base.h"

namespace Engine
{

    class UniformBuffer
    {
    public:
        virtual ~UniformBuffer() = default;

        virtual void SetData(const void* data, uint32_t size, uint32_t offset = 0) = 0;

        // 绑定到指定 binding 点（OpenGL glBindBufferBase 语义；Vulkan 下录入场景
        // 状态机通用 UBO 槽，供场景 dispatcher 按反射 binding 写 descriptor）。
        // 仅 Vulkan 场景绘制路径消费；OpenGL 路径构造时已固定 binding。
        virtual void Bind(uint32_t binding) const = 0;

        static Ref<UniformBuffer> Create(uint32_t size, uint32_t binding);
    };

} // namespace Engine
