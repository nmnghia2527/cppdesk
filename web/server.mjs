#!/usr/bin/env node
/**
 * AeroDesk Web Gateway (Node.js v24+)
 * Zero-dependency HTTP + RFC 6455 WebSocket-to-AeroDesk Encrypted TCP Bridge
 * Speaks the native AeroDesk binary protocol (CNG SHA-256 Handshake, SplitMix64 E2EE, Zstd/JPEG tiles)
 */

import http from 'node:http';
import net from 'node:net';
import dgram from 'node:dgram';
import crypto from 'node:crypto';
import zlib from 'node:zlib';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const PUBLIC_DIR = path.join(__dirname, 'public');

const PORT = Number(process.env.PORT || 8080);
const PROTOCOL_MAGIC = 0x4144534B; // "ADSK"
const PROTOCOL_VERSION = 1;
const DEFAULT_HOST_PORT = 50990;
const DEFAULT_DISCOVERY_PORT = 50998;
const DEFAULT_RELAY_PORT = 50999;

const PacketType = {
    HELLO: 0x01,
    AUTH_CHALLENGE: 0x02,
    AUTH_RESPONSE: 0x03,
    AUTH_WAITING: 0x04,
    AUTH_RESULT: 0x05,
    PERMISSION_UPDATE: 0x06,
    PING: 0x07,
    PONG: 0x08,
    DISCONNECT: 0x09,
    VIDEO_CONFIG: 0x10,
    VIDEO_FRAME_TILES: 0x11,
    CURSOR_UPDATE: 0x12,
    VIDEO_CONTROL_REQ: 0x13,
    INPUT_MOUSE_MOVE: 0x20,
    INPUT_MOUSE_BUTTON: 0x21,
    INPUT_MOUSE_WHEEL: 0x22,
    INPUT_KEY_EVENT: 0x23,
    INPUT_RELEASE_ALL: 0x24,
    SYSTEM_ACTION: 0x25,
    CLIPBOARD_TEXT: 0x30,
    CHAT_MESSAGE: 0x36,
    RELAY_LOOKUP: 0x52,
    RELAY_LOOKUP_RESP: 0x53,
    RELAY_CONNECT_REQ: 0x54,
    RELAY_BRIDGE_READY: 0x57
};

const FLAG_ENCRYPTED = 0x01;
const WEB_CLIENT_DESK_ID = BigInt(Math.floor(100000000 + Math.random() * 899999999));
const WEB_HOSTNAME = `WebViewer-${os.hostname()}`;

// ---------------- Discovery Cache ----------------
const discoveredPeers = new Map(); // deskIdStr -> { deskId, hostname, ip, port, lastSeen }
const udpSocket = dgram.createSocket({ type: 'udp4', reuseAddr: true });

udpSocket.on('message', (msg, rinfo) => {
    if (msg.length < 87) return;
    const magic = msg.readUInt32LE(0);
    if (magic !== PROTOCOL_MAGIC) return;
    const kind = msg.readUInt8(4);
    if (kind !== 1) return;
    const deskId = msg.readBigUInt64LE(5);
    const tcpPort = msg.readUInt16LE(21);
    const rawHost = msg.subarray(23, 87);
    const nulIdx = rawHost.indexOf(0);
    const hostname = rawHost.subarray(0, nulIdx >= 0 ? nulIdx : 64).toString('utf8');

    const formatted = formatDeskId(deskId);
    discoveredPeers.set(deskId.toString(), {
        deskId: deskId.toString(),
        formattedId: formatted,
        hostname,
        ip: rinfo.address,
        port: tcpPort,
        lastSeen: Date.now()
    });
});

udpSocket.bind(0, () => {
    try { udpSocket.setBroadcast(true); } catch {}
    sendLanDiscoveryQuery(0n);
});

