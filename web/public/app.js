(() => {
    const htmlEl = document.documentElement;
    const btnTheme = document.getElementById('btn-theme-toggle');
    const webViewerIdEl = document.getElementById('web-viewer-id');
    const connectPanel = document.getElementById('connect-panel');
    const sessionPanel = document.getElementById('session-panel');
    const connectForm = document.getElementById('connect-form');
    const inputTarget = document.getElementById('input-target-id');
    const inputPassword = document.getElementById('input-password');
    const statusBanner = document.getElementById('status-banner');
    const peersList = document.getElementById('peers-list');
    const btnRefreshPeers = document.getElementById('btn-refresh-peers');

    const sessionHostLabel = document.getElementById('session-host-label');
    const sessionSasBadge = document.getElementById('session-sas-badge');
    const sessionMetrics = document.getElementById('session-metrics');
    const selectQuality = document.getElementById('select-quality');
    const selectFps = document.getElementById('select-fps');
    const btnTaskMgr = document.getElementById('btn-task-mgr');
    const btnToggleChat = document.getElementById('btn-toggle-chat');
    const btnDisconnect = document.getElementById('btn-disconnect');

    const canvas = document.getElementById('remote-canvas');
    const ctx = canvas.getContext('2d');
    const chatDrawer = document.getElementById('chat-drawer');
    const chatMessages = document.getElementById('chat-messages');
    const chatForm = document.getElementById('chat-form');
    const inputChat = document.getElementById('input-chat');

    let ws = null;
    let isConnected = false;

    // Theme Switcher
    btnTheme.addEventListener('click', () => {
        const next = htmlEl.getAttribute('data-theme') === 'dark' ? 'light' : 'dark';
        htmlEl.setAttribute('data-theme', next);
        btnTheme.textContent = next === 'dark' ? 'Light Mode' : 'Dark Mode';
    });

    function showBanner(text, isError = false) {
        statusBanner.textContent = text;
        statusBanner.style.borderColor = isError ? 'var(--danger)' : 'var(--accent)';
        statusBanner.classList.remove('hidden');
    }

    // Poll LAN Discovery API
    async function refreshPeers() {
        try {
            const res = await fetch('/api/peers');
            const data = await res.json();
            if (data.webViewerId) {
                webViewerIdEl.textContent = data.webViewerId;
            }
            if (!data.peers || data.peers.length === 0) {
                peersList.innerHTML = '<div class="empty-state">No AeroDesk Hosts detected on LAN yet. You can still dial any Desk ID or IP:Port directly.</div>';
                return;
            }
            peersList.innerHTML = '';
            for (const p of data.peers) {
                const row = document.createElement('div');
                row.className = 'peer-item';
                row.innerHTML = `
                    <div>
                        <div class="peer-id">${p.formattedId}</div>
                        <div class="peer-host">${p.hostname} &bull; ${p.ip}:${p.port}</div>
                    </div>
                    <span class="badge-pill">Select</span>
                `;
                row.addEventListener('click', () => {
                    inputTarget.value = p.formattedId;
                    inputPassword.focus();
                });
                peersList.appendChild(row);
            }
        } catch {}
    }

    btnRefreshPeers.addEventListener('click', refreshPeers);
    refreshPeers();
    setInterval(refreshPeers, 3500);

    function ensureWs() {
        if (ws && ws.readyState === WebSocket.OPEN) return Promise.resolve();
        return new Promise((resolve, reject) => {
            const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
            ws = new WebSocket(`${proto}//${location.host}`);
            ws.binaryType = 'arraybuffer';

            ws.onopen = () => resolve();
            ws.onerror = (e) => reject(e);
            ws.onclose = () => {
                isConnected = false;
                sessionPanel.classList.add('hidden');
                connectPanel.classList.remove('hidden');
            };
            ws.onmessage = (ev) => {
                if (typeof ev.data === 'string') {
                    handleJsonMsg(JSON.parse(ev.data));
                } else {
                    handleBinaryTile(ev.data);
                }
            };
        });
    }

    function handleJsonMsg(msg) {
        if (msg.type === 'status') {
            showBanner(msg.message, false);
        } else if (msg.type === 'error') {
            showBanner(msg.message, true);
        } else if (msg.type === 'connected') {
            isConnected = true;
            statusBanner.classList.add('hidden');
            connectPanel.classList.add('hidden');
            sessionPanel.classList.remove('hidden');
            sessionHostLabel.textContent = `${msg.remoteHost} (${msg.remoteId})`;
            sessionSasBadge.textContent = `E2EE SAS: ${msg.sas}`;
            canvas.focus();
        } else if (msg.type === 'disconnected') {
            isConnected = false;
            sessionPanel.classList.add('hidden');
            connectPanel.classList.remove('hidden');
            showBanner('Remote session ended.', false);
        } else if (msg.type === 'video_config' || msg.type === 'frame_sync') {
            if (msg.width > 0 && msg.height > 0 && (canvas.width !== msg.width || canvas.height !== msg.height)) {
                canvas.width = msg.width;
                canvas.height = msg.height;
            }
        } else if (msg.type === 'pong') {
            sessionMetrics.textContent = `RTT: ${msg.rtt} ms`;
        } else if (msg.type === 'chat') {
            appendChatBubble(msg.sender, msg.text);
            chatDrawer.classList.remove('hidden');
        }
    }

    function handleBinaryTile(arrayBuf) {
        const view = new DataView(arrayBuf);
        if (view.byteLength < 9) return;
        const encType = view.getUint8(0);
        const tx = view.getUint16(1, true);
        const ty = view.getUint16(3, true);
        const tw = view.getUint16(5, true);
        const th = view.getUint16(7, true);
        const payload = new Uint8Array(arrayBuf, 9);

        if (encType === 0) {
            // Raw RGBA pixels (decompressed from Zstd on gateway)
            if (payload.byteLength === tw * th * 4) {
                const imgData = new ImageData(new Uint8ClampedArray(payload.buffer, payload.byteOffset, payload.byteLength), tw, th);
                ctx.putImageData(imgData, tx, ty);
            }
        } else if (encType === 2) {
            // JPEG tile
            const blob = new Blob([payload], { type: 'image/jpeg' });
            createImageBitmap(blob).then((bmp) => {
                ctx.drawImage(bmp, tx, ty, tw, th);
                bmp.close();
            }).catch(() => {});
        }
    }

    connectForm.addEventListener('submit', async (e) => {
        e.preventDefault();
        const target = inputTarget.value.trim();
        if (!target) return;
        showBanner('Initializing encrypted connection...', false);
        try {
            await ensureWs();
            ws.send(JSON.stringify({
                type: 'connect',
                target,
                password: inputPassword.value
            }));
        } catch {
            showBanner('Failed to connect to local Web Gateway.', true);
        }
    });

    btnDisconnect.addEventListener('click', () => {
        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({ type: 'disconnect' }));
        }
    });

    function sendVideoControl(keyframe = false) {
        if (!isConnected || !ws) return;
        ws.send(JSON.stringify({
            type: 'video_control',
            quality: Number(selectQuality.value),
            fps: Number(selectFps.value),
            monitor: 0,
            keyframe,
            adaptive: true
        }));
    }

    selectQuality.addEventListener('change', () => sendVideoControl(true));
    selectFps.addEventListener('change', () => sendVideoControl(false));

    btnTaskMgr.addEventListener('click', () => {
        if (isConnected && ws) {
            ws.send(JSON.stringify({ type: 'system_action', action: 1 }));
        }
    });

    btnToggleChat.addEventListener('click', () => {
        chatDrawer.classList.toggle('hidden');
    });

    function appendChatBubble(sender, text) {
        const div = document.createElement('div');
        div.className = 'chat-bubble';
        const sEl = document.createElement('strong');
        sEl.textContent = sender;
        const tEl = document.createElement('span');
        tEl.textContent = text;
        div.appendChild(sEl);
        div.appendChild(tEl);
        chatMessages.appendChild(div);
        chatMessages.scrollTop = chatMessages.scrollHeight;
    }

    chatForm.addEventListener('submit', (e) => {
        e.preventDefault();
        const text = inputChat.value.trim();
        if (!text || !isConnected || !ws) return;
        ws.send(JSON.stringify({ type: 'chat', text }));
        appendChatBubble('You (Browser)', text);
        inputChat.value = '';
    });

    // Canvas Mouse & Keyboard Input Forwarding
    function getNormalizedCoords(ev) {
        const rect = canvas.getBoundingClientRect();
        const x = Math.max(0, Math.min(1, (ev.clientX - rect.left) / Math.max(1, rect.width)));
        const y = Math.max(0, Math.min(1, (ev.clientY - rect.top) / Math.max(1, rect.height)));
        return { x, y };
    }

    canvas.addEventListener('mousemove', (ev) => {
        if (!isConnected || !ws) return;
        const { x, y } = getNormalizedCoords(ev);
        ws.send(JSON.stringify({ type: 'mouse_move', x, y }));
    });

    canvas.addEventListener('mousedown', (ev) => {
        if (!isConnected || !ws) return;
        canvas.focus();
        const { x, y } = getNormalizedCoords(ev);
        const btn = ev.button === 2 ? 2 : (ev.button === 1 ? 3 : 1);
        ws.send(JSON.stringify({ type: 'mouse_button', button: btn, down: true, x, y }));
    });

    canvas.addEventListener('mouseup', (ev) => {
        if (!isConnected || !ws) return;
        const { x, y } = getNormalizedCoords(ev);
        const btn = ev.button === 2 ? 2 : (ev.button === 1 ? 3 : 1);
        ws.send(JSON.stringify({ type: 'mouse_button', button: btn, down: false, x, y }));
    });

    canvas.addEventListener('contextmenu', (ev) => ev.preventDefault());

    canvas.addEventListener('wheel', (ev) => {
        if (!isConnected || !ws) return;
        ev.preventDefault();
        const delta = ev.deltaY < 0 ? 120 : -120;
        ws.send(JSON.stringify({ type: 'mouse_wheel', deltaY: delta }));
    }, { passive: false });

    canvas.addEventListener('keydown', (ev) => {
        if (!isConnected || !ws) return;
        ev.preventDefault();
        ws.send(JSON.stringify({ type: 'key', vk: ev.keyCode || ev.which, down: true }));
    });

    canvas.addEventListener('keyup', (ev) => {
        if (!isConnected || !ws) return;
        ev.preventDefault();
        ws.send(JSON.stringify({ type: 'key', vk: ev.keyCode || ev.which, down: false }));
    });

    const releaseAllModifiers = () => {
        if (isConnected && ws && ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({ type: 'release_all' }));
        }
    };
    window.addEventListener('blur', releaseAllModifiers);
    canvas.addEventListener('blur', releaseAllModifiers);
})();
