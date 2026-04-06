const fs = require('fs');
let html = fs.readFileSync('c:\\Users\\nrf\\Desktop\\bysj\\ESP32S3-Home-main\\ESP32S3-Home-main\\components\\Webserver\\index.html', 'utf8');

// Replace function fetchData() { fetch('/data').then... => function updateDataUI(data) { ... }
let oldFetchData = \            function fetchData() {
                fetch('/data')
                    .then(response => response.json())
                    .then(data => {\;

let newFetchData = \            function updateDataUI(data) {\;

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
            } // end update

            let isFetching = false;
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

let newInit = \            // === WS Logic ===
            let wsUrl = 'ws://' + window.location.host + '/ws';
            let ws = null;
            let fallbackInterval = null;

            function initWS() {
                try {
                    ws = new WebSocket(wsUrl);
                    ws.onopen = function() {
                        console.log("WebSocket connected.");
                        if (fallbackInterval) { clearInterval(fallbackInterval); fallbackInterval = null; }
                        setInterval(() => {
                            if (ws.readyState === WebSocket.OPEN) ws.send("get");
                        }, 5000);
                    };
                    ws.onmessage = function(event) {
                        try {
                            const payload = JSON.parse(event.data);
                            updateDataUI(payload);
                        } catch(e) {}
                    };
                    ws.onclose = function() {
                        if (!fallbackInterval) fallbackInterval = setInterval(fetchData, 5000);
                    };
                } catch(e) {
                    if (!fallbackInterval) fallbackInterval = setInterval(fetchData, 5000);
                }
            }

            fetchData(); 
            initWS();\;

html = html.replace(oldInit, newInit);

fs.writeFileSync('c:\\Users\\nrf\\Desktop\\bysj\\ESP32S3-Home-main\\ESP32S3-Home-main\\components\\Webserver\\index.html', html);
