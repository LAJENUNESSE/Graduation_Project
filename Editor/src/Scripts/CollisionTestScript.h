#pragma once

#include "Script/ScriptableEntity.h"
#include "Core/Log.h"

namespace Engine
{

    // 碰撞测试脚本：挂到带刚体+碰撞盒的实体上，碰撞时在控制台输出日志
    class CollisionTestScript : public ScriptableEntity
    {
    public:
        void OnCreate() override
        {
            auto& tag = GetComponent<TagComponent>();
            ENGINE_INFO("[碰撞测试] 脚本已挂载到实体: {}", tag.Tag);
        }

        void OnCollisionEnter(const CollisionCallbackInfo& info) override
        {
            auto& myTag = GetComponent<TagComponent>();
            std::string otherName = "未知";
            Entity other = info.OtherEntity;
            if (other && other.HasComponent<TagComponent>())
                otherName = other.GetComponent<TagComponent>().Tag;

            ENGINE_WARN("[碰撞进入] {} <-> {}  |  接触点: ({:.2f}, {:.2f}, {:.2f})  法线: ({:.2f}, {:.2f}, {:.2f})  冲量: {:.3f}",
                        myTag.Tag, otherName,
                        info.ContactPoint.x, info.ContactPoint.y, info.ContactPoint.z,
                        info.ContactNormal.x, info.ContactNormal.y, info.ContactNormal.z,
                        info.Impulse);
        }

        void OnCollisionStay(const CollisionCallbackInfo& info) override
        {
            // 持续碰撞时每帧打印会太多，只在冲量较大时输出
            if (info.Impulse > 0.5f)
            {
                auto& myTag = GetComponent<TagComponent>();
                ENGINE_TRACE("[碰撞持续] {} 冲量: {:.3f}", myTag.Tag, info.Impulse);
            }
        }

        void OnCollisionExit(Entity other) override
        {
            auto& myTag = GetComponent<TagComponent>();
            std::string otherName = "未知";
            if (other && other.HasComponent<TagComponent>())
                otherName = other.GetComponent<TagComponent>().Tag;

            ENGINE_INFO("[碰撞结束] {} <-> {}", myTag.Tag, otherName);
        }

        void OnTriggerEnter(Entity other) override
        {
            auto& myTag = GetComponent<TagComponent>();
            std::string otherName = "未知";
            if (other && other.HasComponent<TagComponent>())
                otherName = other.GetComponent<TagComponent>().Tag;

            ENGINE_WARN("[触发器进入] {} <-> {}", myTag.Tag, otherName);
        }

        void OnTriggerExit(Entity other) override
        {
            auto& myTag = GetComponent<TagComponent>();
            std::string otherName = "未知";
            if (other && other.HasComponent<TagComponent>())
                otherName = other.GetComponent<TagComponent>().Tag;

            ENGINE_INFO("[触发器退出] {} <-> {}", myTag.Tag, otherName);
        }

        void OnDestroy() override
        {
            ENGINE_INFO("[碰撞测试] 脚本已销毁");
        }
    };

} // namespace Engine
