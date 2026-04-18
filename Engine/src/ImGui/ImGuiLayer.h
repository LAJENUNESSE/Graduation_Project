#pragma once

#include "Core/Layer.h"

struct ImFont;

namespace Engine
{

    class ImGuiLayer : public Layer
    {
    public:
        ImGuiLayer();
        ~ImGuiLayer() = default;

        void OnAttach() override;
        void OnDetach() override;
        void OnEvent(Event& event) override;

        void Begin();
        void End();

        void SetBlockEvents(bool block) { m_BlockEvents = block; }

        // 代码等宽字体（JetBrains Mono 拉丁 + Sarasa Mono SC CJK fallback）。
        // 由 ScriptEditorPanel 等代码编辑面板 PushFont/PopFont 使用。
        // 若字体文件缺失返回 nullptr（调用方应做 null 检查）。
        static ImFont* GetCodeFont() { return s_CodeFont; }

    private:
        bool            m_BlockEvents = true;
        static ImFont*  s_CodeFont;
    };

} // namespace Engine
