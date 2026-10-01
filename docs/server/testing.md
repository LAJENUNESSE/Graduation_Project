# network-server 测试指南

> 适用分支：`feature/network-server`
> 依赖：`Server` target（无头）+ `Editor` target（GUI）
> 场景：`assets/scenes/network-demo.scene`

---

## 0. 构建（首次必做）

仓库当前不含 `build/` 目录，需要先配置并构建。`asio` 是本分支新增的 vcpkg 依赖，
首次配置会由 vcpkg manifest 模式自动安装（含 ffmpeg 从源码构建，耗时较长）。

```bash
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --preset default

"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config RelWithDebInfo --target Server

"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config RelWithDebInfo --target Editor
```

产物路径（VS 生成器带配置子目录）：

```text
build/Server/RelWithDebInfo/Server.exe
build/Editor/RelWithDebInfo/Editor.exe
```

---

## 1. 协议层冒烟回归（无需 GUI，自动化）

`tests/manual/net_smoke.py` 覆盖握手、心跳、快照流、水平输入与重力回归五项，退出码 0 表示全通过。

**步骤**：先起服务器，再跑脚本。

```bash
# 终端 A
./build/Server/RelWithDebInfo/Server.exe --scene assets/scenes/network-demo.scene

# 终端 B
python tests/manual/net_smoke.py
```

实测输出（本机 Windows 2026-10-01）：

```text
[1/5] Welcome: player=P1, scene=3487B
[2/5] Pong: RTT 33.2ms, 时间戳回显一致
[3/5] Snapshot: 收到 16 帧（tick 119 → 164），2 个 Dynamic 实体
[4/5] 水平输入生效：Player1 z 0.09 → 3.81 (Δz=+3.72)
[5/5] 重力正常：抬升至 y=2.57，松手后回落 2.02m 到 y=0.55（静止高度约 0.55）
PASS
```

可用参数：`--host`、`--port`、`--hold-sec`（按住 W，默认 1.5s）、
`--lift-sec`（按住 Q 抬升，默认 0.8s）、`--fall-sec`（松手后观察，默认 3.0s）。

### 这个脚本在防什么

第 5 项「重力正常」是历史 bug 的回归门禁。服务端曾每 tick 用 `setLinearVelocity`
整体覆盖刚体速度，无输入时写入 `(0,0,0)`，把 Bullet 上一 tick 累加的重力速度清零，
导致方块以约 `g*dt ≈ 0.16 m/s` **匀速下沉**而非按 9.81 m/s² 自由落体。
纯协议往返测试完全测不出这个（WASD 水平移动是正常的），只有观察快照里的 Y 坐标才暴露。

**为什么要先按 Q 抬升再测回落**：`network-demo.scene` 里方块起点 y=1.5，落到地面
y≈0.55 只需约 0.44s，脚本连上时它早已落地静止，开局直接测下落会得到 Δy≈0 的假失败。
脚本因此先用 Q 把方块重新抬到空中，再松手观察回落幅度。

**门禁有效性已实测验证**（不是"空门禁"）：临时把注入逻辑改回旧的覆盖式写法重编，
同一脚本给出

```text
FAIL: 重力异常：从峰值 y=2.48 松手 3.0s 内仅回落 0.095m（期望 > 1.0m，当前最低 y=2.39）。
```

修复版本回落 2.02m，退化版本只回落 0.095m，断言阈值 1.0m 的区分度很充裕。

### 顺带修正的两处交接文档错误

1. **`C2S_Hello` 名字长度是 u16 不是 u8**。`ByteReader::Str` 读两字节，先前脚本写成
   单字节 `\x06`，服务端解析出错误长度后 `Str()` 返回 false——但 `Server/src/Main.cpp`
   里 `reader.U16(...) && reader.Str(...)` 的**返回值被丢弃**，只校验协议版本，
   所以照样回 Welcome，形成"假通过"。脚本已按协议写满两字节。
2. **快照实体步长是 44 字节不是 56**。`EncodeSnapshot` 实际只写 pos3 + rot3 + scale3
   共 **9** 个 float（8 字节 uuid + 36 字节浮点 = 44），`SnapshotCodec.h` / `Protocol.h`
   里"12 个 float"的旧注释是错的，已一并修正。

---

## 2. GUI 闭环测试（需人工，双 Editor）

**1. 启动服务器**（终端 A）：

```bash
./build/Server/RelWithDebInfo/Server.exe --scene assets/scenes/network-demo.scene
```

日志应出现 `Listening on port 30000`（默认 `0.0.0.0:30000`）。

**2. 开两个 Editor 实例**（终端 B、C 各起一个）：

```bash
./build/Editor/RelWithDebInfo/Editor.exe
```

**3. 两个实例都操作**：菜单「网络 → 连接服务器...」→ 地址 `127.0.0.1`、端口 `30000` → 连接。

**4. 检查点**：

