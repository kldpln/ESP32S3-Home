import asyncio
import websockets
import time
import urllib.request
import socket

# === 配置区 ===
# 如果你的 Windows 系统解析不了 esp.local，请将这里改为你看串口打印出的 ESP32 IP 地址
# 例如：HOST = "192.168.31.100"
HOST = "esp.local" 
WS_URL = f"ws://{HOST}/ws"
HTTP_URL = f"http://{HOST}/data"

async def test_rapid_reconnect(iterations=30):
    print(f"\nWebSocket 快速连接与断开测试")
    print(f"执行次数: {iterations}")
    success = 0
    for i in range(iterations):
        try:
            # 建立连接、发送请求并立即断开
            async with websockets.connect(WS_URL, close_timeout=0.1) as ws:
                await ws.send("get")
                resp = await asyncio.wait_for(ws.recv(), timeout=2.0)
                success += 1
            if (i+1) % 10 == 0:
                print(f"进度: {i+1}/{iterations}")
        except Exception as e:
            pass # 忽略报错，只是压测
    print(f"结果: 成功 {success}/{iterations}，成功率 {success / iterations * 100:.2f}%")
   

async def test_concurrent_connections(max_conn=12):
    print(f"\nWebSocket 并发连接保持与 LRU 清理测试")
    print(f"目标并发数: {max_conn}")
    connections = []
    for i in range(max_conn):
        try:
            # 建立连接但不发送后续数据，用于观察服务端连接管理行为
            ws = await asyncio.wait_for(websockets.connect(WS_URL), timeout=3.0)
            connections.append(ws)
            print(f"连接 {i+1}: 成功")
        except Exception as e:
            print(f"连接 {i+1}: 失败，达到协议栈连接上限")
            
    print("观测阶段: 等待 12 秒，以验证后端是否能够自动清理无效连接")
    await asyncio.sleep(12)
    
    # 验证这些死链接是否被 ESP32 主动从服务端掐断
    closed_by_server = 0
    for ws in connections:
        if ws.state.name == "CLOSED":  # websockets 13.x/14.x 写法，兼容最新版
            closed_by_server += 1
        else:
             await ws.close()
    print(f"结果: 服务端主动关闭失效连接 {closed_by_server} 个")

def test_http_spam(iterations=50):
    print(f"\nHTTP 连续请求稳定性测试")
    print(f"请求次数: {iterations}")
    success = 0
    start = time.time()
    for i in range(iterations):
        try:
            req = urllib.request.Request(HTTP_URL)
            with urllib.request.urlopen(req, timeout=1.0) as response:
                if response.status == 200:
                    success += 1
        except Exception:
            pass
    cost = time.time() - start
    print(f"结果: 成功 {success}/{iterations}，成功率 {success / iterations * 100:.2f}% ，耗时 {cost:.2f} s")

async def main():
    print("========================================")
    print("=      ESP32 网络稳定性测试脚本         =")
    print("========================================")
    
    # 测试网络连通性
    try:
        if HOST.endswith(".local"):
            # 简单的连通性测试
            urllib.request.urlopen(HTTP_URL, timeout=3.0)
    except Exception as e:
        print(f"连接性检查失败: 无法访问 {HOST}。如 mDNS 不可用，请将 HOST 修改为 ESP32 的实际 IP 地址。")
        return

    test_http_spam(30)
    await test_rapid_reconnect(30)
    await test_concurrent_connections(12)
    
    print("\n测试完成: 已输出各项实验结果。")

if __name__ == "__main__":
    asyncio.run(main())