function sendLanDiscoveryQuery(targetId = 0n) {
    const pkt = Buffer.alloc(87, 0);
    pkt.writeUInt32LE(PROTOCOL_MAGIC, 0);
    pkt.writeUInt8(targetId === 0n ? 1 : 2, 4);
    pkt.writeBigUInt64LE(WEB_CLIENT_DESK_ID, 5);
    pkt.writeBigUInt64LE(targetId, 13);
    pkt.writeUInt16LE(0, 21);
    Buffer.from(WEB_HOSTNAME.slice(0, 63), 'utf8').copy(pkt, 23);

    for (let slot = 0; slot < 6; slot++) {
        const port = DEFAULT_DISCOVERY_PORT - slot;
        udpSocket.send(pkt, port, '127.0.0.1');
        udpSocket.send(pkt, port, '255.255.255.255');
    }
}

setInterval(() => sendLanDiscoveryQuery(0n), 3000).unref();

// ---------------- Crypto & Binary Protocol Helpers ----------------
function formatDeskId(idBig) {
    const s = idBig.toString().padStart(9, '0');
    return `${s.slice(0, 3)} ${s.slice(3, 6)} ${s.slice(6, 9)}`;
}

function parseDeskId(input) {
    const clean = String(input || '').replace(/[\s\-]/g, '');
    if (/^\d{9}$/.test(clean)) {
        const val = BigInt(clean);
        if (val >= 100000000n && val <= 999999999n) return val;
    }
    return 0n;
}

function writeStringBuf(str) {
    const sBuf = Buffer.from(String(str || ''), 'utf8');
    const len = Math.min(sBuf.length, 65535);
    const out = Buffer.allocUnsafe(2 + len);
    out.writeUInt16LE(len, 0);
    sBuf.copy(out, 2, 0, len);
    return out;
}

function sha256Buf(buf) {
    return crypto.createHash('sha256').update(buf).digest();
}

function hashPasswordToken(passwordPlain, hostIdBig) {
    const combined = `AeroDesk-v1:${hostIdBig.toString()}:${passwordPlain}`;
    return crypto.createHash('sha256').update(Buffer.from(combined, 'utf8')).digest('hex');
}

function computeChallengeResponse(passwordPlain, hostIdBig, clientIdBig, nonceBuf) {
    const token = hashPasswordToken(passwordPlain, hostIdBig);
    const strPart = writeStringBuf(token);
    const idsPart = Buffer.allocUnsafe(16);
    idsPart.writeBigUInt64LE(hostIdBig, 0);
    idsPart.writeBigUInt64LE(clientIdBig, 8);
    return sha256Buf(Buffer.concat([strPart, idsPart, nonceBuf]));
}

function deriveSessionKey(hostIdBig, clientIdBig, nonceBuf) {
    const labelPart = writeStringBuf('AeroDesk-SessionKey-v1');
    const idsPart = Buffer.allocUnsafe(16);
    idsPart.writeBigUInt64LE(hostIdBig, 0);
    idsPart.writeBigUInt64LE(clientIdBig, 8);
    return sha256Buf(Buffer.concat([labelPart, idsPart, nonceBuf]));
}

function sessionFingerprintHex(sessionKeyBuf) {
    const d = sha256Buf(sessionKeyBuf);
    const h = d.subarray(0, 4).toString('hex').toUpperCase();
    return `${h.slice(0, 4)}-${h.slice(4, 8)}`;
}

const MASK64 = 0xFFFFFFFFFFFFFFFFn;

