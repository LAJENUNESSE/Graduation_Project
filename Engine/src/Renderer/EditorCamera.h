#pragma once

#include "Renderer/Camera.h"
#include "Core/Timestep.h"
#include "Events/Event.h"
#include "Events/MouseEvent.h"

#include <glm/glm.hpp>

namespace Engine
{

    class EditorCamera : public Camera
    {
    public:
        EditorCamera() = default;
        EditorCamera(float fov, float aspectRatio, float nearClip, float farClip);

        void OnUpdate(Timestep ts, bool allowInput = true);
        void OnEvent(Event& e);

        float GetDistance() const
        {
            return m_Distance;
        }

        void SetDistance(float distance)
        {
            m_Distance = distance;
        }

        void SetViewportSize(float width, float height);

        const glm::mat4& GetViewMatrix() const
        {
            return m_ViewMatrix;
        }

        glm::mat4 GetViewProjection() const
        {
            return m_Projection * m_ViewMatrix;
        }

        glm::vec3 GetUpDirection() const;
        glm::vec3 GetRightDirection() const;
        glm::vec3 GetForwardDirection() const;

        const glm::vec3& GetPosition() const
        {
            return m_Position;
        }

        glm::quat GetOrientation() const;

        float GetPitch() const
        {
            return m_Pitch;
        }

        float GetYaw() const
        {
            return m_Yaw;
        }

        void SetViewMatrix(const glm::mat4& viewMatrix);

        float GetNearClip() const { return m_NearClip; }
        float GetFarClip() const { return m_FarClip; }
        float GetFOV() const { return m_FOV; }
        float GetAspectRatio() const { return m_AspectRatio; }

    private:
        void UpdateProjection();
        void UpdateView();

        bool OnMouseScroll(MouseScrolledEvent& e);

        glm::vec2 PanSpeed() const;
        float RotationSpeed() const;
        float ZoomSpeed() const;

        float m_FOV = 45.0f;
        float m_AspectRatio = 1.778f;
        float m_NearClip = 0.1f;
        float m_FarClip = 1000.0f;

        glm::mat4 m_ViewMatrix{1.0f};
        glm::vec3 m_Position = {0.0f, 0.0f, 5.0f};
        glm::vec3 m_FocalPoint = {0.0f, 0.0f, 0.0f};

        float m_Distance = 10.0f;
        float m_Pitch = 0.0f;
        float m_Yaw = 0.0f;
        bool m_ViewMatrixDirty = false;

        glm::vec2 m_InitialMousePosition = {0.0f, 0.0f};
        bool m_MouseCaptured = false;

        float m_ViewportWidth = 1280;
        float m_ViewportHeight = 720;
    };

} // namespace Engine
