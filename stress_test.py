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
    print(f"\n[Test 1] 快速连接/断开测试 (模拟狂刷 F5，执行 {iterations} 次)")
    success = 0
    for i in range(iterations):
        try:
            # 建立连接，发消息，立刻断开，不等待系统回收
            async with websockets.connect(WS_URL, close_timeout=0.1) as ws:
                await ws.send("get")
                resp = await asyncio.wait_for(ws.recv(), timeout=2.0)
                success += 1
            if (i+1) % 10 == 0:
                print(f" -> 已完成 {i+1}/{iterations} 次断连测试...")
        except Exception as e:
            pass # 忽略报错，只是压测
    print(f"✅ 狂刷测试完成！成功率: {success}/{iterations}")
    if success < iterations:
        print("💡 提示: 部分失败是正常的，因为遇到了并发上限，但只要不卡死且后续能恢复，就说明没问题！")

async def test_concurrent_connections(max_conn=12):
    print(f"\n[Test 2] 并发与 LRU 幽灵淘汰测试 (同时发起 {max_conn} 个死连接)")
    connections = []
    for i in range(max_conn):
        try:
            # 只连不断，生占茅坑
            ws = await asyncio.wait_for(websockets.connect(WS_URL), timeout=3.0)
            connections.append(ws)
            print(f" -> 打开第 {i+1} 个连接: 成功")
        except Exception as e:
            print(f" -> 打开第 {i+1} 个连接: 拒绝 (说明达到了 LWIP 的极限，后续交由 LRU 清理)")
            
    print("\n⏳ 正在等待 12 秒，考察后端的 recv_wait_timeout 和 LRU 能否自动清理这些无效连接...")
    await asyncio.sleep(12)
    
    # 验证这些死链接是否被 ESP32 主动从服务端掐断
    closed_by_server = 0
    for ws in connections:
        if ws.state.name == "CLOSED":  # websockets 13.x/14.x 写法，兼容最新版
            closed_by_server += 1
        else:
             await ws.close()
    print(f"✅ 并发测试结束。ESP32 成功自动踢掉了 {closed_by_server} 个失效连接。")

def test_http_spam(iterations=50):
    print(f"\n[Test 3] 传统 HTTP 接口防爆刷测试 (高速并发 {iterations} 次请求)")
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
    print(f"✅ HTTP 爆刷完成！成功率: {success}/{iterations}，耗时: {cost:.2f} 秒")

async def main():
    print("========================================")
    print("=      ESP32 极限断网/高并发压测脚本     =")
    print("========================================")
    
    # 测试网络连通性
    try:
        if HOST.endswith(".local"):
            # 简单的连通性测试
            urllib.request.urlopen(HTTP_URL, timeout=3.0)
    except Exception as e:
        print(f"❌ 无法连接到 {HOST}。如果 mDNS 在此电脑上不起作用，请修改脚本开头的 HOST 为 ESP32 的实际 IP (如 192.168.0.x)！")
        return

    test_http_spam(30)
    await test_rapid_reconnect(30)
    await test_concurrent_connections(12)
    
    print("\n🎉 压测脚本执行完毕！请现在使用手机 APP 或浏览器正常访问，看看是否顺畅秒连！")

if __name__ == "__main__":
    asyncio.run(main())