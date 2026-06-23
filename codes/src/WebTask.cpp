// ─────────────────────────────────────────────────────────────────────────────
//  WebTask.cpp  —  Wi-Fi, WebSocket, HTTP dashboard
//  Dashboard redesign: Session Timer (hero) + Distraction Score (hero) + stats
//  No compass. Minimal dark aesthetic matching TFT layout logic.
// ─────────────────────────────────────────────────────────────────────────────
#include "Config.h"
#include "NavState.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WebSocketsServer.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Update.h>

// ── Cardinal helper ───────────────────────────────────────────────────────────
static String heading_to_cardinal(float h) {
    static const char* d[] = {"N","NE","E","SE","S","SW","W","NW"};
    return d[(int)((h + 22.5f) / 45.0f) & 7];
}

static WebServer        s_http(80);
static WebSocketsServer s_ws(WS_PORT);
static DNSServer        s_dns;
const byte              DNS_PORT = 53;

// ─────────────────────────────────────────────────────────────────────────────
//  Embedded dashboard HTML
//  Design:
//    • Dark background (#07090d) single teal accent (#1D9E75)
//    • Hero 1: Session ring + time remaining + controls
//    • Hero 2: Distraction score + colour-coded bar + status text
//    • Stat grid: motion, EM, tilt, impacts
//    • Sparkline: vibration last 60 s
// ─────────────────────────────────────────────────────────────────────────────
static const char DASHBOARD_HTML[] PROGMEM = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>DeskMon</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{
  --bg:#07090d;--surface:#0d1520;--surface2:#111e2c;
  --accent:#1D9E75;--accent-dim:#0f4a36;
  --text:#b4c0d0;--dim:#445566;--white:#e8eef6;
  --warn:#d4a030;--danger:#c44040;
  --radius:8px;--radius-lg:12px;
  --border:1px solid #1a2535;
}
html,body{height:100%;background:var(--bg);color:var(--text);font-family:system-ui,sans-serif}
body{padding:16px;max-width:680px;margin:0 auto;display:flex;flex-direction:column;gap:12px}

/* header */
.hdr{display:flex;justify-content:space-between;align-items:center;padding:10px 14px;
     background:var(--surface);border-radius:var(--radius);border:var(--border)}
.hdr-title{font-size:13px;font-weight:500;color:var(--white);display:flex;align-items:center;gap:6px}
.hdr-title svg{width:14px;height:14px;fill:var(--accent)}
.conn{display:flex;align-items:center;gap:5px;font-size:11px;color:var(--dim)}
.conn.ok{color:var(--accent)}
.conn-dot{width:6px;height:6px;border-radius:50%;background:currentColor;flex-shrink:0}

/* hero session */
.session-hero{background:var(--surface);border-radius:var(--radius-lg);border:var(--border);
              padding:16px;display:flex;align-items:center;gap:16px}
.ring-wrap{position:relative;width:88px;height:88px;flex-shrink:0}
.ring-wrap svg{position:absolute;inset:0;width:100%;height:100%}
.ring-center{position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:1px}
.ring-pct{font-size:15px;font-weight:500;color:var(--white);font-variant-numeric:tabular-nums}
.ring-lbl{font-size:9px;color:var(--dim)}
.sess-info{flex:1;min-width:0}
.sess-phase{font-size:11px;color:var(--dim);margin-bottom:4px}
.sess-time{font-size:32px;font-weight:500;color:var(--white);font-variant-numeric:tabular-nums;line-height:1;margin-bottom:4px;letter-spacing:.02em}
.sess-meta{font-size:11px;color:var(--dim)}
.sess-meta b{color:var(--text)}
.pomo-dots{display:flex;gap:5px;margin-top:8px}
.pomo-dot{width:7px;height:7px;border-radius:50%;background:var(--accent-dim)}
.pomo-dot.done{background:var(--accent)}
.sess-btns{display:flex;flex-direction:column;gap:6px}
.btn{background:var(--surface2);border:var(--border);color:var(--text);
     border-radius:var(--radius);padding:7px 14px;font-size:12px;cursor:pointer;white-space:nowrap;
     transition:background .15s}
