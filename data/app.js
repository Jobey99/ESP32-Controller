const $ = (id) => document.getElementById(id);

window.onerror = function (message, source, lineno, colno) {
  try { $("healthLine").textContent = `JS ERROR: ${message} @ ${lineno}`; } catch { }
};

function esc(s) { return (s || "").replaceAll("&", "&amp;").replaceAll("<", "&lt;"); }

async function apiGet(path) {
  const r = await fetch(path, { cache: "no-store" });
  const t = await r.text();
  if (!r.ok) throw new Error(t);
  try { return JSON.parse(t); } catch { throw new Error("Bad JSON: " + t); }
}

async function apiPost(path, obj) {
  const r = await fetch(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(obj)
  });
  const t = await r.text();
  let j;
  try { j = JSON.parse(t); } catch { j = { raw: t }; }
  if (!r.ok) throw new Error(j.error || t);
  return j;
}

// ---------- Sidebar / Navigation ----------
function setupSidebar() {
  const sidebar = $("sidebar");
  const toggle = $("menuToggle");
  const links = document.querySelectorAll(".nav-item");
  const panels = document.querySelectorAll(".panel");
  const title = $("pageTitle");

  // Nav Click
  links.forEach(link => {
    link.onclick = (e) => {
      // Setup UI
      links.forEach(x => x.classList.remove("active"));
      link.classList.add("active");

      const targetId = link.dataset.target;
      panels.forEach(p => p.classList.remove("show"));
      document.getElementById(targetId).classList.add("show");

      title.textContent = link.textContent;

      // Close mobile menu if open
      sidebar.classList.remove("open");
    };
  });

  // Mobile Toggle
  if (toggle) {
    toggle.onclick = () => {
      sidebar.classList.toggle("open");
    };
  }

  document.querySelector('.main-content').onclick = (e) => {
    if (e.target !== toggle && window.innerWidth <= 768) {
      sidebar.classList.remove("open");
    }
  };
}

// Connectivity Tabs
window.switchConn = (tab, targetId) => {
  document.querySelectorAll("#tab-conn .tab").forEach(t => t.classList.remove("active"));
  tab.classList.add("active");

  document.querySelectorAll("#tab-conn .subpanel").forEach(p => p.classList.remove("show"));
  $(targetId).classList.add("show");
};


// ---------- Network Tools Logic ----------

// WiFi Chart
function drawWifiChart(networks) {
  const cvs = $("wifiChart");
  if (!cvs) return;
  const ctx = cvs.getContext("2d");
  const w = cvs.width = cvs.parentElement.offsetWidth;
  const h = cvs.height = 200;

  ctx.fillStyle = "#111";
  ctx.fillRect(0, 0, w, h);

  if (!networks || !networks.length) {
    ctx.fillStyle = "#666";
    ctx.fillText("No Scan Data", 20, 30);
    return;
  }

  // Sort by RSSI weak -> strong for layering or just iterate
  // Draw Bars
  const barW = Math.min(60, (w - 40) / networks.length);
  const maxRssi = -30;
  const minRssi = -95;

  networks.forEach((n, i) => {
    const rssi = Math.max(Math.min(n.rssi, maxRssi), minRssi);
    const norm = (rssi - minRssi) / (maxRssi - minRssi); // 0..1
    const barH = norm * (h - 40);
    const x = 20 + i * (barW + 5);
    const y = h - 20 - barH;

    // Color based on strength
    ctx.fillStyle = norm > 0.7 ? "#22c55e" : (norm > 0.4 ? "#eab308" : "#ef4444");
    ctx.fillRect(x, y, barW, barH);

    ctx.fillStyle = "#fff";
    ctx.font = "10px sans-serif";
    ctx.fillText(n.ssid.substr(0, 8), x, h - 5);
    ctx.fillStyle = "#aaa";
    ctx.fillText(n.rssi, x, y - 5);
  });
}

