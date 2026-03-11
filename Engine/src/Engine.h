#pragma once

// Core
#include "Core/Application.h"
#include "Core/Assert.h"
#include "Core/Base.h"
#include "Core/KeyCodes.h"
#include "Core/Layer.h"
#include "Core/Log.h"
#include "Core/MouseCodes.h"
#include "Core/Timestep.h"
#include "Core/UUID.h"

// Events
#include "Events/ApplicationEvent.h"
#include "Events/Event.h"
#include "Events/KeyEvent.h"
#include "Events/MouseEvent.h"

// Renderer
#include "Renderer/Buffer.h"
#include "Renderer/Camera.h"
#include "Renderer/EditorCamera.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Mesh.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Renderer.h"
#include "Renderer/Shader.h"
#include "Renderer/Texture.h"
#include "Renderer/VertexArray.h"

// Scene
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "Scene/SceneCamera.h"
#include "Scene/SceneSerializer.h"

// Reflection
#include "Reflection/AutoInspector.h"
#include "Reflection/AutoSerializer.h"
#include "Reflection/ComponentMeta.h"
#include "Reflection/ComponentRegistry.h"
#include "Reflection/PropertyInfo.h"
#include "Reflection/PropertyTypes.h"

// Script
#include "Script/NativeScriptComponent.h"
#include "Script/ScriptRegistry.h"
#include "Script/ScriptableEntity.h"

// Asset
#include "Asset/AssetHandle.h"
#include "Asset/AssetManager.h"
#include "Asset/AssetRef.h"
#include "Asset/AssetType.h"
#include "Asset/SlotMap.h"

// Debug / Performance
#include "Debug/GPUTimerQuery.h"
#include "Debug/PerformanceMonitor.h"
#include "Debug/ProfileTimer.h"
