#!/usr/bin/env python3
"""network-server 协议冒烟回归脚本。

先启动服务器，再跑本脚本：

    ./build/Server/RelWithDebInfo/Server.exe --scene assets/scenes/network-demo.scene

    python tests/manual/net_smoke.py

覆盖的链路：
    1. C2S_Hello  -> S2C_Welcome（含场景 YAML 全量下发、长度自校验）
    2. C2S_Ping   -> S2C_Pong（RTT + 时间戳回显校验）
    3. C2S_Input  -> S2C_Snapshot（快照流是否按 20Hz 到达、实体数是否正确）
    4. 水平输入：按住 W 后 +Z 位移是否达标（输入 -> 权威刚体链路是否打通）
    5. 重力回归门禁：Q 抬升 -> 松手 -> 测自由落体回落幅度

本脚本占用的槽位由服务器决定（`Welcome.playerIndex`），**不假定是 P1**。
服务器上若已有别的连接（比如编辑器）占着 0 号槽，本脚本会拿到 P2，
此时它自动改用 P2 的实体做测试。若你希望它测 P1，先断开其它连接或重启服务器。

第 5 项是历史 bug 的回归门禁。服务端曾每 tick 用 setLinearVelocity 整体覆盖速度，
无输入时写 (0,0,0)，把 Bullet 累加的重力速度清零，方块于是以约 g*dt ≈ 0.16 m/s
匀速下沉而不是按 9.81 m/s² 加速下落。

之所以不直接在开局测下落：network-demo.scene 里方块起点 y=1.5，落到地面 y≈0.55
只需约 0.44s，脚本连上时它早就落地静止了，Δy≈0 会造成假失败。所以改成先用 Q
把方块重新抬到空中再松手测回落：
    正常：抬升约 4m，松手后约 0.9s 落回地面（20Hz 快照有足够采样）
    退化：松手后匀速下沉，3s 内仅回落约 0.48m -> 断言失败

退出码：0 = 全部通过，1 = 断言失败，2 = 连接/协议错误。

注意：脚本会就地移动方块，跑完之后场景里方块已不在初始位置。
连续跑到第 3 次以后，"按初始位置认实体"可能失效 —— 重启服务器即可。
"""

import argparse
import socket
import struct
import sys
import time

# 与 Engine/src/Network/Protocol.h 保持一致
MSG_HELLO = 1
MSG_WELCOME = 2
MSG_REJECT = 3
MSG_INPUT = 10
MSG_SNAPSHOT = 11
MSG_PING = 20
MSG_PONG = 21
PROTOCOL_VERSION = 1

kInputW = 1 << 0
kInputQ = 1 << 4

# network-demo.scene：地面顶面 y=0.05，方块半高 0.5，故静止高度约 0.55
REST_Y = 0.55
# network-demo.scene：Player1 起点 (-2, 1.5, 0)，Player2 起点 (2, 1.5, 0)
# 玩家槽位 -> 初始 x。被控实体必须按 Welcome 返回的 playerIndex 选，
# 不能假定自己一定拿到 0 号槽：服务器上若已有别的连接（比如编辑器），
# 本脚本会拿到 1 号槽，此时按 Player1 的特征去找会认错实体（见下方 locate 处注释）。
PLAYER_START_X = {-2.0: 0, 2.0: 1}  # 初始 x -> 槽位
PLAYER_SLOT_START_X = {v: k for k, v in PLAYER_START_X.items()}  # 槽位 -> 初始 x

FORWARD_MOVE_THRESHOLD = 0.5  # 按住 W 后 +Z 位移下限
LIFT_THRESHOLD = 1.0  # 按住 Q 后 +Y 抬升下限
FALL_THRESHOLD = 1.0  # 松手后自由落体回落幅度下限（见下方重力回归说明）
POS_MATCH_TOLERANCE = 0.75  # 按初始位置认实体时的容差


def frame(msg_type, payload=b"", version=PROTOCOL_VERSION):
    """[u32 消息总长含头][u16 类型][u16 版本][payload]"""
    return struct.pack("<IHH", len(payload) + 8, msg_type, version) + payload


