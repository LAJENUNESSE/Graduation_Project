# 多端同步（服务器权威）架构

> 状态：最小可演示闭环已实现并通过自动化回归（协议握手 / 心跳 / 快照 / 输入驱动 /
> 重力行为五项，见 `网络同步对照测试单.md`）。本文描述当前实现、关键设计决策与
> 明确的局限性。

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
4. **服务器权威、客户端不预测**：原型阶段客户端纯插值渲染（100ms 延迟），不做客户端预测/回滚——实现简单且演示效果足够。取舍详见"传输层选型与预测回滚"一节。
5. **传输层选 TCP 而非 UDP**：理由不是"TCP 更好"，而是当前规模下 TCP 的缺点触发不到、优点直接省掉一整层自研可靠协议。取舍详见"传输层选型与预测回滚"一节。
6. **玩家实体预置在场景中**：不做动态实体生成协议。场景放 2 个 `Player1/Player2` 标记的 Dynamic 刚体，服务器按连接顺序分配。输入注入线速度而非直接改 Transform，保留碰撞响应。
7. **快照只同步 Dynamic 刚体**：Static/Kinematic 体不动，快照体积与场景动态实体数成正比，20Hz 下几十个实体仅 ~1KB/帧。

## 传输层选型与预测回滚

本节记录两个**有意为之**的简化及其量化依据——它们是当前实现的边界，也是后续可
演进的接口。

### 为什么是 TCP（而非 UDP）

实现事实：`TcpClient/TcpServer` 基于 standalone Asio（`asio::ip::tcp`），全工程零
UDP 代码；`SOCK_DGRAM`、`asio::ip::udp` 均无引用。帧格式 `[u32 总长][u16 类型]
[u16 版本][payload]` 的长度前缀切帧，本身就是面向字节流的设计。

**TCP 的固有代价**是队头阻塞（head-of-line blocking）：丢一个包，内核会拦住其后
所有包等待重传。对实时同步而言，一个 100ms 前的位置包重传回来已无价值，却堵住了
更新的数据。这正是主流竞技游戏（Source、Overwatch 等）改用 UDP + 自研可靠层的
原因。

**当前选 TCP 的理由**（按实际权重）：

1. **规模决定了缺点触发不到**：局域网 2 人 demo，丢包率接近 0，队头阻塞几乎不会
   发生；而 TCP 的"自动可靠有序"立刻省掉自研可靠层（序号、选择性重传、ACK、
   分片重组）的全部复杂度。
2. **上层逻辑因此极简**：快照无需自建乱序重排与重传超时；场景 YAML（~3.5KB）
   可直接塞进一个帧完整下发，无需分片协议。
3. **已知小瑕疵**：未开启 `TCP_NODELAY`，Nagle 算法生效（小包会攒批）。对 20Hz
   快照影响可忽略；若将来提高频率需补上或改 UDP。

**结论**：这是"队头阻塞（理论成本） vs 实现复杂度（真实成本）"的权衡。规模小的时候
前者不显现、后者是硬成本，故选 TCP。

### 客户端预测与回滚（未实现，评估结论：不做）

现状：`RuntimeMode::NetworkClient` 下客户端**不步进本地物理**，仅通过
`ApplyInterpolatedTransforms` 写快照位置后渲染。按下 W 到看见移动，需等一个 RTT
加 100ms 插值延迟，有可感知的"粘滞感"。生产做法是客户端**本地立即先动**（客户端
预测），收到权威快照后若不一致则**回滚重放**。

评估后的**三个硬障碍**：

1. **客户端没有物理世界**：`NetworkClient` 模式跳过 `OnUpdateRuntime`，要预测就得
   让客户端拥有一份真实的 Bullet 物理世界（场景需含 RigidBody、需
   `OnRuntimeStart`、需每帧 `stepSimulation`）。这一步把"改网络层"变成了"改运行时
   架构"。
2. **物理确定性难以对齐**：回滚要求"同输入序列 → 服务器与客户端算出完全相同结果"。
   Bullet 的跨机器 bit-level 确定性需要严格统一固定步长、初始状态、求解器迭代与
   接触求解顺序；而当前服务器每 tick"读回 Bullet 累加的重力速度再覆盖水平分量"，
   该累积过程本身即浮点相关，客户端重放难以完全复现。
3. **回滚需要状态还原**：需为被预测实体保存历史（位置+速度）并支持 teleport 回滚
   （`setWorldTransform` + `setLinearVelocity` + 强制唤醒，须与"休眠刚体"修复
   配合），代码量与风险都不小。

**可行折中**（若将来要做）：只预测**水平位移**。水平分量每 tick 被硬重置为输入
值、不依赖物理累积，确定性要求大幅降低；重力/Y 轴不预测。即便这个折中版本，仍需
客户端物理世界 + 输入历史 + 权威快照硬纠正，属中等改动。

**当前决策**：鉴于演示目标为局域网 2 人、100ms 插值延迟在演示中几乎不可感知，且
难点在"给客户端配确定性物理"而非网络层本身，**本阶段不做预测回滚**。

### 面向生产级的演进路径

| 环节 | 现状 | 生产级做法 |
|---|---|---|
| 传输层 | TCP，受队头阻塞 | UDP + 自研可靠层（KCP / QUIC / ENet） |
| 小包延迟 | 未开 `TCP_NODELAY`，Nagle 生效 | 关闭 Nagle，或改用 UDP |
| 客户端手感 | 纯插值，输入无即时反馈 | 客户端预测 + 服务器回滚校正 |
| 带宽 | 全量快照 | 增量压缩 / 位打包 / 兴趣管理 |
| 丢包应对 | 依赖 TCP 重传（可能已过期） | 丢弃过期快照，只应用最新 |

## 已知限制（原型范围外）

- 无断线重连、无状态恢复：客户端断开后需重新连接（会重新走完整握手）
- 无加密/认证：仅限局域网/可信环境
- 场景以连接时刻 YAML 为准，运行期服务器端实体增删不同步
- SPH 流体/粒子不做网络同步：服务器无 GPU 跑不了 compute shader，演示场景不含流体
- TCP 队头阻塞在公网会放大延迟抖动：设计目标是局域网演示；公网建议换 UDP/KCP（选型依据见上文"传输层选型与预测回滚"）
- 客户端无预测回滚，输入有约 1 RTT + 100ms 插值的"粘滞感"：局域网演示可接受，评估详见同上一节

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
