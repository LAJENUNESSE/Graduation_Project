#pragma once

namespace Engine
{

    // 无头模式开关：Server 等无图形上下文的进程置位后，
    // 图形资源工厂（Mesh/Terrain VAO/纹理）与输入系统走无 GL 路径（占位或跳过）。
    // 必须在任何场景加载前设置，运行期只读。
    class Headless
    {
    public:
        static void SetEnabled(bool enabled) { s_Enabled = enabled; }
        static bool Enabled() { return s_Enabled; }

    private:
        static inline bool s_Enabled = false;
    };

} // namespace Engine
