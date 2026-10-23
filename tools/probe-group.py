#!/usr/bin/env python3
"""群聊探针：用裸连接扮演「另一个群成员」，验证 1033–1042 与群消息。

为什么需要它：客户端自检（tests/groupflow.cpp）用的是假 ChatServer，
只能证明"我们发出去的帧对"；真实服务端的群成员集合、存储过程返回值、
以及「群消息会给每个成员各写一条离线流」这些行为，只有在真服务端上才能验。

典型用法（客户端登录成 666672 建群并拉 666673）：

    # 客户端：登录 → 建群「自检群」→ 拉 666673 → 发一条群消息
    MYCHAT_AUTOLOGIN="666672:Test@12345" \\
    MYCHAT_GROUPCREATE="自检群:666673" \\
    MYCHAT_SENDMSG="群消息自检" \\
    MYCHAT_SMOKE=out.png build/mingw-debug/qtClient.exe

    # 探针：以 666673 登录，把离线流里的消息拉出来（应看到 group=true + group_id）
    python tools/probe-group.py 666673 Test@12345 pull

    # 探针：以 666673 的身份往群里发一条
    python tools/probe-group.py 666673 Test@12345 send 20001 "来自探针的群消息"

    # 探针：看群成员（1041）
    python tools/probe-group.py 666673 Test@12345 members 20001

注意：
  · 服务端**没有群列表接口**，探针也不知道群号 —— 建群的回包里才有（1034 的
    group_id），或者从 pull 出来的 `group_id` 里读；
  · 群消息的 1022 推送在服务端有 bug（成员遍历用了 co_return，见 api-group.md §9.3），
    所以 pull 才是可靠的验收手段；
  · `send` 之前必须先在群里（SISMEMBER chatgroup_<gid>），否则服务端回 1022。

退出码：0 成功；2 参数错误；3 登录失败；4 服务端返回 error；5 拉取超时。
"""
import json
import socket
import struct
import sys
import urllib.request

GATE = "http://127.0.0.1:8080"

CHAT_LOGIN_REQ = 1001
CHAT_LOGIN_RSP = 1002
TEXT_CHAT_MSG_REQ = 1013
TEXT_CHAT_MSG_RSP = 1014
SYNC_CHAT_MESSAGE_REQ = 1023
SYNC_CHAT_MESSAGE_RSP = 1024
GROUP_CREATE_REQ = 1033
GROUP_CREATE_RSP = 1034
GROUP_ADD_MEMBERS_REQ = 1035
GROUP_ADD_MEMBERS_RSP = 1036
GROUP_DISMISS_REQ = 1039
GROUP_DISMISS_RSP = 1040
GET_GROUP_USER_INFO_REQ = 1041
GET_GROUP_USER_INFO_RSP = 1042