// Subnet Calc
window.doSubnetCalc = () => {
  const ip = $("calcIp").value;
  const cidr = parseInt($("calcCidr").value);
  const out = $("calcOut");

  // JS bitwise is 32bit signed, carefully handle IPs
  function ipToLong(ip) {
    return ip.split('.').reduce((acc, oct) => (acc << 8) + parseInt(oct, 10), 0) >>> 0;
  }
  function longToIp(long) {
    return [(long >>> 24) & 255, (long >>> 16) & 255, (long >>> 8) & 255, long & 255].join('.');
  }

  try {
    const mask = (~((1 << (32 - cidr)) - 1)) >>> 0;
    const ipLong = ipToLong(ip);
    const netLong = (ipLong & mask) >>> 0;
    const bcastLong = (netLong | (~mask)) >>> 0;
    const count = Math.pow(2, 32 - cidr) - 2;

    out.innerHTML = `CIDR: /${cidr}\nMask: ${longToIp(mask)}\nNet:  ${longToIp(netLong)}\nBcast:${longToIp(bcastLong)}\nHosts:${count > 0 ? count : 0}\nRange:${longToIp(netLong + 1)} - ${longToIp(bcastLong - 1)}`;
  } catch (e) { out.textContent = "Error: " + e; }
};

// DNS & WAN
async function checkWan() {
  const out = $("wanOut");
  out.textContent = "Checking...";
  try {
    const r = await apiGet("/api/internet");
    out.innerHTML = `DNS Resolve (Google): ${r.dns ? "OK" : "FAIL"}\nPing 8.8.8.8:       ${r.ping ? "OK" : "FAIL"}`;
  } catch (e) { out.textContent = "Error: " + e.message; }
}

async function dnsLookup() {
  const host = $("dnsHost").value;
  if (!host) return;
  $("dnsOut").textContent = "Resolving...";
  try {
    const r = await apiPost("/api/dns", { host });
    $("dnsOut").textContent = r.ok ? `IP: ${r.ip}` : "Not Found";
  } catch (e) { $("dnsOut").textContent = e.message; }
}

// Port Scanner
async function doPortScan() {
  const host = $("psIp").value;
  const pStr = $("psPorts").value;
  if (!host || !pStr) return alert("Missing Target/Ports");

  const ports = pStr.split(',').map(s => parseInt(s.trim())).filter(n => !isNaN(n));
  $("psOut").textContent = `Starting scan on ${host}...`;

  try {
    // Start Scan
    await apiPost("/api/portscan", { host, ports });

    // Poll
    const interval = setInterval(async () => {
      try {
        const r = await apiGet("/api/portscan/status");
        if (r.running) {
          $("psOut").textContent = `Scanning... ${r.progress}%`;
        } else {
          clearInterval(interval);
          const open = r.open.join(", ");
          $("psOut").innerHTML = `Scan Complete.\nOpen Ports: ${open.length ? open : "None"}`;
        }
      } catch (e) {
        clearInterval(interval);
        $("psOut").textContent = "Polling Error: " + e.message;
      }
    }, 1000);

  } catch (e) { $("psOut").textContent = "Error: " + e.message; }
}


// Regular Logic Updates
function setStatus(msg) {
  if ($("wifiStatusOut")) $("wifiStatusOut").textContent = msg;
}

async function refreshHealth() {
  try {
    const h = await apiGet("/api/health");
    $("healthLine").textContent =
      `FW: ${h.fw} | IP: ${h.wifi.staConnected ? h.wifi.staIp : h.wifi.apIp} | Uptime: ${Math.floor(h.uptime_s / 60)}m`;

    if (h.wifi.staConnected) {
      if ($("wifiConnLine")) $("wifiConnLine").textContent =
        `Connected to "${h.wifi.staSsid}" (${h.wifi.staIp}) RSSI ${h.wifi.rssi}dBm`;
    } else {
      if ($("wifiConnLine")) $("wifiConnLine").textContent =
        `Not connected to Station. AP: ${h.wifi.apSsid} (${h.wifi.apIp})`;
    }
  } catch (e) { console.error(e); }
}