function transformPayload(buf, sessionKeyBuf, frameSeqBig) {
    if (!buf || buf.length === 0) return;
    const k0 = sessionKeyBuf.readBigUInt64LE(0);
    const k1 = sessionKeyBuf.readBigUInt64LE(8);
    const k2 = sessionKeyBuf.readBigUInt64LE(16);
    const k3 = sessionKeyBuf.readBigUInt64LE(24);

    let state = (k0 ^ ((k1 + 0x9E3779B97F4A7C15n) & MASK64) ^ ((frameSeqBig * 0xBF58476D1CE4E5B9n) & MASK64)) & MASK64;

    const nextWord = () => {
        state = (state + 0x9E3779B97F4A7C15n) & MASK64;
        let z = (state ^ k2) & MASK64;
        z = (((z ^ (z >> 30n)) & MASK64) * 0xBF58476D1CE4E5B9n) & MASK64;
        z = (((z ^ (z >> 27n) ^ k3) & MASK64) * 0x94D049BB133111EBn) & MASK64;
        return (z ^ (z >> 31n)) & MASK64;
    };

    let i = 0;
    const len = buf.length;
    while (i + 8 <= len) {
        const ks = nextWord();
        const chunk = buf.readBigUInt64LE(i) ^ ks;
        buf.writeBigUInt64LE(chunk, i);
        i += 8;
    }
    if (i < len) {
        const ks = nextWord();
        const tail = Buffer.allocUnsafe(8);
        tail.writeBigUInt64LE(ks, 0);
        for (let j = 0; i < len; i++, j++) {
            buf[i] ^= tail[j];
        }
    }
}

// ---------------- RFC 6455 WebSocket Implementation ----------------
function sendWsJson(socket, obj) {
    if (!socket || socket.destroyed) return;
    const payload = Buffer.from(JSON.stringify(obj), 'utf8');
    sendWsFrame(socket, 0x01, payload);
}

function sendWsBinary(socket, payload) {
    if (!socket || socket.destroyed) return;
    sendWsFrame(socket, 0x02, payload);
}

function sendWsFrame(socket, opcode, payload) {
    const len = payload.length;
    let header;
    if (len < 126) {
        header = Buffer.allocUnsafe(2);
        header[0] = 0x80 | opcode;
        header[1] = len;
    } else if (len < 65536) {
        header = Buffer.allocUnsafe(4);
        header[0] = 0x80 | opcode;
        header[1] = 126;
        header.writeUInt16BE(len, 2);
    } else {
        header = Buffer.allocUnsafe(10);
        header[0] = 0x80 | opcode;
        header[1] = 127;
        header.writeBigUInt64BE(BigInt(len), 2);
    }
    socket.write(Buffer.concat([header, payload]));
}

// ---------------- Target Resolution (LAN UDP + Relay TCP) ----------------
async function resolveTargetDesk(targetInput) {
    const trimmed = String(targetInput || '').trim();
    const deskId = parseDeskId(trimmed);
    if (deskId === 0n) {
        const colon = trimmed.lastIndexOf(':');
        if (colon > 0) {
            return { ip: trimmed.slice(0, colon), port: Number(trimmed.slice(colon + 1)) || DEFAULT_HOST_PORT };
        }
        return { ip: trimmed, port: DEFAULT_HOST_PORT };
    }

    sendLanDiscoveryQuery(deskId);
    for (let i = 0; i < 6; i++) {
        const peer = discoveredPeers.get(deskId.toString());
        if (peer && Date.now() - peer.lastSeen < 15000) {
            return { ip: peer.ip, port: peer.port };
        }
        await new Promise(r => setTimeout(r, 45));
    }

    // Fallback: Query local Relay Server on 127.0.0.1:50999
    return new Promise((resolve) => {
        const rs = net.connect({ host: '127.0.0.1', port: DEFAULT_RELAY_PORT, timeout: 1200 });
        let buf = Buffer.alloc(0);
        rs.on('connect', () => {
            const pay = Buffer.allocUnsafe(8);
            pay.writeBigUInt64LE(deskId, 0);
            const hdr = Buffer.allocUnsafe(10);
            hdr.writeUInt32LE(PROTOCOL_MAGIC, 0);
            hdr.writeUInt8(PacketType.RELAY_LOOKUP, 4);
            hdr.writeUInt8(0, 5);
            hdr.writeUInt32LE(8, 6);
            rs.write(Buffer.concat([hdr, pay]));
        });
        rs.on('data', (chunk) => {
            buf = Buffer.concat([buf, chunk]);
            if (buf.length >= 10) {
                const pSize = buf.readUInt32LE(6);
                if (buf.length >= 10 + pSize) {
                    const p = buf.subarray(10, 10 + pSize);
                    const found = p.readUInt8(0);
                    if (found) {
                        const ipLen = p.readUInt16LE(1);
                        const ip = p.subarray(3, 3 + ipLen).toString('utf8');
                        const port = p.readUInt16LE(3 + ipLen);
                        rs.destroy();
                        resolve({ ip, port });
                        return;
                    }
                    rs.destroy();
                    resolve(null);
                }
            }
        });
        rs.on('error', () => resolve(null));
        rs.on('timeout', () => { rs.destroy(); resolve(null); });
    });
}

