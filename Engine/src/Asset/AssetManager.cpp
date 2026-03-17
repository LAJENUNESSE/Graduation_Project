#include "engpch.h"
#include "Asset/AssetManager.h"
#include "Asset/AsyncLoadQueue.h"
#include "Asset/FileWatcher.h"
#include "Core/Assert.h"
#include "Core/Log.h"
#include "Renderer/Mesh.h"
#include "Renderer/Texture.h"

namespace Engine
{

    SlotMap<Texture2D>      AssetManager::s_Textures;
    SlotMap<Mesh>           AssetManager::s_Meshes;
    SlotMap<TextureCubemap> AssetManager::s_Cubemaps;

    std::unordered_map<std::string, AssetHandle> AssetManager::s_TexturePathIndex;
    std::unordered_map<std::string, AssetHandle> AssetManager::s_MeshPathIndex;
    std::unordered_map<std::string, AssetHandle> AssetManager::s_CubemapPathIndex;
    std::unordered_map<AssetHandle, AssetType>   AssetManager::s_HandleTypes;

    Scope<AsyncLoadQueue> AssetManager::s_AsyncQueue;
    Scope<FileWatcher>    AssetManager::s_FileWatcher;
    bool                  AssetManager::s_Initialized = false;

    void AssetManager::Init()
    {
        if (s_Initialized)
            return;

        s_Initialized = true;
        s_AsyncQueue  = CreateScope<AsyncLoadQueue>();
        s_FileWatcher = CreateScope<FileWatcher>();

        RegisterBuiltins();

        ENGINE_CORE_INFO("AssetManager initialized");
    }

    void AssetManager::Shutdown()
    {
        if (!s_Initialized)
            return;

        if (s_AsyncQueue)
            s_AsyncQueue->Shutdown();
        s_AsyncQueue.reset();
        s_FileWatcher.reset();

        s_TexturePathIndex.clear();
        s_MeshPathIndex.clear();
        s_CubemapPathIndex.clear();
        s_HandleTypes.clear();
        s_Textures.Clear();
        s_Meshes.Clear();
        s_Cubemaps.Clear();

        s_Initialized = false;
        ENGINE_CORE_INFO("AssetManager shutdown");
    }

    void AssetManager::RegisterBuiltins()
    {
        // Builtin white texture (1x1 white)
        {
            auto     tex   = Texture2D::Create(1, 1);
            uint32_t white = 0xFFFFFFFF;
            tex->SetData(&white, sizeof(uint32_t));
            AssetHandle h                       = s_Textures.Insert(tex, "builtin:white");
            s_TexturePathIndex["builtin:white"] = h;
            s_HandleTypes[h]                    = AssetType::Texture2D;
        }

        // Builtin meshes
        {
            auto        cube                = Mesh::CreateCube();
            AssetHandle h                   = s_Meshes.Insert(cube, "builtin:Cube");
            s_MeshPathIndex["builtin:Cube"] = h;
            s_HandleTypes[h]                = AssetType::Mesh;
        }
        {
            auto        plane                = Mesh::CreatePlane();
            AssetHandle h                    = s_Meshes.Insert(plane, "builtin:Plane");
            s_MeshPathIndex["builtin:Plane"] = h;
            s_HandleTypes[h]                 = AssetType::Mesh;
        }
        {
            auto        sphere                = Mesh::CreateSphere();
            AssetHandle h                     = s_Meshes.Insert(sphere, "builtin:Sphere");
            s_MeshPathIndex["builtin:Sphere"] = h;
            s_HandleTypes[h]                  = AssetType::Mesh;
        }
    }

    void AssetManager::Update(float deltaTime)
    {
        if (!s_Initialized)
            return;

        // Poll async load results
        if (s_AsyncQueue)
        {
            auto results = s_AsyncQueue->PollResults();
            for (auto& result : results)
            {
                auto tex = Texture2D::Create(result.Pixels.data(), result.Width, result.Height);
                s_Textures.Replace(result.TargetHandle, tex);
                ENGINE_CORE_INFO("Async texture loaded: {0}", result.Path);
            }
        }

        // Poll file watcher for changes
        if (s_FileWatcher)
        {
            auto changed = s_FileWatcher->CheckChanges(deltaTime);
            for (auto& handle : changed)
            {
                auto it = s_HandleTypes.find(handle);
                if (it != s_HandleTypes.end())
                    ReloadAsset(handle, it->second);
            }
        }
    }