.btn:hover{background:#1a2e42}
.btn.primary{background:var(--accent-dim);color:var(--accent);border-color:var(--accent-dim)}
.btn.primary:hover{background:#1a5c40}

/* hero score */
.score-hero{background:var(--surface);border-radius:var(--radius-lg);border:var(--border);padding:16px}
.score-top{display:flex;justify-content:space-between;align-items:baseline;margin-bottom:10px}
.score-lbl{font-size:12px;color:var(--dim)}
.score-num{font-size:36px;font-weight:500;color:var(--white);font-variant-numeric:tabular-nums;line-height:1}
.score-track{height:6px;background:var(--surface2);border-radius:3px;overflow:hidden;margin-bottom:8px}
.score-fill{height:100%;border-radius:3px;transition:width .4s,background .4s}
.score-hint{font-size:11px;color:var(--dim)}

/* stat grid */
.grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px}
.tile{background:var(--surface);border-radius:var(--radius);border:var(--border);padding:10px 12px}
.tile-lbl{font-size:10px;color:var(--dim);margin-bottom:6px}
.tile-val{font-size:18px;font-weight:500;color:var(--white);line-height:1}
.tile-val.ok{color:var(--accent)}
.tile-val.warn{color:var(--warn)}
.tile-val.danger{color:var(--danger)}
.tile-sub{font-size:10px;color:var(--dim);margin-top:3px}

/* sparkline */
.spark-wrap{background:var(--surface);border-radius:var(--radius);border:var(--border);padding:12px}
.spark-hdr{font-size:10px;color:var(--dim);margin-bottom:8px}
canvas{display:block;width:100%;height:52px;border-radius:4px}

/* footer */
.ftr{font-size:10px;color:var(--dim);text-align:center}
</style>
</head>
<body>

<!-- header -->
<div class="hdr">
  <div class="hdr-title">
    <svg viewBox="0 0 16 16"><rect x="1" y="3" width="14" height="9" rx="1.5" fill="none"
      stroke="var(--accent)" stroke-width="1.4"/><path d="M5 12v2M11 12v2M3 14h10"
      stroke="var(--accent)" stroke-width="1.2" stroke-linecap="round"/></svg>
    DeskMon
  </div>
  <div class="conn" id="conn"><span class="conn-dot"></span><span id="conn-txt">disconnected</span></div>
</div>

<!-- session hero -->
<div class="session-hero">
  <div class="ring-wrap">
    <svg viewBox="0 0 88 88">
      <circle cx="44" cy="44" r="36" fill="none" stroke="#1a2535" stroke-width="6"/>
      <circle cx="44" cy="44" r="36" fill="none" stroke="var(--accent)" stroke-width="6"
        stroke-linecap="round" stroke-dasharray="226"
        id="ring-path" stroke-dashoffset="226" transform="rotate(-90 44 44)"/>
    </svg>
    <div class="ring-center">
      <span class="ring-pct" id="ring-pct">0%</span>
      <span class="ring-lbl">done</span>
    </div>
  </div>
  <div class="sess-info">
    <div class="sess-phase" id="sess-phase">session paused</div>
    <div class="sess-time" id="sess-time">--:--</div>
    <div class="sess-meta">target <b id="sess-target">25 min</b> · <span id="sess-elapsed">0:00</span> elapsed</div>
    <div class="pomo-dots" id="pomo-dots"></div>
  </div>
  <div class="sess-btns">
    <button class="btn primary" id="btn-toggle" onclick="toggleSession()">▶ start</button>
    <button class="btn" onclick="send({cmd:'session_reset'})">↺ reset</button>
  </div>
</div>

<!-- distraction score hero -->
<div class="score-hero">
  <div class="score-top">
    <span class="score-lbl">distraction score</span>
    <span class="score-num" id="score-num">--</span>
  </div>
  <div class="score-track"><div class="score-fill" id="score-fill" style="width:0%"></div></div>
  <div class="score-hint" id="score-hint">waiting for data…</div>
</div>

<!-- stat grid -->
<div class="grid">
  <div class="tile">
    <div class="tile-lbl">motion</div>
    <div class="tile-val" id="t-motion">--</div>
    <div class="tile-sub" id="t-vib">vib --</div>
  </div>
  <div class="tile">
    <div class="tile-lbl">EM field</div>
    <div class="tile-val" id="t-em">--</div>
    <div class="tile-sub" id="t-emv">var --</div>
  </div>
  <div class="tile">
    <div class="tile-lbl">tilt</div>
    <div class="tile-val" id="t-tilt">--</div>
    <div class="tile-sub" id="t-roll">roll --</div>
  </div>
  <div class="tile">
    <div class="tile-lbl">impacts</div>
    <div class="tile-val" id="t-impact">--</div>
    <div class="tile-sub" id="t-em-spk">EM spk --</div>
  </div>
</div>

<!-- sparkline -->
<div class="spark-wrap">
  <div class="spark-hdr">vibration — last 60 s</div>
  <canvas id="spark"></canvas>
</div>

<div class="ftr" id="ftr">deskmon.local · ws:<span id="ws-port">81</span></div>

<script>
const CIRC = 2 * Math.PI * 36;
const vibBuf = new Array(60).fill(0);
let sessionActive = false;

const ws = new WebSocket('ws://' + location.hostname + ':81');

ws.onopen = () => {
  const c = document.getElementById('conn');
  c.className = 'conn ok';
  document.getElementById('conn-txt').textContent = location.hostname;
};
ws.onclose = () => {
  const c = document.getElementById('conn');
  c.className = 'conn';
  document.getElementById('conn-txt').textContent = 'disconnected';
};

ws.onmessage = e => {
  let d;
  try { d = JSON.parse(e.data); } catch { return; }
  if (d.type !== 'state') return;
  const s = d.state;

  // session
  sessionActive = s.session_active;
  const remain = Math.max(0, s.session_duration - s.elapsed_sec);
  const pct    = s.session_duration > 0
                   ? Math.min(100, (s.elapsed_sec / s.session_duration) * 100) : 0;

  document.getElementById('sess-time').textContent  = fmtMMSS(remain);
  document.getElementById('ring-pct').textContent   = Math.round(pct) + '%';
  document.getElementById('ring-path').style.strokeDashoffset =
    (CIRC * (1 - pct / 100)).toFixed(1);
  document.getElementById('sess-phase').textContent =
    s.session_active ? 'focus session · pomodoro ' + s.pomodoro_count : 'session paused';
  document.getElementById('sess-elapsed').textContent = fmtMMSS(s.elapsed_sec);
  document.getElementById('sess-target').textContent  =
    Math.round(s.session_duration / 60) + ' min';
  document.getElementById('btn-toggle').textContent =
    s.session_active ? '⏸ pause' : '▶ start';

  // pomo dots (4-cycle)
  const dotEl = document.getElementById('pomo-dots');
  dotEl.innerHTML = '';
  const inCycle = s.pomodoro_count % 4;
  for (let i = 0; i < 4; i++) {
    const d2 = document.createElement('div');
    d2.className = 'pomo-dot' + (i < inCycle ? ' done' : '');
    dotEl.appendChild(d2);
  }

  // distraction score
  const sc = s.distraction_score;
  document.getElementById('score-num').textContent = sc;
  const col = sc > 80 ? '#c44040' : sc > 60 ? '#d4a030' : '#1D9E75';
  const fill = document.getElementById('score-fill');
  fill.style.width   = sc + '%';
  fill.style.background = col;
  const hints = ['calm — no significant noise','mild distraction detected',
                 'noisy environment','high interference — consider a break'];
  document.getElementById('score-hint').textContent =
    hints[sc > 80 ? 3 : sc > 60 ? 2 : sc > 30 ? 1 : 0];

  // stat tiles
  const mv = document.getElementById('t-motion');
  mv.textContent = s.moving ? 'moving' : 'still';
  mv.className   = 'tile-val ' + (s.moving ? 'warn' : 'ok');
  document.getElementById('t-vib').textContent = 'vib ' + s.vib_rms.toFixed(3);

  const em = document.getElementById('t-em');
  const em_noisy = s.em_var > 8;
  em.textContent = em_noisy ? 'noisy' : 'stable';
  em.className   = 'tile-val ' + (em_noisy ? 'warn' : 'ok');
  document.getElementById('t-emv').textContent = 'var ' + s.em_var.toFixed(1) + ' µT';

  document.getElementById('t-tilt').textContent  = Math.abs(s.pitch).toFixed(1) + '°';
  document.getElementById('t-roll').textContent  = 'roll ' + s.roll.toFixed(1) + '°';
  document.getElementById('t-impact').textContent = s.impact_count ?? '--';
  document.getElementById('t-em-spk').textContent = 'EM spk ' + (s.em_spike_count ?? '--');

  // sparkline
  vibBuf.shift(); vibBuf.push(s.vib_rms);
  drawSparkline();
};

function drawSparkline() {
  const el  = document.getElementById('spark');
  const dpr = window.devicePixelRatio || 1;
  el.width  = el.offsetWidth  * dpr;
  el.height = el.offsetHeight * dpr;
  const cx  = el.getContext('2d');
  cx.scale(dpr, dpr);
  const W = el.offsetWidth, H = el.offsetHeight;
  cx.clearRect(0, 0, W, H);
  const mx = Math.max(...vibBuf, 0.01);
  cx.strokeStyle = '#1D9E75';
  cx.lineWidth   = 1.5;
  cx.lineJoin    = 'round';
  cx.beginPath();
  vibBuf.forEach((v, i) => {
    const x = (i / (vibBuf.length - 1)) * W;
    const y = H - (v / mx) * (H - 4) - 2;
    i === 0 ? cx.moveTo(x, y) : cx.lineTo(x, y);
  });
  cx.stroke();
}

function fmtMMSS(s) {
  const m = Math.floor(s / 60), ss = s % 60;
  return String(m).padStart(2,'0') + ':' + String(ss).padStart(2,'0');
}

function send(obj) { ws.send(JSON.stringify(obj)); }

function toggleSession() {
  send({ cmd: sessionActive ? 'session_stop' : 'session_start' });
}
</script>
</body>
</html>
)html";

