#!/usr/bin/env python3
import socket
import struct
import sys

DATA = 1
HEARTBEAT = 2
HEARTBEAT_ACK = 3


def recv_exact(sock, size):
    # TCP recv 不保证一次返回 size 字节，因此循环直到收齐指定数量。
    chunks = bytearray()
    while len(chunks) < size:
        chunk = sock.recv(size - len(chunks))
        if not chunk:
            raise ConnectionError("server closed the connection")
        chunks.extend(chunk)
    return bytes(chunks)


def recv_frame(sock):
    # 先读固定 4 字节长度，再读完整 body；!I 表示网络字节序的 uint32。
    body_size = struct.unpack("!I", recv_exact(sock, 4))[0]
    if body_size < 1:
        raise ValueError("invalid frame length")
    # body 的第一个字节是帧类型，剩下的字节是载荷。
    body = recv_exact(sock, body_size)
    return body[0], body[1:]


def main():
    # 命令行可覆盖服务器地址、端口和发送内容，方便本机或远程联调。
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 8080
    message = (sys.argv[3] if len(sys.argv) > 3 else "hello from python").encode()
    # 协议长度统计类型字节加载荷，但不包括 4 字节长度头。
    body = bytes([DATA]) + message

    with socket.create_connection((host, port)) as sock:
        # sendall 会循环发送，避免普通 send 只发送部分帧。
        sock.sendall(struct.pack("!I", len(body)) + body)
        while True:
            frame_type, payload = recv_frame(sock)
            if frame_type == HEARTBEAT:
            # 服务器发起心跳时回一个空 HEARTBEAT_ACK 帧。
                ack = bytes([HEARTBEAT_ACK])
                sock.sendall(struct.pack("!I", len(ack)) + ack)
                continue
            if frame_type == DATA:
                # Echo 响应到达，解码并打印后结束客户端。
                print(payload.decode(errors="replace"))
                return
            if frame_type != HEARTBEAT_ACK:
                raise ValueError(f"unexpected frame type: {frame_type}")


if __name__ == "__main__":
    main()