    void AssetManager::ReloadAsset(AssetHandle handle, AssetType type)
    {
        switch (type)
        {
        case AssetType::Texture2D:
        {
            const std::string& path = s_Textures.GetPath(handle);
            if (!path.empty() && path.find("builtin:") == std::string::npos)
            {
                auto tex = Texture2D::Create(path);
                s_Textures.Replace(handle, tex);
                ENGINE_CORE_INFO("Hot-reloaded texture: {0}", path);
            }
            break;
        }
        case AssetType::Mesh:
        {
            const std::string& path = s_Meshes.GetPath(handle);
            if (!path.empty() && path.find("builtin:") == std::string::npos)
            {
                auto mesh = Mesh::CreateFromFile(path);
                if (mesh)
                {
                    s_Meshes.Replace(handle, mesh);
                    ENGINE_CORE_INFO("Hot-reloaded mesh: {0}", path);
                }
            }
            break;
        }
        default:
            break;
        }
    }

    // ---- Load specializations ----

    template <> AssetHandle AssetManager::Load<Texture2D>(const std::string& path)
    {
        if (path.empty())
            return {};

        auto it = s_TexturePathIndex.find(path);
        if (it != s_TexturePathIndex.end())
        {
            if (s_Textures.Get(it->second))
                return it->second;
            // Stale entry, remove it
            s_TexturePathIndex.erase(it);
        }

        auto        tex          = Texture2D::Create(path);
        AssetHandle h            = s_Textures.Insert(tex, path);
        s_TexturePathIndex[path] = h;
        s_HandleTypes[h]         = AssetType::Texture2D;

        // Watch for hot-reload
        if (s_FileWatcher && path.find("builtin:") == std::string::npos)
            s_FileWatcher->Watch(path, h);

        ENGINE_CORE_INFO("AssetManager: Loaded texture '{0}'", path);
        return h;
    }

    template <> AssetHandle AssetManager::Load<Mesh>(const std::string& path)
    {
        if (path.empty())
            return {};

        auto it = s_MeshPathIndex.find(path);
        if (it != s_MeshPathIndex.end())
        {
            if (s_Meshes.Get(it->second))
                return it->second;
            s_MeshPathIndex.erase(it);
        }

        Ref<Mesh> mesh;
        if (path == "builtin:Cube")
            mesh = Mesh::CreateCube();
        else if (path == "builtin:Plane")
            mesh = Mesh::CreatePlane();
        else if (path == "builtin:Sphere")
            mesh = Mesh::CreateSphere();
        else
            mesh = Mesh::CreateFromFile(path);

        if (!mesh)
            return {};

        AssetHandle h         = s_Meshes.Insert(mesh, path);
        s_MeshPathIndex[path] = h;
        s_HandleTypes[h]      = AssetType::Mesh;

        if (s_FileWatcher && path.find("builtin:") == std::string::npos)
            s_FileWatcher->Watch(path, h);

        ENGINE_CORE_INFO("AssetManager: Loaded mesh '{0}'", path);
        return h;
    }

    template <> AssetHandle AssetManager::Load<TextureCubemap>(const std::string& path)
    {
        // Cubemaps use a joined path as key (semicolon-separated)
        if (path.empty())
            return {};

        auto it = s_CubemapPathIndex.find(path);
        if (it != s_CubemapPathIndex.end())
        {
            if (s_Cubemaps.Get(it->second))
                return it->second;
            s_CubemapPathIndex.erase(it);
        }

        // Split path by semicolons
        std::vector<std::string> faces;
        std::istringstream       iss(path);
        std::string              face;
        while (std::getline(iss, face, ';'))
            faces.push_back(face);

        auto        cubemap      = TextureCubemap::Create(faces);
        AssetHandle h            = s_Cubemaps.Insert(cubemap, path);
        s_CubemapPathIndex[path] = h;
        s_HandleTypes[h]         = AssetType::TextureCubemap;

        return h;
    }

