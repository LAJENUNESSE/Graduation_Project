#!/usr/bin/env python3
"""一键网络同步测试：自动起服务器 → 跑冒烟 → 收尾清理。

用法（在仓库根目录）：

    python tests/manual/run_oneclick_test.py

它会依次做这些事，全程不需要你手动点或切窗口：

    1. 检查 Server.exe / net_smoke.py 是否存在
    2. 清掉可能残留的旧 Server.exe（避免端口被占）
    3. 自动挑一个空闲端口（默认从 31000 起找）
    4. 后台启动 Server，并等它真的开始监听
    5. 等 10 秒，让方块落地休眠（这是回归本次休眠刚体 bug 的关键：修复前
       正因如此才必然失败）
    6. 跑 net_smoke.py，抓取输出与退出码
    7. 杀掉 Server，打印结论摘要

退出码：0 = PASS，1 = 测试失败，2 = 环境/启动错误。
所有原始日志留在 tests/manual/_oneclick_logs/ 下，便于贴给别人看。
"""

import argparse
import os
import re
import socket
import subprocess
import sys
import time
from pathlib import Path

# 仓库根目录 = 本文件的上两级
REPO_ROOT = Path(__file__).resolve().parents[2]
SERVER_EXE = REPO_ROOT / "build" / "Server" / "RelWithDebInfo" / "Server.exe"
SMOKE_PY = REPO_ROOT / "tests" / "manual" / "net_smoke.py"
SCENE = "assets/scenes/network-demo.scene"
LOG_DIR = REPO_ROOT / "tests" / "manual" / "_oneclick_logs"

DEFAULT_PORT_START = 31000
PORT_SCAN_RANGE = 40

# 方块落地静止约 1s 后 Bullet 会把它置为 inactive（休眠）。
# 修复前，休眠刚体收到 setLinearVelocity 不会醒 → 注入无效 → Δz=0。
# 所以这里**故意**等久一点，确保刚体确实睡着，才能测到这个回归。
SETTLE_SECONDS = 10.0


def setup_console_encoding():
    """让中文在 Windows 控制台里能正常显示。

    Windows 控制台默认代码页常是 936(GBK)，而 Python 输出的是 UTF-8 字节，
    两者不匹配时中文会变成乱码（输出本身没错，只是显示不对）。

    处理顺序：
      1. 先把控制台代码页切到 65001(UTF-8) —— Git Bash / cmd 都有效
      2. 再把 Python 的 stdout/stderr 强制成 UTF-8
    任一步失败都不致命：最坏情况只是显示乱码，测试逻辑不受影响。
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
    log(f"\n[一键测试] FAIL: {msg}")
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


def kill_stale_servers():
    """清掉本仓库可能残留的 Server.exe。

    只按镜像名杀，不杀 GameViewerServer.exe / iGoSwServer.exe 这类无关进程。
    """
    # 注意：Windows 的 tasklist 在中文系统上输出 GBK，直接按 UTF-8 解会抛
    # UnicodeDecodeError（发生在读取线程里，主流程看不见但会刷一堆红字）。
    # 这里统一用 errors="replace" 兜底，不依赖具体代码页。
    def run_cp(args, timeout):
        return subprocess.run(args, capture_output=True, text=True,
                              encoding="utf-8", errors="replace", timeout=timeout)

    try:
        out = (run_cp(["tasklist", "/FI", "IMAGENAME eq Server.exe", "/NH"], 15).stdout) or ""
    except (OSError, subprocess.SubprocessError) as exc:
        log(f"[一键测试] 跳过残留清理（tasklist 不可用：{exc}）")
        return

    if "Server.exe" not in out:
        log("[一键测试] 无残留 Server.exe")
        return

    log("[一键测试] 发现残留 Server.exe，正在清理…")
    try:
        run_cp(["taskkill", "/F", "/IM", "Server.exe"], 20)
    except (OSError, subprocess.SubprocessError) as exc:
        log(f"[一键测试] 清理失败：{exc}（若启动报端口占用，请手动 taskkill）")
    time.sleep(1.0)


def wait_for_listen(port, proc, timeout=20.0):
    """等服务器真正 LISTENING。

    双重判定：既看进程是否还活着，也看端口能否连上——只看日志会因编码/缓冲
    而漏判，只看端口又可能在进程已崩时白等。
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        if proc.poll() is not None:
            return False, "服务器进程已退出（多半是端口占用或场景加载失败）"
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.5):
                return True, ""
        except OSError:
            time.sleep(0.2)
    return False, f"等待 {timeout:.0f}s 仍未监听 {port}"


def read_log(path):
    try:
        return Path(path).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def decode_best_effort(raw):
    """把子进程输出的原始字节尽量还原成文本。

    子进程的 stdout 编码取决于它自己的环境（可能被 PYTHONIOENCODING 控制，
    也可能受系统代码页影响）。按 UTF-8 → GBK 顺序试，最后兜底 replace，
    避免把 GBK 字节硬按 UTF-8 解出满屏 "锟斤拷"。
    """
    for enc in ("utf-8", "gbk"):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return raw.decode("utf-8", "replace")


