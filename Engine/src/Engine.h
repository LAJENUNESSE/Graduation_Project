#pragma once

// Core
#include "Core/Application.h"
#include "Core/Base.h"
#include "Core/Layer.h"
#include "Core/Log.h"
#include "Core/Assert.h"
#include "Core/Timestep.h"
#include "Core/UUID.h"
#include "Core/KeyCodes.h"
#include "Core/MouseCodes.h"

// Events
#include "Events/Event.h"
#include "Events/ApplicationEvent.h"
#include "Events/KeyEvent.h"
#include "Events/MouseEvent.h"

// Renderer
#include "Renderer/Renderer.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Buffer.h"
#include "Renderer/Shader.h"
#include "Renderer/Texture.h"
#include "Renderer/VertexArray.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Camera.h"
#include "Renderer/EditorCamera.h"
#include "Renderer/Mesh.h"

// Scene
#include "Scene/SceneCamera.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/SceneSerializer.h"

// Debug / Performance
#include "Debug/ProfileTimer.h"
#include "Debug/GPUTimerQuery.h"
#include "Debug/PerformanceMonitor.h"