async function loadWifiForm() {
  try {
    const w = await apiGet("/api/wifi");
    $("wMode").value = w.mode || "apsta";
    $("wStaSsid").value = w.staSsid || "";
    $("wApSsid").value = w.apSsid || "";
    $("wApCh").value = w.apChan ?? 6;
  } catch (e) { }
}

async function scanCached() {
  const sel = $("wifiScanList");
  sel.options.length = 0;
  sel.add(new Option("Loading...", ""));
  try {
    const res = await apiGet("/api/wifi/scan");
    sel.options.length = 0;
    sel.add(new Option("(Select Network)", ""));
    (res.networks || []).forEach(n => {
      sel.add(new Option(`${n.ssid} (${n.rssi}dBm)${n.open ? "" : "🔒"}`, n.ssid));
    });
    // Draw Chart
    drawWifiChart(res.networks);
  } catch (e) {
    sel.options.length = 0;
    sel.add(new Option("Scan Failed", ""));
  }
}

// Websockets
let wsLog, wsTerm, wsRS232, wsProxy, wsDisc, wsUdp, wsTcpServer;


function logLine(s) {
  const el = $("log"); if (el) { const d = document.createElement("div"); d.textContent = s; el.appendChild(d); el.scrollTop = el.scrollHeight; }
}

function connectLogWs() {
  const proto = location.protocol === "https:" ? "wss" : "ws";
  wsLog = new WebSocket(`${proto}://${location.host}/ws`);
  wsLog.onmessage = (e) => logLine(e.data);
  wsLog.onclose = () => setTimeout(connectLogWs, 2000);
}

function connectTermWs() {
  const proto = location.protocol === "https:" ? "wss" : "ws";
  wsTerm = new WebSocket(`${proto}://${location.host}/term`);
  wsTerm.onmessage = (e) => {
    try {
      const msg = JSON.parse(e.data);
      if (msg.type === "status") $("termOut").innerHTML += `<div class="muted">STATUS: ${msg.connected ? "Connected" : "Discon"}</div>`;
      else if (msg.type === "rx") $("termOut").innerHTML += `<div><span class="rx">RX</span> ${esc(msg.ascii)}</div>`;
      else if (msg.type === "tx") $("termOut").innerHTML += `<div><span class="tx">TX</span> ok</div>`;
      else if (msg.type === "error") $("termOut").innerHTML += `<div class="error">ERROR: ${esc(msg.msg)}</div>`;
      else if (msg.type === "log") $("termOut").innerHTML += `<div class="muted">LOG: ${esc(msg.msg)}</div>`;
      $("termOut").scrollTop = $("termOut").scrollHeight;
    } catch { }
  };
  wsTerm.onclose = () => setTimeout(connectTermWs, 2000);
}

function connectRS232Ws() {
  const proto = location.protocol === "https:" ? "wss" : "ws";
  wsRS232 = new WebSocket(`${proto}://${location.host}/wsrs232`);
  wsRS232.onmessage = (e) => {
    try {
      const msg = JSON.parse(e.data);
      if (msg.type === "status") {
        if (msg.baud && $("rs232Baud")) $("rs232Baud").value = msg.baud;
        if ($("rs232Invert") && msg.invert !== undefined) $("rs232Invert").checked = msg.invert;

        let autoBtn = $("btnAutoScan");
        if (autoBtn) autoBtn.textContent = msg.auto ? "Stop Auto-Baud" : "Auto-Baud Scan";
      }
      else if (msg.type === "rx") $("rs232Out").innerHTML += `<div><span class="rx">RX</span> ${esc(msg.ascii)}</div>`;
      else if (msg.type === "tx") $("rs232Out").innerHTML += `<div><span class="tx">TX</span> ${esc(msg.ascii)}</div>`;
      else if (msg.type === "sys") {
        if ($("rs232SysOut")) $("rs232SysOut").innerHTML = `<div>* ${esc(msg.msg)}</div>` + $("rs232SysOut").innerHTML;
      }
      else if (msg.type === "error") $("rs232Out").innerHTML += `<div class="error">ERROR: ${esc(msg.msg)}</div>`;
      $("rs232Out").scrollTop = $("rs232Out").scrollHeight;
    } catch (e) { }
  };
  wsRS232.onclose = () => setTimeout(connectRS232Ws, 1000);
}

