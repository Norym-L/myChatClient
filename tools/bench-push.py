#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""推送链路压测：1013 发送 → 1022/1047 投递（2026-10-24 实验分支的调度器直推）。

与 `bench-chat.py` 的分工：
  · `bench-chat.py` 打的是**请求-回包**（心跳 / 登录 / 拉取 / 发消息），量服务端处理能力；
  · 本工具打的是**推送**：一条 1013 服务端只回一个 1014，正文要通过
    `onlinechatmsgstream_<实例>` → 调度器 → 1047 才到接收方。
    所以它同时统计三段：发送侧 1014 RTT、接收侧 1047 到达率 / 端到端延迟 /
    **丢了多少条**（推送失败会落到 offlinestream，接收方收不到）。

三种模式：

    # ① 端到端：同一个进程里两个连接（发送方 + 接收方），测推送
    python tools/bench-push.py e2e --from 1001 --to 666672 \\
        --rates 50,200,500,1000 --duration 8 --inflight 32

    # ② 只发：接收方是真客户端（日志里数「收到直推消息」），适合测客户端承受力
    python tools/bench-push.py send --from 1001 --to 666672 --count 2000 --inflight 32

    # ③ 只收：挂着数 1047（排查"发出去了但没收着"）
    python tools/bench-push.py listen --uid 666672 --seconds 30

    # ④ 清场：把离线流拉空并 ACK（压测中推送失败的消息会落在这里）
    python tools/bench-push.py drain --uid 666672

前置：同一个 GateServer（8080）+ 至少一个 chat 实例；
`--from` 与 `--to` 必须是**好友**（否则 1013 回 1022，脚本会直接报错停下）。
两个账号密码可以不同：`--from-password` / `--to-password`（`--password` 是默认值）。

指标口径：
  · `1014 p50/p99`：发送方到服务端的往返（含服务端一次 Redis XADD）；
  · `e2e p50/p99`：接收方收到的时刻 − Stream ID 里服务端写入的时刻，
    含调度器排队 + socket 写出（同一台机器，时钟可比）；
  · `lost = accepted − received`：**推送没送到**的条数，正常应恒为 0。