class FrameReader:
    """非阻塞收包 + 累积缓冲，避免读半帧导致失步。"""

    def __init__(self, sock):
        self.sock = sock
        self.buf = bytearray()

    def pump(self):
        try:
            data = self.sock.recv(65536)
            if data:
                self.buf.extend(data)
        except BlockingIOError:
            pass

        out = []
        while len(self.buf) >= 8:
            total, msg_type, _version = struct.unpack_from("<IHH", self.buf, 0)
            if total < 8 or len(self.buf) < total:
                break
            out.append((msg_type, bytes(self.buf[8:total])))
            del self.buf[:total]
        return out

    def read_one(self, timeout=3.0, skip_types=()):
        """读取一帧，可跳过指定的消息类型（例如握手前先到的旧快照）。"""
        deadline = time.time() + timeout
        while time.time() < deadline:
            for msg_type, body in self.pump():
                if msg_type in skip_types:
                    continue
                return msg_type, body
            time.sleep(0.005)
        raise TimeoutError("等待消息超时")


def decode_snapshot(body):
    """tick(u32) + count(u16) + N * [uuid(u64) + pos/rot/scale 共 9*f32] = 6 + N*44 字节

    注意：是 9 个 float 不是 12 个。SnapshotCodec.h / Protocol.h 的旧注释写的是 12，
    但 EncodeSnapshot 实际只写 pos3 + rot3 + scale3 共 9 个（注释已修正）。
    """
    tick, count = struct.unpack_from("<IH", body, 0)
    offset = 6
    entities = {}
    for _ in range(count):
        (uuid,) = struct.unpack_from("<Q", body, offset)
        values = struct.unpack_from("<9f", body, offset + 8)
        entities[uuid] = (values[0], values[1], values[2])
        offset += 44
    return tick, entities


def hold_and_collect(sock, reader, bits, seconds, seq_start=0):
    """按住给定按键 seconds 秒（50Hz 上行），期间持续收快照。返回 (seq, snapshots)。"""
    seq = seq_start
    next_input_at = 0.0
    deadline = time.time() + seconds
    snapshots = []
    while time.time() < deadline:
        now = time.time()
        if now >= next_input_at:
            sock.sendall(frame(MSG_INPUT, struct.pack("<IH", seq, bits)))
            seq += 1
            next_input_at = now + 0.02
        for msg_type, body in reader.pump():
            if msg_type == MSG_SNAPSHOT:
                snapshots.append(decode_snapshot(body))
        time.sleep(0.005)
    return seq, snapshots


def positions_of(snapshots, uuid):
    """从 (tick, entities) 序列里取出某个实体的 y 值序列。"""
    return [entities[uuid][1] for _, entities in snapshots if uuid in entities]


def fail(message):
    print(f"FAIL: {message}")
    sys.exit(1)


