const path = require('path');
const express = require('express');
const bodyParser = require('body-parser');
const cors = require('cors');
const http = require('http');
const WebSocket = require('ws');

const app = express();
app.use(cors());
app.use(bodyParser.json());

const PORT = process.env.MOCK_PORT || 3000;

// Serve the UI from ./data
app.use('/', express.static(path.join(__dirname, 'data')));

// Simple in-memory state for mocks
let bootMs = Date.now();
let wifi = { mode: 'ap', staSsid: '', apSsid: 'ESP32-AV-Tool', apChan: 6 };
let captures = [
  { id: 'c1', ts: Date.now(), srcIp: '192.168.4.10', srcPort: 23, localPort: 5000, hex: 'AA 11', ascii: 'AA', pinned: false, repeats: 1, lastTs: Date.now(), suffixHint: '\\r', payloadType: 'ascii' }
];

// Endpoints
app.get('/api/health', (req, res) => {
  const uptime_s = Math.floor((Date.now() - bootMs) / 1000);
  res.json({ fw: 'dev', uptime_s, heap_free: 123456, wifi: { mode: wifi.mode, staConnected: false, staIp: '', staSsid: wifi.staSsid, apIp: '192.168.4.1', apSsid: wifi.apSsid, rssi: 0 }, learn: { enabled: false, port: 6100 } });
});

app.get('/api/wifi', (req, res) => { res.json({ mode: wifi.mode, staSsid: wifi.staSsid, apSsid: wifi.apSsid, apChan: wifi.apChan }); });

app.post('/api/wifi', (req, res) => {
  const body = req.body || {};
  if (typeof body.mode === 'string') wifi.mode = body.mode;
  if (typeof body.staSsid === 'string') wifi.staSsid = body.staSsid;
  if (typeof body.apSsid === 'string') wifi.apSsid = body.apSsid;
  if (typeof body.apPass === 'string') {/* ignore */}
  res.json({ ok: true, note: 'mocked: reboot not necessary' });
});

app.get('/api/wifi/scan', (req, res) => {
  const doc = { count: 3, note: 'Mock scan', networks: [ { ssid: 'HomeWiFi', rssi: -45, chan: 6, open: false }, { ssid: 'ESP32-AV-Tool', rssi: -30, chan: wifi.apChan, open: true }, { ssid: 'Guest', rssi: -70, chan: 11, open: false } ] };
  res.json(JSON.stringify(doc));
});

app.get('/api/captures', (req, res) => { res.json({ captures }); });

// Simple ping endpoint
app.get('/api/ping', (req, res) => { res.json({ host: req.query.host || '8.8.8.8', ok: true, avg_time_ms: 3 }); });

// Serve discovery results mock
app.get('/api/discovery/results', (req, res) => {
  res.json({ running: false, progress: 100, results: [] });
});

// Create HTTP server and upgrade to websockets
const server = http.createServer(app);
const wss = new WebSocket.Server({ server, path: '/ws' });
const wsTerm = new WebSocket.Server({ server, path: '/term' });

wss.on('connection', (socket) => {
  socket.send('log connected');
  const id = setInterval(() => {
    socket.send(JSON.stringify({ t: 'log', msg: `Mock log ${new Date().toISOString()}` }));
  }, 4000);
  socket.on('close', () => clearInterval(id));
});

wsTerm.on('connection', (socket) => {
  socket.send(JSON.stringify({ type: 'status', msg: 'term connected (mock)' }));
  socket.on('message', (m) => {
    // echo back
    socket.send(JSON.stringify({ type: 'echo', data: m.toString() }));
  });
});

server.listen(PORT, () => {
  console.log(`Mock server listening: http://localhost:${PORT}/`);
});