// ── JSON payload ──────────────────────────────────────────────────────────────
static String build_state_json(const NavState& st) {
    JsonDocument doc;
    doc["type"] = "state";
    JsonObject s = doc["state"].to<JsonObject>();

    s["heading"]         = (int)st.orient.heading;
    s["pitch"]           = roundf(st.orient.pitch * 10) / 10.0f;
    s["roll"]            = roundf(st.orient.roll  * 10) / 10.0f;
    s["moving"]          = st.motion.moving;
    s["vib_rms"]         = st.motion.vibration_rms;
    s["em_var"]          = st.motion.em_variance;
    s["impact_count"]    = st.motion.impact_count;
    s["em_spike_count"]  = st.motion.em_spike_count;
    s["session_active"]  = st.session.active;
    s["elapsed_sec"]     = st.session.elapsed_sec;
    s["session_duration"]= st.session.session_duration;
    s["pomodoro_count"]  = st.session.pomodoro_count;
    s["distraction_score"]= st.session.distraction_score;
    s["ws_clients"]      = st.wifi.ws_clients;

    String out;
    serializeJson(doc, out);
    return out;
}

// ── WebSocket command handler ──────────────────────────────────────────────────
static void handle_ws_command(const String& payload) {
    JsonDocument doc;
    if (deserializeJson(doc, payload) != DeserializationError::Ok) return;
    const char* cmd = doc["cmd"] | "";

    if (strcmp(cmd, "session_start") == 0) {
        WITH_STATE([&]{
            g_state.session.active      = true;
            g_state.session.elapsed_sec = 0;
        });
        nav_log_event(EventType::SESSION_START);
    } else if (strcmp(cmd, "session_stop") == 0) {
        WITH_STATE([&]{ g_state.session.active = false; });
        nav_log_event(EventType::SESSION_END);
    } else if (strcmp(cmd, "session_reset") == 0) {
        WITH_STATE([&]{
            g_state.session.active      = false;
            g_state.session.elapsed_sec = 0;
        });
    } else if (strcmp(cmd, "set_duration") == 0) {
        uint32_t dur = doc["value"] | SESSION_DEFAULT_SEC;
        if (dur >= 60 && dur <= 90*60) {
            WITH_STATE([&]{ g_state.session.session_duration = dur; });
        }
    }
}