    // ---- LoadAsync ----

    AssetHandle AssetManager::LoadAsync(const std::string& path)
    {
        if (path.empty())
            return {};

        // Check cache first
        auto it = s_TexturePathIndex.find(path);
        if (it != s_TexturePathIndex.end())
        {
            if (s_Textures.Get(it->second))
                return it->second;
            s_TexturePathIndex.erase(it);
        }

        // Create 1x1 gray placeholder
        auto     placeholder = Texture2D::Create(1, 1);
        uint32_t gray        = 0xFF808080;
        placeholder->SetData(&gray, sizeof(uint32_t));

        AssetHandle h            = s_Textures.Insert(placeholder, path);
        s_TexturePathIndex[path] = h;
        s_HandleTypes[h]         = AssetType::Texture2D;

        // Submit to async queue
        if (s_AsyncQueue)
            s_AsyncQueue->SubmitTexture(path, h);

        if (s_FileWatcher)
            s_FileWatcher->Watch(path, h);

        ENGINE_CORE_INFO("AssetManager: Async loading texture '{0}'", path);
        return h;
    }

    // ---- Get specializations ----

    template <> Texture2D* AssetManager::Get<Texture2D>(AssetHandle h)
    {
        ENGINE_CORE_ASSERT(!h.IsValid() || s_HandleTypes.count(h) == 0 || s_HandleTypes[h] == AssetType::Texture2D,
                           "AssetHandle type mismatch: expected Texture2D");
        return s_Textures.Get(h);
    }
    template <> Mesh* AssetManager::Get<Mesh>(AssetHandle h)
    {
        ENGINE_CORE_ASSERT(!h.IsValid() || s_HandleTypes.count(h) == 0 || s_HandleTypes[h] == AssetType::Mesh,
                           "AssetHandle type mismatch: expected Mesh");
        return s_Meshes.Get(h);
    }
    template <> TextureCubemap* AssetManager::Get<TextureCubemap>(AssetHandle h)
    {
        ENGINE_CORE_ASSERT(!h.IsValid() || s_HandleTypes.count(h) == 0 || s_HandleTypes[h] == AssetType::TextureCubemap,
                           "AssetHandle type mismatch: expected TextureCubemap");
        return s_Cubemaps.Get(h);
    }

    template <> Ref<Texture2D> AssetManager::GetRef<Texture2D>(AssetHandle h)
    {
        return s_Textures.GetRef(h);
    }
    template <> Ref<Mesh> AssetManager::GetRef<Mesh>(AssetHandle h)
    {
        return s_Meshes.GetRef(h);
    }
    template <> Ref<TextureCubemap> AssetManager::GetRef<TextureCubemap>(AssetHandle h)
    {
        return s_Cubemaps.GetRef(h);
    }

    template <> bool AssetManager::IsValid<Texture2D>(AssetHandle h)
    {
        return s_Textures.Get(h) != nullptr;
    }
    template <> bool AssetManager::IsValid<Mesh>(AssetHandle h)
    {
        return s_Meshes.Get(h) != nullptr;
    }
    template <> bool AssetManager::IsValid<TextureCubemap>(AssetHandle h)
    {
        return s_Cubemaps.Get(h) != nullptr;
    }

    // ---- GetPath ----

    const std::string& AssetManager::GetPath(AssetHandle handle, AssetType type)
    {
        static const std::string empty;
        switch (type)
        {
        case AssetType::Texture2D:
            return s_Textures.GetPath(handle);
        case AssetType::Mesh:
            return s_Meshes.GetPath(handle);
        case AssetType::TextureCubemap:
            return s_Cubemaps.GetPath(handle);
        default:
            return empty;
        }
    }

    template <> const std::string& AssetManager::GetPath<Texture2D>(AssetHandle h)
    {
        return s_Textures.GetPath(h);
    }
    template <> const std::string& AssetManager::GetPath<Mesh>(AssetHandle h)
    {
        return s_Meshes.GetPath(h);
    }
    template <> const std::string& AssetManager::GetPath<TextureCubemap>(AssetHandle h)
    {
        return s_Cubemaps.GetPath(h);
    }

} // namespace Engine
