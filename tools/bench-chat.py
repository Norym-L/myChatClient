#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ChatServer TCP 层压测 v2（绕过 GateServer，直连 chat 实例）。

v2 相对 v1 的改动
  1. 流水线：每条连接可同时挂 K 个未完成请求（v1 是"发一条等一条"，自带流控，
     实测天花板会被自己的 1-in-flight 卡住）。响应靠回包里的 msgid 精确关联
     —— LogicSystem.h 的 HANDLER_PREFIX 会把请求的 msgid 原样回显，可以乱序匹配。
  2. 开环定速：rate 阶段按目标速率发，而不是"打满为止"，才能区分
     "服务器扛不住" 和 "客户端发不出来"（同时会报客户端 CPU 占用）。
  3. 多进程：--procs 起多个子进程各自跑 asyncio，用来排除 Python 客户端自身瓶颈。
  4. 混合业务：--mix 按权重发 心跳/好友列表/在线查询/拉取/发消息，不再只打心跳。
  5. 突发改真·齐步走：一次循环里同步写完全部 N 帧（Windows 的 asyncio 定时器
     粒度约 15.6ms，用 sleep 对齐反而抖成锯齿）。
  6. 建连阶段改并发建连（v1 是 for 循环串行，量的是客户端建连速度）。
  7. 统计改分桶直方图，跨进程合并便宜；报 p50/p90/p99/均值/max/失败数。
  8. 拉取消息自动 ACK（否则 PEL 会一直重复投递同一批消息）。

用法
    python tools/bench-chat.py                      # 全部阶段
    python tools/bench-chat.py qps --host 10.0.0.5  # 只打稳态吞吐
    python tools/bench-chat.py qps --conns 1,8,32,128 --inflight 8 --procs 4
    python tools/bench-chat.py rate --rates 8000,16000,24000 --rate-duration 10
    python tools/bench-chat.py qps --mix heartbeat=60,sendmsg=10,friendlist=15,online=10,pull=5
    python tools/bench-chat.py latency qps --json out.json

阶段
    latency  单连接串行 RTT 基线（每种消息 rounds 次）
    login    并发登录（1/2/4/.../N 条连接同时发 1001）
    qps      闭路流水线稳态吞吐（每档 duration 秒）
    rate     开环定速稳态（目标速率 vs 实际达成）
    burst    瞬时突发（N 条连接在同一时刻各发 1 条心跳）
    conn     TCP 连接承载（并发建连，不登录）

怎么读结果
    1/QPS = 单条消息的平均串行成本；如果 p50 随并发数 N 线性涨、而 QPS 只涨一点点，
    就是服务端存在"单个串行资源"，加并发没用，得查那条链路（Redis 连接、日志、锁）。
    客户端 CPU 高（>70%）说明是脚本自己发不出来，换 --procs 或换语言再测。
