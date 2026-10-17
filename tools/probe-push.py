#!/usr/bin/env python3
"""用一个裸连接扮演「发申请的那个人」，用来触发服务端的好友申请推送。

用途：验证 NotifyAddFriendReq（1009）与 NotifyAuthFriendReq（1012）这两条**推送**链路。
推送只发给**在线**的对方，所以标准做法是：

    # 终端 1：真的客户端登录成 B（接收方），保持运行
    MYCHAT_AUTOLOGIN="666672:Test@12345" QT_QPA_PLATFORM=offscreen build/mingw-debug/qtClient.exe

    # 终端 2：用裸连接扮演 A，给 B 发申请，并挂在那里等 30 秒
    python tools/probe-push.py 666674 NewPass@1 666672 "来自裸探针的申请" --listen 30

B 那一侧应该能看到：
    [chat] 收到 NotifyAddFriendReq {"error":0, "add_friend_request_id":...}
    [chat] 发送 SyncFriendRequestReq {"uid":"666672"}
    [chat] 收到好友申请列表, 共 N 条

加了 --listen 之后，A 自己也在等：这时候让 B 接受 / 拒绝这条申请
（`MYCHAT_AUTHAPPLY=accept`），A 这边会收到 1012：

    [收到] message_id = 1012  {"error":0,"add_friend_request_id":16,"state":1}

为什么不复用 qtClientContactTest：那个程序发完申请就退出，而且第二段会登录成 B
把真客户端的会话顶掉 —— 推送要么发生得太早、要么发到了一个已经换人的连接上。

退出码：0 申请已发出（--listen 时还包括收完推送）；2 参数错误；3 HTTP/TCP 登录失败；
       4 服务端拒绝申请（带 error）。

`--remark <文本>` 用来填 `AddFriendReq` 的备注字段：1008 列表回包会把原值放进 `nick`，
所以这是验证「备注有没有打通」的那一环 —— 不传的话 `nick` 永远是空串，看不出问题。
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
ADD_FRIEND_REQ = 1005
ADD_FRIEND_RSP = 1006
CHECK_ONLINE_REQ = 1018
CHECK_ONLINE_RSP = 1019

NAMES = {
    CHAT_LOGIN_RSP: "ChatLoginRsp",
    ADD_FRIEND_RSP: "AddFriendRsp",
    CHECK_ONLINE_RSP: "CheckIsUserOnlineRsp",
    1012: "NotifyAuthFriendReq",
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


def main() -> int:
    argv = sys.argv[1:]

    # --listen <秒>：发完申请后不关闭连接，继续收推送（验证 1012）
    listen_seconds = 0
    if "--listen" in argv:
        index = argv.index("--listen")
        try:
            listen_seconds = int(argv[index + 1])
        except (IndexError, ValueError):
            print("[FAIL] --listen 后面要跟秒数")
            return 2
        del argv[index:index + 2]

    # --remark <文本>：给这次申请带上备注（服务端会在列表里以 nick 原样返回）
    remark = ""
    if "--remark" in argv:
        index = argv.index("--remark")
        try:
            remark = argv[index + 1]
        except IndexError:
            print("[FAIL] --remark 后面要跟备注文本")
            return 2
        del argv[index:index + 2]

    if len(argv) < 3:
        print(__doc__)
        print("用法: probe-push.py <sender_uid> <sender_pwd> <receiver_uid> "
              "[verify_info] [--remark 文本] [--listen 秒]")
        return 2

    sender_uid, sender_pwd, receiver_uid = argv[0], argv[1], argv[2]
    verify_info = argv[3] if len(argv) > 3 else "来自裸探针的好友申请"

    login = http_login(sender_uid, sender_pwd)
    print(f"[http] /user_login -> {json.dumps(login, ensure_ascii=False)}")
    if login.get("error") != 0:
        print(f"[FAIL] 发送方登录失败, error = {login.get('error')}")
        return 3

    token = login.get("token", "")
    host = login.get("host") or "127.0.0.1"
    port = int(login.get("port") or 8090)

    sock = socket.create_connection((host, port), timeout=5)
    send_frame(sock, CHAT_LOGIN_REQ,
               {"uid": str(login.get("uid", sender_uid)), "token": token})
    mid, payload, err = recv_frame(sock)
    if err:
        print(f"[FAIL] 聊天服登录失败: {err}")
        return 3
    print(f"[tcp] {NAMES.get(mid, mid)} {payload}")
    if mid != CHAT_LOGIN_RSP or json.loads(payload).get("error") != 0:
        return 3

    # 先问一句接收方在不在线 —— 服务端只推给在线用户，这条输出是"推送该不该出现"的前提
    send_frame(sock, CHECK_ONLINE_REQ, {"uid": receiver_uid})
    mid, payload, err = recv_frame(sock)
    print(f"[tcp] {NAMES.get(mid, mid)} {payload}" if not err else f"[warn] 查在线失败: {err}")

    send_frame(sock, ADD_FRIEND_REQ,
               {"uid": sender_uid, "to_uid": receiver_uid,
                "verify_info": verify_info, "remark": remark})
    mid, payload, err = recv_frame(sock)
    if err:
        print(f"[FAIL] 等申请回包失败: {err}")
        return 3
    print(f"[tcp] {NAMES.get(mid, mid)} (期望 {ADD_FRIEND_RSP}) {payload}")

    error = json.loads(payload).get("error")
    if error != 0:
        sock.close()
        print(f"[FAIL] 服务端拒绝了这次申请, error = {error}")
        return 4

    print("[OK] 申请已发出；接收方在线的话，此刻应该收到了 1009 推送")

    if listen_seconds <= 0:
        sock.close()
        return 0

    # ── 挂着等推送：让对方在客户端里点「接受 / 拒绝」，这里就能收到 1012 ──
    print(f"[listen] 保持连接 {listen_seconds}s，等待 1012（在客户端里处理这条申请）…")
    deadline = time.time() + listen_seconds
    got_push = False
    while time.time() < deadline:
        remain = max(0.5, deadline - time.time())
        try:
            mid, body, err = recv_frame(sock, timeout=remain)
        except socket.timeout:
            break
        if err:
            print(f"[listen] 连接结束：{err}")
            break
        print(f"[收到] message_id = {mid} ({NAMES.get(mid, '?')})  {body.strip()}")
        if mid == 1012:
            got_push = True

    sock.close()
    print("[OK] 收到了 1012 推送" if got_push
          else "[WARN] 监听期间没有收到 1012（对方没处理，或处理时你已掉线）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
