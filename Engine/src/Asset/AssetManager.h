#pragma once

#include "Asset/AssetHandle.h"
#include "Asset/AssetRef.h"
#include "Asset/AssetType.h"
#include "Asset/SlotMap.h"
#include "Core/Base.h"

#include <string>
#include <unordered_map>

namespace Engine
{

    class Texture2D;
    class TextureCubemap;
    class Mesh;
    class AsyncLoadQueue;
    class FileWatcher;

    class AssetManager
    {
    public:
        static void Init();
        static void Shutdown();
        static void Update(float deltaTime);

        // Synchronous load — returns cached handle if already loaded
        template <typename T> static AssetHandle Load(const std::string& path);

        // Asynchronous load (texture only) — returns handle to 1x1 gray placeholder, auto-replaces when done
        static AssetHandle LoadAsync(const std::string& path);

        // Retrieve raw pointer (O(1), nullptr if invalid)
        template <typename T> static T* Get(AssetHandle handle);

        // Retrieve shared_ptr (O(1), nullptr if invalid)
        template <typename T> static Ref<T> GetRef(AssetHandle handle);

        // Get the path associated with a handle
        static const std::string& GetPath(AssetHandle handle, AssetType type);

        // Convenience overloads that infer type
        template <typename T> static const std::string& GetPath(AssetHandle handle);

        // Check if handle is valid
        template <typename T> static bool IsValid(AssetHandle handle);

    private:
        static void RegisterBuiltins();
        static void ReloadAsset(AssetHandle handle, AssetType type);

        static SlotMap<Texture2D>      s_Textures;
        static SlotMap<Mesh>           s_Meshes;
        static SlotMap<TextureCubemap> s_Cubemaps;

        static std::unordered_map<std::string, AssetHandle> s_TexturePathIndex;
        static std::unordered_map<std::string, AssetHandle> s_MeshPathIndex;
        static std::unordered_map<std::string, AssetHandle> s_CubemapPathIndex;

        // Track which handles are textures/meshes/cubemaps for generic GetPath
        static std::unordered_map<AssetHandle, AssetType> s_HandleTypes;

        static Scope<AsyncLoadQueue> s_AsyncQueue;
        static Scope<FileWatcher>    s_FileWatcher;
        static bool                  s_Initialized;
    };

    // Template specializations
    template <> AssetHandle AssetManager::Load<Texture2D>(const std::string& path);
    template <> AssetHandle AssetManager::Load<Mesh>(const std::string& path);
    template <> AssetHandle AssetManager::Load<TextureCubemap>(const std::string& path);

    template <> Texture2D*      AssetManager::Get<Texture2D>(AssetHandle handle);
    template <> Mesh*           AssetManager::Get<Mesh>(AssetHandle handle);
    template <> TextureCubemap* AssetManager::Get<TextureCubemap>(AssetHandle handle);

    template <> Ref<Texture2D>      AssetManager::GetRef<Texture2D>(AssetHandle handle);
    template <> Ref<Mesh>           AssetManager::GetRef<Mesh>(AssetHandle handle);
    template <> Ref<TextureCubemap> AssetManager::GetRef<TextureCubemap>(AssetHandle handle);

    template <> bool AssetManager::IsValid<Texture2D>(AssetHandle handle);
    template <> bool AssetManager::IsValid<Mesh>(AssetHandle handle);
    template <> bool AssetManager::IsValid<TextureCubemap>(AssetHandle handle);

    template <> const std::string& AssetManager::GetPath<Texture2D>(AssetHandle handle);
    template <> const std::string& AssetManager::GetPath<Mesh>(AssetHandle handle);
    template <> const std::string& AssetManager::GetPath<TextureCubemap>(AssetHandle handle);

    // AssetRef<T>::Get() implementation
    template <typename T> T* AssetRef<T>::Get() const
    {
        return AssetManager::Get<T>(m_Handle);
    }

} // namespace Engine