// ---------------- Bridge Session Per Browser WebSocket ----------------
function attachWsBridge(wsSocket) {
    let tcpSock = null;
    let encrypted = false;
    let sessionKey = null;
    let sendSeq = 0n;
    let recvSeq = 0n;
    let tcpRecvBuf = Buffer.alloc(0);
    let pingTimer = null;

    function cleanupTcp() {
        if (pingTimer) { clearInterval(pingTimer); pingTimer = null; }
        if (tcpSock) {
            try { tcpSock.destroy(); } catch {}
            tcpSock = null;
        }
        encrypted = false;
        sessionKey = null;
    }

    function sendAeroFrame(type, flags, payloadBuf) {
        if (!tcpSock || tcpSock.destroyed) return;
        const pay = payloadBuf ? Buffer.from(payloadBuf) : Buffer.alloc(0);
        let actualFlags = flags;
        if (encrypted && sessionKey) {
            actualFlags |= FLAG_ENCRYPTED;
            const seq = sendSeq++;
            if (pay.length > 0) {
                transformPayload(pay, sessionKey, seq);
            }
        }
        const hdr = Buffer.allocUnsafe(10);
        hdr.writeUInt32LE(PROTOCOL_MAGIC, 0);
        hdr.writeUInt8(type, 4);
        hdr.writeUInt8(actualFlags, 5);
        hdr.writeUInt32LE(pay.length, 6);
        tcpSock.write(Buffer.concat([hdr, pay]));
    }

    async function handleWsCommand(cmd) {
        if (cmd.type === 'connect') {
            cleanupTcp();
            sendWsJson(wsSocket, { type: 'status', state: 'resolving', message: `Resolving ${cmd.target}...` });
            const resolved = await resolveTargetDesk(cmd.target);
            if (!resolved || !resolved.ip) {
                sendWsJson(wsSocket, { type: 'error', message: `Could not locate remote desk "${cmd.target}".` });
                return;
            }

            sendWsJson(wsSocket, { type: 'status', state: 'connecting', message: `Connecting to ${resolved.ip}:${resolved.port}...` });
            tcpSock = net.connect({ host: resolved.ip, port: resolved.port });
            tcpSock.setNoDelay(true);
            tcpRecvBuf = Buffer.alloc(0);
            encrypted = false;
            sendSeq = 0n;
            recvSeq = 0n;

            let remoteIdBig = 0n;
            let remoteHost = '';
            let challengeNonce = null;

            tcpSock.on('connect', () => {
                const verBuf = Buffer.allocUnsafe(10);
                verBuf.writeUInt16LE(PROTOCOL_VERSION, 0);
                verBuf.writeBigUInt64LE(WEB_CLIENT_DESK_ID, 2);
                const helloPay = Buffer.concat([verBuf, writeStringBuf(WEB_HOSTNAME)]);
                sendAeroFrame(PacketType.HELLO, 0, helloPay);
            });

            tcpSock.on('data', (chunk) => {
                tcpRecvBuf = Buffer.concat([tcpRecvBuf, chunk]);
                while (tcpRecvBuf.length >= 10) {
                    const magic = tcpRecvBuf.readUInt32LE(0);
                    if (magic !== PROTOCOL_MAGIC) {
                        cleanupTcp();
                        sendWsJson(wsSocket, { type: 'error', message: 'Protocol framing error.' });
                        return;
                    }
                    const pktType = tcpRecvBuf.readUInt8(4);
                    const flags = tcpRecvBuf.readUInt8(5);
                    const payloadLen = tcpRecvBuf.readUInt32LE(6);
                    if (payloadLen > 32 * 1024 * 1024) {
                        cleanupTcp();
                        sendWsJson(wsSocket, { type: 'error', message: 'Oversized frame rejected.' });
                        return;
                    }
                    if (tcpRecvBuf.length < 10 + payloadLen) break;

                    const payload = Buffer.from(tcpRecvBuf.subarray(10, 10 + payloadLen));
                    tcpRecvBuf = tcpRecvBuf.subarray(10 + payloadLen);

                    if (encrypted && sessionKey) {
                        if ((flags & FLAG_ENCRYPTED) === 0) {
                            cleanupTcp();
                            sendWsJson(wsSocket, { type: 'error', message: 'Unencrypted frame rejected.' });
                            return;
                        }
                        const seq = recvSeq++;
                        if (payload.length > 0) {
                            transformPayload(payload, sessionKey, seq);
                        }
                    }

                    handleHostFrame(pktType, payload, {
                        getRemoteId: () => remoteIdBig,
                        setRemoteId: (v) => { remoteIdBig = v; },
                        getRemoteHost: () => remoteHost,
                        setRemoteHost: (v) => { remoteHost = v; },
                        getNonce: () => challengeNonce,
                        setNonce: (v) => { challengeNonce = v; },
                        password: String(cmd.password || '')
                    });
                }
            });

            tcpSock.on('close', () => {
                cleanupTcp();
                sendWsJson(wsSocket, { type: 'disconnected' });
            });

            tcpSock.on('error', (err) => {
                cleanupTcp();
                sendWsJson(wsSocket, { type: 'error', message: `Socket error: ${err.message}` });
            });
        } else if (cmd.type === 'disconnect') {
            sendAeroFrame(PacketType.DISCONNECT, 0, null);
            cleanupTcp();
            sendWsJson(wsSocket, { type: 'disconnected' });
        } else if (cmd.type === 'mouse_move' && encrypted) {
            const b = Buffer.allocUnsafe(8);
            b.writeFloatLE(Number(cmd.x) || 0, 0);
            b.writeFloatLE(Number(cmd.y) || 0, 4);
            sendAeroFrame(PacketType.INPUT_MOUSE_MOVE, 0, b);
        } else if (cmd.type === 'mouse_button' && encrypted) {
            const b = Buffer.allocUnsafe(10);
            b.writeUInt8(Number(cmd.button) || 1, 0);
            b.writeUInt8(cmd.down ? 1 : 0, 1);
            b.writeFloatLE(Number(cmd.x) || 0, 2);
            b.writeFloatLE(Number(cmd.y) || 0, 6);
            sendAeroFrame(PacketType.INPUT_MOUSE_BUTTON, 0, b);
        } else if (cmd.type === 'mouse_wheel' && encrypted) {
            const b = Buffer.allocUnsafe(8);
            b.writeInt32LE(Number(cmd.deltaY) || 0, 0);
            b.writeInt32LE(0, 4);
            sendAeroFrame(PacketType.INPUT_MOUSE_WHEEL, 0, b);
        } else if (cmd.type === 'key' && encrypted) {
            const b = Buffer.allocUnsafe(6);
            b.writeUInt16LE(Number(cmd.vk) || 0, 0);
            b.writeUInt16LE(0, 2);
            b.writeUInt8(cmd.down ? 1 : 0, 4);
            b.writeUInt8(0, 5);
            sendAeroFrame(PacketType.INPUT_KEY_EVENT, 0, b);
        } else if (cmd.type === 'release_all' && encrypted) {
            sendAeroFrame(PacketType.INPUT_RELEASE_ALL, 0, null);
        } else if (cmd.type === 'video_control' && encrypted) {
            const b = Buffer.allocUnsafe(8);
            b.writeUInt8(Number(cmd.quality ?? 1), 0);
            b.writeInt32LE(Number(cmd.monitor ?? 0), 1);
            b.writeUInt8(cmd.keyframe ? 1 : 0, 5);
            b.writeUInt8(Number(cmd.fps ?? 30), 6);
            b.writeUInt8(cmd.adaptive ? 1 : 0, 7);
            sendAeroFrame(PacketType.VIDEO_CONTROL_REQ, 0, b);
        } else if (cmd.type === 'system_action' && encrypted) {
            const b = Buffer.allocUnsafe(1);
            b.writeUInt8(Number(cmd.action) || 1, 0);
            sendAeroFrame(PacketType.SYSTEM_ACTION, 0, b);
        } else if (cmd.type === 'chat' && encrypted) {
            const pay = Buffer.concat([writeStringBuf(WEB_HOSTNAME), writeStringBuf(cmd.text)]);
            sendAeroFrame(PacketType.CHAT_MESSAGE, 0, pay);
        }
    }

    function handleHostFrame(pktType, payload, ctx) {
        let pos = 0;
        const readU8 = () => payload.readUInt8(pos++);
        const readU16 = () => { const v = payload.readUInt16LE(pos); pos += 2; return v; };
        const readI32 = () => { const v = payload.readInt32LE(pos); pos += 4; return v; };
        const readU32 = () => { const v = payload.readUInt32LE(pos); pos += 4; return v; };
        const readU64 = () => { const v = payload.readBigUInt64LE(pos); pos += 8; return v; };
        const readString = () => {
            const len = readU16();
            const s = payload.subarray(pos, pos + len).toString('utf8');
            pos += len;
            return s;
        };

        if (pktType === PacketType.AUTH_CHALLENGE) {
            const rId = readU64();
            const rHost = readString();
            const nonce = Buffer.from(payload.subarray(pos, pos + 32));
            ctx.setRemoteId(rId);
            ctx.setRemoteHost(rHost);
            ctx.setNonce(nonce);

            const resp = Buffer.allocUnsafe(33);
            if (ctx.password.length > 0) {
                resp.writeUInt8(1, 0);
                const dig = computeChallengeResponse(ctx.password, rId, WEB_CLIENT_DESK_ID, nonce);
                dig.copy(resp, 1);
            } else {
                resp.writeUInt8(0, 0);
                resp.fill(0, 1);
            }
            sendAeroFrame(PacketType.AUTH_RESPONSE, 0, resp);
        } else if (pktType === PacketType.AUTH_WAITING) {
            sendWsJson(wsSocket, {
                type: 'status',
                state: 'waiting',
                message: `Waiting for ${ctx.getRemoteHost()} (${formatDeskId(ctx.getRemoteId())}) to click Accept...`
            });
        } else if (pktType === PacketType.AUTH_RESULT) {
            const code = readU8();
            const perms = readU8();
            const msg = readString();
            if (code !== 0) {
                cleanupTcp();
                sendWsJson(wsSocket, { type: 'error', message: msg || 'Authentication rejected.' });
                return;
            }
            sessionKey = deriveSessionKey(ctx.getRemoteId(), WEB_CLIENT_DESK_ID, ctx.getNonce());
            encrypted = true;
            sendSeq = 0n;
            recvSeq = 0n;
            const sas = sessionFingerprintHex(sessionKey);

            sendWsJson(wsSocket, {
                type: 'connected',
                remoteId: formatDeskId(ctx.getRemoteId()),
                remoteHost: ctx.getRemoteHost(),
                permissions: perms,
                sas
            });

            // Request initial keyframe
            const vc = Buffer.allocUnsafe(8);
            vc.writeUInt8(1, 0); // Balanced
            vc.writeInt32LE(0, 1);
            vc.writeUInt8(1, 5); // Keyframe
            vc.writeUInt8(30, 6);
            vc.writeUInt8(1, 7);
            sendAeroFrame(PacketType.VIDEO_CONTROL_REQ, 0, vc);

            pingTimer = setInterval(() => {
                const pb = Buffer.allocUnsafe(12);
                pb.writeBigUInt64LE(BigInt(Date.now()), 0);
                pb.writeUInt32LE(15, 8);
                sendAeroFrame(PacketType.PING, 0, pb);
            }, 1500);
        } else if (pktType === PacketType.VIDEO_CONFIG) {
            const monIdx = readI32();
            const fw = readI32();
            const fh = readI32();
            const mcount = readU16();
            sendWsJson(wsSocket, { type: 'video_config', monitorIndex: monIdx, width: fw, height: fh, monitorCount: mcount });
        } else if (pktType === PacketType.VIDEO_FRAME_TILES) {
            const fw = readU16();
            const fh = readU16();
            const tileCount = readU16();

            for (let i = 0; i < tileCount; i++) {
                const tx = readU16();
                const ty = readU16();
                const tw = readU16();
                const th = readU16();
                const enc = readU8();
                const dSize = readU32();
                const tileData = payload.subarray(pos, pos + dSize);
                pos += dSize;

                if (enc === 2) {
                    // JPEG tile: send binary frame [type=2, tx, ty, tw, th, jpegBytes...]
                    const hdr = Buffer.allocUnsafe(9);
                    hdr.writeUInt8(2, 0);
                    hdr.writeUInt16LE(tx, 1);
                    hdr.writeUInt16LE(ty, 3);
                    hdr.writeUInt16LE(tw, 5);
                    hdr.writeUInt16LE(th, 7);
                    sendWsBinary(wsSocket, Buffer.concat([hdr, tileData]));
                } else {
                    // Zstd (1) or RawBGRA (0) -> convert BGRA to RGBA and send binary frame [type=0, tx, ty, tw, th, rgbaBytes...]
                    let rawBgra = tileData;
                    if (enc === 1) {
                        try {
                            rawBgra = zlib.zstdDecompressSync(tileData);
                        } catch {
                            continue;
                        }
                    }
                    const rgba = Buffer.from(rawBgra);
                    for (let p = 0; p + 3 < rgba.length; p += 4) {
                        const b = rgba[p];
                        rgba[p] = rgba[p + 2];
                        rgba[p + 2] = b;
                        rgba[p + 3] = 255;
                    }
                    const hdr = Buffer.allocUnsafe(9);
                    hdr.writeUInt8(0, 0);
                    hdr.writeUInt16LE(tx, 1);
                    hdr.writeUInt16LE(ty, 3);
                    hdr.writeUInt16LE(tw, 5);
                    hdr.writeUInt16LE(th, 7);
                    sendWsBinary(wsSocket, Buffer.concat([hdr, rgba]));
                }
            }
            sendWsJson(wsSocket, { type: 'frame_sync', width: fw, height: fh, tiles: tileCount });
        } else if (pktType === PacketType.PONG) {
            const sentTs = Number(readU64());
            const rtt = Math.max(1, Date.now() - sentTs);
            sendWsJson(wsSocket, { type: 'pong', rtt });
        } else if (pktType === PacketType.CHAT_MESSAGE) {
            const sender = readString();
            const text = readString();
            sendWsJson(wsSocket, { type: 'chat', sender, text });
        } else if (pktType === PacketType.PERMISSION_UPDATE) {
            const perms = readU8();
            sendWsJson(wsSocket, { type: 'permissions', permissions: perms });
        }
    }

    // Parse incoming RFC 6455 client frames from browser
    let wsBuf = Buffer.alloc(0);
    wsSocket.on('data', (chunk) => {
        wsBuf = Buffer.concat([wsBuf, chunk]);
        while (wsBuf.length >= 2) {
            const byte0 = wsBuf[0];
            const byte1 = wsBuf[1];
            const opcode = byte0 & 0x0F;
            const masked = (byte1 & 0x80) !== 0;
            let payloadLen = byte1 & 0x7F;
            let offset = 2;

            if (payloadLen === 126) {
                if (wsBuf.length < 4) return;
                payloadLen = wsBuf.readUInt16BE(2);
                offset = 4;
            } else if (payloadLen === 127) {
                if (wsBuf.length < 10) return;
                payloadLen = Number(wsBuf.readBigUInt64BE(2));
                offset = 10;
            }

            if (payloadLen > 1024 * 1024) {
                cleanupTcp();
                wsSocket.destroy();
                return;
            }

            const maskBytes = masked ? 4 : 0;
            if (wsBuf.length < offset + maskBytes + payloadLen) return;

            const mask = masked ? wsBuf.subarray(offset, offset + 4) : null;
            offset += maskBytes;
            const data = Buffer.from(wsBuf.subarray(offset, offset + payloadLen));
            wsBuf = wsBuf.subarray(offset + payloadLen);

            if (opcode === 0x08) {
                cleanupTcp();
                wsSocket.end();
                return;
            }
            if (masked && mask) {
                for (let i = 0; i < data.length; i++) {
                    data[i] ^= mask[i & 3];
                }
            }
            if (opcode === 0x09) {
                sendWsFrame(wsSocket, 0x0A, data);
                continue;
            }
            if (opcode === 0x01) {
                try {
                    const cmd = JSON.parse(data.toString('utf8'));
                    handleWsCommand(cmd);
                } catch {}
            }
        }
    });

    wsSocket.on('close', cleanupTcp);
    wsSocket.on('error', cleanupTcp);
}