def main():
    parser = argparse.ArgumentParser(description="network-server 协议冒烟回归")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=30000)
    parser.add_argument("--hold-sec", type=float, default=1.5, help="按住 W 的时长")
    parser.add_argument("--lift-sec", type=float, default=0.8, help="按住 Q 抬升的时长")
    parser.add_argument("--fall-sec", type=float, default=3.0, help="松手后观察自由落体的时长")
    args = parser.parse_args()

    try:
        sock = socket.create_connection((args.host, args.port), timeout=3)
    except OSError as exc:
        print(f"FAIL: 无法连接 {args.host}:{args.port} — {exc}")
        print("      请确认 Server.exe 已启动（--scene assets/scenes/network-demo.scene）")
        sys.exit(2)

    sock.setblocking(False)
    reader = FrameReader(sock)

    # ── 1. Hello -> Welcome ────────────────────────────────────────────
    # 注意：名字长度是 u16（ByteReader::Str 读两字节），早期脚本写成单字节会导致
    # 服务端解析出错误长度并静默丢弃客户端名——但返回值被丢弃所以照样回 Welcome，
    # 造成"假通过"。这里按协议写满两字节。
    name = b"Editor"
    payload = struct.pack("<HH", PROTOCOL_VERSION, len(name)) + name
    sock.sendall(frame(MSG_HELLO, payload))

    try:
        # 快照是 20Hz 持续广播的，握手前随时可能先到一帧，跳过即可
        msg_type, body = reader.read_one(skip_types=(MSG_SNAPSHOT,))
    except TimeoutError:
        fail("握手无响应")

    if msg_type == MSG_REJECT:
        (reason_len,) = struct.unpack_from("<H", body, 0)
        fail(f"服务器拒绝：{body[2:2 + reason_len].decode('utf-8', 'replace')}")
    if msg_type != MSG_WELCOME:
        fail(f"期望 Welcome(2)，实际收到类型 {msg_type}")

    player_index, yaml_size = struct.unpack_from("<HI", body, 0)
    if len(body) != 6 + yaml_size:
        fail(f"Welcome 长度不符：声明 {yaml_size}B，实际 {len(body) - 6}B")
    print(f"[1/5] Welcome: player=P{player_index + 1}, scene={yaml_size}B")
    if player_index != 0:
        print(f"      注意：拿到的是 P{player_index + 1} 而非 P1，"
              f"说明服务器上已有其它连接占用 0 号槽（编辑器？）。\n"
              f"      本脚本会自动改用 P{player_index + 1} 的实体做测试，但如果那不是你想要的，"
              f"请断开其它连接或重启服务器后重跑。")

    # ── 2. Ping -> Pong ────────────────────────────────────────────────
    sent_time = time.time()
    sock.sendall(frame(MSG_PING, struct.pack("<d", sent_time)))
    try:
        msg_type, body = reader.read_one(skip_types=(MSG_SNAPSHOT,))
    except TimeoutError:
        fail("Ping 无响应")
    if msg_type != MSG_PONG:
        fail(f"期望 Pong(21)，实际收到类型 {msg_type}")

    (echoed,) = struct.unpack("<d", body)
    if abs(echoed - sent_time) > 1e-6:
        fail(f"Pong 回显时间戳不一致：发 {sent_time!r} 收 {echoed!r}")
    print(f"[2/5] Pong: RTT {(time.time() - sent_time) * 1000:.1f}ms, 时间戳回显一致")

    # ── 3. 按住 W：验证水平输入驱动权威刚体 ────────────────────────────
    seq, snaps_w = hold_and_collect(sock, reader, kInputW, args.hold_sec)

    if len(snaps_w) < 3:
        fail(f"快照数量不足：{len(snaps_w)}（20Hz × {args.hold_sec}s 期望 ≥3）")

    first_tick, first_entities = snaps_w[0]
    last_tick, last_entities = snaps_w[-1]
    print(f"[3/5] Snapshot: 收到 {len(snaps_w)} 帧（tick {first_tick} → {last_tick}），"
          f"{len(last_entities)} 个 Dynamic 实体")
    if not first_entities:
        fail("首帧快照无 Dynamic 实体")

    # 按 Welcome 返回的槽位确定自己那个实体的初始 x，避免依赖 entt 遍历顺序。
    #
    # ⚠️ 这里**必须**用 player_index，不能写成"找 x≈-2 的那个"：
    # 服务器上若已有别的连接占着 0 号槽（比如编辑器已连上），本脚本会拿到 1 号槽，
    # 此时自己的实体在 x=+2。若还按 x≈-2 去找，会认成对方那个静止不动的实体，
    # 于是"按住 W 没有位移"——但那其实是别人没输入，不是脚本的输入没生效。
    expected_x = PLAYER_SLOT_START_X.get(player_index)
    if expected_x is None:
        fail(f"未知的玩家槽位 {player_index}（场景里只约定 Player1/Player2 两个可控实体）")

    controlled = None
    for uuid, pos in first_entities.items():
        if abs(pos[0] - expected_x) < POS_MATCH_TOLERANCE:
            controlled = uuid
            break
    if controlled is None:
        seen = ", ".join(f"x={p[0]:+.2f}" for p in first_entities.values())
        fail(f"未能在快照中定位自己的实体（槽位 P{player_index + 1}，期望初始 x≈{expected_x}）；"
             f"当前快照实体位置：{seen}\n"
             f"      若期望的实体已被移动过（例如上一轮测试留下的位移），"
             f"重启服务器再跑本脚本。")

    # 顺带记下另一个玩家（用来确认输入只驱动自己的实体）
    other = None
    for uuid, pos in first_entities.items():
        if uuid != controlled and abs(pos[0] + expected_x) < POS_MATCH_TOLERANCE:
            other = uuid
            break

    start_pos = first_entities[controlled]
    end_pos = last_entities.get(controlled, start_pos)

    dz = end_pos[2] - start_pos[2]
    if dz <= FORWARD_MOVE_THRESHOLD:
        fail(f"按住 W 后未前进：Δz={dz:.3f}（期望 > {FORWARD_MOVE_THRESHOLD}）。\n"
             f"      被控实体：槽位 P{player_index + 1}，初始 x={start_pos[0]:+.2f}；"
             f"z {start_pos[2]:.2f} → {end_pos[2]:.2f}")
    print(f"[4/5] 水平输入生效：P{player_index + 1} z {start_pos[2]:.2f} → {end_pos[2]:.2f} (Δz={dz:+.2f})")

    # 确认输入没有串到对方实体上
    if other is not None:
        other_last = last_entities.get(other)
        if other_last is not None:
            other_dz = abs(other_last[2] - first_entities[other][2])
            if other_dz > FORWARD_MOVE_THRESHOLD:
                fail(f"输入串到了对方实体：对方 z 也位移了 {other_dz:.2f}")

    # ── 5. 重力回归门禁：Q 抬升 → 松手 → 测自由落体 ────────────────────
    # 直接在开局测下落是测不到的：方块从 y=1.5 自由落体只需约 0.44s 就落到 y≈0.55，
    # 脚本连上时早已落地。所以先用 Q 把方块重新抬到空中，再松手观察回落幅度：
    #   正常：抬升约 4m，松手后约 0.9s 落回地面（20Hz 快照有足够采样）
    #   退化：松手后以约 0.16 m/s 匀速下沉，3s 内仅回落约 0.48m → 断言失败
    seq, snaps_q = hold_and_collect(sock, reader, kInputQ, args.lift_sec, seq)
    if not snaps_q:
        fail("抬升阶段未收到快照")
    y_lift = positions_of(snaps_q, controlled)
    if not y_lift:
        fail("抬升阶段快照中找不到被控实体")
    peak_y = max(y_lift)
    if peak_y - REST_Y < LIFT_THRESHOLD:
        fail(f"按住 Q 后未抬升：峰值 y={peak_y:.2f}，静止高度 {REST_Y}（期望抬升 > {LIFT_THRESHOLD}）")

    seq, snaps_fall = hold_and_collect(sock, reader, 0, args.fall_sec, seq)
    y_fall = positions_of(snaps_fall, controlled) or [peak_y]
    low_y = min(y_fall)

    sock.close()

    dropped = peak_y - low_y
    if dropped < FALL_THRESHOLD:
        fail(f"重力异常：从峰值 y={peak_y:.2f} 松手 {args.fall_sec}s 内仅回落 {dropped:.3f}m"
             f"（期望 > {FALL_THRESHOLD}m，当前最低 y={low_y:.2f}）。\n"
             f"      这通常意味着服务端每 tick 用 setLinearVelocity 整体覆盖了速度，\n"
             f"      把 Bullet 累加的重力分量清零，方块会以约 0.16 m/s 匀速下沉而非自由落体。\n"
             f"      检查 Server/src/Main.cpp 的输入注入是否保留了 Y 轴速度。")
    print(f"[5/5] 重力正常：P{player_index + 1} 抬升至 y={peak_y:.2f}，松手后回落 {dropped:.2f}m "
          f"到 y={low_y:.2f}（静止高度约 {REST_Y}）")

    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
