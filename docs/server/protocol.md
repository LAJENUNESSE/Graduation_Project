# 线上协议规范（v1）

传输：TCP（asio）。字节序：**所有多字节字段一律小端（LE）**，逐字段显式编码，
禁止裸结构体 memcpy（规避填充/对齐/端序问题）。

## 帧格式

```
偏移  大小  字段
0     4    totalLength   u32  帧总长（含本头 8 字节 + payload），上限 64MB
4     2    messageType   u16  MessageType 枚举值
6     2    version       u16  协议版本（当前 1），不匹配拒绝
8     ...  payload       -    按 messageType 解读
```

接收端按 `totalLength` 聚包；`totalLength < 8` 或 `> 64MB` 视为协议错误断开。

## 消息类型

| 值 | 名称 | 方向 | 说明 |
|---|---|---|---|
| 1 | `C2S_Hello` | C→S | 握手（连接后第一帧） |
| 2 | `S2C_Welcome` | S→C | 接受连接：槽位 + 场景全量 |
| 3 | `S2C_Reject` | S→C | 拒绝连接，随后服务器断开 |
| 10 | `C2S_Input` | C→S | 输入位掩码（可 50Hz+ 高频发送） |
| 11 | `S2C_Snapshot` | S→C | 权威状态快照（20Hz 广播） |
| 20 | `C2S_Ping` | C→S | 心跳（客户端 1s 间隔） |
| 21 | `S2C_Pong` | S→C | 心跳应答（原样回传时间戳） |

## Payload 布局

### C2S_Hello (1)

```
u16  protocolVersion  必须等于帧头 version
u16  nameLength
utf8 name[nameLength]  客户端显示名
```

### S2C_Welcome (2)

```
u16  assignedPlayerIndex   玩家槽位（0 起）；对应场景 Tag "Player{index+1}"
u32  sceneYamlSize
u8   sceneYaml[sceneYamlSize]  完整 .scene YAML（UTF-8）
```

### S2C_Reject (3)

```
u16  reasonLength
utf8 reason[reasonLength]  如 "server full" / "protocol version mismatch"
```

### C2S_Input (10)

```
u32  seq   单调递增序号；服务器丢弃 seq <= 已见最大值的包（乱序/重发保护）
u16  bits  按键位掩码：
           bit0 W, bit1 S, bit2 A, bit3 D, bit4 Q, bit5 E, bit6 Space, bit7 Shift
```

### S2C_Snapshot (11)

```
u32  tick    服务器 tick 计数（60Hz 递增）
u16  count   实体数
每实体（44 字节 × count）:
  u64  uuid        实体 IDComponent.ID（客户端按 UUID 定位实体）
  f32  tx, ty, tz  Translation
  f32  rx, ry, rz  Rotation（欧拉角，弧度）
  f32  sx, sy, sz  Scale
```

同步范围：**仅 Dynamic 刚体**（`RigidBodyComponent.Type == Dynamic`）。

### C2S_Ping (20) / S2C_Pong (21)

```
f64  clientTimeSec  客户端 steady_clock 时间戳；Pong 原样回传
```

RTT = 收到 Pong 时刻 − 回传的 clientTimeSec。

## 会话流程

```
C                          S
│── TCP connect ──────────►│
│── C2S_Hello ────────────►│  校验版本；分配槽位（满则 Reject）
│◄──────────── S2C_Welcome │  槽位 + 场景 YAML
│   （加载场景 → 联机 Play）
│── C2S_Input (50Hz) ─────►│  注入权威刚体速度
│◄── S2C_Snapshot (20Hz) ──│  客户端缓冲 + 100ms 插值渲染
│── C2S_Ping (1s) ────────►│
│◄──────────── S2C_Pong    │
```

断开：任一端关闭 TCP 即视为会话结束；服务器释放槽位。