def main():
    setup_console_encoding()

    parser = argparse.ArgumentParser(description="一键网络同步测试")
    parser.add_argument("--port", type=int, default=None,
                        help="指定端口（默认自动找空闲端口）")
    parser.add_argument("--settle-sec", type=float, default=SETTLE_SECONDS,
                        help="起服后等待刚体休眠的秒数（默认 10）")
    parser.add_argument("--keep-server", action="store_true",
                        help="测试结束后不杀服务器（方便手工再连）")
    args = parser.parse_args()

    os.chdir(REPO_ROOT)
    LOG_DIR.mkdir(parents=True, exist_ok=True)

    log("=" * 62)
    log(" 一键网络同步测试")
    log(f" 仓库：{REPO_ROOT}")
    log("=" * 62)

    # ── 1. 环境检查 ──────────────────────────────────────────────
    if not SERVER_EXE.is_file():
        fail(f"找不到 {SERVER_EXE}\n"
             f"       请先构建：见 docs/server/网络同步对照测试单.md 页末附录")
    if not SMOKE_PY.is_file():
        fail(f"找不到 {SMOKE_PY}")

    log(f"[1/6] 环境就绪")
    log(f"      Server.exe  {SERVER_EXE.stat().st_size / 1024 / 1024:.1f} MB")
    log(f"      冒烟脚本    {SMOKE_PY.relative_to(REPO_ROOT)}")

    # ── 2. 清理残留 ──────────────────────────────────────────────
    kill_stale_servers()

    # ── 3. 选端口 ────────────────────────────────────────────────
    if args.port is not None:
        port = args.port
        if not is_port_free(port):
            fail(f"指定端口 {port} 已被占用，换一个或去掉 --port 让脚本自选")
    else:
        port = pick_free_port(DEFAULT_PORT_START, PORT_SCAN_RANGE)
        if port is None:
            fail(f"{DEFAULT_PORT_START}~{DEFAULT_PORT_START + PORT_SCAN_RANGE} 无空闲端口")
    log(f"[2/6] 使用端口 {port}")

    # ── 4. 启动服务器 ────────────────────────────────────────────
    server_log = LOG_DIR / "server.log"
    log(f"[3/6] 启动服务器…（日志：{server_log.relative_to(REPO_ROOT)}）")

    env = os.environ.copy()
    # MSBuild/cmake 的代理变量大小写冲突不影响运行时，但保持干净更稳
    for k in ("HTTP_PROXY", "HTTPS_PROXY", "http_proxy", "https_proxy"):
        env.pop(k, None)

    with open(server_log, "w", encoding="utf-8", errors="replace") as lf:
        proc = subprocess.Popen(
            [str(SERVER_EXE), "--scene", SCENE, "--port", str(port)],
            cwd=str(REPO_ROOT), env=env,
            stdout=lf, stderr=subprocess.STDOUT,
        )

    try:
        ok, why = wait_for_listen(port, proc)
        if not ok:
            tail = read_log(server_log)[-1500:]
            fail(f"{why}\n------- 服务器日志尾部 -------\n{tail}\n"
                 f"-----------------------------")
        log(f"      已监听 127.0.0.1:{port}")

        # ── 5. 等刚体休眠（回归关键）──────────────────────────────
        log(f"[4/6] 等待 {args.settle_sec:.0f}s 让方块落地休眠"
            f"（修复前正是此状态下注入失效）…")
        # 期间若服务器挂了要早点报出来
        for _ in range(int(args.settle_sec * 5)):
            if proc.poll() is not None:
                fail("等待期间服务器意外退出\n"
                     f"------- 服务器日志尾部 -------\n{read_log(server_log)[-1500:]}")
            time.sleep(0.2)

        # ── 6. 跑冒烟 ────────────────────────────────────────────
        smoke_log = LOG_DIR / "smoke.log"
        log(f"[5/6] 运行冒烟测试…（日志：{smoke_log.relative_to(REPO_ROOT)}）")
        log("-" * 62)

        # 用 bytes 捕获再自己解码：子进程的编码取决于它自己的 stdout 环境，
        # 不能假定一定是 UTF-8。按 UTF-8 → GBK 顺序尝试，都不行再 replace。
        child_env = dict(env)
        child_env["PYTHONIOENCODING"] = "utf-8"  # 明确要求子进程输出 UTF-8
        smoke = subprocess.run(
            [sys.executable, str(SMOKE_PY), "--port", str(port)],
            cwd=str(REPO_ROOT), capture_output=True, timeout=180,
            env=child_env,
        )
        raw = (smoke.stdout or b"") + (smoke.stderr or b"")
        smoke_out = decode_best_effort(raw)
        print(smoke_out, end="" if smoke_out.endswith("\n") else "\n", flush=True)
        smoke_log.write_text(smoke_out, encoding="utf-8")

        log("-" * 62)

    except subprocess.TimeoutExpired:
        fail("冒烟测试超时（180s）")
    finally:
        # ── 7. 收尾 ──────────────────────────────────────────────
        if args.keep_server:
            log(f"[6/6] --keep-server 已指定，服务器保持运行（端口 {port}，PID {proc.pid}）")
            log(f"      测完后手动结束：taskkill /F /PID {proc.pid}")
        else:
            log("[6/6] 关闭服务器…")
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)

    # ── 结论 ─────────────────────────────────────────────────────
    log("")
    log("=" * 62)
    if smoke.returncode == 0:
        log(" 结论：PASS — 协议握手 / 心跳 / 快照 / 输入驱动 / 重力 全部正常")
        log("=" * 62)
        return 0

    log(f" 结论：FAIL（冒烟脚本退出码 {smoke.returncode}）")
    # 从输出里摘出关键失败行，省得用户翻日志
    fails = [ln for ln in smoke_out.splitlines()
             if ln.startswith("FAIL") or ln.strip().startswith("被控实体")]
    if fails:
        log("")
        for ln in fails:
            log(f"   {ln}")
    log("")
    log(f" 完整日志：{LOG_DIR.relative_to(REPO_ROOT)}\\")
    log("=" * 62)
    return 1


if __name__ == "__main__":
    sys.exit(main())
