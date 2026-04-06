const fs = require('fs');
let html = fs.readFileSync('c:\\Users\\nrf\\Desktop\\bysj\\ESP32S3-Home-main\\ESP32S3-Home-main\\components\\Webserver\\index.html', 'utf8');

// Replace function fetchData() { fetch('/data').then... => function updateDataUI(data) { ... }
let oldFetchData = \            function fetchData() {
                fetch('/data')
                    .then(response => response.json())
                    .then(data => {\;

let newFetchData = \            let isFetching = false;
            
            function updateDataUI(data) {\;

html = html.replace(oldFetchData, newFetchData);

let oldCatch = \                        /* 当你的数据成功加载后调用这行代码 */
                        if (window.AndroidBridge) {
                            window.AndroidBridge.hideLoadingMask();
                        }
                    })
                    .catch(error => {
                        console.error('Error:', error);
                        // 数据加载失败时也可以尝试关闭，防止一直卡在加载页面
                        if (window.AndroidBridge) {
                            window.AndroidBridge.hideLoadingMask();
                        }
                    });
            }\;

let newCatch = \                        /* 当你的数据成功加载后调用这行代码 */
                        if (window.AndroidBridge) {
                            window.AndroidBridge.hideLoadingMask();
                        }
            }

            function fetchData() {
                if (isFetching) return;
                isFetching = true;
                fetch('/data')
                    .then(response => response.json())
                    .then(data => updateDataUI(data))
                    .catch(error => {
                        console.error('Error:', error);
                        if (window.AndroidBridge) window.AndroidBridge.hideLoadingMask();
                    })
                    .finally(() => { isFetching = false; });
            }\;

html = html.replace(oldCatch, newCatch);

let oldInit = \            //启动定时器
            setInterval(fetchData, 5000); // 从2秒改为5秒，减小HTTP轮询给局域网带来的负担
            fetchData(); // 初始执行一次\;

let newInit = \            // === 新增：WebSocket 协议加速局域网获取 !==
            let wsUrl = 'ws://' + window.location.host + '/ws';
            let ws = null;
            let fallbackInterval = null;

            function initWS() {
                try {
                    ws = new WebSocket(wsUrl);
                    ws.onopen = function() {
                        console.log("WebSocket connected. Starting 5s light ping.");
                        if (fallbackInterval) { clearInterval(fallbackInterval); fallbackInterval = null; }
                        setInterval(() => {
                            if (ws.readyState === WebSocket.OPEN) ws.send("get");
                        }, 5000);
                    };
                    ws.onmessage = function(event) {
                        try {
                            const payload = JSON.parse(event.data);
                            updateDataUI(payload);
                        } catch(e) { console.error("WS Parse Error", e); }
                    };
                    ws.onerror = function(err) {
                        console.error("WS Error", err);
                    };
                    ws.onclose = function() {
                        console.log("WebSocket closed. Falling back to HTTP polling.");
                        if (!fallbackInterval) fallbackInterval = setInterval(fetchData, 5000);
                    };
                } catch(e) {
                    console.log("WebSocket failed, keep using HTTP polling");
                    if (!fallbackInterval) fallbackInterval = setInterval(fetchData, 5000);
                }
            }

            // 尽早初始 HTTP 执行一次，保证第一时间加载出来，后续由 WS 接管
            fetchData(); 
            // 开始连接 WebSocket
            initWS();\;

html = html.replace(oldInit, newInit);

fs.writeFileSync('c:\\Users\\nrf\\Desktop\\bysj\\ESP32S3-Home-main\\ESP32S3-Home-main\\components\\Webserver\\index.html', html);
