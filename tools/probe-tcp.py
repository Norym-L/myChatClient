#!/usr/bin/env python3
"""裸 TCP 探测 ChatServer 的帧层行为（不依赖客户端，用来给协议定性）。

用途：当服务端是"会动的那一端"时，先用裸帧把「服务端到底怎么回」钉死，
再回头改客户端 —— 比在客户端里加日志猜要快得多。

    python tools/probe-tcp.py <port> [uid]

不带 uid 时只测心跳帧的两种 body 形态：
    [A] body_len = 0   —— 心跳的规格就是"不带 message_data"
    [B] body = "{}"    —— 备选形态
两者服务端都应回 HeartBeatRsp（未登录时会带 error=1011 且**不关连接**，
所以不用登录就能验帧路径）。

带 uid 时再发一条「查在线」并打印**回包的真实 message_id**，
用来核对服务端 `checkIsUserOnlineHandler` 里 send(...) 用的 messageType 对不对
（这里曾踩过：写成了 MESSAGE_ADD_FRIEND_RSP=1006，期望是 1019）。
"""
import socket
import struct
import sys

HOST = "127.0.0.1"
HEART_BEAT_REQ = 1016
HEART_BEAT_RSP = 1017
CHECK_ONLINE_REQ = 1018
CHECK_ONLINE_RSP = 1019


def recv_frame(sock, timeout=3.0):
    sock.settimeout(timeout)
    head = b""
    while len(head) < 4:
        chunk = sock.recv(4 - len(head))
        if not chunk:
            return None, None, "对端关闭了连接（读到 EOF）"
        head += chunk
    message_id, body_len = struct.unpack(">HH", head)
    body = b""
    while len(body) < body_len:
        chunk = sock.recv(body_len - len(body))
        if not chunk:
            return message_id, None, "body 读到一半对端关闭"
        body += chunk
    return message_id, body.decode("utf-8", "replace"), None


def probe_heartbeat(port):
    for label, body in (("A", b""), ("B", b"{}")):
        fmt = "body_len=0" if not body else 'body="{}"'
        sock = socket.create_connection((HOST, port), timeout=3)
        sock.sendall(struct.pack(">HH", HEART_BEAT_REQ, len(body)) + body)
        try:
            mid, payload, err = recv_frame(sock)
            if err:
                print(f"  [{label}] {fmt} -> 失败：{err}")
            else:
                name = {HEART_BEAT_RSP: "HeartBeatRsp"}.get(mid, f"?({mid})")
                print(f"  [{label}] {fmt} -> 收到 {name} {payload}")
        except socket.timeout:
            print(f"  [{label}] {fmt} -> 超时，没有任何回包")
        finally:
            sock.close()


def probe_online(port, uid):
    sock = socket.create_connection((HOST, port), timeout=3)
    body = ('{"uid":"%s"}' % uid).encode()
    sock.sendall(struct.pack(">HH", CHECK_ONLINE_REQ, len(body)) + body)
    mid, payload, err = recv_frame(sock)
    sock.close()

    if err:
        print(f"  [查在线 uid={uid}] 失败：{err}")
        return
    flag = "OK" if mid == CHECK_ONLINE_RSP else f"⚠ 期望 {CHECK_ONLINE_RSP}"
    print(f"  [查在线 uid={uid}] 回包 message_id = {mid}  ({flag})")
    print(f"      body = {payload.strip()}")


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8090
    print(f"目标 {HOST}:{port}")
    probe_heartbeat(port)
    if len(sys.argv) > 2:
        probe_online(port, sys.argv[2])
