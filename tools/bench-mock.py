#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ChatServer 协议的本地 mock 服务器 —— 只用来验证 bench-chat.py 本身对不对。

故意复刻服务端的两个关键行为：
  1. 每条请求都要过一道**全局串行资源**（模拟那条单 Redis 连接），
     --serial-us 就是这条链路的成本，压测脚本应该能把这个天花板量出来；
  2. 回包顺序 ≠ 请求顺序（每个请求一个 task），用来验证 msgid 关联能不能扛乱序；
  3. 推送帧（1015/1022）不带 msgid 占位，验证客户端会不会把它误当回包。

用法：
    python tools/bench-mock.py --port 18090 --serial-us 200
    python tools/bench-chat.py --no-gate --port 18090 qps --conns 1,4,16
"""

import argparse
import asyncio
import json
import os
import struct
import sys
import time
from collections import Counter

if os.name == "nt":
    try:
        import ctypes
        ctypes.WinDLL("winmm").timeBeginPeriod(1)   # 否则 sleep(200µs) 会变成 15.6ms
    except Exception:
        pass

MID = {
    "login": 1001, "search": 1003, "sendmsg": 1013, "push_chat": 1015,
    "heartbeat": 1016, "online": 1018, "friendlist": 1020, "push_recv": 1022,
    "pull": 1023, "ack": 1025,
}
RSP = {1001: 1002, 1003: 1004, 1013: 1014, 1016: 1017, 1018: 1019,
       1020: 1021, 1023: 1024, 1025: 1026}

SERIAL = None          # asyncio.Semaphore(1) + sleep = 全局串行链路
SERIAL_SEC = 0.0002
PEERS = set()          # 视为好友的 uid（--peers），用来压 1013
STATS = Counter()      # mid -> 请求数
INFLIGHT = 0
PEAK_INFLIGHT = 0
SESSIONS = {}          # uid -> Session


def frame(mid: int, obj: dict) -> bytes:
    body = json.dumps(obj, ensure_ascii=False, indent=4).encode()   # 学 toStyledString
    return struct.pack("!HH", mid, len(body)) + body


class Session:
    def __init__(self, writer):
        self.writer = writer
        self.uid = None
        self.closed = False

    def reply(self, mid: int, obj: dict):
        if self.closed:
            return
        try:
            self.writer.write(frame(mid, obj))
        except Exception:
            self.closed = True


async def serial_hop():
    """模拟一次串行资源往返。"""
    async with SERIAL:
        await asyncio.sleep(SERIAL_SEC)


async def serve(mid: int, obj: dict, sess: Session):
    global INFLIGHT
    tag = obj.get("msgid")
    STATS[mid] += 1
    if mid == MID["login"]:
        await serial_hop()
        sess.uid = str(obj.get("uid", ""))
        SESSIONS[sess.uid] = sess
        sess.reply(RSP[mid], {"error": 0, "msgid": tag, "uid": sess.uid})
        INFLIGHT -= 1
        return

    await serial_hop()
    if mid == MID["heartbeat"]:
        sess.reply(RSP[mid], {"error": 0, "msgid": tag})
    elif mid == MID["online"]:
        other = str(obj.get("uid", ""))
        on = other in SESSIONS and not SESSIONS[other].closed
        sess.reply(RSP[mid], {"error": 0, "msgid": tag,
                              "online": bool(on)})
    elif mid == MID["search"]:
        sess.reply(RSP[mid], {"error": 0, "msgid": tag,
                              "uid": obj.get("uid"), "name": "mock", "nick": "mock"})
    elif mid == MID["friendlist"]:
        others = [{"uid": u, "name": "mock" + u[-2:], "nick": "m"}
                  for u in sorted(PEERS) if u != sess.uid]
        sess.reply(RSP[mid], {"error": 0, "msgid": tag, "friends": others,
                              "count": len(others)})
    elif mid == MID["pull"]:
        msgs = [{"id": f"m{i}", "type": "1", "from_uid": "666672",
                 "body": "mock message"} for i in range(2)]
        sess.reply(RSP[mid], {"error": 0, "msgid": tag, "msgs": msgs})
    elif mid == MID["ack"]:
        sess.reply(RSP[mid], {"error": 0, "msgid": tag})
    elif mid == MID["sendmsg"]:
        to = str(obj.get("to_uid", ""))
        peer = SESSIONS.get(to)
        if to not in PEERS:
            sess.reply(RSP[mid], {"error": 1004, "msgid": tag})     # NOT_FRIEND 之类
        else:
            sess.reply(RSP[mid], {"error": 0, "msgid": tag})
            if peer is not None:
                peer.reply(MID["push_recv"], {"from_uid": sess.uid, "body": "mock",
                                              "msg_id": "1"})
    else:
        sess.reply(RSP.get(mid, mid + 1), {"error": 0, "msgid": tag})
    INFLIGHT -= 1


async def handle(reader, writer):
    global INFLIGHT, PEAK_INFLIGHT
    sess = Session(writer)
    tasks = set()
    try:
        while True:
            hdr = await reader.readexactly(4)
            mid, ln = struct.unpack("!HH", hdr)
            body = await reader.readexactly(ln) if ln else b""
            try:
                obj = json.loads(body) if body else {}
            except Exception:
                obj = {}
            INFLIGHT += 1
            PEAK_INFLIGHT = max(PEAK_INFLIGHT, INFLIGHT)
            t = asyncio.create_task(serve(mid, obj, sess))
            tasks.add(t)
            t.add_done_callback(tasks.discard)
    except (asyncio.IncompleteReadError, ConnectionResetError, OSError):
        pass
    finally:
        sess.closed = True
        for t in tasks:
            t.cancel()
        try:
            writer.close()
        except Exception:
            pass


async def report_loop():
    """每 3 秒往 stderr 打一行，方便一边压一边看服务端在飞/速率。"""
    last_n, last_t = 0, time.perf_counter()
    while True:
        await asyncio.sleep(3.0)
        now = time.perf_counter()
        total = sum(STATS.values())
        rate = (total - last_n) / (now - last_t)
        last_n, last_t = total, now
        peak = PEAK_INFLIGHT
        print(f"[mock] {rate:8.0f} req/s  累计={total}  在飞={INFLIGHT}  峰值在飞={peak}",
              file=sys.stderr, flush=True)


async def amain(args):
    global SERIAL, SERIAL_SEC
    SERIAL = asyncio.Semaphore(1)
    SERIAL_SEC = args.serial_us / 1e6
    server = await asyncio.start_server(handle, args.host, args.port, backlog=1024)
    print(f"mock 服务端在 {args.host}:{args.port}，串行成本 {args.serial_us}µs/条", flush=True)
    rep = asyncio.create_task(report_loop())
    t0 = time.perf_counter()
    async with server:
        try:
            await asyncio.sleep(args.run_sec)
        except asyncio.CancelledError:
            pass
    rep.cancel()
    wall = time.perf_counter() - t0
    total = sum(STATS.values())
    print(f"\n[stats] 运行 {wall:.1f}s  请求 {total}  吞吐 {total / wall:.0f}/s  "
          f"峰值在飞 {PEAK_INFLIGHT}", flush=True)
    for mid, n in sorted(STATS.items()):
        print(f"  mid={mid} × {n}", flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=18090)
    ap.add_argument("--serial-us", type=int, default=200,
                    help="模拟单条串行链路的成本（微秒）")
    ap.add_argument("--run-sec", type=float, default=600)
    ap.add_argument("--peers", default="666672,666673,666681,666682,666683",
                    help="视为好友的 uid（逗号分隔），1013/1020 用")
    args = ap.parse_args()
    PEERS.update(u.strip() for u in args.peers.split(",") if u.strip())
    try:
        asyncio.run(amain(args))
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
