#!/usr/bin/env python3
"""三终端联机演示：一键拉起「1 个服务器 + 2 个编辑器客户端」。

与 run_oneclick_test.py 的区别
------------------------------
run_oneclick_test.py 用的是**无头脚本客户端**（net_smoke.py），全程自动、无人
交互，用来自动判 PASS/FAIL。

本脚本用的是**真的 Editor 图形客户端**，所以它做不了自动化断言——连接动作
仍然要你在客户端里点「网络 → 连接服务器...」。它只负责把三个终端窗口摆好、
进程起好、端口定好，省掉你手工开窗、cd、敲路径的功夫。

用法（仓库根目录）：

    python tests/manual/run_three_terminal_demo.py

可选参数：

    --port 31000      指定端口（默认自动找空闲端口）
    --scene <path>    服务器下发/客户端启动的场景（默认 network-demo.scene）
    --no-editor       只起服务器，不起两个编辑器
    --keep            脚本退出后不关进程（默认会关）

每个进程各占一个新终端窗口，窗口标题已标注角色；服务器窗口里会打印出
端口号，照着填进客户端的连接对话框即可，两个客户端都连**同一个端口**。
"""

import argparse
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

# 仓库根目录 = 本文件的上两级
REPO_ROOT = Path(__file__).resolve().parents[2]
SERVER_EXE = REPO_ROOT / "build" / "Server" / "RelWithDebInfo" / "Server.exe"
EDITOR_EXE = REPO_ROOT / "build" / "Editor" / "RelWithDebInfo" / "Editor.exe"

DEFAULT_SCENE = "assets/scenes/network-demo.scene"
DEFAULT_PORT_START = 31000
PORT_SCAN_RANGE = 40

# 开两个客户端前先等服务器就绪并让场景跑起来；客户端要连的是已监听端口，
# 早连会拿到「无法建立 TCP 连接」。
SERVER_READY_TIMEOUT = 20.0


def setup_console_encoding():
    """让中文在 Windows 控制台里能正常显示。

    Windows 控制台默认代码页常是 936(GBK)，而 Python 输出的是 UTF-8 字节，
    两者不匹配时中文会变成乱码（输出本身没错，只是显示不对）。
    """
    if sys.platform == "win32":
        try:
            import ctypes
            ctypes.windll.kernel32.SetConsoleOutputCP(65001)
            ctypes.windll.kernel32.SetConsoleCP(65001)
        except Exception:
            pass

    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass  # 非 TextIOWrapper（如被重定向）时忽略


def log(msg):
    print(msg, flush=True)


def fail(msg, code=2):
    log(f"\n[三终端演示] FAIL: {msg}")
    sys.exit(code)


def is_port_free(port):
    """能 bind 成功就说明空闲。"""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            s.bind(("127.0.0.1", port))
            return True
        except OSError:
            return False


def pick_free_port(start, count):
    for port in range(start, start + count):
        if is_port_free(port):
            return port
    return None