// ---------------- HTTP Server ----------------
const MIME_TYPES = {
    '.html': 'text/html; charset=utf-8',
    '.css': 'text/css; charset=utf-8',
    '.js': 'application/javascript; charset=utf-8',
    '.json': 'application/json; charset=utf-8'
};

const server = http.createServer((req, res) => {
    const url = new URL(req.url || '/', `http://${req.headers.host || 'localhost'}`);

    if (url.pathname === '/api/peers') {
        sendLanDiscoveryQuery(0n);
        const now = Date.now();
        const active = [];
        for (const p of discoveredPeers.values()) {
            if (now - p.lastSeen <= 15000) active.push(p);
        }
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({
            webViewerId: formatDeskId(WEB_CLIENT_DESK_ID),
            hostname: WEB_HOSTNAME,
            peers: active
        }));
        return;
    }

    const relPath = url.pathname === '/' ? 'index.html' : url.pathname.replace(/^\/+/, '');
    const filePath = path.resolve(PUBLIC_DIR, relPath);
    if (filePath !== PUBLIC_DIR && !filePath.startsWith(PUBLIC_DIR + path.sep)) {
        res.writeHead(403);
        res.end('Forbidden');
        return;
    }

    fs.readFile(filePath, (err, content) => {
        if (err) {
            res.writeHead(404);
            res.end('Not found');
            return;
        }
        const ext = path.extname(filePath);
        res.writeHead(200, { 'Content-Type': MIME_TYPES[ext] || 'application/octet-stream' });
        res.end(content);
    });
});

server.on('upgrade', (req, socket) => {
    const key = req.headers['sec-websocket-key'];
    if (!key) {
        socket.destroy();
        return;
    }
    const accept = crypto
        .createHash('sha1')
        .update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11')
        .digest('base64');

    socket.write(
        'HTTP/1.1 101 Switching Protocols\r\n' +
        'Upgrade: websocket\r\n' +
        'Connection: Upgrade\r\n' +
        `Sec-WebSocket-Accept: ${accept}\r\n\r\n`
    );
    attachWsBridge(socket);
});

server.listen(PORT, () => {
    console.log(`[AeroDesk Web Portal] Listening on http://localhost:${PORT}`);
    console.log(`[AeroDesk Web Portal] Web Client Desk ID: ${formatDeskId(WEB_CLIENT_DESK_ID)}`);
});
