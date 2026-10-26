#!/usr/bin/env python3
"""聊天直推探针：用裸连接验 1047（消息调度器直推完整正文）。

背景（docs/ServerDocs/design-message-push.md）：2026-10-24 服务端把聊天消息
改由 MessageScheduler 推送 —— 接收方在线时直接下发 **1047**
（`group_id` / `from_uid` / `type` / `body` / `time`），不再推 1022、
也不再写 `offlinestream_<uid>`；离线时才落离线流，客户端上线后仍走 1023 拉取。

两种用法（互不干扰）：

    # ① 当接收方：挂着等推送，打印收到的每一帧（重点看 1047 的字段）
    python tools/probe-chatpush.py 666673 Test@12345 listen 30

    # ② 当发送方：给在线的那个人发一条（回 1014 只代表已交给调度器）
    python tools/probe-chatpush.py 666673 Test@12345 send 666672 "来自探针的消息"

典型端到端：

    # 终端 1：真客户端登录成 666672（收件人）
    MYCHAT_AUTOLOGIN="666672:Test@12345" QT_QPA_PLATFORM=offscreen \\
    MYCHAT_SMOKE=build/mingw-debug/smoke-push.png build/mingw-debug/qtClient.exe

    # 终端 2：探针以 666673 发一条 —— 客户端应立刻上屏，日志里出现
    #         [chat] 收到直推消息 ...（而不是「收到新消息通知 -> 发 1023」）
    python tools/probe-chatpush.py 666673 Test@12345 send 666672 "直推自检"

退出码：0 成功；2 参数错误；3 登录失败；4 服务端返回 error；5 等待超时
       （listen 模式下超时但收到过 1047 也算 0）。
"""
import json
import socket
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

NAMES = {
    CHAT_LOGIN_RSP: "ChatLoginRsp",
    TEXT_CHAT_MSG_RSP: "TextChatMsgRsp",
    SYNC_CHAT_MESSAGE_RSP: "SyncChatMessageRsp",
    ACK_CHAT_MESSAGE_RSP: "AckChatMessageRsp",
    NOTIFY_RECEIVE_MESSAGE: "NotifyReceiveMessage(1022)",
    CHAT_MESSAGE: "ChatMessage(1047)",
}


def recv_frame(sock, timeout=5.0):
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


def send_frame(sock, message_id, payload: dict):
    body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    sock.sendall(struct.pack(">HH", message_id, len(body)) + body)


def http_login(uid: str, password: str):
    data = json.dumps({"user": uid, "password": password}).encode("utf-8")
    request = urllib.request.Request(
        GATE + "/user_login", data=data,
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=5) as response:
        return json.loads(response.read().decode("utf-8"))


def login(uid: str, password: str):
    """HTTP 拿 token → 连聊天服登录。返回 (sock, uid)。"""
    result = http_login(uid, password)
    if result.get("error") != 0:
        print(f"[FAIL] /user_login 失败: {json.dumps(result, ensure_ascii=False)}")
        return None, None

    token = result.get("token", "")
    host = result.get("host") or "127.0.0.1"
    port = int(result.get("port") or 8090)

    sock = socket.create_connection((host, port), timeout=5)
    send_frame(sock, CHAT_LOGIN_REQ,
               {"uid": str(result.get("uid", uid)), "token": token})
    mid, payload, err = recv_frame(sock)
    if err:
        print(f"[FAIL] 聊天服登录失败: {err}")
        sock.close()
        return None, None
    print(f"[tcp] {NAMES.get(mid, mid)} {payload}")
    if mid != CHAT_LOGIN_RSP or json.loads(payload).get("error") != 0:
        sock.close()
        return None, None
    print(f"[OK] 已登录 uid = {result.get('uid', uid)} @ {host}:{port}")
    return sock, str(result.get("uid", uid))


def cmd_listen(sock, seconds: int):
    """挂着收推送：1047 是重点，1022 出现说明还在走旧链路。"""
    print(f"[listen] 等待 {seconds}s，收到的每一帧都会打印…")
    deadline = time.time() + seconds
    got_push = 0
    while True:
        left = deadline - time.time()
        if left <= 0:
            break
        try:
            mid, payload, err = recv_frame(sock, timeout=left)
        except socket.timeout:
            break
        if err:
            print(f"[listen] {err}")
            break
        if mid == CHAT_MESSAGE:
            got_push += 1
            data = json.loads(payload)
            print(f"[1047] group_id={data.get('group_id')!r} "
                  f"from={data.get('from_uid')!r} type={data.get('type')} "
                  f"body={data.get('body')!r} time={data.get('time')!r}")
        else:
            print(f"[{mid}] {NAMES.get(mid, '')} {payload}")

    if got_push:
        print(f"[OK] 收到 {got_push} 条 1047（直推链路生效）")
        return 0
    print("[WARN] 一条 1047 都没收到 —— 服务端可能还是旧链路（1022 + 拉取）")
    return 0


def cmd_send(sock, to_uid: str, text: str):
    send_frame(sock, TEXT_CHAT_MSG_REQ,
               {"msg_id": "probe-push-1", "to_uid": to_uid, "type": "1", "body": text})
    for _ in range(8):
        mid, payload, err = recv_frame(sock)
        if err:
            print(f"[FAIL] 等回包失败: {err}")
            return 5
        if mid != TEXT_CHAT_MSG_RSP:
            print(f"[push] 收到 {NAMES.get(mid, mid)} {payload}")
            continue
        print(f"[tcp] TextChatMsgRsp {payload}")
        return 0 if json.loads(payload).get("error") == 0 else 4
    print("[FAIL] 一直没等到 1014")
    return 5


def main() -> int:
    argv = sys.argv[1:]
    if len(argv) < 3:
        print(__doc__)
        print("用法: probe-chatpush.py <uid> <password> <listen [秒] | send <to_uid> <text>>")
        return 2

    uid, password, command = argv[0], argv[1], argv[2]
    sock, _ = login(uid, password)
    if sock is None:
        return 3

    try:
        if command == "listen":
            return cmd_listen(sock, int(argv[3]) if len(argv) > 3 else 20)
        if command == "send" and len(argv) >= 5:
            return cmd_send(sock, argv[3], argv[4])
        print(__doc__)
        return 2
    finally:
        sock.close()


if __name__ == "__main__":
    sys.exit(main())