NAMES = {
    CHAT_LOGIN_RSP: "ChatLoginRsp",
    TEXT_CHAT_MSG_RSP: "TextChatMsgRsp",
    SYNC_CHAT_MESSAGE_RSP: "SyncChatMessageRsp",
    GROUP_CREATE_RSP: "GroupCreateRsp",
    GROUP_ADD_MEMBERS_RSP: "GroupAddMembersRsp",
    GROUP_DISMISS_RSP: "GroupDismissRsp",
    GET_GROUP_USER_INFO_RSP: "GetGroupUserInfoRsp",
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
    # msgid 只是给客户端认回包用的；探针按「请求型号」等一帧就够
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
    return sock, str(result.get("uid", uid))


def wait_reply(sock, expect_id):
    """等到 expect_id 的那一帧（中间可能夹着 1022 推送，跳过但不打印错）。"""
    for _ in range(8):
        mid, payload, err = recv_frame(sock)
        if err:
            print(f"[FAIL] 等回包失败: {err}")
            return None
        if mid != expect_id:
            print(f"[push] 忽略 {NAMES.get(mid, mid)} {payload}")
            continue
        return payload
    print(f"[FAIL] 连续 8 帧都不是 {NAMES.get(expect_id, expect_id)}")
    return None


def cmd_pull(sock, count: int):
    send_frame(sock, SYNC_CHAT_MESSAGE_REQ, {"count": str(count)})
    payload = wait_reply(sock, SYNC_CHAT_MESSAGE_RSP)
    if payload is None:
        return 5
    print(f"[tcp] SyncChatMessageRsp {payload}")
    data = json.loads(payload)
    if data.get("error") != 0:
        return 4
    for message in data.get("msgs", []):
        print(f"[msg] group={message.get('group')} group_id={message.get('group_id')} "
              f"from={message.get('from_uid')} type={message.get('type')} "
              f"body={message.get('body')!r} id={message.get('id')}")
    return 0


def cmd_send(sock, gid: str, text: str):
    send_frame(sock, TEXT_CHAT_MSG_REQ,
               {"msg_id": "probe-group-1", "to_uid": gid, "type": "1",
                "body": text, "group": True})
    payload = wait_reply(sock, TEXT_CHAT_MSG_RSP)
    if payload is None:
        return 5
    print(f"[tcp] TextChatMsgRsp {payload}")
    return 0 if json.loads(payload).get("error") == 0 else 4


def cmd_create(sock, name: str):
    send_frame(sock, GROUP_CREATE_REQ,
               {"name": name, "avator": "", "announcement": "探针创建"})
    payload = wait_reply(sock, GROUP_CREATE_RSP)
    if payload is None:
        return 5
    print(f"[tcp] GroupCreateRsp {payload}")
    data = json.loads(payload)
    if data.get("error") != 0:
        return 4
    print(f"[OK] group_id = {data.get('group_id')}")
    return 0


def cmd_add(sock, gid: str, uids):
    send_frame(sock, GROUP_ADD_MEMBERS_REQ, {"group_id": gid, "members": list(uids)})
    payload = wait_reply(sock, GROUP_ADD_MEMBERS_RSP)
    if payload is None:
        return 5
    print(f"[tcp] GroupAddMembersRsp {payload}")
    return 0 if json.loads(payload).get("error") == 0 else 4


def cmd_dismiss(sock, gid: str):
    send_frame(sock, GROUP_DISMISS_REQ, {"group_id": gid})
    payload = wait_reply(sock, GROUP_DISMISS_RSP)
    if payload is None:
        return 5
    print(f"[tcp] GroupDismissRsp {payload}")
    return 0 if json.loads(payload).get("error") == 0 else 4


def cmd_members(sock, gid: str):
    # ⚠ 字段名是 gid，不是 group_id
    send_frame(sock, GET_GROUP_USER_INFO_REQ, {"gid": gid})
    payload = wait_reply(sock, GET_GROUP_USER_INFO_RSP)
    if payload is None:
        return 5
    print(f"[tcp] GetGroupUserInfoRsp {payload}")
    data = json.loads(payload)
    if data.get("error") != 0:
        return 4
    for member in data.get("mb_infos", []):
        print(f"[member] uid={member.get('uid')} role={member.get('role')} "
              f"nick_g={member.get('nick_g')!r} join_at={member.get('join_at')}")
    return 0


def main() -> int:
    argv = sys.argv[1:]
    if len(argv) < 3:
        print(__doc__)
        print("用法: probe-group.py <uid> <password> "
              "<pull [count] | send <gid> <text> | create <name> | "
              "add <gid> <uid,uid> | dismiss <gid> | members <gid>>")
        return 2

    uid, password, command = argv[0], argv[1], argv[2]
    sock, self_uid = login(uid, password)
    if sock is None:
        return 3
    print(f"[OK] 已登录 uid = {self_uid}")

    try:
        if command == "pull":
            return cmd_pull(sock, int(argv[3]) if len(argv) > 3 else 10)
        if command == "send" and len(argv) >= 5:
            return cmd_send(sock, argv[3], argv[4])
        if command == "create" and len(argv) >= 4:
            return cmd_create(sock, argv[3])
        if command == "add" and len(argv) >= 5:
            return cmd_add(sock, argv[3], [u for u in argv[4].split(",") if u])
        if command == "dismiss" and len(argv) >= 4:
            return cmd_dismiss(sock, argv[3])
        if command == "members" and len(argv) >= 4:
            return cmd_members(sock, argv[3])
        print(__doc__)
        return 2
    finally:
        sock.close()


if __name__ == "__main__":
    sys.exit(main())