function connectDiscWs() {
  const proto = location.protocol === "https:" ? "wss" : "ws";
  wsDisc = new WebSocket(`${proto}://${location.host}/wsdisc`);
  wsDisc.onmessage = (e) => {
    try {
      const msg = JSON.parse(e.data);
      if (msg.ip) {
        const d = document.createElement("div");
        d.innerHTML = `<b>${msg.ip}</b> ${msg.openPorts.join(",")}`;
        $("discOut").appendChild(d);
      }
    } catch { }
  };
  wsDisc.onclose = () => setTimeout(connectDiscWs, 2000);
}

function connectUdpWs() {
  const proto = location.protocol === "https:" ? "wss" : "ws";
  wsUdp = new WebSocket(`${proto}://${location.host}/wsudp`);
  wsUdp.onmessage = (e) => {
    try {
      const msg = JSON.parse(e.data);
      if (msg.type === "rx") {
        $("udpOut").innerHTML += `<div><span class="rx">RX</span> ${msg.from}:${msg.port} ${esc(msg.ascii)}</div>`;
        $("udpOut").scrollTop = $("udpOut").scrollHeight;
      }
    } catch { }
  };
  wsUdp.onclose = () => setTimeout(connectUdpWs, 2000);
}

function connectTcpServerWs() {
  const proto = location.protocol === "https:" ? "wss" : "ws";
  wsTcpServer = new WebSocket(`${proto}://${location.host}/wstcpserver`);
  wsTcpServer.onmessage = (e) => {
    try {
      const msg = JSON.parse(e.data);
      if (msg.type === "rx") {
        $("tcpServerOut").innerHTML += `<div><span class="rx">RX</span> ${msg.from} ${esc(msg.ascii)}</div>`;
        $("tcpServerOut").scrollTop = $("tcpServerOut").scrollHeight;
      } else if (msg.type === "event") {
        $("tcpServerOut").innerHTML += `<div class="muted">Event: ${msg.event} ${msg.ip}</div>`;
        if (msg.event === "connect") updateTcpClients();
        if (msg.event === "disconnect") updateTcpClients();
      }
    } catch { }
  };
  wsTcpServer.onclose = () => setTimeout(connectTcpServerWs, 2000);
}

// Placeholder for now
function updateTcpClients() {
  // We could fetch /api/tcpserver/clients if we implemented it, 
  // or just rely on the event log for now.
}


// Global helpers
window.openTerminal = (ip, port) => {
  $("termHost").value = ip;
  $("termPort").value = port || 23;
  document.querySelector('[data-target="tab-terminal"]').click();
};

window.sendPreset = (n) => wsRS232 && wsRS232.send(JSON.stringify({ action: "preset", n }));

