#include "engpch.h"
#include "Renderer/EditorCamera.h"

#include "Core/Base.h"
#include "Core/Input.h"
#include "Core/KeyCodes.h"
#include "Core/MouseCodes.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace Engine
{

    EditorCamera::EditorCamera(float fov, float aspectRatio, float nearClip, float farClip)
        : m_FOV(fov), m_AspectRatio(aspectRatio), m_NearClip(nearClip), m_FarClip(farClip)
    {
        UpdateProjection();
        UpdateView();
    }

    void EditorCamera::UpdateProjection()
    {
        m_AspectRatio = m_ViewportWidth / m_ViewportHeight;
        m_Projection = glm::perspective(glm::radians(m_FOV), m_AspectRatio, m_NearClip, m_FarClip);
    }

    void EditorCamera::UpdateView()
    {
        m_Position = m_FocalPoint - GetForwardDirection() * m_Distance;
        glm::quat orientation = GetOrientation();
        m_ViewMatrix = glm::translate(glm::mat4(1.0f), m_Position) * glm::toMat4(orientation);
        m_ViewMatrix = glm::inverse(m_ViewMatrix);
    }

    glm::vec2 EditorCamera::PanSpeed() const
    {
        float x = std::min(m_ViewportWidth / 1000.0f, 2.4f);
        float xFactor = 0.0366f * (x * x) - 0.1778f * x + 0.3021f;

        float y = std::min(m_ViewportHeight / 1000.0f, 2.4f);
        float yFactor = 0.0366f * (y * y) - 0.1778f * y + 0.3021f;

        return {xFactor, yFactor};
    }

    float EditorCamera::RotationSpeed() const
    {
        return 0.8f;
    }

    float EditorCamera::ZoomSpeed() const
    {
        float distance = m_Distance * 0.2f;
        distance = std::max(distance, 0.0f);
        float speed = distance * distance;
        speed = std::min(speed, 100.0f);
        return speed;
    }

    void EditorCamera::OnUpdate(Timestep ts)
    {
        if (Input::IsKeyPressed(KeyCode::LeftAlt))
        {
            glm::vec2 mouse = Input::GetMousePosition();
            glm::vec2 delta = (mouse - m_InitialMousePosition) * 0.003f;
            m_InitialMousePosition = mouse;

            if (Input::IsMouseButtonPressed(MouseCode::ButtonMiddle))
            {
                // Pan
                glm::vec2 panSpeed = PanSpeed();
                m_FocalPoint += -GetRightDirection() * delta.x * panSpeed.x * m_Distance;
                m_FocalPoint += GetUpDirection() * delta.y * panSpeed.y * m_Distance;
            }
            else if (Input::IsMouseButtonPressed(MouseCode::ButtonLeft))
            {
                // Rotate
                float yawSign = GetUpDirection().y < 0 ? -1.0f : 1.0f;
                m_Yaw += yawSign * delta.x * RotationSpeed();
                m_Pitch += delta.y * RotationSpeed();
            }
            else if (Input::IsMouseButtonPressed(MouseCode::ButtonRight))
            {
                // Zoom
                float zoomDelta = delta.x + delta.y;
                m_Distance -= zoomDelta * ZoomSpeed();
                if (m_Distance < 1.0f)
                {
                    m_FocalPoint += GetForwardDirection();
                    m_Distance = 1.0f;
                }
            }
        }
        else
        {
            m_InitialMousePosition = Input::GetMousePosition();
        }

        UpdateView();
    }

    void EditorCamera::OnEvent(Event& e)
    {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<MouseScrolledEvent>(ENGINE_BIND_EVENT_FN(EditorCamera::OnMouseScroll));
    }

    bool EditorCamera::OnMouseScroll(MouseScrolledEvent& e)
    {
        float delta = e.GetYOffset() * 0.1f;
        m_Distance -= delta * ZoomSpeed();
        if (m_Distance < 1.0f)
        {
            m_FocalPoint += GetForwardDirection();
            m_Distance = 1.0f;
        }
        UpdateView();
        return false;
    }

    void EditorCamera::SetViewportSize(float width, float height)
    {
        if (width <= 0.0f || height <= 0.0f)
        {
            return;
        }
        m_ViewportWidth = width;
        m_ViewportHeight = height;
        UpdateProjection();
    }

    glm::vec3 EditorCamera::GetUpDirection() const
    {
        return glm::rotate(GetOrientation(), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    glm::vec3 EditorCamera::GetRightDirection() const
    {
        return glm::rotate(GetOrientation(), glm::vec3(1.0f, 0.0f, 0.0f));
    }

    glm::vec3 EditorCamera::GetForwardDirection() const
    {
        return glm::rotate(GetOrientation(), glm::vec3(0.0f, 0.0f, -1.0f));
    }

    glm::quat EditorCamera::GetOrientation() const
    {
        return glm::quat(glm::vec3(-m_Pitch, -m_Yaw, 0.0f));
    }

    void EditorCamera::SetViewMatrix(const glm::mat4& viewMatrix)
    {
        m_ViewMatrix = viewMatrix;
        glm::mat4 invView = glm::inverse(viewMatrix);
        m_Position = glm::vec3(invView[3]);
        glm::vec3 forward = -glm::normalize(glm::vec3(invView[2]));
        m_Pitch = std::asin(-forward.y);
        m_Yaw = std::atan2(forward.x, -forward.z);
        m_FocalPoint = m_Position + forward * m_Distance;
    }

} // namespace Engine
