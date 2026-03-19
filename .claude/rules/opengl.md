---
paths:
  - "Engine/Platform/OpenGL/**"
---

# Platform/OpenGL

`Engine/src/Renderer/` 中抽象接口的 OpenGL 4.3 具体实现。

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

## 注意事项

- Shader uniform 名称区分大小写，必须与 GLSL 中完全一致
- Framebuffer 附件顺序：color=slot 0, depth=slot 1, entity ID=slot 2
- Texture unit 绑定是全局状态，多处绑定时注意冲突
- Compute Shader 的 workgroup 大小必须与 GLSL 中 `layout(local_size_x=...)` 一致
- MSVC 需要 `/utf-8` 编译选项以支持中文字符串字面量
- OpenGL 状态有缓存以减少冗余调用
- **纹理必须用 `glTexStorage2D`（immutable storage）创建**，`glTexImage2D` 在 `imageLoad`/`imageStore` 场景下为未定义行为
- 新增渲染后端时应在此目录下新建对应文件夹（如 `Platform/Vulkan/`）
