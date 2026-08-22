---
paths:
  - "Engine/Platform/OpenGL/**"
---

# Platform/OpenGL

`Engine/src/Renderer/` 中抽象接口的 OpenGL 4.3 具体实现，**默认后端**。

## 文件映射

| 抽象接口 | OpenGL 实现 |
|----------|-------------|
| RendererAPI | OpenGLRendererAPI |
| Buffer (Vertex/Index) | OpenGLBuffer |
| VertexArray | OpenGLVertexArray |
| Framebuffer | OpenGLFramebuffer |
| Texture (2D/Cubemap) | OpenGLTexture |
| Shader | OpenGLShader |
| UniformBuffer | OpenGLUniformBuffer |
| StorageBuffer (SSBO) | OpenGLStorageBuffer |
| GraphicsContext | OpenGLContext |
| IBLGenerator | OpenGLIBLGenerator |
| GPUAsyncReadback | OpenGLAsyncReadback |
| — | OpenGLGPUTimerQuery（实现 `Engine/src/Debug/GPUTimerQuery.h` 声明的类，无独立头文件） |

## 注意事项

- Shader uniform 名称区分大小写，必须与 GLSL 中完全一致
- Framebuffer 附件顺序：color=attachment 0, entity ID(RED_INTEGER)=attachment 1, depth=attachment 2（见 `Editor/src/EditorViewportController.cpp:26-27` 与 `EditorSelectionGizmoController.cpp:95` 的 `ReadPixel(1, ...)`)
- Texture unit 绑定是全局状态，多处绑定时注意冲突
- Compute Shader 的 workgroup 大小必须与 GLSL 中 `layout(local_size_x=...)` 一致
- MSVC 需要 `/utf-8` 编译选项以支持中文字符串字面量
- OpenGL 状态有缓存以减少冗余调用
- **参与 `imageLoad`/`imageStore` 的纹理必须用 `glTexStorage2D`（immutable storage）创建**（IBL atlas 见 `OpenGLTexture.cpp:64`、`OpenGLIBLGenerator.cpp:138`），否则为未定义行为；resize 路径与 cubemap 上传仍用可变存储 `glTexImage2D`（`OpenGLTexture.cpp:118-122,185,207`）
- 异步回读走 `OpenGLAsyncReadback`（fence + persistent map），不要在 `Engine/src/` 直接调 `glFenceSync`
- 与 Vulkan 后端并存：所有 OpenGL-only 代码必须留在此目录下，禁止泄漏到 `Engine/src/`

## 与 Vulkan 后端共存

- `RendererAPI::Get().GetAPI()` 返回 `API::OpenGL` 或 `API::Vulkan`，工厂据此分派
- `Editor.exe` 默认走 OpenGL；`Editor.exe --vulkan` 切到 Vulkan 路径（详见 `vulkan.md`）
- Vulkan 后端 39 文件位于 `Engine/Platform/Vulkan/`，已合并主分支
- 新增 OpenGL 资源/状态调用都应同步设计 Vulkan 对应实现，避免再次抽象泄漏
