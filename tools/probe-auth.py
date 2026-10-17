#!/usr/bin/env python3
"""用一个裸连接扮演「处理好友申请的那个人」（B），用来触发 1012 推送。

场景：客户端登录成 A 并保持在线 → 本脚本以 B 的身份接受 / 拒绝 A 发来的申请
→ A 的客户端应当收到 NotifyAuthFriendReq(1012)，界面自己从「申请已发送」翻成
「对方已接受 / 对方已拒绝」。

    # 终端 1：A 的客户端（666674），先给 B 发一条申请并挂着
    MYCHAT_AUTOLOGIN="666674:NewPass@1" MYCHAT_OPENDLG=sendfriend:666672 ...

    # 终端 2：以 B 的身份处理（默认接受）
    python tools/probe-auth.py 666672 Test@12345 accept --applicant 666674

参数：
    <uid> <pwd> <accept|refuse> [--applicant <from_uid>] [--listen <秒>] [--dump]

--applicant 用来在多条待处理申请里挑"谁发的那条"（不给就取第一条待处理的）。
--listen 用来在处理完之后继续挂着收推送（验 1012 在不在这条连接上，而不是在 A 那边）。
--dump     把 1008 回包逐条原样打印出来。用来核对列表字段（尤其 `nick` 备注，
           它**只有发送方视角有值**，漏了很难从界面上看出来）。

退出码：0 处理成功；2 参数错误；3 登录/收包失败；4 没有可处理的申请；
       5 服务端拒绝（带 error，1021 = 已被处理过，1011 = 无权处理）。
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
SYNC_FRIEND_REQ = 1007
SYNC_FRIEND_RSP = 1008
AUTH_FRIEND_REQ = 1010
AUTH_FRIEND_RSP = 1011
NOTIFY_AUTH_FRIEND = 1012
CHECK_ONLINE_REQ = 1018
CHECK_ONLINE_RSP = 1019

NAMES = {
    CHAT_LOGIN_RSP: "ChatLoginRsp",
    SYNC_FRIEND_RSP: "SyncFriendRequestRsp",
    AUTH_FRIEND_RSP: "AuthFriendRsp",
    NOTIFY_AUTH_FRIEND: "NotifyAuthFriendReq",
    CHECK_ONLINE_RSP: "CheckIsUserOnlineRsp",
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
    listen_seconds = 0
    applicant = ""
    dump = "--dump" in argv
    if dump:
        argv.remove("--dump")
    if "--listen" in argv:
        index = argv.index("--listen")
        listen_seconds = int(argv[index + 1])
        del argv[index:index + 2]
    if "--applicant" in argv:
        index = argv.index("--applicant")
        applicant = argv[index + 1]
        del argv[index:index + 2]

    if len(argv) < 3:
        print(__doc__)
        return 2

    uid, password, action = argv[0], argv[1], argv[2]
    if action not in ("accept", "refuse"):
        print("[FAIL] 第三个参数只能是 accept / refuse")
        return 2
    state = 1 if action == "accept" else 2

    login = http_login(uid, password)
    if login.get("error") != 0:
        print(f"[FAIL] 登录失败: {json.dumps(login, ensure_ascii=False)}")
        return 3

    sock = socket.create_connection((login.get("host") or "127.0.0.1",
                                     int(login.get("port") or 8090)), timeout=5)
    send_frame(sock, CHAT_LOGIN_REQ, {"uid": str(login.get("uid", uid)),
                                      "token": login.get("token", "")})
    mid, payload, err = recv_frame(sock)
    if err or mid != CHAT_LOGIN_RSP or json.loads(payload).get("error") != 0:
        print(f"[FAIL] 聊天服登录失败: {err or payload}")
        return 3
    print(f"[tcp] ChatLoginRsp OK (uid={login.get('uid')})")

    # 先问一下申请人是否在线 —— 1012 只推给在线的人，这条输出是"推送该不该出现"的前提
    if applicant:
        send_frame(sock, CHECK_ONLINE_REQ, {"uid": applicant})
        mid, payload, err = recv_frame(sock)
        print(f"[tcp] 申请人 {applicant} 在线状态: {payload.strip() if not err else err}")

    # 拉列表，找出目标那条申请
    send_frame(sock, SYNC_FRIEND_REQ, {"uid": str(login.get("uid", uid))})
    mid, payload, err = recv_frame(sock)
    if err or mid != SYNC_FRIEND_RSP:
        print(f"[FAIL] 拉取申请列表失败: {err or payload}")
        return 3

    infos = json.loads(payload).get("apply_infos") or []
    print(f"[tcp] 申请列表 {len(infos)} 条")
    if dump:
        for info in infos:
            print(f"[dump] {json.dumps(info, ensure_ascii=False)}")
    target = None
    for info in infos:
        if int(info.get("state", 0)) != 0:
            continue
        if applicant and str(info.get("from_uid")) != str(applicant):
            continue
        target = info
        break

    if target is None:
        print("[FAIL] 没有可处理的申请（都处理过了，或申请人不是 %s）" % (applicant or "任意"))
        sock.close()
        return 4

    request_id = str(target.get("id"))
    print(f"[pick] 目标申请 id={request_id} from={target.get('from_uid')} "
          f"verify={target.get('verify_info')}")

    # 字段类型按 api-friend.md §10：请求里的 id 与 state 都是**字符串**
    send_frame(sock, AUTH_FRIEND_REQ,
               {"add_friend_request_id": request_id, "state": str(state)})
    mid, payload, err = recv_frame(sock)
    if err:
        print(f"[FAIL] 等处理回包失败: {err}")
        return 3
    print(f"[tcp] {NAMES.get(mid, mid)} {payload.strip()}")
    error = json.loads(payload).get("error")
    if error != 0:
        print(f"[FAIL] 服务端拒绝处理, error = {error}")
        sock.close()
        return 5

    print(f"[OK] 已{'接受' if state == 1 else '拒绝'}申请 {request_id}；"
          f"申请人 {target.get('from_uid')} 在线的话此刻应收到 1012")

    if listen_seconds > 0:
        print(f"[listen] 保持连接 {listen_seconds}s …")
        deadline = time.time() + listen_seconds
        while time.time() < deadline:
            try:
                mid, body, err = recv_frame(sock, timeout=max(0.5, deadline - time.time()))
            except socket.timeout:
                break
            if err:
                break
            print(f"[收到] message_id = {mid} ({NAMES.get(mid, '?')}) {body.strip()}")

    sock.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