static void on_ws_event(uint8_t num, WStype_t type,
                        uint8_t* payload, size_t length) {
    if (type == WStype_CONNECTED) {
        WITH_STATE([&]{ g_state.wifi.ws_clients++; });
    } else if (type == WStype_DISCONNECTED) {
        WITH_STATE([&]{ if (g_state.wifi.ws_clients) g_state.wifi.ws_clients--; });
    } else if (type == WStype_TEXT) {
        handle_ws_command(String((char*)payload));
    }
}

// ── Wi-Fi init ────────────────────────────────────────────────────────────────
static void wifi_connect() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < 20)
        vTaskDelay(pdMS_TO_TICKS(500));

    if (WiFi.status() != WL_CONNECTED) {
        WiFi.mode(WIFI_AP);
        WiFi.softAP("DeskMon-Setup", "deskmon123");
        s_dns.start(DNS_PORT, "*", WiFi.softAPIP());
        WITH_STATE([&]{
            g_state.wifi.connected = false;
            strlcpy(g_state.wifi.ip, WiFi.softAPIP().toString().c_str(), 16);
        });
        log_w("Wi-Fi failed — AP mode at %s", WiFi.softAPIP().toString().c_str());
    } else {
        MDNS.begin(MDNS_HOSTNAME);
        WITH_STATE([&]{
            g_state.wifi.connected = true;
            strlcpy(g_state.wifi.ip, WiFi.localIP().toString().c_str(), 16);
        });
        log_i("Wi-Fi OK — http://%s", WiFi.localIP().toString().c_str());
    }
}

// ── WebTask ───────────────────────────────────────────────────────────────────
void WebTask(void* pvParams) {
    wifi_connect();

    s_http.on("/", HTTP_GET, []{
        s_http.send_P(200, "text/html", DASHBOARD_HTML);
    });
    s_http.onNotFound([]{
        if (WiFi.getMode() == WIFI_AP) {
            s_http.sendHeader("Location",
                String("http://") + WiFi.softAPIP().toString(), true);
            s_http.send(302, "text/plain", "");
        } else {
            s_http.send(404, "text/plain", "Not found");
        }
    });

    s_http.begin();
    s_ws.begin();
    s_ws.onEvent(on_ws_event);

    TickType_t last_bcast = xTaskGetTickCount();

    while (true) {
        if (WiFi.getMode() == WIFI_AP) s_dns.processNextRequest();
        s_http.handleClient();
        s_ws.loop();

        if (xTaskGetTickCount() - last_bcast >= pdMS_TO_TICKS(100)) {
            last_bcast = xTaskGetTickCount();
            NavState st = state_snapshot();
            if (st.wifi.ws_clients > 0) {
                String payload = build_state_json(st);
                s_ws.broadcastTXT(payload);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