| 预期现象 | 排查方向 |
| --- | --- |
| 连接后自动加载 NetworkDemo 场景并进入播放态（灰地面 + 蓝方块 + 橙方块 + 黄障碍物） | 控制台过滤 `NetSession` / `Welcome` |
| 「网络」菜单底部显示 `已连接 (P1, RTT xxms)`，另一实例显示 P2 | Server 日志的 `Welcome sent` 行 |
| 视口聚焦后按 WASDQE，自己的方块移动，另一 Editor 里同步可见 | 先点一下视口确保其聚焦；看 Server 日志的 tick 位置打印 |
| **方块落地后不再持续下沉**（应停在 y≈0.55） | 若持续缓慢下沉，说明重力注入又退化了，跑 §1 脚本确认 |
| 一方断开后恢复本地编辑态，另一方不受影响 | 菜单「网络 → 断开连接」 |
| 两实例看到的方块位置一致（约 100ms 插值延迟属正常） | — |

> Windows 防火墙首次放行 `Server.exe` 会弹窗，本机测试选「允许」。

---

## 3. 配置项

**Server 命令行参数**（`Server/src/Main.cpp`）：

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `--scene <路径>` | `assets/scenes/network-demo.scene` | 权威场景，连接时全量下发 |
| `--port <n>` | 30000 | TCP 监听端口 |
| `--tickrate <n>` | 60 | 物理步进频率（Hz） |
| `--duration <秒>` | 0（一直运行） | 定时退出，便于自动化 |

**代码内常量**（改动需重编）：

| 常量 | 位置 | 值 |
| --- | --- | --- |
| `kMaxPlayers` | `Server/src/SessionManager.h` | 2 |
| `kSnapshotRate` | `Server/src/Main.cpp` | 20 Hz |
| `moveSpeed` | `Server/src/Main.cpp` | 5.0 |
| `kInterpDelaySec` | `Editor/src/NetworkSessionController.cpp` | 0.1 s |
| 输入采样间隔 | 同文件 `PollNetwork` | 0.02 s（50 Hz） |

**自定义场景**：复制 `network-demo.scene` 修改。规则——可控实体必须有
`TagComponent.Tag: PlayerN`（N 从 1 起）且 `RigidBodyComponent.Type: 1`（Dynamic）。
服务器只同步 Dynamic 刚体；要让障碍物参与同步就把它改成 Dynamic。

---

## 4. 速度注入语义（改这块前先读）

`Server/src/Main.cpp` 的输入注入是**分层接管**，不是整体覆盖：

```text
水平 X/Z：输入直接接管，松键立即停止（演示场景不需要惯性滑行）
垂直 Y  ：默认保留 Bullet 累加的重力；Q/E 按下时才由输入接管
```

实现依赖 `Scene::ReadEntityLinearVelocity()` 先读回当前速度。
如果以后改成直接构造速度再 `ApplyEntityLinearVelocity`，会重新引入"重力被清零"的问题——
`tests/manual/net_smoke.py` 的第 5 项就是拦这个的。

读取链路的完整路径（三层）：

```text
Server/src/Main.cpp
  -> Scene::ReadEntityLinearVelocity()                       Engine/src/Scene/Scene.cpp
  -> SceneRuntimeCoordinator::ReadEntityLinearVelocity()     Engine/src/Scene/Runtime/SceneRuntimeCoordinator.cpp
  -> BulletPhysicsWorld::GetEntityLinearVelocity()           Engine/src/Physics/BulletPhysicsWorld.cpp
```

---

## 5. 已知未覆盖的盲区

- 双 Editor 同开时的资源冲突（logs 目录共享、自动保存）未实测
- 长时间运行（>10 分钟）的快照缓冲与内存稳定性未做压力测试
- Linux 侧仅有 CI 编译证据，无运行证据：

  ```bash
  cmake --preset linux-default
  cmake --build build --target Server
  ./build/Server/Server --scene assets/scenes/network-demo.scene   # Ninja 输出无配置子目录
  ```

---

## 6. 非 Vulkan preset 下的构建缺陷（已修）

`Editor` 之前在默认 preset（`ENGINE_ENABLE_VULKAN=OFF`）下**无法编译**：
`Editor/src/EditorViewportController.cpp` 无条件 `#include "Platform/Vulkan/VulkanContext.h"`，
而该头文件内容被宏保护，只在启用 Vulkan 时才进入编译单元。报错是

```text
Engine/Platform/Vulkan/VulkanDeletionQueue.h(3,10): error C1083:
无法打开包括文件: "vulkan/vulkan.h"
```

已给该 include 与对应使用点加上 `#ifdef ENGINE_ENABLE_VULKAN` 保护。同一类问题此前也在
`Engine/src/Scene/Systems/GrassRenderSystem.cpp` 修过。**如果以后新代码在默认 preset 下
报 `vulkan/vulkan.h` 找不到，先检查是不是漏了宏保护**——本机未装 Vulkan SDK，
`vs2022-vulkan` preset 暂时无法在本机验证。
