#!/usr/bin/env python3
"""
测试 SOME/IP 连接的脚本
用于验证 mock 模式下的 SOME/IP 服务是否正常工作
"""

import socket
import struct
import time

def create_someip_header(service_id, method_id, client_id, session_id, message_type=0x00):
    """创建 SOME/IP 消息头"""
    # SOME/IP 头部格式：
    # Service ID (16 bits) | Method ID (16 bits)
    # Length (32 bits) - 包含头部和负载的总长度
    # Client ID (16 bits) | Session ID (16 bits)
    # Protocol Version (8 bits) | Interface Version (8 bits)
    # Message Type (8 bits) | Return Code (8 bits)
    
    # 长度字段：8字节头部 + 负载长度（这里假设无负载）
    length = 8
    
    header = struct.pack('>HH I HH BBBB',
                        service_id,
                        method_id,
                        length,
                        client_id,
                        session_id,
                        0x01,  # Protocol Version
                        0x01,  # Interface Version
                        message_type,
                        0x00)  # Return Code
    return header

def test_someip_connection(host='127.0.0.1', port=56101):
    """测试 SOME/IP 连接"""
    print(f"尝试连接到 {host}:{port}...")
    
    try:
        # 创建 TCP 连接
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5)
        sock.connect((host, port))
        print("✓ 连接成功！")
        
        # 创建 SOME/IP 请求消息（仅做连通性探测；method 0x6102 未注册，
        # 预期收到 E_UNKNOWN_METHOD 响应。CR-008：旧 0x0001/0x8001 已 reserved，
        # 不得用于探测。）
        service_id = 0x6101
        method_id = 0x6102  # 连通性探测用未注册 method，非业务调用
        client_id = 0x0001
        session_id = 0x0001
        
        header = create_someip_header(service_id, method_id, client_id, session_id)
        print(f"发送 SOME/IP 请求: Service=0x{service_id:04X}, Method=0x{method_id:04X} (连通性探测)")
        
        sock.send(header)
        
        # 等待响应
        try:
            response = sock.recv(1024)
            if response:
                print(f"✓ 收到响应，长度: {len(response)} 字节")
                # 解析响应头
                if len(response) >= 16:
                    resp_service, resp_method, resp_length, resp_client, resp_session, \
                    resp_proto, resp_iface, resp_type, resp_code = struct.unpack('>HH I HH BBBB', response[:16])
                    print(f"  响应: Service=0x{resp_service:04X}, Method=0x{resp_method:04X}, "
                          f"Type=0x{resp_type:02X}, Code=0x{resp_code:02X}")
            else:
                print("未收到响应")
        except socket.timeout:
            print("等待响应超时")
        
        sock.close()
        print("✓ 连接已关闭")
        return True
        
    except ConnectionRefusedError:
        print("✗ 连接被拒绝，请确保 SOME/IP 服务正在运行")
        return False
    except socket.timeout:
        print("✗ 连接超时")
        return False
    except Exception as e:
        print(f"✗ 连接失败: {e}")
        return False

if __name__ == "__main__":
    print("SOME/IP 连接测试")
    print("=" * 50)
    test_someip_connection()