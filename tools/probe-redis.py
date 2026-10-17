#!/usr/bin/env python3
"""直接问 Redis 要「在线 key」的 TTL，用来证明心跳真的在续期。

    python tools/probe-redis.py uip_666672 [更多 key...]

背景：服务端把在线标记写成 `uip_<uid>`（值 = 用户所在 ChatServer 名，
TTL = HEART_BEAT_EXPIRE_SECONDS = 30s），登录时写一次，之后靠客户端每 10s 的心跳续命。

判读方法：客户端只跑 15 秒、退出后立刻读 TTL ——
  · TTL ≈ 25 → t=10s 那次心跳把键重新点满了（登录写的那个此刻只该剩 ≈15）
  · TTL ≈ 15 → 心跳没生效
  · 键不存在 → 登录那次写的也已过期，说明完全没有续期
"""
import socket
import sys

HOST, PORT, PASSWORD = "127.0.0.1", 6380, "123456"


def cmd(sock, *args):
    payload = f"*{len(args)}\r\n".encode()
    for a in args:
        raw = a.encode()
        payload += f"${len(raw)}\r\n".encode() + raw + b"\r\n"
    sock.sendall(payload)
    return read_reply(sock)


def read_reply(sock):
    line = b""
    while not line.endswith(b"\r\n"):
        chunk = sock.recv(1)
        if not chunk:
            return None
        line += chunk
    kind, rest = line[:1], line[1:-2]
    if kind == b"+":
        return rest.decode()
    if kind == b"-":
        return f"ERR {rest.decode()}"
    if kind == b":":
        return int(rest)
    if kind == b"$":
        length = int(rest)
        if length == -1:
            return None
        data = b""
        while len(data) < length + 2:
            data += sock.recv(length + 2 - len(data))
        return data[:-2].decode()
    return line.decode()


if __name__ == "__main__":
    keys = sys.argv[1:] or ["uip_666672"]
    sock = socket.create_connection((HOST, PORT), timeout=3)
    auth = cmd(sock, "AUTH", PASSWORD)
    if auth != "OK":
        print("AUTH 失败:", auth)
    for key in keys:
        ttl = cmd(sock, "TTL", key)
        if ttl == -2:
            print(f"  {key}: 不存在（键已过期或从未写入）")
        else:
            print(f"  {key}: TTL={ttl}s value={cmd(sock, 'GET', key)!r}")
    sock.close()
