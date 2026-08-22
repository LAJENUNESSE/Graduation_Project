#pragma once

#include <cstdint>

namespace Engine
{

    // GPU 资源内存分类（互斥：粒子缓冲从 SSBO 中单独划出）
    // 独立成轻量头文件：StorageBuffer.h 的工厂默认参数需要完整类型，
    // 避免让所有缓冲使用者间接引入 Texture/Framebuffer 头。
    enum class GpuMemCategory : uint8_t
    {
        Texture = 0,
        MeshBuffer,
        UniformStorage,
        Particle,
        FramebufferAttachment,
        Other,
        Count
    };

} // namespace Engine
