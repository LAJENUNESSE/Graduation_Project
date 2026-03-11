#include "engpch.h"
#include "Reflection/AutoInspector.h"
#include "Reflection/ComponentMeta.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

namespace Engine
{

    // 内建简易 Vec3 控件（当外部未提供 DrawVec3Fn 时使用）
    static void DefaultDrawVec3(const char* label, float* values, float /*resetValue*/)
    {
        ImGui::DragFloat3(label, values, 0.1f);
    }

    void AutoInspector::Draw(const ComponentMeta& meta, void* component, DrawVec3Fn drawVec3)
    {
        if (!drawVec3)
            drawVec3 = &DefaultDrawVec3;

        for (auto& prop : meta.Properties)
        {
            if (prop.Hints.Transient)
                continue;

            void* ptr = static_cast<char*>(component) + prop.Offset;

            // Group 分组标题
            if (prop.Hints.Group)
            {
                ImGui::Separator();
                ImGui::Text("%s", prop.Hints.Group);
            }

            float speed = prop.Hints.Speed > 0.0f ? prop.Hints.Speed : 0.1f;
            const char* fmt = prop.Hints.Format ? prop.Hints.Format : "%.3f";

            ImGui::PushID(prop.Name);

            switch (prop.Type)
            {
            case PropertyType::Float:
            {
                float* val = static_cast<float*>(ptr);
                if (prop.Hints.Min != 0.0f || prop.Hints.Max != 0.0f)
                    ImGui::DragFloat(prop.DisplayName, val, speed, prop.Hints.Min, prop.Hints.Max, fmt);
                else
                    ImGui::DragFloat(prop.DisplayName, val, speed, 0.0f, 0.0f, fmt);
                break;
            }
            case PropertyType::Int:
            {
                int* val = static_cast<int*>(ptr);
                int imin = static_cast<int>(prop.Hints.Min);
                int imax = static_cast<int>(prop.Hints.Max);
                ImGui::DragInt(prop.DisplayName, val, speed, imin, imax);
                break;
            }
            case PropertyType::UInt32:
            {
                int tempVal = static_cast<int>(*static_cast<uint32_t*>(ptr));
                int imin = static_cast<int>(prop.Hints.Min);
                int imax = prop.Hints.Max > 0.0f ? static_cast<int>(prop.Hints.Max) : 1000000;
                if (ImGui::DragInt(prop.DisplayName, &tempVal, speed, imin, imax))
                    *static_cast<uint32_t*>(ptr) = static_cast<uint32_t>(std::max(tempVal, 0));
                break;
            }
            case PropertyType::Bool:
                ImGui::Checkbox(prop.DisplayName, static_cast<bool*>(ptr));
                break;
            case PropertyType::String:
            case PropertyType::AssetPath:
            {
                auto& str = *static_cast<std::string*>(ptr);
                char buf[256];
                memset(buf, 0, sizeof(buf));
                std::strncpy(buf, str.c_str(), sizeof(buf) - 1);
                if (ImGui::InputText(prop.DisplayName, buf, sizeof(buf)))
                    str = std::string(buf);
                break;
            }
            case PropertyType::Vec2:
            {
                auto& v = *static_cast<glm::vec2*>(ptr);
                ImGui::DragFloat2(prop.DisplayName, glm::value_ptr(v), speed);
                break;
            }
            case PropertyType::Vec3:
                drawVec3(prop.DisplayName, static_cast<float*>(ptr), 0.0f);
                break;
            case PropertyType::Vec4:
            {
                auto& v = *static_cast<glm::vec4*>(ptr);
                ImGui::DragFloat4(prop.DisplayName, glm::value_ptr(v), speed);
                break;
            }
            case PropertyType::Color3:
                ImGui::ColorEdit3(prop.DisplayName, static_cast<float*>(ptr));
                break;
            case PropertyType::Color4:
                ImGui::ColorEdit4(prop.DisplayName, static_cast<float*>(ptr));
                break;
            case PropertyType::Enum:
            {
                int* val = static_cast<int*>(ptr);
                if (prop.Hints.EnumNames && prop.Hints.EnumCount > 0)
                {
                    const char* currentName =
                        (*val >= 0 && *val < prop.Hints.EnumCount) ? prop.Hints.EnumNames[*val] : "???";
                    if (ImGui::BeginCombo(prop.DisplayName, currentName))
                    {
                        for (int i = 0; i < prop.Hints.EnumCount; i++)
                        {
                            bool selected = (*val == i);
                            if (ImGui::Selectable(prop.Hints.EnumNames[i], selected))
                                *val = i;
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }
                else
                {
                    ImGui::DragInt(prop.DisplayName, val, 1.0f);
                }
                break;
            }
            }

            ImGui::PopID();
        }
    }

} // namespace Engine