"""
import argparse
import asyncio
import json
import statistics
import struct
import sys
import time
import urllib.request

GATE = "http://127.0.0.1:8080"

CHAT_LOGIN_REQ = 1001
CHAT_LOGIN_RSP = 1002
TEXT_CHAT_MSG_REQ = 1013
TEXT_CHAT_MSG_RSP = 1014
SYNC_CHAT_MESSAGE_REQ = 1023
SYNC_CHAT_MESSAGE_RSP = 1024
ACK_CHAT_MESSAGE_REQ = 1025
ACK_CHAT_MESSAGE_RSP = 1026
NOTIFY_RECEIVE_MESSAGE = 1022
CHAT_MESSAGE = 1047


# ──────────────────────────────────────────────────────────────
#  连接与帧
# ──────────────────────────────────────────────────────────────
def http_login(uid: str, password: str):
    data = json.dumps({"user": uid, "password": password}).encode("utf-8")
    request = urllib.request.Request(
        GATE + "/user_login", data=data,
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=5) as response:
        return json.loads(response.read().decode("utf-8"))


async def chat_login(uid: str, password: str):
    """HTTP 换 token → 连 chat → 1001。返回 (reader, writer, 实际 uid)。"""
    result = http_login(uid, password)
    if result.get("error") != 0:
        raise RuntimeError(f"/user_login 失败: {json.dumps(result, ensure_ascii=False)}")

    host = result.get("host") or "127.0.0.1"
    port = int(result.get("port") or 8090)
    reader, writer = await asyncio.open_connection(host, port)
    writer.write(frame(CHAT_LOGIN_REQ, {"uid": str(result.get("uid", uid)),
                                        "token": result.get("token", "")}))
    await writer.drain()

    mid, body = await read_frame(reader)
    data = json.loads(body)
    if mid != CHAT_LOGIN_RSP or data.get("error") != 0:
        writer.close()
        raise RuntimeError(f"聊天服登录失败: {body}")
    return reader, writer, str(data.get("uid", uid)), f"{host}:{port}"


def frame(message_id: int, payload: dict) -> bytes:
    body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    return struct.pack(">HH", message_id, len(body)) + body


async def read_frame(reader):
    head = await reader.readexactly(4)
    message_id, length = struct.unpack(">HH", head)
    body = await reader.readexactly(length) if length else b""
    return message_id, body.decode("utf-8", "replace")


def percentiles(values, points=(50, 90, 99)):
    if not values:
        return {p: 0.0 for p in points} | {"max": 0.0, "avg": 0.0}
    ordered = sorted(values)
    out = {}
    for p in points:
        index = min(len(ordered) - 1, int(round(p / 100 * (len(ordered) - 1))))
        out[p] = ordered[index]
    out["max"] = ordered[-1]
    out["avg"] = statistics.fmean(ordered)
    return out


def stream_ms(stream_id: str) -> int:
    """`<毫秒>-<序号>` → 毫秒。"""
    try:
        return int(stream_id.split("-", 1)[0])
    except (ValueError, IndexError):
        return 0


# ──────────────────────────────────────────────────────────────
#  发送侧：K 条在飞 + 有节奏地发
# ──────────────────────────────────────────────────────────────
class Sender:
    def __init__(self, reader, writer, to_uid: str, inflight: int, tag: str,
                 group: str = ""):
        self.reader = reader
        self.writer = writer
        self.to_uid = to_uid
        self.group = group          # 非空 = 群号，1013 带 "group": true
        self.inflight = inflight
        self.tag = tag

        self.sent = 0
        self.accepted = 0
        self.failed = 0
        self.error_codes = {}
        self.rtts = []
        self.pending = {}          # msg_id -> 发出时刻
        self._space = asyncio.Event()
        self._space.set()
        self._done = False
        self.reader_task = asyncio.create_task(self._read_loop())

    async def _read_loop(self):
        try:
            while True:
                mid, body = await read_frame(self.reader)
                if mid != TEXT_CHAT_MSG_RSP:
                    continue  # 发送方也可能收到别人发来的 1047，这里不关心

                data = json.loads(body)
                msg_id = data.get("msg_id")
                sent_at = self.pending.pop(msg_id, None)
                if sent_at is not None:
                    self.rtts.append((time.perf_counter() - sent_at) * 1000)

                error = data.get("error", 1)
                if error == 0:
                    self.accepted += 1
                else:
                    self.failed += 1
                    self.error_codes[error] = self.error_codes.get(error, 0) + 1

                if len(self.pending) < self.inflight:
                    self._space.set()
        except (asyncio.IncompleteReadError, ConnectionError):
            self._done = True

    async def run_rate(self, rate: float, duration: float):
        """按目标速率发 duration 秒；rate<=0 表示打满（只在 inflight 上限处停）。

        ⚠ **不能**用 `next_at += interval` 那种写法：Windows 的定时器粒度会
        把 5ms 的 sleep 拖到 7.5ms，一旦"落后就重置基准"，目标 200/s 只能跑出
        133/s（实测）。这里改成**按累计应发条数**配速 —— 落后时连发补上，
        超前才睡，平均速率才和 target 对得上。
        """
        start = time.perf_counter()
        deadline = start + duration

        while True:
            now = time.perf_counter()
            if now >= deadline:
                break

            # rate<=0 = 打满：把窗口一直灌满，由 inflight 与回包速度决定实际速率
            due = (self.sent + self.inflight) if rate <= 0 else int((now - start) * rate)

            while self.sent < due:
                if len(self.pending) >= self.inflight:
                    self._space.clear()
                    await self._space.wait()
                    if self._done:
                        break

                msg_id = f"{self.tag}-{self.sent}"
                self.pending[msg_id] = time.perf_counter()
                payload = {
                    "msg_id": msg_id, "to_uid": self.to_uid, "type": "1",
                    "body": f"stress {self.sent}",
                }
                if self.group:
                    payload["group"] = True     # ⚠ JSON 布尔，服务端只认这个
                self.writer.write(frame(TEXT_CHAT_MSG_REQ, payload))
                self.sent += 1

                if self.sent % 16 == 0:
                    await self.writer.drain()

            if rate > 0:
                # 下一条的应发时刻；至少睡 0.5ms，避免忙等把 CPU 打满
                wait = (self.sent + 1) / rate - (time.perf_counter() - start)
                await asyncio.sleep(max(0.0005, min(wait, 0.05)))
            else:
                await asyncio.sleep(0.0005)

        await self.writer.drain()

    async def wait_drain(self, timeout=10.0):
        """等最后几条回包（最多 timeout 秒）。"""
        deadline = time.perf_counter() + timeout
        while self.pending and time.perf_counter() < deadline:
            await asyncio.sleep(0.02)

    def close(self, close_writer: bool = False):
        # ⚠ 默认**不关 writer**：多档压测共用一条发送连接，
        #   上一档关掉连接，下一档会在 drain() 上直接 Connection lost。
        self.reader_task.cancel()
        if close_writer:
            self.writer.close()


# ──────────────────────────────────────────────────────────────
#  接收侧：只数 1047
# ──────────────────────────────────────────────────────────────
class Receiver:
    def __init__(self, reader, writer):
        self.reader = reader
        self.writer = writer
        self.received = 0
        self.old_1022 = 0
        self.e2e_ms = []
        self.first_at = None
        self.last_at = None
        self.task = asyncio.create_task(self._loop())

    async def _loop(self):
        try:
            while True:
                mid, body = await read_frame(self.reader)
                now_ms = int(time.time() * 1000)
                if mid == CHAT_MESSAGE:
                    data = json.loads(body)
                    self.received += 1
                    if self.first_at is None:
                        self.first_at = now_ms
                    self.last_at = now_ms
                    written = stream_ms(str(data.get("time", "")))
                    if written:
                        self.e2e_ms.append(now_ms - written)
                elif mid == NOTIFY_RECEIVE_MESSAGE:
                    # 实验分支下聊天消息不该再走 1022；出现说明还挂着旧链路
                    self.old_1022 += 1
        except (asyncio.IncompleteReadError, ConnectionError):
            pass

    def close(self):
        self.task.cancel()
        self.writer.close()


async def cmd_e2e(args):
    rx_reader, rx_writer, rx_uid, rx_addr = await chat_login(
        args.to, args.to_password or args.password)
    print(f"[setup] 接收方 {rx_uid} @ {rx_addr} 已登录")
    receiver = Receiver(rx_reader, rx_writer)
    await asyncio.sleep(0.3)  # 让服务端把 uip_<uid> 写好

    tx_reader, tx_writer, tx_uid, tx_addr = await chat_login(
        args.from_uid, args.from_password or args.password)
    print(f"[setup] 发送方 {tx_uid} @ {tx_addr} 已登录\n")

    rows = []
    for rate in [float(x) for x in args.rates.split(",") if x.strip()]:
        before_received = receiver.received
        before_accepted = 0
        sender = Sender(tx_reader, tx_writer, args.group or args.to, args.inflight,
                        f"b{int(rate)}", group=args.group)

        started = time.perf_counter()
        try:
            await sender.run_rate(rate, args.duration)
        except (ConnectionError, asyncio.IncompleteReadError) as exc:
            # 服务端把连接关了：把已经发出去的量报出来，别让它看起来像脚本崩了
            print(f"[rate {rate:>6.0f}/s] 连接被对端关闭（{type(exc).__name__}）"
                  f"：已发 {sender.sent} 已受理 {sender.accepted}")
            sender.close(close_writer=True)
            tx_writer.close()
            raise
        await sender.wait_drain()
        elapsed = time.perf_counter() - started

        # 给调度器一点时间把尾巴推完
        await asyncio.sleep(args.settle)

        accepted = sender.accepted
        received = receiver.received - before_received
        lost = accepted - received
        rtt = percentiles(sender.rtts)
        e2e = percentiles(receiver.e2e_ms[-(received or 1):] if received else [])

        rows.append({
            "rate_target": rate,
            "duration": round(elapsed, 2),
            "sent": sender.sent,
            "accepted": accepted,
            "failed": sender.failed,
            "errors": sender.error_codes,
            "achieved": round(sender.sent / elapsed, 1),
            "received": received,
            "lost": lost,
            "1022": receiver.old_1022,
            "rtt_p50": round(rtt[50], 2), "rtt_p99": round(rtt[99], 2),
            "rtt_max": round(rtt["max"], 2),
            "e2e_p50": round(e2e[50], 2), "e2e_p99": round(e2e[99], 2),
            "e2e_max": round(e2e["max"], 2),
        })

        print(f"[rate {rate:>6.0f}/s] 发 {sender.sent:>6} 收 {accepted:>6} "
              f"到达 {received:>6} 丢 {lost:>4} | 达成 {rows[-1]['achieved']:>7.1f}/s | "
              f"1014 p50 {rows[-1]['rtt_p50']:>6.1f}ms p99 {rows[-1]['rtt_p99']:>7.1f}ms | "
              f"e2e p50 {rows[-1]['e2e_p50']:>5.1f}ms p99 {rows[-1]['e2e_p99']:>6.1f}ms"
              + (f" | 错误 {sender.error_codes}" if sender.error_codes else ""))
        sender.close()
        await asyncio.sleep(args.interval)

    receiver.close()
    tx_writer.close()

    print("\n" + "=" * 100)
    print(f"{'目标':>7} {'发送':>7} {'接收':>7} {'到达':>7} {'丢':>5} "
          f"{'达成/s':>9} {'1014p50':>9} {'1014p99':>9} {'e2ep50':>8} {'e2ep99':>8}")
    for row in rows:
        print(f"{row['rate_target']:>7.0f} {row['sent']:>7} {row['accepted']:>7} "
              f"{row['received']:>7} {row['lost']:>5} {row['achieved']:>9.1f} "
              f"{row['rtt_p50']:>9.1f} {row['rtt_p99']:>9.1f} "
              f"{row['e2e_p50']:>8.1f} {row['e2e_p99']:>8.1f}")

    if args.json:
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump({"mode": "e2e", "rows": rows}, handle, ensure_ascii=False, indent=2)
        print(f"\n[json] {args.json}")

    total_lost = sum(row["lost"] for row in rows)
    return 0 if total_lost == 0 else 6


async def cmd_send(args):
    reader, writer, uid, addr = await chat_login(
        args.from_uid, args.from_password or args.password)
    print(f"[setup] 发送方 {uid} @ {addr} → {args.to}，共 {args.count} 条，"
          f"在飞 {args.inflight}\n")

    sender = Sender(reader, writer, args.group or args.to, args.inflight, "bench",
                    group=args.group)
    started = time.perf_counter()

    if args.rate > 0:
        # 定速：按 count/rate 秒发完（多用于"真客户端作接收方"的承受力测试）
        await sender.run_rate(args.rate, args.count / args.rate)
        await sender.wait_drain()
        elapsed = time.perf_counter() - started
        rtt = percentiles(sender.rtts)
        print(f"[send] 目标 {args.rate:.0f}/s，发出 {sender.sent} 服务端受理 {sender.accepted} "
              f"失败 {sender.failed} 用时 {elapsed:.2f}s（{sender.sent / elapsed:.1f}/s）")
        print(f"[send] 1014 RTT p50 {rtt[50]:.1f}ms p90 {rtt[90]:.1f}ms "
              f"p99 {rtt[99]:.1f}ms max {rtt['max']:.1f}ms")
        if sender.error_codes:
            print(f"[send] 错误码分布 {sender.error_codes}")
        print("[hint] 接收方应收到同样条数的 1047；差额就是推送失败的条数"
              "（会落在 offlinestream_<uid>，用 drain 子命令清掉）")
        sender.close(close_writer=True)
        writer.close()
        if args.json:
            with open(args.json, "w", encoding="utf-8") as handle:
                json.dump({"mode": "send", "sent": sender.sent, "accepted": sender.accepted,
                           "failed": sender.failed, "seconds": round(elapsed, 2),
                           "achieved": round(sender.sent / elapsed, 1),
                           "rtt": {str(k): round(v, 2) for k, v in rtt.items()},
                           "errors": sender.error_codes}, handle, ensure_ascii=False, indent=2)
        return 0 if sender.failed == 0 else 4

    while sender.sent < args.count:
        batch = min(args.inflight - len(sender.pending), args.count - sender.sent)
        if batch <= 0:
            await asyncio.sleep(0.005)
            continue
        for _ in range(batch):
            msg_id = f"bench-{sender.sent}"
            sender.pending[msg_id] = time.perf_counter()
            payload = {
                "msg_id": msg_id, "to_uid": args.group or args.to, "type": "1",
                "body": f"stress {sender.sent}",
            }
            if args.group:
                payload["group"] = True
            sender.writer.write(frame(TEXT_CHAT_MSG_REQ, payload))
            sender.sent += 1
        await sender.writer.drain()
        await asyncio.sleep(args.gap)

    await sender.wait_drain()
    elapsed = time.perf_counter() - started
    rtt = percentiles(sender.rtts)

    print(f"[send] 发出 {sender.sent} 服务端受理 {sender.accepted} 失败 {sender.failed} "
          f"用时 {elapsed:.2f}s（{sender.sent / elapsed:.1f}/s）")
    print(f"[send] 1014 RTT p50 {rtt[50]:.1f}ms p90 {rtt[90]:.1f}ms "
          f"p99 {rtt[99]:.1f}ms max {rtt['max']:.1f}ms")
    if sender.error_codes:
        print(f"[send] 错误码分布 {sender.error_codes}")
    print("[hint] 接收方应收到同样条数的 1047；差额就是推送失败的条数"
          "（会落在 offlinestream_<uid>，用 drain 子命令清掉）")

    sender.close()
    writer.close()
    if args.json:
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump({"mode": "send", "sent": sender.sent, "accepted": sender.accepted,
                       "failed": sender.failed, "seconds": round(elapsed, 2),
                       "achieved": round(sender.sent / elapsed, 1),
                       "rtt": {str(k): round(v, 2) for k, v in rtt.items()},
                       "errors": sender.error_codes}, handle, ensure_ascii=False, indent=2)
    return 0 if sender.failed == 0 else 4


async def cmd_listen(args):
    reader, writer, uid, addr = await chat_login(args.uid, args.password)
    print(f"[setup] {uid} @ {addr} 已登录，监听 {args.seconds}s …")
    receiver = Receiver(reader, writer)
    await asyncio.sleep(args.seconds)

    e2e = percentiles(receiver.e2e_ms)
    print(f"[listen] 收到 1047 {receiver.received} 条，1022 {receiver.old_1022} 条")
    if receiver.received:
        span = max(1, receiver.last_at - receiver.first_at)
        print(f"[listen] 到达速率 ≈ {receiver.received * 1000 / span:.1f}/s，"
              f"e2e p50 {e2e[50]:.1f}ms p99 {e2e[99]:.1f}ms max {e2e['max']:.1f}ms")

    receiver.close()
    writer.close()
    return 0


async def cmd_drain(args):
    reader, writer, uid, addr = await chat_login(args.uid, args.password)
    print(f"[setup] {uid} @ {addr} 已登录，开始清空离线流 …")

    total = 0
    while True:
        writer.write(frame(SYNC_CHAT_MESSAGE_REQ, {"count": "10"}))
        await writer.drain()
        mid, body = await read_frame(reader)
        while mid != SYNC_CHAT_MESSAGE_RSP:
            mid, body = await read_frame(reader)
        data = json.loads(body)
        if data.get("error") != 0:
            print(f"[drain] 拉取失败: {body}")
            break
        msgs = data.get("msgs") or []
        if not msgs:
            break
        ids = [m.get("time") or m.get("id") for m in msgs]
        total += len(ids)
        writer.write(frame(ACK_CHAT_MESSAGE_REQ, {"ids": ids}))
        await writer.drain()
        mid, body = await read_frame(reader)
        while mid != ACK_CHAT_MESSAGE_RSP:
            mid, body = await read_frame(reader)
        print(f"[drain] 确认 {len(ids)} 条（累计 {total}）")

    print(f"[drain] 完成，共清掉 {total} 条离线消息")
    writer.close()
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="1013 → 1047 推送链路压测")
    sub = parser.add_subparsers(dest="command", required=True)

    def common(p):
        p.add_argument("--password", default="Test@12345")
        p.add_argument("--json", default="")

    p_e2e = sub.add_parser("e2e", help="发送 + 接收同进程，测推送到达与延迟")
    p_e2e.add_argument("--from", dest="from_uid", required=True)
    p_e2e.add_argument("--to", required=True, help="接收方 uid（群发时仍需是群成员）")
    p_e2e.add_argument("--group", default="", help="群号：1013 带 group=true，to_uid 用群号")
    p_e2e.add_argument("--from-password", default="")
    p_e2e.add_argument("--to-password", default="")
    p_e2e.add_argument("--rates", default="50,200,500,1000")
    p_e2e.add_argument("--duration", type=float, default=8.0)
    p_e2e.add_argument("--inflight", type=int, default=32)
    p_e2e.add_argument("--settle", type=float, default=1.0, help="每档结束后等推送收尾")
    p_e2e.add_argument("--interval", type=float, default=0.5, help="档位之间歇一下")
    common(p_e2e)

    p_send = sub.add_parser("send", help="只发（接收方可以是真客户端）")
    p_send.add_argument("--from", dest="from_uid", required=True)
    p_send.add_argument("--to", required=True)
    p_send.add_argument("--group", default="", help="群号（同上）")
    p_send.add_argument("--from-password", default="")
    p_send.add_argument("--count", type=int, default=1000)
    p_send.add_argument("--inflight", type=int, default=32)
    p_send.add_argument("--gap", type=float, default=0.0)
    p_send.add_argument("--rate", type=float, default=0.0, help="定速（条/秒）；0 = 打满")
    common(p_send)

    p_listen = sub.add_parser("listen", help="只收，数 1047")
    p_listen.add_argument("--uid", required=True)
    p_listen.add_argument("--seconds", type=float, default=30.0)
    common(p_listen)

    p_drain = sub.add_parser("drain", help="拉空离线流并确认（清场）")
    p_drain.add_argument("--uid", required=True)
    common(p_drain)

    args = parser.parse_args()
    handler = {"e2e": cmd_e2e, "send": cmd_send,
               "listen": cmd_listen, "drain": cmd_drain}[args.command]
    return asyncio.run(handler(args))


if __name__ == "__main__":
    sys.exit(main())
