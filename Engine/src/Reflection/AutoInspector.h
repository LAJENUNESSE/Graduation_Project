#pragma once

namespace Engine
{
    struct ComponentMeta;

    class AutoInspector
    {
    public:
        // 绘制组件的所有反射属性
        // drawVec3Fn: 复用 PropertiesPanel::DrawVec3Control 风格的函数指针
        using DrawVec3Fn = void(*)(const char* label, float* values, float resetValue);

        static void Draw(const ComponentMeta& meta, void* component, DrawVec3Fn drawVec3 = nullptr);
    };

} // namespace Engine