"""

import argparse
import asyncio
import json
import os
import random
import struct
import sys
import tempfile
import time
import urllib.request
from collections import Counter
from multiprocessing import get_context

# ──────────────────────────────────────────────────────────────
#  协议常量
# ──────────────────────────────────────────────────────────────
GATE = "http://127.0.0.1:8080"
PASSWORD = "Test@12345"
# 测试账号（密码统一，均为真实注册/建库账号）；--uids 可覆盖
UIDS = ["666672", "666673", "666681", "666682", "666683"]

# 与 src/public/const.h 的 MessageType 一一对应（枚举从 1001 起顺序递增）
MID = {
    "login": 1001, "login_rsp": 1002,
    "search": 1003, "search_rsp": 1004,
    "sendmsg": 1013, "sendmsg_rsp": 1014,
    "push_chat_msg": 1015,          # 服务端推送：收到聊天消息
    "heartbeat": 1016, "heartbeat_rsp": 1017,
    "online": 1018, "online_rsp": 1019,
    "friendlist": 1020, "friendlist_rsp": 1021,
    "push_recv": 1022,              # 服务端推送：有新消息
    "pull": 1023, "pull_rsp": 1024,
    "ack": 1025, "ack_rsp": 1026,
}
RSP_OF = {
    MID["login"]: MID["login_rsp"], MID["search"]: MID["search_rsp"],
    MID["sendmsg"]: MID["sendmsg_rsp"], MID["heartbeat"]: MID["heartbeat_rsp"],
    MID["online"]: MID["online_rsp"], MID["friendlist"]: MID["friendlist_rsp"],
    MID["pull"]: MID["pull_rsp"], MID["ack"]: MID["ack_rsp"],
}
LABEL = {
    MID["login"]: "1001 登录", MID["search"]: "1003 查用户", MID["sendmsg"]: "1013 发消息",
    MID["heartbeat"]: "1016 心跳", MID["online"]: "1018 在线查询",
    MID["friendlist"]: "1020 好友列表", MID["pull"]: "1023 拉取消息",
    MID["ack"]: "1025 ACK",
}

OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))
DEFAULT_STAGES = ["latency", "login", "qps", "rate", "burst", "conn"]
TAG_TOKEN = b'"__TAG__"'
WANT_OBJ, WANT_RTT = "obj", "rtt"     # pending 里 future 要收的东西：回包对象 / RTT

_WINMM = None


def bump_timer():
    """Windows 默认定时器粒度是 15.6ms，会把 sleep(1ms) 放大 15 倍。

    开环定速、突发收尾都靠毫秒级 sleep，不把粒度降到 1ms，测出来的耗时全是假的。
    """
    global _WINMM
    if os.name != "nt":
        return
    try:
        import ctypes
        _WINMM = ctypes.WinDLL("winmm")
        _WINMM.timeBeginPeriod(1)
    except Exception:
        _WINMM = None


def restore_timer():
    if _WINMM is not None:
        try:
            _WINMM.timeEndPeriod(1)
        except Exception:
            pass


# ──────────────────────────────────────────────────────────────
#  统计：分桶直方图（跨进程合并只需要传 dict）
# ──────────────────────────────────────────────────────────────
_E1, _E2, _E3 = 50, 295, 520     # 0.02ms / 0.2ms / 2ms 三段的桶数边界


def _bucket(x: float) -> int:
    if x < 0.0:
        x = 0.0
    if x < 1.0:
        return int(x / 0.02)            # 0.02ms 一档
    if x < 50.0:
        return _E1 + int((x - 1.0) / 0.2)    # 0.2ms 一档
    if x < 500.0:
        return _E2 + int((x - 50.0) / 2.0)   # 2ms 一档
    return _E3                          # 溢出桶：>=500ms


def _bucket_hi(i: int) -> float:
    if i < _E1:
        return (i + 1) * 0.02
    if i < _E2:
        return 1.0 + (i - _E1 + 1) * 0.2
    if i < _E3:
        return 50.0 + (i - _E2 + 1) * 2.0
    return 500.0


class Hist:
    __slots__ = ("n", "s", "mn", "mx", "b")

    def __init__(self):
        self.n = 0
        self.s = 0.0
        self.mn = float("inf")
        self.mx = 0.0
        self.b = {}

    def add(self, x: float):
        self.n += 1
        self.s += x
        if x < self.mn:
            self.mn = x
        if x > self.mx:
            self.mx = x
        i = _bucket(x)
        self.b[i] = self.b.get(i, 0) + 1

    def merge(self, other: "Hist"):
        if not other or not other.n:
            return self
        self.n += other.n
        self.s += other.s
        self.mn = min(self.mn, other.mn)
        self.mx = max(self.mx, other.mx)
        for k, v in other.b.items():
            self.b[k] = self.b.get(k, 0) + v
        return self

    def merge_dict(self, d: dict):
        if not d or not d.get("n"):
            return self
        self.n += d["n"]
        self.s += d["s"]
        self.mn = min(self.mn, d["mn"])
        self.mx = max(self.mx, d["mx"])
        for k, v in d["b"].items():
            k = int(k)
            self.b[k] = self.b.get(k, 0) + v
        return self

    def as_dict(self) -> dict:
        return {"n": self.n, "s": self.s,
                "mn": self.mn if self.n else 0.0, "mx": self.mx,
                "b": {str(k): v for k, v in self.b.items()}}

    def mean(self) -> float:
        return self.s / self.n if self.n else 0.0

    def pct(self, p: float) -> float:
        if not self.n:
            return 0.0
        target = max(1, int(round(self.n * p)))
        acc = 0
        for i in sorted(self.b):
            acc += self.b[i]
            if acc >= target:
                return _bucket_hi(i)
        return self.mx

    def diff_pcts(self, before: dict):
        """用两次桶快照相减，得到本轮新增样本的 (n, p50, p90, p99)。"""
        h = {}
        for k, v in self.b.items():
            d = v - before.get(k, 0)
            if d > 0:
                h[k] = d
        n = sum(h.values())
        if not n:
            return 0, 0.0, 0.0, 0.0

        def pct(p):
            target = max(1, int(round(n * p)))
            acc = 0
            for i in sorted(h):
                acc += h[i]
                if acc >= target:
                    return _bucket_hi(i)
            return 500.0
        return n, pct(.5), pct(.9), pct(.99)


def fmt_hist(h: Hist, units="ms") -> str:
    if not h or not h.n:
        return "n=0"
    return (f"n={h.n:<7} p50≈{h.pct(.5):7.2f}  p90≈{h.pct(.9):7.2f}  "
            f"p99≈{h.pct(.99):7.2f}  均值={h.mean():6.2f}  max={h.mx:7.2f} {units}")


def pct(values, p):
    if not values:
        return 0.0
    s = sorted(values)
    idx = min(len(s) - 1, max(0, int(round((len(s) - 1) * p))))
    return s[idx]


def print_table(title, rows):
    print()
    print("─" * 96)
    print(title)
    print("─" * 96)
    for r in rows:
        print(r)


# ──────────────────────────────────────────────────────────────
#  报文
# ──────────────────────────────────────────────────────────────
def frame(mid: int, body: bytes) -> bytes:
    return struct.pack("!HH", mid, len(body)) + body


class Plan:
    """一条请求模板：body 里 msgid 用占位符，发送时替换成自增 tag。"""
    __slots__ = ("mid", "tmpl", "label")

    def __init__(self, mid: int, obj: dict, label: str):
        o = dict(obj)
        o["msgid"] = "__TAG__"
        self.mid = mid
        self.tmpl = json.dumps(o, ensure_ascii=False, separators=(",", ":")).encode()
        self.label = label

    def build(self, tag: int) -> bytes:
        return frame(self.mid, self.tmpl.replace(TAG_TOKEN, b"%d" % tag))


PLAN_HB = Plan(MID["heartbeat"], {}, "1016 心跳")
PLAN_FRIENDLIST = Plan(MID["friendlist"], {}, "1020 好友列表")
PLAN_PULL = Plan(MID["pull"], {"count": "10"}, "1023 拉取消息")
_PLAN_CACHE = {}


def plan_for(label: str, peer: str, friend: str = None) -> Plan:
    if label == "heartbeat":
        return PLAN_HB
    if label == "friendlist":
        return PLAN_FRIENDLIST
    if label == "pull":
        return PLAN_PULL
    if label == "online":
        key = f"online:{peer}"
        if key not in _PLAN_CACHE:
            _PLAN_CACHE[key] = Plan(MID["online"], {"uid": peer}, "1018 在线查询")
        return _PLAN_CACHE[key]
    if label == "search":
        key = f"search:{peer}"
        if key not in _PLAN_CACHE:
            _PLAN_CACHE[key] = Plan(MID["search"], {"uid": peer}, "1003 查用户")
        return _PLAN_CACHE[key]
    if label == "sendmsg":
        if not friend:
            return None
        key = f"sendmsg:{friend}"
        if key not in _PLAN_CACHE:
            _PLAN_CACHE[key] = Plan(MID["sendmsg"],
                                    {"to_uid": friend, "type": "1", "body": "bench msg"},
                                    "1013 发消息")
        return _PLAN_CACHE[key]
    return None


def pick_friend(uid: str, friends: dict):
    lst = friends.get(uid) if friends else None
    if not lst:
        return None
    return lst[(hash(uid) % len(lst))]


class Mixer:
    """平滑加权轮询（nginx SWRR）：按权重轮流挑请求模板，避免连着打 70 条心跳。"""

    def __init__(self, items):
        self.items = items              # [(Plan, weight)]
        self.cur = [0] * len(items)
        self.total = sum(w for _, w in items)

    def pick(self) -> Plan:
        best, bi = -1, 0
        for i, (_, w) in enumerate(self.items):
            self.cur[i] += w
            if self.cur[i] > best:
                best, bi = self.cur[i], i
        self.cur[bi] -= self.total
        return self.items[bi][0]


def build_mixer(mix, uid, peer, friends) -> Mixer:
    items = []
    for label, w in mix:
        if w <= 0:
            continue
        p = plan_for(label, peer, pick_friend(uid, friends))
        if p is not None:
            items.append((p, w))
    if not items:
        items = [(PLAN_HB, 1)]
    return Mixer(items)


# ──────────────────────────────────────────────────────────────
#  连接
# ──────────────────────────────────────────────────────────────
def _loads(body: bytes):
    if not body:
        return {}
    try:
        return json.loads(body)
    except Exception:
        return {}


class Client:
    """一条 TCP 连接：后台读协程 + 可流水线发送。"""

    def __init__(self, host, port, timeout=5.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.reader = None
        self.writer = None
        self.loop = None
        self.pending = {}               # tag -> (mid, t_send, future|None, 占额度?, 要什么)
        self.per_mid = {}               # mid -> Hist（只统计 fire 出去的请求）
        self.push = Counter()
        self.biz = Counter()            # mid -> 业务错误码
        self.timeouts = 0
        self.fail = 0
        self.sent = 0
        self.acks = 0
        self.closed = False
        self.uid = None
        self.mixer = None
        self.auto_ack = True
        self.sem = None
        self._tag = 0
        self._reader_task = None
        self._sweeper_task = None

    # ── 生命周期 ──
    async def connect(self, inflight: int = 1):
        self.reader, self.writer = await asyncio.open_connection(
            self.host, self.port, limit=1 << 17)
        self.loop = asyncio.get_running_loop()
        self.sem = asyncio.Semaphore(max(1, inflight))
        self._reader_task = self.loop.create_task(self._reader_loop())
        self._sweeper_task = self.loop.create_task(self._sweeper())
        return self

    async def close(self):
        self.closed = True
        tasks = [t for t in (self._reader_task, self._sweeper_task) if t]
        for t in tasks:
            t.cancel()
        if tasks:
            await asyncio.gather(*tasks, return_exceptions=True)
        if self.writer:
            try:
                self.writer.close()
                await asyncio.wait_for(self.writer.wait_closed(), 1.0)
            except Exception:
                pass
            self.writer = None

    # ── 收发 ──
    def _next_tag(self) -> int:
        self._tag += 1
        return self._tag

    async def _reader_loop(self):
        try:
            while True:
                hdr = await self.reader.readexactly(4)
                mid, ln = struct.unpack("!HH", hdr)
                body = await self.reader.readexactly(ln) if ln else b""
                now = time.perf_counter()
                obj = _loads(body)
                tag = obj.get("msgid")
                ent = self.pending.pop(tag, None) if type(tag) is int else None
                if ent is None:
                    self.push[mid] += 1        # 服务端主动推送 / 迟到回包
                    continue
                req_mid, t0, fut, credit, want = ent
                if credit and self.sem is not None:
                    self.sem.release()
                if fut is not None:
                    if not fut.done():
                        fut.set_result(obj if want == WANT_OBJ else (now - t0) * 1000.0)
                    continue
                h = self.per_mid.get(req_mid)
                if h is None:
                    h = self.per_mid[req_mid] = Hist()
                h.add((now - t0) * 1000.0)
                if obj.get("error") not in (0, None):
                    self.biz[req_mid] += 1
                if req_mid == MID["pull"] and self.auto_ack:
                    ids = [m.get("id") for m in (obj.get("msgs") or [])
                           if isinstance(m, dict) and m.get("id")]
                    if ids:
                        self.loop.create_task(self._ack(ids))
        except asyncio.CancelledError:
            raise
        except (asyncio.IncompleteReadError, ConnectionResetError, OSError):
            self.closed = True
        except Exception:
            self.closed = True

    async def _sweeper(self):
        """回收超时未回包的 tag，否则丢一条消息就会把在飞额度永久占死。"""
        try:
            while not self.closed:
                await asyncio.sleep(0.25)
                now = time.perf_counter()
                for tag, (mid, t0, fut, credit, want) in list(self.pending.items()):
                    if now - t0 <= self.timeout:
                        continue
                    self.pending.pop(tag, None)
                    if credit and self.sem is not None:
                        self.sem.release()
                    if fut is not None:
                        if not fut.done():
                            fut.set_exception(asyncio.TimeoutError())
                    else:
                        self.timeouts += 1
        except asyncio.CancelledError:
            raise

    async def _ack(self, ids):
        try:
            self.acks += 1
            await self.call_once(MID["ack"], {"ids": ids}, timeout=3.0)
        except Exception:
            pass

    async def call_once(self, mid: int, obj: dict, timeout: float = None):
        """发一条并等这条的回包（靠 msgid 关联，忽略期间到达的推送）。"""
        timeout = self.timeout if timeout is None else timeout
        tag = self._next_tag()
        o = dict(obj)
        o["msgid"] = tag
        data = json.dumps(o, ensure_ascii=False, separators=(",", ":")).encode()
        fut = self.loop.create_future()
        self.pending[tag] = (mid, time.perf_counter(), fut, False, WANT_OBJ)
        try:
            self.writer.write(frame(mid, data))
            await self.writer.drain()
            return await asyncio.wait_for(fut, timeout)
        finally:
            self.pending.pop(tag, None)

    def fire_nowait(self, plan: Plan) -> bool:
        """不等额度地发一条（突发阶段用）。"""
        return self._write(plan, credit=False)

    def fire_timed(self, plan: Plan):
        """突发用：发一条并返回一个"回包到达时给出 RTT(ms)" 的 future。

        不能用 sleep 轮询等回包 —— Windows 的定时器粒度是 15.6ms，
        轮询会把 1ms 的等待量成 15.6ms，突发耗时全错。
        """
        tag = self._next_tag()
        fut = self.loop.create_future()
        self.pending[tag] = (plan.mid, time.perf_counter(), fut, False, WANT_RTT)
        try:
            self.writer.write(plan.build(tag))
        except Exception:
            self.pending.pop(tag, None)
            self.fail += 1
            return None
        self.sent += 1
        return fut

    async def fire(self, plan: Plan) -> bool:
        """流水线发一条；在飞数已达上限就等回包腾出额度。"""
        await self.sem.acquire()
        if self.closed or self.writer is None:
            self.sem.release()
            self.fail += 1
            return False
        return self._write(plan, credit=True)

    def _write(self, plan: Plan, credit: bool) -> bool:
        tag = self._next_tag()
        self.pending[tag] = (plan.mid, time.perf_counter(), None, credit, None)
        try:
            self.writer.write(plan.build(tag))
        except Exception:
            self.pending.pop(tag, None)
            if credit:
                self.sem.release()
            self.fail += 1
            return False
        self.sent += 1
        return True

    async def login(self, tok: dict, timeout: float = None):
        rsp = await self.call_once(MID["login"],
                                   {"uid": str(tok["uid"]), "token": tok["token"]},
                                   timeout)
        if rsp.get("error") not in (0, None):
            raise RuntimeError(f"TCP 登录失败 uid={tok['uid']}: {rsp}")
        self.uid = str(tok["uid"])
        return rsp


def merge_clients(clients) -> dict:
    per, push, biz = {}, Counter(), Counter()
    timeouts = fail = sent = acks = 0
    for c in clients:
        for mid, h in c.per_mid.items():
            tgt = per.get(mid)
            if tgt is None:
                tgt = per[mid] = Hist()
            tgt.merge(h)
        push.update(c.push)
        biz.update(c.biz)
        timeouts += c.timeouts
        fail += c.fail
        sent += c.sent
        acks += c.acks
    return {"per_mid": per, "push": push, "biz": biz, "timeouts": timeouts,
            "fail": fail, "sent": sent, "acks": acks, "clients": len(clients)}


def merge_dicts(dicts) -> dict:
    per = {}
    push, biz = Counter(), Counter()
    tot = {"timeouts": 0, "fail": 0, "sent": 0, "acks": 0, "clients": 0,
           "wall": 0.0, "cpu": 0.0, "error": None}
    for d in dicts:
        if not d:
            continue
        if d.get("error"):
            tot["error"] = d["error"]
            continue
        for mid_s, hd in d["per_mid"].items():
            mid = int(mid_s)
            tgt = per.get(mid)
            if tgt is None:
                tgt = per[mid] = Hist()
            tgt.merge_dict(hd)
        push.update({int(k): v for k, v in d["push"].items()})
        biz.update({int(k): v for k, v in d["biz"].items()})
        for k in ("timeouts", "fail", "sent", "acks", "clients"):
            tot[k] += d[k]
        tot["wall"] = max(tot["wall"], d["wall"])
        tot["cpu"] += d["cpu"]
    tot["per_mid"] = per
    tot["push"] = push
    tot["biz"] = biz
    return tot


def client_rec(clients, wall, cpu) -> dict:
    rec = merge_clients(clients)
    rec["per_mid"] = {mid: h.as_dict() for mid, h in rec["per_mid"].items()}
    rec["push"] = dict(rec["push"])
    rec["biz"] = dict(rec["biz"])
    rec["wall"] = wall
    rec["cpu"] = cpu
    return rec


def headline(per_mid: dict, skip=(MID["ack"],)) -> Hist:
    """所有请求的合并直方图（默认排除自动 ACK）。"""
    h = Hist()
    for mid, hh in per_mid.items():
        if mid in skip:
            continue
        h.merge(hh if isinstance(hh, Hist) else Hist().merge_dict(hh))
    return h


def n_resp(per_mid: dict, skip=(MID["ack"],)) -> int:
    return sum(h.n for mid, h in per_mid.items() if mid not in skip)


def push_str(push) -> str:
    if not push:
        return "无"
    return ", ".join(f"{k}×{v}" for k, v in sorted(push.items()))


# ──────────────────────────────────────────────────────────────
#  账号 / token / 好友探测
# ──────────────────────────────────────────────────────────────
def http_login(uid: str, gate: str, password: str) -> dict:
    body = json.dumps({"user": uid, "password": password}).encode()
    req = urllib.request.Request(gate + "/user_login", data=body,
                                 headers={"Content-Type": "application/json"})
    with OPENER.open(req, timeout=15) as r:
        data = json.loads(r.read().decode())
    if data.get("error") != 0:
        raise RuntimeError(f"HTTP 登录失败 uid={uid}: {data}")
    return data


def prepare_tokens(args):
    tokens = []
    if args.no_gate:
        for uid in args.uids:
            tokens.append({"uid": uid, "token": "bench-dummy"})
        print(f"  [warn] --no-gate：跳过 GateServer，直接用假 token（只能对 mock 服务端用）")
        return tokens
    t0 = time.perf_counter()
    for uid in args.uids:
        try:
            tokens.append(http_login(uid, args.gate, args.password))
        except Exception as e:
            print(f"  [warn] 账号 {uid} 取 token 失败: {e}")
    if tokens:
        print(f"账号准备完成：{len(tokens)} 个 ({(time.perf_counter() - t0) * 1000:.0f} ms)")
    return tokens


async def probe_friends(host, port, tokens, timeout=6.0):
    """每账号登录一次查 1020，返回 {uid: [好友 uid]}，用于决定能不能压 1013。"""
    friends = {}
    for tok in tokens:
        c = Client(host, port, timeout=timeout)
        try:
            await c.connect(1)
            await c.login(tok)
            rsp = await c.call_once(MID["friendlist"], {}, timeout)
            lst = []
            for f in (rsp.get("friends") or []):
                if isinstance(f, dict) and f.get("uid"):
                    lst.append(str(f["uid"]))
            friends[str(tok["uid"])] = lst
        except Exception as e:
            print(f"  [warn] 探测好友失败 uid={tok['uid']}: {e}")
            friends[str(tok["uid"])] = []
        finally:
            await c.close()
    return friends


# ──────────────────────────────────────────────────────────────
#  阶段 1：延迟基线
# ──────────────────────────────────────────────────────────────
async def stage_latency(host, port, tokens, rounds, friends, with_sendmsg):
    print()
    print("=" * 96)
    print(f"阶段 1 · 延迟基线（单连接串行，每项 {rounds} 次）")
    print("=" * 96)

    c = Client(host, port)
    await c.connect(1)
    t0 = time.perf_counter()
    await c.login(tokens[0] if len(tokens) < 2 else tokens[1])
    login_ms = (time.perf_counter() - t0) * 1000
    print(f"首次登录（含 Redis 注册/好友集加载/资料查询）: {login_ms:.2f} ms")

    peer = tokens[0]["uid"] if len(tokens) < 2 else tokens[1]["uid"]
    cases = [
        (MID["heartbeat"], {}, "1016 心跳"),
        (MID["online"], {"uid": peer}, "1018 在线查询"),
        (MID["friendlist"], {}, "1020 好友列表"),
        (MID["pull"], {"count": "10"}, "1023 拉取消息"),
        (MID["search"], {"uid": peer}, "1003 查用户"),
    ]
    my_friend = pick_friend(c.uid, friends)
    if with_sendmsg and my_friend:
        cases.append((MID["sendmsg"], {"to_uid": my_friend, "type": "1",
                                       "body": "bench msg"}, "1013 发消息"))

    rows, results = [], {}
    for mid, body, name in cases:
        h = Hist()
        biz = errs = 0
        for _ in range(rounds):
            s = time.perf_counter()
            try:
                rsp = await c.call_once(mid, body, timeout=5.0)
            except Exception:
                errs += 1
                continue
            ms = (time.perf_counter() - s) * 1000
            h.add(ms)
            if isinstance(rsp, dict) and rsp.get("error") not in (0, None):
                biz += 1
        results[name] = {"n": h.n, "err": errs, "biz": biz,
                         "p50": h.pct(.5), "p99": h.pct(.99)}
        rows.append(f"  {name:<16} {fmt_hist(h)}  传输失败={errs}  业务错误={biz}")

    print_table("各消息 RTT", rows)
    print(f"\n  非预期推送（压测期间收到）: {push_str(c.push)}")
    await c.close()
    return results


# ──────────────────────────────────────────────────────────────
#  阶段 2：并发登录
# ──────────────────────────────────────────────────────────────
async def stage_login(host, port, tokens, levels, timeout):
    print()
    print("=" * 96)
    print("阶段 2 · 并发登录（N 条连接同时建连并发 1001）")
    print("=" * 96)

    rows, results = [], []
    for n in levels:
        made = []

        async def one(i):
            c = Client(host, port, timeout=timeout)
            await c.connect(1)
            s = time.perf_counter()
            try:
                await c.login(tokens[i % len(tokens)])
                ms = (time.perf_counter() - s) * 1000
                made.append(c)
                return ms
            except Exception:
                await c.close()
                raise

        t0 = time.perf_counter()
        res = await asyncio.gather(*[one(i) for i in range(n)], return_exceptions=True)
        wall = (time.perf_counter() - t0) * 1000
        lat = [r for r in res if not isinstance(r, Exception)]
        errs = [repr(r) for r in res if isinstance(r, Exception)]
        rate = n / (wall / 1000) if wall else 0
        rows.append(f"  N={n:<4} 总耗时={wall:8.1f} ms  成功={len(lat):<4}/{n}  "
                    f"登录p50={pct(lat, .5) if lat else 0:7.2f}  "
                    f"p99={pct(lat, .99) if lat else 0:7.2f}  "
                    f"max={max(lat) if lat else 0:7.2f} ms  速率={rate:8.1f} login/s")
        if errs:
            rows.append(f"        失败样例: {errs[0][:120]}")
        results.append({"n": n, "wall_ms": wall, "ok": len(lat), "rate": rate,
                        "p50": pct(lat, .5) if lat else 0.0})
        for c in made:
            await c.close()
        await asyncio.sleep(0.3)

    print_table("并发登录（小样本：每档只有 N 个请求，速率仅用于看量级）", rows)
    return results


# ──────────────────────────────────────────────────────────────
#  连接池构建 / 泵
# ──────────────────────────────────────────────────────────────
async def build_clients(host, port, tokens, n, inflight, timeout, mixer_args):
    clients, failed = [], 0
    for i in range(n):
        c = Client(host, port, timeout=timeout)
        try:
            await c.connect(inflight)
            await c.login(tokens[i % len(tokens)])
        except Exception:
            failed += 1
            await c.close()
            continue
        c.mixer = build_mixer(*mixer_args(c.uid))
        clients.append(c)
    if failed:
        print(f"  [warn] {failed}/{n} 条连接建连或登录失败，已跳过")
    return clients


async def pump_closed_loop(c: Client, deadline: float, drain_every=32):
    n = 0
    while not c.closed:
        if time.perf_counter() >= deadline:
            break
        if not await c.fire(c.mixer.pick()):
            break
        n += 1
        if n % drain_every == 0:
            try:
                await c.writer.drain()
            except Exception:
                break
    return n


async def pump_open_loop(c: Client, rate: float, end_t: float):
    """按目标速率发；Windows 定时器粒度约 15.6ms，用"按已过时间补发"抵消。"""
    t_start = time.perf_counter()
    sent = 0
    while not c.closed:
        now = time.perf_counter()
        if now >= end_t:
            break
        due = int((now - t_start) * rate) - sent
        for _ in range(max(0, due)):
            if not await c.fire(c.mixer.pick()):
                return sent
            sent += 1
        await asyncio.sleep(0.001)
    return sent


async def wait_drain(clients, timeout=4.0):
    end = time.perf_counter() + timeout
    while time.perf_counter() < end:
        if not any(c.pending for c in clients):
            return True
        await asyncio.sleep(0.01)
    return False


# ──────────────────────────────────────────────────────────────
#  子进程 worker（--procs 用）
# ──────────────────────────────────────────────────────────────
async def _worker(cfg):
    tokens = cfg["tokens"]
    n = cfg["nconns"]
    peer = cfg["peer"]

    def mixer_args(uid):
        return (cfg["mix"], uid, peer, cfg["friends"])

    clients = await build_clients(cfg["host"], cfg["port"], tokens, n,
                                  cfg["inflight"], cfg["timeout"], mixer_args)
    if not clients:
        return {"error": "no clients"}
    cpu0 = time.process_time()
    t0 = time.perf_counter()
    if cfg["mode"] == "rate":
        end_t = t0 + cfg["duration"]
        per = cfg["rate"] / max(1, len(clients))
        await asyncio.gather(*[pump_open_loop(c, per, end_t) for c in clients])
    else:
        deadline = t0 + cfg["duration"]
        await asyncio.gather(*[pump_closed_loop(c, deadline) for c in clients])
    await wait_drain(clients)
    wall = time.perf_counter() - t0
    cpu = time.process_time() - cpu0
    rec = client_rec(clients, wall, cpu)
    for c in clients:
        await c.close()
    return rec


def _proc_main(cfg, path):
    bump_timer()          # 定时器粒度是 per-process 的，子进程要自己调
    try:
        rec = asyncio.run(_worker(cfg))
    except Exception as e:  # 子进程异常要带回父进程，否则父进程只能干等
        rec = {"error": repr(e)}
    try:
        with open(path, "w", encoding="utf-8") as f:
            json.dump(rec, f, ensure_ascii=False)
    except Exception:
        pass


def run_parallel(cfgs):
    """每个 cfg 起一个子进程跑 _worker，回收它们的统计。

    子进程用 spawn 启动（Windows 上没有 fork），所以 cfg 必须是可 pickle 的纯数据
    —— tokens / friends / mix 都是 dict/list/tuple，没问题。
    结果不用 multiprocessing.Queue 回传：Windows 的 Queue 走命名管道，
    在沙箱/受限权限下 CreateFile 会直接 WinError 5，落临时文件更稳。
    """
    ctx = get_context("spawn")
    jobs = []
    for cfg in cfgs:
        fd, path = tempfile.mkstemp(prefix="bench-shard-", suffix=".json")
        os.close(fd)
        jobs.append((cfg, path))
    running = []
    for cfg, path in jobs:
        p = ctx.Process(target=_proc_main, args=(cfg, path), daemon=True)
        p.start()
        running.append((p, path))
    end = time.time() + max(c["duration"] for c in cfgs) + 180
    recs = []
    for p, path in running:
        left = max(1.0, end - time.time())
        p.join(timeout=left)
        if p.is_alive():
            p.terminate()
            p.join(timeout=5)
        rec = None
        if os.path.exists(path):
            try:
                with open(path, encoding="utf-8") as f:
                    rec = json.load(f)
            except Exception:
                rec = None
            try:
                os.unlink(path)
            except Exception:
                pass
        recs.append(rec if rec is not None else
                    {"error": f"子进程没有回传结果（exit={p.exitcode}）"})
    return recs


def split_int(total: int, parts: int):
    """把 total 尽可能平均地分成 parts 份（余数给前面的）。"""
    parts = max(1, parts)
    base, rem = divmod(max(0, total), parts)
    return [base + (1 if i < rem else 0) for i in range(parts)]


def procs_cfgs(base, mode, nconns, rate=None, procs=1):
    cfgs = []
    conn_split = split_int(nconns, procs)
    rate_split = split_int(rate, procs) if rate else [0] * procs
    for i in range(procs):
        cfg = dict(base)
        cfg["mode"] = mode
        cfg["nconns"] = max(1, conn_split[i])
        if rate:
            cfg["rate"] = rate_split[i]
        cfgs.append(cfg)
    return cfgs



# ──────────────────────────────────────────────────────────────
#  阶段 3：闭路流水线稳态
# ──────────────────────────────────────────────────────────────
async def stage_qps(host, port, tokens, args, friends, peer):
    print()
    print("=" * 96)
    print(f"阶段 3 · 稳态吞吐（闭路 + 流水线，每连接在飞 K={args.inflight}，"
          f"每档 {args.duration}s）")
    print(f"  混合比例: {', '.join(f'{k}={v}' for k, v in args.mix_spec)}")
    print("=" * 96)

    def mixer_args(uid):
        return (args.mix_spec, uid, peer, friends)

    rows, results = [], []
    base = {"host": host, "port": port, "tokens": tokens,
            "inflight": args.inflight, "timeout": args.timeout,
            "duration": args.duration, "mix": args.mix_spec,
            "friends": friends, "peer": peer}
    for n in args.conns:
        if args.procs > 1:
            recs = run_parallel(procs_cfgs(base, "closed", n, procs=args.procs))
            merged = merge_dicts(recs)
            per_mid = merged["per_mid"]
            wall = merged["wall"] or args.duration
            resp = n_resp(per_mid)
            qps = resp / wall if wall else 0
            cpu = merged["cpu"] / wall * 100 if wall else 0
            timeouts, fail = merged["timeouts"], merged["fail"]
            nclients = merged["clients"]
            push = merged["push"]
            acks = merged["acks"]
            if merged.get("error"):
                print(f"  [warn] 子进程异常（N={n}）: {merged['error']}")
        else:
            clients = await build_clients(host, port, tokens, n, args.inflight,
                                          args.timeout, mixer_args)
            if not clients:
                rows.append(f"  N={n:<4} 连接全部失败，跳过")
                continue
            cpu0 = time.process_time()
            t0 = time.perf_counter()
            await asyncio.gather(*[pump_closed_loop(c, t0 + args.duration)
                                   for c in clients])
            await wait_drain(clients)
            wall = time.perf_counter() - t0
            cpu = (time.process_time() - cpu0) / wall * 100 if wall else 0
            rec = merge_clients(clients)
            per_mid = rec["per_mid"]
            resp = n_resp(per_mid)
            qps = resp / wall if wall else 0
            timeouts, fail = rec["timeouts"], rec["fail"]
            nclients = len(clients)
            push = rec["push"]
            acks = rec["acks"]
            for c in clients:
                await c.close()

        h = headline(per_mid)
        rows.append(f"  N={n:<4} QPS={qps:9.1f}  完成={resp:<8} 连接={nclients:<4} "
                    f"{fmt_hist(h)}  超时={timeouts}  失败={fail}  客户端CPU={cpu:5.1f}%")
        results.append({"n": n, "qps": qps, "resp": resp, "wall": wall,
                        "p50": h.pct(.5), "p99": h.pct(.99), "timeouts": timeouts,
                        "fail": fail, "cpu": cpu, "acks": acks,
                        "per_mid": {int(k): {"n": v.n, "p50": v.pct(.5),
                                             "p99": v.pct(.99)}
                                    for k, v in per_mid.items()},
                        "push": {int(k): v for k, v in (push or {}).items()}})
        await asyncio.sleep(0.4)

    print_table("闭路吞吐", rows)
    if results:
        peak = max(results, key=lambda r: r["qps"])
        det = [(LABEL.get(int(mid), str(mid)), d)
               for mid, d in sorted(peak["per_mid"].items())]
        if det:
            print(f"\n  峰值档（N={peak['n']}，QPS={peak['qps']:.1f}）按消息类型拆分：")
            for name, d in det:
                print(f"    {name:<14} n={d['n']:<7} p50≈{d['p50']:7.2f}  "
                      f"p99≈{d['p99']:7.2f} ms")
            if peak.get("acks"):
                print(f"    （另有客户端自动发出的 1025 ACK {peak['acks']} 条，未计入吞吐）")
            if peak.get("push"):
                print(f"    服务端主动推送：{push_str(peak['push'])}（未计入吞吐）")
    return results


# ──────────────────────────────────────────────────────────────
#  阶段 4：开环定速
# ──────────────────────────────────────────────────────────────
async def stage_rate(host, port, tokens, args, friends, peer):
    print()
    print("=" * 96)
    print(f"阶段 4 · 开环定速（目标速率 → 实际达成，每档 {args.rate_duration}s）")
    print("  说明：开环不靠回包节流，能区分「服务器扛不住」和「客户端发不出来」")
    print("=" * 96)

    def mixer_args(uid):
        return (args.mix_spec, uid, peer, friends)

    rows, results = [], []
    for target in args.rates:
        if args.procs > 1:
            base = {"host": host, "port": port, "tokens": tokens,
                    "inflight": args.inflight, "timeout": args.timeout,
                    "duration": args.rate_duration, "mix": args.mix_spec,
                    "friends": friends, "peer": peer}
            recs = run_parallel(procs_cfgs(base, "rate", args.rate_conns,
                                           rate=target, procs=args.procs))
            merged = merge_dicts(recs)
            per_mid = merged["per_mid"]
            wall = merged["wall"] or args.rate_duration
            resp = n_resp(per_mid)
            cpu = merged["cpu"] / wall * 100 if wall else 0
            timeouts, fail = merged["timeouts"], merged["fail"]
            sent = merged["sent"]
        else:
            clients = await build_clients(host, port, tokens, args.rate_conns,
                                          args.inflight, args.timeout, mixer_args)
            if not clients:
                rows.append(f"  目标={target:<7} 连接全部失败，跳过")
                continue
            cpu0 = time.process_time()
            t0 = time.perf_counter()
            per = target / max(1, len(clients))
            await asyncio.gather(*[pump_open_loop(c, per, t0 + args.rate_duration)
                                   for c in clients])
            await wait_drain(clients)
            wall = time.perf_counter() - t0
            cpu = (time.process_time() - cpu0) / wall * 100 if wall else 0
            rec = merge_clients(clients)
            per_mid = rec["per_mid"]
            resp = n_resp(per_mid)
            sent = rec["sent"]
            timeouts, fail = rec["timeouts"], rec["fail"]
            for c in clients:
                await c.close()

        achieved = resp / wall if wall else 0
        h = headline(per_mid)
        ratio = achieved / target if target else 0
        rows.append(f"  目标={target:<7} 实际={achieved:9.1f}/s  达成={ratio * 100:5.1f}%  "
                    f"完成={resp:<8} {fmt_hist(h)}  超时={timeouts}  失败={fail}  "
                    f"客户端CPU={cpu:5.1f}%")
        results.append({"target": target, "achieved": achieved, "ratio": ratio,
                        "resp": resp, "wall": wall, "p50": h.pct(.5),
                        "p99": h.pct(.99), "timeouts": timeouts, "fail": fail,
                        "cpu": cpu, "sent": sent})
        await asyncio.sleep(0.4)

    print_table("开环定速", rows)
    return results


# ──────────────────────────────────────────────────────────────
#  阶段 5：瞬时突发（真·齐步走）
# ──────────────────────────────────────────────────────────────
async def stage_burst(host, port, tokens, args):
    print()
    print("=" * 96)
    print(f"阶段 5 · 瞬时突发（N 条连接在同一时刻各发 1 条心跳，{args.burst_rounds} 轮取中位）")
    print("=" * 96)

    need = max(args.burst_n)
    pool = []
    for i in range(need):
        c = Client(host, port, timeout=args.timeout)
        try:
            await c.connect(64)
            await c.login(tokens[i % len(tokens)])
            pool.append(c)
        except Exception:
            await c.close()
    if not pool:
        print("  预建连接全部失败，跳过")
        return []

    rows, results = [], []
    for n in args.burst_n:
        conns = pool[:n]
        if len(conns) < n:
            rows.append(f"  N={n:<4} 连接不足（{len(conns)}），跳过")
            continue
        walls, oks, fails = [], 0, 0
        all_rtt = []
        for _ in range(args.burst_rounds):
            t0 = time.perf_counter()
            futs = []
            for c in conns:          # 同一个事件循环迭代里写完 N 帧，时间误差 < 1ms
                f = c.fire_timed(PLAN_HB)
                if f is not None:
                    futs.append(f)
            try:
                await asyncio.wait_for(asyncio.gather(*futs, return_exceptions=True), 5.0)
            except asyncio.TimeoutError:
                pass
            walls.append((time.perf_counter() - t0) * 1000)
            round_rtt = [f.result() for f in futs
                         if f.done() and not f.cancelled() and f.exception() is None]
            oks += len(round_rtt)
            fails += n - len(round_rtt)
            all_rtt.extend(round_rtt)
            await asyncio.sleep(0.3)  # 给服务端排空
        # 服务端把 N 条并排处理完的总时间：等于 N 条里最慢的那条的 RTT
        h = Hist()
        for x in all_rtt:
            h.add(x)
        rows.append(f"  N={n:<4} 完成中位={pct(walls, .5):7.1f} ms  "
                    f"最慢={max(walls):7.1f} ms  "
                    f"成功={oks // max(1, args.burst_rounds):<4}/{n}  "
                    f"{fmt_hist(h)}  失败={fails}")
        results.append({"n": n, "wall_ms": pct(walls, .5),
                        "p50": h.pct(.5), "p99": h.pct(.99),
                        "ok": oks, "fail": fails})
    for c in pool:
        await c.close()
    print_table("突发", rows)
    return results


# ──────────────────────────────────────────────────────────────
#  阶段 6：连接承载
# ──────────────────────────────────────────────────────────────
async def stage_conn(host, port, levels):
    print()
    print("=" * 96)
    print("阶段 6 · TCP 连接承载（并发建连，不登录）")
    print("=" * 96)

    rows, results = [], []

    async def one():
        try:
            r, w = await asyncio.wait_for(asyncio.open_connection(host, port), timeout=5)
            return (r, w)
        except Exception:
            return None

    for n in levels:
        t0 = time.perf_counter()
        res = await asyncio.gather(*[one() for _ in range(n)])
        elapsed = (time.perf_counter() - t0) * 1000
        conns = [x for x in res if x]
        rows.append(f"  N={n:<5} 成功={len(conns):<5} 失败={n - len(conns):<3} "
                    f"耗时={elapsed:8.1f} ms  速率={n / (elapsed / 1000) if elapsed else 0:8.0f} conn/s")
        results.append({"n": n, "ok": len(conns), "fail": n - len(conns), "ms": elapsed})
        for _, w in conns:
            try:
                w.close()
            except Exception:
                pass
        await asyncio.sleep(0.6)

    print_table("连接承载", rows)
    return results


# ──────────────────────────────────────────────────────────────
#  结论解读
# ──────────────────────────────────────────────────────────────
def interpret(qps_res, rate_res):
    print()
    print("=" * 96)
    print("结论")
    print("=" * 96)
    pts = []
    if qps_res:
        peak = max(qps_res, key=lambda r: r["qps"])
        if peak["qps"] <= 0:
            pts.append("闭路阶段一条回包都没拿到 —— 先看上面的失败/超时列和服务端日志，"
                       "别急着读吞吐")
            print("  " + pts[-1])
            print()
            return
        pts.append(f"闭路最高吞吐 {peak['qps']:.1f}/s（N={peak['n']}），"
                   f"对应 p50 {peak['p50']:.2f} ms、p99 {peak['p99']:.2f} ms，"
                   f"1/QPS ≈ {1000 / peak['qps']:.3f} ms/条（每条消息占用的串行时间）")
        if len(qps_res) >= 2:
            a, b = qps_res[-2], qps_res[-1]
            if b["qps"] > 0 and a["qps"] > 0:
                grow = b["qps"] / a["qps"] - 1
                lat = b["p50"] / a["p50"] if a["p50"] else 0
                if grow < 0.15 and lat > 1.4:
                    gp = grow * 100
                    gp = 0.0 if abs(gp) < 0.5 else gp
                    pts.append(f"  → N {a['n']}→{b['n']}：吞吐只涨 {gp:+.1f}% 而 p50 涨 "
                               f"{lat:.1f}×，说明服务端已有单个串行资源，再加并发只是排队")
        cmax = max(r["cpu"] for r in qps_res)
        if cmax >= 70:
            pts.append(f"  → 客户端 CPU 已到 {cmax:.0f}%：脚本自己快发不动了，"
                       f"加 --procs N 或换 Go/Rust 压测器再测")
        if any(r["timeouts"] or r["fail"] for r in qps_res):
            pts.append("  → 出现超时/失败：可能是服务端消息队列被填满后丢包，"
                       "或连接被服务端关闭，看服务端 warn 日志里的 \"message queue is full\"")
    if rate_res:
        bad = [r for r in rate_res if r["ratio"] < 0.95]
        if bad:
            r = bad[0]
            pts.append(f"开环目标 {r['target']}/s 只达成 {r['ratio'] * 100:.0f}%："
                       f"这个速率已经超过服务端（或客户端）能力，实际容量见上一行")
        else:
            top = max(rate_res, key=lambda r: r["target"])
            pts.append(f"开环 {top['target']}/s 目标全部达成，容量还有余量")
        cpu_top = max(r["cpu"] for r in rate_res)
        if cpu_top >= 70:
            pts.append(f"  → 开环阶段客户端 CPU {cpu_top:.0f}%，先确认不是脚本瓶颈")
    if not pts:
        pts.append("（未选择 qps / rate 阶段，无吞吐结论）")
    for p in pts:
        print("  " + p)
    print()


# ──────────────────────────────────────────────────────────────
#  入口
# ──────────────────────────────────────────────────────────────
def parse_mix(spec: str):
    """heartbeat=60,sendmsg=10 → [("heartbeat",60),("sendmsg",10)]"""
    out = []
    if not spec:
        return out
    for item in spec.split(","):
        item = item.strip()
        if not item:
            continue
        if "=" in item:
            k, v = item.split("=", 1)
            out.append((k.strip(), int(v)))
        else:
            out.append((item, 1))
    return out


def ints(s):
    return [int(x) for x in s.split(",") if x.strip()]


async def amain(args, tokens, friends, peer):
    out = {"target": f"{args.host}:{args.port}", "when": time.strftime("%F %T"),
           "inflight": args.inflight, "mix": args.mix_spec, "procs": args.procs}
    stages = args.stages or DEFAULT_STAGES

    if "latency" in stages:
        out["latency"] = await stage_latency(args.host, args.port, tokens,
                                             args.rounds, friends, args.with_sendmsg)
    if "login" in stages:
        out["login"] = await stage_login(args.host, args.port, tokens,
                                         [1, 2, 4, 8, 16, 32, 64], args.timeout)
    if "qps" in stages:
        out["qps"] = await stage_qps(args.host, args.port, tokens, args, friends, peer)
    if "rate" in stages:
        out["rate"] = await stage_rate(args.host, args.port, tokens, args, friends, peer)
    if "burst" in stages:
        out["burst"] = await stage_burst(args.host, args.port, tokens, args)
    if "conn" in stages:
        out["conn"] = await stage_conn(args.host, args.port, [64, 256, 512, 1024])

    interpret(out.get("qps") or [], out.get("rate") or [])
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="ChatServer TCP 压测 v2（流水线 / 开环 / 多进程 / 混合业务）")
    ap.add_argument("stages", nargs="*", default=None,
                    help="latency login qps rate burst conn（默认全部）")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8090)
    ap.add_argument("--gate", default=GATE, help="GateServer 地址（取 token 用）")
    ap.add_argument("--password", default=PASSWORD)
    ap.add_argument("--uids", default=",".join(UIDS), help="逗号分隔的测试账号")
    ap.add_argument("--no-gate", action="store_true",
                    help="跳过 GateServer（配 mock 服务端用，token 为假值）")
    ap.add_argument("--timeout", type=float, default=5.0, help="单请求超时秒数")

    ap.add_argument("--rounds", type=int, default=300, help="延迟阶段重复次数")
    ap.add_argument("--duration", type=float, default=8.0, help="qps 每档秒数")
    ap.add_argument("--conns", default="1,4,16,32,64", help="qps 阶段的并发连接数列表")
    ap.add_argument("--inflight", type=int, default=4,
                    help="每条连接同时挂起的请求数 K（1 = 退回 v1 的串行行为）")
    ap.add_argument("--procs", type=int, default=1, help="qps / rate 用几个进程跑")
    ap.add_argument("--mix", default=None,
                    help="混合比例，如 heartbeat=60,sendmsg=10,friendlist=15,online=10,pull=5；"
                         "默认 heartbeat=70,friendlist=15,online=10,pull=5")
    ap.add_argument("--rates", default="8000,16000,24000", help="rate 阶段的目标速率列表")
    ap.add_argument("--rate-duration", type=float, default=10.0)
    ap.add_argument("--rate-conns", type=int, default=32, help="rate 阶段的连接数")
    ap.add_argument("--burst-n", default="4,8,16,32,64")
    ap.add_argument("--burst-rounds", type=int, default=3)
    ap.add_argument("--with-sendmsg", action="store_true",
                    help="延迟基线里也测 1013（会给好友留离线消息，默认不测）")
    ap.add_argument("--seed", type=int, default=20261022)
    ap.add_argument("--json", default=None, help="结果落盘路径")
    args = ap.parse_args(argv)

    args.uids = [u.strip() for u in args.uids.split(",") if u.strip()]
    args.conns = ints(args.conns)
    args.rates = ints(args.rates)
    args.burst_n = ints(args.burst_n)
    random.seed(args.seed)
    bump_timer()

    spec = parse_mix(args.mix) if args.mix else None
    if not spec:
        spec = [("heartbeat", 70), ("friendlist", 15), ("online", 10), ("pull", 5)]
    args.mix_spec = spec

    print("=" * 96)
    print(f"ChatServer 压测 v2   目标 {args.host}:{args.port}   {time.strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"  在飞 K={args.inflight}  进程={args.procs}  连接阶梯={args.conns}")
    print("=" * 96)

    tokens = prepare_tokens(args)
    if not tokens:
        print("没有任何可用账号，退出")
        return 1
    peer = tokens[-1]["uid"]
    for t in tokens:
        if str(t["uid"]) != str(tokens[0]["uid"]):
            peer = t["uid"]
            break

    friends = {}
    if any(s in (args.stages or DEFAULT_STAGES) for s in ("qps", "rate")) or args.with_sendmsg:
        try:
            friends = asyncio.run(probe_friends(args.host, args.port, tokens,
                                                max(2.0, args.timeout)))
            n_friend = sum(len(v) for v in friends.values())
            print(f"好友关系探测：{n_friend} 条（决定了 1013 能不能压；为 0 时混合比例里会自动跳过）")
            if n_friend == 0 and any(lbl == "sendmsg" for lbl, _ in spec):
                print("  [warn] 测试账号之间没有好友关系，本次跳过 1013；"
                      "先让两个账号互加好友再跑")
        except Exception as e:
            print(f"  [warn] 好友探测失败（继续跑，只是不发 1013）: {e}")

    try:
        out = asyncio.run(amain(args, tokens, friends, str(peer)))
    finally:
        restore_timer()      # 把系统定时器粒度还回去，别影响同机其它程序

    if args.json:
        path = args.json
        with open(path, "w", encoding="utf-8") as f:
            json.dump(out, f, ensure_ascii=False, indent=2)
        print(f"结果已写入 {path}")
    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