def wait_port_listening(port, timeout=SERVER_READY_TIMEOUT):
    """等端口真的能连上。只看进程存活会误判（进程在跑但还没 bind）。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.5):
                return True
        except OSError:
            time.sleep(0.2)
    return False


def spawn_in_new_console(title, argv, cwd, env):
    """在新控制台窗口里起进程。

    Windows 下用 CREATE_NEW_CONSOLE，让 Server / Editor 各自拿到独立的终端
    窗口（GUI 客户端其实不需要终端，但独立窗口便于把 stderr 留在眼前，也满足
    「开三个终端」的用法）。非 Windows 平台退化成普通 Popen，只是不再分窗。

    返回 Popen；spawn 失败时抛 OSError。
    """
    creationflags = 0
    if sys.platform == "win32":
        # CREATE_NEW_CONSOLE = 0x00000010
        creationflags = 0x00000010

    # Windows 上给新窗口临时改标题（cmd 的 title 命令），非 Windows 直接忽略
    if sys.platform == "win32":
        cmdline = f'title {title} && "{argv[0]}"'
        for a in argv[1:]:
            cmdline += f' "{a}"'
        return subprocess.Popen(cmdline, cwd=str(cwd), env=env,
                                creationflags=creationflags, shell=True)

    return subprocess.Popen(argv, cwd=str(cwd), env=env, shell=False)


def main():
    setup_console_encoding()

    parser = argparse.ArgumentParser(description="三终端联机演示（服务器 + 两个编辑器）")
    parser.add_argument("--port", type=int, default=None,
                        help="指定端口（默认自动找空闲端口）")
    parser.add_argument("--scene", default=DEFAULT_SCENE,
                        help=f"服务器与客户端使用的场景（默认 {DEFAULT_SCENE}）")
    parser.add_argument("--no-editor", action="store_true",
                        help="只起服务器，不起两个编辑器")
    parser.add_argument("--keep", action="store_true",
                        help="脚本退出后不关进程（默认会关，方便 Ctrl+C 清理）")
    args = parser.parse_args()

    os.chdir(REPO_ROOT)

    log("=" * 62)
    log(" 三终端联机演示：1 个服务器 + 2 个编辑器客户端")
    log(f" 仓库：{REPO_ROOT}")
    log("=" * 62)

    # ── 1. 环境检查 ──────────────────────────────────────────────
    if not SERVER_EXE.is_file():
        fail(f"找不到 {SERVER_EXE}\n"
             f"       请先构建：见 docs/server/网络同步对照测试单.md 页末附录")
    if not args.no_editor and not EDITOR_EXE.is_file():
        fail(f"找不到 {EDITOR_EXE}\n"
             f"       请先构建 Editor；或加 --no-editor 只起服务器")

    scene_path = REPO_ROOT / args.scene
    if not scene_path.is_file():
        fail(f"找不到场景 {scene_path}")

    log(f"[1/5] 环境就绪（场景：{args.scene}）")

    # ── 2. 选端口 ────────────────────────────────────────────────
    if args.port is not None:
        port = args.port
        if not is_port_free(port):
            fail(f"指定端口 {port} 已被占用，换一个或去掉 --port 让脚本自选")
    else:
        port = pick_free_port(DEFAULT_PORT_START, PORT_SCAN_RANGE)
        if port is None:
            fail(f"{DEFAULT_PORT_START}~{DEFAULT_PORT_START + PORT_SCAN_RANGE} 无空闲端口")
    log(f"[2/5] 使用端口 {port}")

    env = os.environ.copy()
    # 代理变量对本机回环连接只会添乱，清掉更干净
    for k in ("HTTP_PROXY", "HTTPS_PROXY", "http_proxy", "https_proxy"):
        env.pop(k, None)

    procs = []

    try:
        # ── 3. 起服务器 ──────────────────────────────────────────
        log("[3/5] 启动服务器（新终端窗口）…")
        server_proc = spawn_in_new_console(
            f"Server :{port}",
            [str(SERVER_EXE), "--scene", args.scene, "--port", str(port)],
            REPO_ROOT, env,
        )
        procs.append(("服务器", server_proc))

        log(f"      等待服务器监听 127.0.0.1:{port} …")
        if not wait_port_listening(port):
            fail(f"等了 {SERVER_READY_TIMEOUT:.0f}s 服务器仍未监听 {port}\n"
                 f"       看服务器窗口里的报错（多半是场景路径或端口占用）")
        log(f"      服务器已就绪")

        # ── 4. 起两个编辑器客户端 ────────────────────────────────
        if args.no_editor:
            log("[4/5] --no-editor 已指定，跳过客户端")
        else:
            log("[4/5] 启动 2 个编辑器客户端（各一个新终端窗口）…")
            for idx in (1, 2):
                editor_proc = spawn_in_new_console(
                    f"Editor P{idx}",
                    [str(EDITOR_EXE)],
                    REPO_ROOT, env,
                )
                procs.append((f"客户端 P{idx}", editor_proc))
                time.sleep(1.5)  # 错开启动，避免同时抢 GPU/字体缓存
            log("      两个客户端已启动")

        # ── 5. 打印操作指引 ──────────────────────────────────────
        log("")
        log("=" * 62)
        log(" 接下来在客户端里手动操作：")
        log("=" * 62)
        log(f"  1. 切到「Editor P1」窗口 → 顶部菜单 网络 → 连接服务器...")
        log(f"  2. 地址填 127.0.0.1，端口填 {port}，点「连接」")
        log(f"  3. 切到「Editor P2」窗口，重复同样操作（同一端口 {port}）")
        log("")
        log("  连上后：服务器下发场景，客户端进入联机运行态；")
        log("         按住 W/A/S/D 移动，两个窗口里能看到各自的方块。")
        log("")
        log("  期望：P1 与 P2 各控制自己的方块，互不串位。")
        log("=" * 62)

        if args.keep:
            log("")
            log("--keep 已指定：脚本不再管理进程，三个窗口保持打开。")
            log("演示结束后手动关窗口，或 taskkill /F /IM Editor.exe /IM Server.exe")
            return 0

        log("")
        log("按 Ctrl+C 结束演示并关闭所有窗口…")
        while True:
            time.sleep(0.5)

    except KeyboardInterrupt:
        log("")
        log("[5/5] 收到 Ctrl+C，正在关闭所有进程…")
        return 0
    finally:
        if not args.keep:
            for name, p in procs:
                if p.poll() is None:
                    try:
                        p.terminate()
                        p.wait(timeout=3)
                    except (subprocess.TimeoutExpired, OSError):
                        try:
                            p.kill()
                        except OSError:
                            pass
            log("      已关闭")


if __name__ == "__main__":
    sys.exit(main())