// Init
window.onload = async () => {
  setupSidebar();
  connectLogWs();
  connectTermWs();
  connectRS232Ws();
  connectDiscWs();
  connectUdpWs();
  connectTcpServerWs();

  loadWifiForm();
  refreshHealth();
  setInterval(refreshHealth, 2000);
  scanCached();

  $("btnWifiScan").onclick = () => {
    setStatus("Scanning...");
    apiGet("/api/wifi/scan?fresh=1").then(scanCached);
  };
  $("btnSaveWifi").onclick = async () => {
    await apiPost("/api/wifi", {
      mode: $("wMode").value,
      staSsid: $("wStaSsid").value,
      staPass: $("wStaPass").value
    });
    alert("Saved.");
  };

  // Help Modal
  const modal = $("helpModal");
  if ($("btnHelp")) $("btnHelp").onclick = () => modal.classList.add("show");
  if ($("btnCloseHelp")) $("btnCloseHelp").onclick = () => modal.classList.remove("show");
  window.onclick = (e) => { if (e.target == modal) modal.classList.remove("show"); };

  // Tools
  $("btnCheckWan").onclick = checkWan;
  $("btnDnsLookup").onclick = dnsLookup;
  $("btnPortScan").onclick = doPortScan;

  // Ping
  $("btnPing").onclick = async () => {
    const h = $("pingHost").value;
    $("pingOut").innerText += `\n> Pinging ${h}...`;
    const res = await apiGet("/api/ping?host=" + h);
    $("pingOut").innerText += `\n  ${res.ok ? "Reply" : "Timeout"} (${res.avg_time_ms}ms)`;
  };

  // MDNS
  const mdnsSel = $("mdnsService");
  const mdnsCust = $("mdnsCustom");
  if (mdnsSel) mdnsSel.onchange = () => {
    mdnsCust.style.display = mdnsSel.value === "custom" ? "block" : "none";
  };

  $("btnMdnsScan").onclick = async () => {
    $("mdnsOut").textContent = "Scanning...";
    let svc = mdnsSel.value === "custom" ? mdnsCust.value : mdnsSel.value;
    const proto = $("mdnsProto").value;

    // Auto-fix common mistakes if user types full string
    if (svc.includes("._tcp")) svc = svc.replace("._tcp", "");
    if (svc.includes("._udp")) svc = svc.replace("._udp", "");

    try {
      await apiPost("/api/mdns/scan", { service: svc, proto: proto });

      // Poll for results (mDNS query runs on background task)
      const interval = setInterval(async () => {
        try {
          const res = await apiGet("/api/mdns/status");
          if (res.running) {
            $("mdnsOut").textContent = "Scanning...";
          } else {
            clearInterval(interval);
            $("mdnsOut").innerHTML = "";
            (res.results || []).forEach(r => {
              $("mdnsOut").innerHTML += `<div class="item"><b>${r.hostname}</b> ${r.ip}:${r.port}</div>`;
            });
            if (!res.results?.length) $("mdnsOut").textContent = "No devices found.";
          }
        } catch (e) { clearInterval(interval); $("mdnsOut").textContent = "Error: " + e; }
      }, 1000);
    } catch (e) { $("mdnsOut").textContent = "Error: " + e.message; }
  };


  // SSDP
  if ($("btnSsdp")) $("btnSsdp").onclick = async () => {
    $("ssdpOut").textContent = "Sending M-SEARCH...";
    try {
      await apiPost("/api/ssdp/scan", {});

      const interval = setInterval(async () => {
        try {
          const res = await apiGet("/api/ssdp/status");
          if (res.running) {
            $("ssdpOut").textContent = `Scanning... Found ${res.results.length} devices...`;
          } else {
            clearInterval(interval);
            $("ssdpOut").innerHTML = "";
            (res.results || []).forEach(r => {
              $("ssdpOut").innerHTML += `<div class="item">
                  <b>${esc(r.friendlyName || r.ip)}</b> <br>
                  <span class="small">${esc(r.usn || r.st)}</span> <br>
                  <a href="${r.url}" target="_blank">${r.url}</a>
                </div>`;
            });
            if (!res.results?.length) $("ssdpOut").textContent = "No devices found.";
          }
        } catch (e) { clearInterval(interval); $("ssdpOut").textContent = "Error: " + e; }
      }, 1000);

    } catch (e) { alert(e.message); }
  };
  await apiPost("/api/discovery/start", { subnet: $("discSubnet").value, from: parseInt($("discFrom").value), to: parseInt($("discTo").value) });

  if ($("btnDiscStop")) $("btnDiscStop").onclick = async () => {
    await apiPost("/api/discovery/stop", {});
    $("discOut").innerHTML += "<div>Stopped.</div>";
  };
  if ($("btnSubnetScan")) $("btnSubnetScan").onclick = async () => {
    $("discOut2").textContent = "Deep scanning...";
    await apiPost("/api/scan/subnet", {});
  };

  // Proxy
  if ($("btnProxyStart")) $("btnProxyStart").onclick = async () => {
    $("proxyOut").innerText = "Starting proxy...";
    await apiPost("/api/proxy/start", {
      listenPort: parseInt($("proxyListen").value),
      targetHost: $("proxyTargetHost").value,
      targetPort: parseInt($("proxyTargetPort").value),
      captureToLearn: $("proxyCapToLearn").checked
    });
  };
  if ($("btnProxyStop")) $("btnProxyStop").onclick = async () => {
    await apiPost("/api/proxy/stop", {});
    $("proxyOut").innerText += "\nStopped.";
  };

  // TCP Server
  const btnTcp = $("btnTcpServerToggle");
  const updateTcpBtn = (running, port) => {
    btnTcp.textContent = running ? "Stop Server" : "Start Server";
    btnTcp.className = running ? "btn btn-danger" : "btn btn-success";
    if ($("tcpServerPort")) $("tcpServerPort").value = port;
    if ($("tcpServerStatus")) $("tcpServerStatus").textContent = running ? "Running" : "Stopped";
  };

  // Check initial state
  apiGet("/api/tcpserver").then(r => updateTcpBtn(r.running, r.port)).catch(() => { });

  if (btnTcp) btnTcp.onclick = async () => {
    const isRunning = btnTcp.classList.contains("btn-danger");
    const port = parseInt($("tcpServerPort").value) || 23;
    try {
      const r = await apiPost("/api/tcpserver", { action: isRunning ? "stop" : "start", port });
      updateTcpBtn(r.running, r.port);
    } catch (e) { alert("Error: " + e.message); }
  };

  // Learner
  if ($("btnSaveLearner")) $("btnSaveLearner").onclick = async () => {
    await apiPost("/api/learner", { enabled: $("learnEnabled").checked, port: parseInt($("learnPort").value) });
    alert("Saved.");
  };

  // PJLink
  window.sendPjl = async (cmd) => {
    const ip = $("pjlIp").value;
    if (!ip) return alert("IP required");
    $("pjlOut").textContent = "Sending...";
    const res = await apiPost("/api/pjlink", { ip, pass: $("pjlPass").value, cmd });
    $("pjlOut").textContent = res.response;
  };




  // RS232 Terminal Handlers
  if ($("btnRs232Send")) $("btnRs232Send").onclick = () => {
    const data = $("rs232Send").value;
    const mode = $("rs232Mode").value;
    const suffix = $("rs232Suffix").value;
    if (wsRS232 && data) {
      wsRS232.send(JSON.stringify({ action: "send", data, mode, suffix }));
      $("rs232Send").value = "";
    }
  };

  if ($("rs232Baud")) $("rs232Baud").onchange = (e) => {
    if (wsRS232) wsRS232.send(JSON.stringify({ action: "baud", baud: parseInt(e.target.value) }));
  };

  if ($("rs232Invert")) $("rs232Invert").onchange = (e) => {
    if (wsRS232) wsRS232.send(JSON.stringify({ action: "invert", val: e.target.checked }));
  };

  if ($("btnLoadProfile")) $("btnLoadProfile").onclick = () => {
    const id = parseInt($("rs232Profile").value);
    if (wsRS232) wsRS232.send(JSON.stringify({ action: "profile", id }));
  };

  let autoBauding = false;
  if ($("btnAutoScan")) $("btnAutoScan").onclick = () => {
    autoBauding = !autoBauding;
    if (wsRS232) wsRS232.send(JSON.stringify({ action: "autobaud", start: autoBauding }));
  };

  if ($("btnLoopback")) $("btnLoopback").onclick = () => {
    if (wsRS232) wsRS232.send(JSON.stringify({ action: "loopback" }));
  };

  // TCP Terminal Handlers
  if ($("btnTermConnect")) $("btnTermConnect").onclick = () => {
    const host = $("termHost").value;
    const port = parseInt($("termPort").value);
    if (wsTerm && host && port) {
      wsTerm.send(JSON.stringify({ action: "connect", host, port }));
    } else {
      alert("Check connection or host/port");
    }
  };

  if ($("btnTermDisconnect")) $("btnTermDisconnect").onclick = () => {
    if (wsTerm) wsTerm.send(JSON.stringify({ action: "disconnect" }));
  };

  if ($("btnTermSend")) $("btnTermSend").onclick = () => {
    const data = $("termSend").value;
    const mode = $("termMode").value;
    const suffix = $("termSuffix").value;
    if (wsTerm && data) {
      wsTerm.send(JSON.stringify({ action: "send", data, mode, suffix }));
      $("termSend").value = "";
    }
  };

  // UDP Send Handler
  if ($("btnUdpSend")) $("btnUdpSend").onclick = async () => {
    try {
      await apiPost("/api/udp/send", {
        ip: $("udpTarget").value,
        port: parseInt($("udpPort").value),
        data: $("udpSendMsg").value
      });
      $("udpOut").innerHTML += `<div><span class="tx">TX</span> sent</div>`;
    } catch (e) { alert(e.message); }
  };

  // UDP Listener Set
  if ($("btnUdpSetPort")) $("btnUdpSetPort").onclick = async () => {
    await apiPost("/api/udp/listen", { port: parseInt($("udpListenPort").value) });
    alert("Listening port updated.");
  };

  // WiFi Forget
  if ($("btnForgetWifi")) $("btnForgetWifi").onclick = async () => {
    if (confirm("Forget WiFi and return to AP mode?")) {
      await apiPost("/api/wifi/forget", {});
      alert("Resetting to AP mode...");
    }
  };

  // Config Tools
  if ($("btnLoadCfg")) $("btnLoadCfg").onclick = async () => {
    const cfg = await apiGet("/api/config");
    $("cfgText").value = JSON.stringify(cfg, null, 2);
  };

  if ($("btnDownloadCfg")) $("btnDownloadCfg").onclick = () => {
    const txt = $("cfgText").value;
    if (!txt) return alert("Load config first");
    const blob = new Blob([txt], { type: "application/json" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = "esp32-av-config.json";
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
  };

  if ($("btnUploadCfg")) $("btnUploadCfg").onclick = async () => {
    const val = $("cfgText").value;
    try {
      JSON.parse(val);
      await apiPost("/api/config", JSON.parse(val));
      alert("Config Saved.");
    } catch (e) { alert("Invalid JSON: " + e.message); }
  };

  // System
  if ($("btnReboot")) $("btnReboot").onclick = async () => {
    if (confirm("Reboot device?")) {
      await apiPost("/api/reboot", {});
      alert("Rebooting...");
    }
  };

  // OTA
  const doUpload = (fileId, path, barId) => {
    const f = $(fileId).files[0];
    if (!f) return alert("Select file");
    const fd = new FormData();
    fd.append("update", f);
    const xhr = new XMLHttpRequest();
    xhr.open("POST", path);
    xhr.upload.onprogress = (e) => {
      if (e.lengthComputable && $(barId)) {
        $(barId).querySelector(".bar").style.width = Math.round((e.loaded / e.total) * 100) + "%";
      }
    };
    xhr.onload = () => {
      if (xhr.status === 200) alert("Update Complete! Rebooting...");
      else alert("Error: " + xhr.responseText);
    };
    xhr.onerror = () => alert("Upload Failed");
    xhr.send(fd);
  };

  if ($("btnUpdateFw")) $("btnUpdateFw").onclick = () => doUpload("fileFw", "/update", "progFw");
  if ($("btnUpdateFs")) $("btnUpdateFs").onclick = () => doUpload("fileFs", "/update?type=fs", "progFs");

  if ($("btnCheckOta")) $("btnCheckOta").onclick = async () => {
    $("otaOnlineStatus").textContent = "Checking...";
    try {
      const res = await apiPost("/api/ota/check", {});
      $("otaOnlineStatus").textContent = res.msg || "Check Complete";
    } catch (e) {
      $("otaOnlineStatus").textContent = "Error: " + e.message;
    }
  };

  // Use SSID from Scan ID
  if ($("btnUseSsid")) $("btnUseSsid").onclick = () => {
    const sel = $("wifiScanList");
    if (sel.value) $("wStaSsid").value = sel.value;
  };
};

