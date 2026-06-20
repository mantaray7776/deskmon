#include "Config.h"
#include "NavState.h"
#include <WiFi.h>
#include <DNSServer.h>     
#include <ESPmDNS.h>
#include <WebSocketsServer.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Update.h>

String heading_to_cardinal_ws(float heading) {
    static const char* directions[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int index = (int)((heading + 22.5f) / 45.0f) & 7;
    return directions[index];
}

static WebServer          s_http(80);
static WebSocketsServer   s_ws(WS_PORT);
static DNSServer          s_dns; 
const byte DNS_PORT = 53;

static float s_impact_thresh = IMPACT_THRESHOLD;
static float s_em_thresh     = EM_SPIKE_THRESHOLD;

// ── Embedded dashboard HTML ──────────────────────────────────────────────────
static const char DASHBOARD_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>DeskMon</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#0d0f14;color:#e8eaf0;font-family:system-ui,sans-serif;padding:16px}
h1{font-size:18px;margin-bottom:16px;color:#fff}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:10px;margin-bottom:16px}
.tile{background:#161a22;border:1px solid #2a3040;border-radius:8px;padding:12px}
.tile-label{font-size:10px;color:#7a8099;margin-bottom:4px}
.tile-val{font-size:24px;font-weight:700;color:#fff}
.tile-sub{font-size:11px;color:#7a8099;margin-top:2px}
.bar-track{background:#1e2430;border-radius:3px;height:6px;margin-top:6px;overflow:hidden}
.bar-fill{height:100%;border-radius:3px;background:#2ecc8a;transition:width .3s}
.btn{display:inline-block;padding:8px 20px;border-radius:6px;border:none;cursor:pointer;font-size:13px;margin-right:8px;margin-bottom:8px}
.btn-green{background:#1D9E75;color:#fff}
.btn-red{background:#D85A30;color:#fff}
.btn-gray{background:#2a3040;color:#e8eaf0}
.log{background:#0a0c10;border:1px solid #2a3040;border-radius:6px;padding:10px;height:140px;overflow-y:auto;font-family:monospace;font-size:11px;color:#7a8099}
.log span{display:block;margin-bottom:2px}
#conn{color:#D85A30}
#conn.ok{color:#2ecc8a}
canvas{background:#061018;border-radius:6px;width:100%;height:80px}
</style>
</head><body>
<h1>DeskMon <span id="conn">● disconnected</span></h1>
<div class="grid">
  <div class="tile"><div class="tile-label">HEADING</div><div class="tile-val" id="hdg">---°</div><div class="tile-sub" id="dir">--</div></div>
  <div class="tile"><div class="tile-label">TILT</div><div class="tile-val" id="tilt">--°</div><div class="tile-sub" id="roll">roll --°</div></div>
  <div class="tile"><div class="tile-label">MOTION</div><div class="tile-val" id="state" style="font-size:16px">--</div><div class="tile-sub" id="vib">vib --</div></div>
  <div class="tile"><div class="tile-label">EM FIELD</div><div class="tile-val" id="em" style="font-size:16px">--</div><div class="tile-sub" id="emv">var --</div></div>
  <div class="tile"><div class="tile-label">SESSION</div><div class="tile-val" id="sess" style="font-size:18px">--:--</div><div class="tile-sub" id="pomo">pomodoro --</div></div>
  <div class="tile"><div class="tile-label">SCORE</div><div class="tile-val" id="score">--</div><div class="bar-track"><div class="bar-fill" id="score-bar" style="width:0%"></div></div></div>
</div>
<canvas id="vib-chart"></canvas>
<br>
<button class="btn btn-green" onclick="send({cmd:'session_start'})">Start session</button>
<button class="btn btn-red"   onclick="send({cmd:'session_stop'})">Stop</button>
<button class="btn btn-gray"  onclick="send({cmd:'session_reset'})">Reset</button>
<br><br>
<div class="log" id="log"></div>
<script>
const ws=new WebSocket('ws://'+location.hostname+':81');
const ctx=document.getElementById('vib-chart').getContext('2d');
const vibData=new Array(60).fill(0);
ws.onopen=()=>{document.getElementById('conn').textContent='● connected';document.getElementById('conn').className='ok';}
ws.onclose=()=>{document.getElementById('conn').textContent='● disconnected';document.getElementById('conn').className='';}
ws.onmessage=e=>{
  const d=JSON.parse(e.data);
  if(d.type!=='state')return;
  const s=d.state;
  document.getElementById('hdg').textContent=s.heading.toFixed(0)+'°';
  document.getElementById('dir').textContent=s.cardinal;
  document.getElementById('tilt').textContent=Math.abs(s.pitch).toFixed(1)+'°';
  document.getElementById('roll').textContent='roll '+s.roll.toFixed(1)+'°';
  document.getElementById('state').textContent=s.moving?'MOVING':'STILL';
  document.getElementById('vib').textContent='vib '+s.vib_rms.toFixed(3);
  document.getElementById('em').textContent=s.em_var>8?'NOISY':'STABLE';
  document.getElementById('emv').textContent='var '+s.em_var.toFixed(2);
  const rem=s.session_duration-s.elapsed_sec;
  const m=Math.floor(rem/60),sec=rem%60;
  document.getElementById('sess').textContent=(s.session_active?(m+'\''+String(sec).padStart(2,'0')+'\"'):'--:--');
  document.getElementById('pomo').textContent='pomodoro '+s.pomodoro_count;
  document.getElementById('score').textContent=s.distraction_score;
  const pct=s.distraction_score;
  const bar=document.getElementById('score-bar');
  bar.style.width=pct+'%';
  bar.style.background=pct>60?'#e8613a':pct>30?'#e8a83a':'#2ecc8a';
  vibData.shift();vibData.push(s.vib_rms);
  const c=document.getElementById('vib-chart');
  c.width=c.offsetWidth*devicePixelRatio;c.height=80*devicePixelRatio;
  const cx=c.getContext('2d');cx.scale(devicePixelRatio,devicePixelRatio);
  cx.clearRect(0,0,c.offsetWidth,80);
  const mx=Math.max(...vibData,0.01);
  cx.strokeStyle='#e8613a';cx.lineWidth=1.5;cx.beginPath();
  vibData.forEach((v,i)=>{const x=i*(c.offsetWidth/devicePixelRatio/60),y=80-(v/mx*70);i===0?cx.moveTo(x,y):cx.lineTo(x,y);});
  cx.stroke();
};
function send(obj){ws.send(JSON.stringify(obj));}
</script>
</body></html>
)rawhtml";

// ── Build state JSON payload ─────────────────────────────────────────────────
static String build_state_json(const NavState& st) {
    JsonDocument doc;
    doc["type"] = "state";
    JsonObject s = doc["state"].to<JsonObject>();

    s["heading"]          = (int)st.orient.heading;
    s["pitch"]            = round(st.orient.pitch * 10) / 10.0f;
    s["roll"]             = round(st.orient.roll  * 10) / 10.0f;
    s["cardinal"]         = heading_to_cardinal_ws(st.orient.heading);
    s["moving"]           = st.motion.moving;
    s["vib_rms"]          = st.motion.vibration_rms;
    s["em_var"]           = st.motion.em_variance;
    s["session_active"]   = st.session.active;
    s["elapsed_sec"]      = st.session.elapsed_sec;
    s["session_duration"] = st.session.session_duration;
    s["pomodoro_count"]   = st.session.pomodoro_count;
    s["distraction_score"]= st.session.distraction_score;
    s["ws_clients"]       = st.wifi.ws_clients;

    String out;
    serializeJson(doc, out);
    return out;
}

static void handle_ws_command(const String& payload) {
    JsonDocument doc;
    if (deserializeJson(doc, payload) != DeserializationError::Ok) return;
    const char* cmd = doc["cmd"] | "";

    if (strcmp(cmd, "session_start") == 0) {
        WITH_STATE([&]{
            g_state.session.active = true;
            g_state.session.elapsed_sec = 0;
        });
    } else if (strcmp(cmd, "session_stop") == 0) {
        WITH_STATE([&]{ g_state.session.active = false; });
    }
}

static void on_ws_event(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    if (type == WStype_CONNECTED) {
        WITH_STATE([&]{ g_state.wifi.ws_clients++; });
    } else if (type == WStype_DISCONNECTED) {
        WITH_STATE([&]{ if (g_state.wifi.ws_clients) g_state.wifi.ws_clients--; });
    } else if (type == WStype_TEXT) {
        handle_ws_command(String((char*)payload));
    }
}

static void wifi_connect() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < 20)
        vTaskDelay(pdMS_TO_TICKS(500));

    if (WiFi.status() != WL_CONNECTED) {
        WiFi.mode(WIFI_AP);
        WiFi.softAP("DeskMon-Setup", "123");
        s_dns.start(DNS_PORT, "*", WiFi.softAPIP());
        WITH_STATE([&]{
            g_state.wifi.connected = false;
            strlcpy(g_state.wifi.ip, WiFi.softAPIP().toString().c_str(), 16);
        });
    } else {
        MDNS.begin("deskmon");
        WITH_STATE([&]{
            g_state.wifi.connected = true;
            strlcpy(g_state.wifi.ip, WiFi.localIP().toString().c_str(), 16);
        });
    }
}

void WebTask(void* pvParams) {
    wifi_connect();

    s_http.on("/", HTTP_GET, [](){ s_http.send_P(200, "text/html", DASHBOARD_HTML); });
    
    s_http.onNotFound([](){
        if (WiFi.getMode() == WIFI_AP) {
            s_http.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
            s_http.send(302, "text/plain", ""); 
        } else {
            s_http.send(404, "text/plain", "Not Found");
        }
    });

    s_http.begin();
    s_ws.begin();
    s_ws.onEvent(on_ws_event);

    TickType_t last_broadcast = xTaskGetTickCount();

    while (true) {
        if (WiFi.getMode() == WIFI_AP) s_dns.processNextRequest();
        s_http.handleClient();
        s_ws.loop();

        if (xTaskGetTickCount() - last_broadcast >= pdMS_TO_TICKS(100)) {
            last_broadcast = xTaskGetTickCount();
            NavState st = state_snapshot();
            if (st.wifi.ws_clients > 0) {
                String payload = build_state_json(st); 
                s_ws.broadcastTXT(payload);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}