# 多端同步（服务器权威）架构

> 状态：最小可演示闭环已实现。本文描述当前实现与关键设计决策。

## 总览

服务器权威（server-authoritative）模型：专用 Server 进程 headless 跑物理/逻辑仿真，
多个 Editor 客户端连入后只做渲染与输入采集，不步进本地物理。

```
┌────────────┐   TCP 30000    ┌────────────────────────────┐
│ Editor #1  │◄──────────────►│ Server（headless exe）       │
│ (Windows)  │                │  60Hz 固定步长               │
└────────────┘                │  SceneRuntimeCoordinator     │
┌────────────┐   TCP 30000    │  (renderer = nullptr)        │
│ Editor #2  │◄──────────────►│  Bullet3 物理 + Lua 脚本     │
└────────────┘                └────────────────────────────┘
```

- 每个 Editor 连接时被分配一个玩家槽位（Player1/Player2，绑定场景中 `Tag == PlayerN` 的实体）
- 输入（WASDQE 位掩码）以 50Hz 上行，服务器注入到权威刚体线速度
- 服务器以 20Hz 广播所有 Dynamic 刚体的 Transform 快照，客户端插值后渲染

## 组件

### Engine/src/Network/（Server 与 Editor 共享）

| 文件 | 职责 |
|---|---|
| `Protocol.h` | 帧格式、消息类型、`ByteWriter/ByteReader` 逐字段小端编解码、`EncodeFrame/TryDecodeFrame` |
| `SnapshotCodec.h` | 快照/输入 payload 编解码（`EncodeSnapshot/DecodeSnapshot/EncodeInput/DecodeInput`） |
| `TcpServer.h/.cpp` | 多客户端 TCP 服务端：单 IO 线程 accept/read/write，事件经互斥队列交给主线程 `Poll()`；`Send/Broadcast` post 回 IO 线程 |
| `TcpClient.h/.cpp` | 客户端：阻塞连接 + IO 线程收发 + 主线程 `Poll()` |

### Server/（headless 服务器进程）

| 文件 | 职责 |
|---|---|
| `Main.cpp` | 启动流程：`Log::Init` → 项目根发现 → `Headless::SetEnabled(true)` → 加载场景 → `OnRuntimeStart(nullptr)` → 60Hz 主循环（网络轮询 + 输入注入 + 物理步进 + 20Hz 快照广播） |
| `SessionManager.h/.cpp` | 会话 → 玩家槽位/实体映射、输入状态（序号防乱序）、满员拒绝 |

### Engine 无渲染模式（headless）

`Engine/src/Core/Headless.h` 是全局开关。置位后：

- `AssetManager`：跳过 builtin GL 资源注册；纹理/天空盒只登记路径占位；异步加载退化为占位；热重载禁用
- `Mesh::CreateCube/Plane/Sphere` 与 `Mesh::CreateFromFile`：跳过 VAO/VBO/IBO 与纹理上传（物理侧只需 `GetModelPath()`，assimp 独立重读）
- `TerrainMeshGenerator::BuildLOD`：跳过 VAO 创建（物理只用 `HeightData`）
- `Input`：无窗口一律返回 false（同时修复了 headless 下 Lua `IsKeyPressed` 的空指针崩溃）

### Editor 客户端接入

| 文件 | 职责 |
|---|---|
| `Editor/src/NetworkSessionController.h/.cpp` | 连接对话框 UI、握手/心跳、快照缓冲与插值、输入采样上行 |
| `Editor/src/EditorSceneSession` | 新增 `RuntimeMode {Local, NetworkClient}` 与 `OpenSceneFromMemory`（服务器 YAML → 临时文件 → 正常反序列化） |
| `Editor/src/EditorRenderController` | 联机 Play 模式跳过本地 `OnUpdateRuntime`（不步进物理），渲染管线照常 |
| `Editor/src/EditorLayer` | 收到 Welcome 场景后重建场景并进入联机 Play；断开连接自动 Stop 恢复本地编辑 |

## 关键设计决策

1. **网络代码放 Engine 而非 Server/Editor**：两端链接同一个静态库，协议/编解码只有一份。
2. **显式逐字段小端编码，禁止裸结构体 memcpy**：项目此前三次踩 std140 对齐坑，网络布局同样按"显式写每个字节"处理，规避填充/对齐/端序问题。
3. **单 IO 线程 + 主线程 Poll**：所有 socket 读写在一个 io_context 线程完成，跨线程只传事件队列；发送统一 post 回 IO 线程，会话表无锁。不使用 strand（单线程模型下冗余，曾引发 bad executor 崩溃）。
4. **服务器权威、客户端不预测**：原型阶段客户端纯插值渲染（100ms 延迟），不做客户端预测/回滚——实现简单且演示效果足够。
5. **玩家实体预置在场景中**：不做动态实体生成协议。场景放 2 个 `Player1/Player2` 标记的 Dynamic 刚体，服务器按连接顺序分配。输入注入线速度而非直接改 Transform，保留碰撞响应。
6. **快照只同步 Dynamic 刚体**：Static/Kinematic 体不动，快照体积与场景动态实体数成正比，20Hz 下几十个实体仅 ~1KB/帧。

## 已知限制（原型范围外）

- 无断线重连、无状态恢复：客户端断开后需重新连接（会重新走完整握手）
- 无加密/认证：仅限局域网/可信环境
- 场景以连接时刻 YAML 为准，运行期服务器端实体增删不同步
- SPH 流体/粒子不做网络同步：服务器无 GPU 跑不了 compute shader，演示场景不含流体
- TCP 不可靠延迟抖动在公网会放大：设计目标是局域网演示；公网建议换 UDP/KCP

## 构建与运行

```bash
# Windows
cmake --preset default
cmake --build build --config RelWithDebInfo --target Server
cmake --build build --config RelWithDebInfo --target Editor

# 启动服务器（默认 0.0.0.0:30000）
./build/Server/RelWithDebInfo/Server.exe --scene assets/scenes/network-demo.scene
# 可选参数：--port 30000 --tickrate 60 --duration <秒>（默认一直运行）

# Editor：菜单 网络 → 连接服务器... → 输入 IP:端口 → 连接
# 连接成功自动加载服务器场景并进入联机运行态；视口聚焦后 WASDQE 控制自己的玩家方块
```

CI（`.github/workflows/build.yml`）在 Windows 与 Linux 矩阵中加入 `--target Server` 构建验证。
