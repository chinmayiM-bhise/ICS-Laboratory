/*
 ==============================================================================================
  PROJECT: ICS Laboratory - Mini Power Plant SCADA & OT Testbed
  NODE: HMI Supervisory Controller & Web Gateway (Purdue Model Level 2)
  COMMUNICATION PROTOCOL: MQTT v3.1.1 & HTTP Web SCADA Dashboard
  
  REAL-WORLD OT CONTEXT & VULNERABILITY MATRIX:
  - T0855: Unauthenticated MQTT Command Publication (plant/commands)
  - T0836: Setpoint Modification without validation (plant/config)
  - T0887: Plaintext Telemetry Sniffing (plant/telemetry)
  - T0802: Wildcard Topic Interception (#)
  - T0812: Default Hardcoded Wi-Fi & Static Credentials
 ==============================================================================================
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>

// --- Wi-Fi SoftAP Configuration (Level 2 Supervisory Network) ---
const char* ssid     = "Mini_OT_SCADA";
const char* password = "admin123";

// --- MQTT Broker Configuration ---
// By default, connects to MQTT broker running at 192.168.4.1 or custom gateway
const char* mqtt_server = "192.168.4.1";
const int   mqtt_port   = 1883;
const char* mqtt_client = "HMI_SCADA_MASTER";

// --- MQTT Topics ---
const char* TOPIC_TELEMETRY = "plant/telemetry";
const char* TOPIC_COMMANDS  = "plant/commands";
const char* TOPIC_CONFIG    = "plant/config";
const char* TOPIC_ALERTS    = "plant/alerts";

// Network & Server Instances
WiFiClient espClient;
PubSubClient mqttClient(espClient);
ESP8266WebServer server(80);

// --- Real-Time Plant State Cache (Updated via MQTT Telemetry) ---
float currentTemp     = 24.0;
float currentHum      = 50.0;
bool  isMotorOn       = false;
bool  isRelayOn       = false;
bool  isAlarmOn       = false;
bool  isIrDetected    = false;
float powerOutput     = 0.8;
float pressure        = 3.1;
float tempThreshold   = 30.0;
String plantName      = "MINI POWER PLANT - GENERATION UNIT 1";

// Ring Buffer for Recent MQTT SCADA Logs
String eventLog[5] = {
  "[SYSTEM] HMI Master Gateway Online",
  "[MQTT] Connected to Broker on Port 1883",
  "[OT-NSM] Subscribed to plant/#",
  "[STATUS] Telemetry Link Established",
  "[STATUS] Normal Plant Operation"
};

void addLog(String event) {
  for (int i = 0; i < 4; i++) {
    eventLog[i] = eventLog[i + 1];
  }
  eventLog[4] = event;
}

// -----------------------------------------------------------------------------
// Helper: Simple JSON field extractor
// -----------------------------------------------------------------------------
float parseJsonFloat(String json, String key) {
  int idx = json.indexOf("\"" + key + "\":");
  if (idx == -1) idx = json.indexOf(key + ":");
  if (idx == -1) return -999.0;
  int colon = json.indexOf(':', idx);
  int comma = json.indexOf(',', colon);
  int brace = json.indexOf('}', colon);
  int end   = (comma != -1 && comma < brace) ? comma : brace;
  String val = json.substring(colon + 1, end);
  val.trim();
  val.replace("\"", "");
  return val.toFloat();
}

int parseJsonInt(String json, String key) {
  return (int)parseJsonFloat(json, key);
}

// -----------------------------------------------------------------------------
// MQTT Inbound Message Callback
// -----------------------------------------------------------------------------
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  String msg = String((char*)payload);
  String topicStr = String(topic);

  Serial.printf("[HMI MQTT RECV] Topic: %s | Msg: %s\n", topic, msg.c_str());

  if (topicStr == TOPIC_TELEMETRY) {
    float t = parseJsonFloat(msg, "temp");
    if (t != -999.0) currentTemp = t;
    
    float h = parseJsonFloat(msg, "hum");
    if (h != -999.0) currentHum = h;

    int m = parseJsonInt(msg, "motor");
    if (m != -999) isMotorOn = (m == 1);

    int r = parseJsonInt(msg, "relay");
    if (r != -999) isRelayOn = (r == 1);

    int a = parseJsonInt(msg, "alarm");
    if (a != -999) isAlarmOn = (a == 1);

    int ir = parseJsonInt(msg, "ir_obstacle");
    if (ir != -999) isIrDetected = (ir == 1);

    float p = parseJsonFloat(msg, "power");
    if (p != -999.0) powerOutput = p;

    float pres = parseJsonFloat(msg, "pressure");
    if (pres != -999.0) pressure = pres;

    float thresh = parseJsonFloat(msg, "threshold");
    if (thresh != -999.0) tempThreshold = thresh;
  }
  else if (topicStr == TOPIC_ALERTS) {
    addLog("[ALERT MQTT] " + msg);
  }
  else if (topicStr == TOPIC_COMMANDS) {
    addLog("[CMD OVER MQTT] " + msg);
  }
}

// -----------------------------------------------------------------------------
// Maintain MQTT Connection
// -----------------------------------------------------------------------------
void reconnectMqtt() {
  if (!mqttClient.connected()) {
    if (mqttClient.connect(mqtt_client)) {
      Serial.println("[HMI] Connected to MQTT Broker!");
      mqttClient.subscribe("plant/#"); // Subscribe to all plant topics
      addLog("[MQTT] Subscribed to wildcard 'plant/#'");
    }
  }
}

// -----------------------------------------------------------------------------
// Embedded SCADA Web Dashboard HTML (Purdue Model Level 2 HMI)
// -----------------------------------------------------------------------------
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>OT SCADA Dashboard - Mini Power Plant</title>
    <style>
        :root {
            --bg-dark: #070d14;
            --card-bg: #0f1926;
            --border-color: #1e324d;
            --accent-cyan: #00d2ff;
            --accent-green: #00ff88;
            --accent-red: #ff3344;
            --accent-amber: #ffaa00;
            --text-main: #e2e8f0;
            --text-muted: #889bb0;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; font-family: 'Segoe UI', -apple-system, sans-serif; }
        body { background: var(--bg-dark); color: var(--text-main); padding: 15px; }
        
        .header {
            display: flex; justify-content: space-between; align-items: center;
            background: var(--card-bg); border: 1px solid var(--border-color);
            padding: 15px 25px; border-radius: 8px; margin-bottom: 20px;
            box-shadow: 0 4px 15px rgba(0,210,255,0.08);
        }
        .header h1 { font-size: 1.4rem; color: var(--accent-cyan); letter-spacing: 1px; }
        .badge { font-size: 0.75rem; padding: 4px 10px; border-radius: 4px; font-weight: bold; }
        .badge-mqtt { background: rgba(0,210,255,0.15); color: var(--accent-cyan); border: 1px solid var(--accent-cyan); }
        .badge-live { background: rgba(0,255,136,0.15); color: var(--accent-green); border: 1px solid var(--accent-green); }
        
        .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 15px; margin-bottom: 20px; }
        .card {
            background: var(--card-bg); border: 1px solid var(--border-color);
            border-radius: 8px; padding: 18px; text-align: center;
            transition: transform 0.2s, border-color 0.2s;
        }
        .card:hover { border-color: var(--accent-cyan); }
        .card-label { font-size: 0.8rem; color: var(--text-muted); text-transform: uppercase; margin-bottom: 8px; }
        .card-val { font-size: 1.8rem; font-weight: bold; color: #fff; }
        .status-pill { display: inline-block; padding: 4px 14px; border-radius: 20px; font-weight: bold; font-size: 0.85rem; margin-top: 5px; }
        .status-on { background: rgba(0,255,136,0.2); color: var(--accent-green); border: 1px solid var(--accent-green); }
        .status-off { background: rgba(255,51,68,0.2); color: var(--accent-red); border: 1px solid var(--accent-red); }
        .status-warn { background: rgba(255,170,0,0.2); color: var(--accent-amber); border: 1px solid var(--accent-amber); }

        .control-panel {
            background: var(--card-bg); border: 1px solid var(--border-color);
            border-radius: 8px; padding: 20px; margin-bottom: 20px;
        }
        .control-panel h2 { font-size: 1.1rem; color: var(--accent-cyan); margin-bottom: 15px; }
        .btn-group { display: flex; flex-wrap: wrap; gap: 10px; margin-bottom: 15px; }
        button {
            cursor: pointer; border: none; padding: 10px 18px; border-radius: 5px;
            font-weight: bold; font-size: 0.85rem; text-transform: uppercase;
            transition: opacity 0.2s, transform 0.1s;
        }
        button:active { transform: scale(0.97); }
        .btn-start { background: var(--accent-green); color: #05140b; }
        .btn-stop { background: #4a5568; color: #fff; }
        .btn-estop { background: var(--accent-red); color: #fff; }
        .btn-update { background: var(--accent-cyan); color: #041724; }

        .threshold-box { display: flex; align-items: center; gap: 10px; margin-top: 10px; }
        input[type="number"] {
            background: #050a10; border: 1px solid var(--border-color);
            color: #fff; padding: 8px 12px; border-radius: 4px; width: 140px;
        }

        .log-section {
            background: #050a10; border: 1px solid var(--border-color);
            border-radius: 8px; padding: 15px; font-family: 'Courier New', monospace;
            font-size: 0.82rem; height: 160px; overflow-y: auto;
        }
        .log-line { padding: 3px 0; border-bottom: 1px solid rgba(255,255,255,0.05); color: var(--accent-cyan); }
    </style>
</head>
<body>
    <div class="header">
        <div>
            <h1>MINI POWER PLANT // SCADA HMI</h1>
            <p style="font-size:0.8rem; color:var(--text-muted); margin-top:3px;">Purdue Model Level 2 Supervisory Station</p>
        </div>
        <div style="display:flex; gap:10px;">
            <span class="badge badge-mqtt">MQTT: TCP 1883</span>
            <span class="badge badge-live" id="connStatus">LIVE TELEMETRY</span>
        </div>
    </div>

    <div class="grid">
        <div class="card">
            <div class="card-label">Core Temperature</div>
            <div class="card-val" id="dispTemp">-- °C</div>
        </div>
        <div class="card">
            <div class="card-label">Humidity</div>
            <div class="card-val" id="dispHum">-- %</div>
        </div>
        <div class="card">
            <div class="card-label">Generator Motor</div>
            <div id="dispMotor"><span class="status-pill status-off">OFF</span></div>
        </div>
        <div class="card">
            <div class="card-label">IR Safety Interlock</div>
            <div id="dispIr"><span class="status-pill status-on">CLEAR</span></div>
        </div>
        <div class="card">
            <div class="card-label">Active Power Output</div>
            <div class="card-val" id="dispPower">-- kW</div>
        </div>
        <div class="card">
            <div class="card-label">System Pressure</div>
            <div class="card-val" id="dispPressure">-- Bar</div>
        </div>
        <div class="card">
            <div class="card-label">Safety Thermal Trip Limit</div>
            <div class="card-val" id="dispThresh">-- °C</div>
        </div>
    </div>

    <div class="control-panel">
        <h2>Supervisory Actuation & Setpoint Control</h2>
        <div class="btn-group">
            <button class="btn-start" onclick="sendMqttCmd('motor', 1)">Start Motor (Override)</button>
            <button class="btn-stop" onclick="sendMqttCmd('motor', 0)">Stop Motor</button>
            <button class="btn-estop" onclick="sendMqttCmd('emergency', 1)">EMERGENCY STOP (E-STOP)</button>
        </div>
        <div class="threshold-box">
            <label style="font-size:0.85rem; color:var(--text-muted);">Set Safety Trip Threshold (°C):</label>
            <input type="number" id="inputThresh" value="30" step="0.5">
            <button class="btn-update" onclick="updateThreshold()">Apply Setpoint</button>
        </div>
    </div>

    <div class="control-panel" style="margin-bottom:0;">
        <h2>Supervisory MQTT Telemetry & Alarm Stream</h2>
        <div class="log-section" id="logContainer">
            <div class="log-line">> [Awaiting MQTT Telemetry Stream...]</div>
        </div>
    </div>

    <script>
        function updateUI() {
            fetch('/api/telemetry')
                .then(res => res.json())
                .then(data => {
                    document.getElementById('dispTemp').innerText = data.temp.toFixed(1) + ' °C';
                    document.getElementById('dispHum').innerText = data.hum.toFixed(1) + ' %';
                    document.getElementById('dispPower').innerText = data.power.toFixed(2) + ' kW';
                    document.getElementById('dispPressure').innerText = data.pressure.toFixed(2) + ' Bar';
                    document.getElementById('dispThresh').innerText = data.threshold.toFixed(1) + ' °C';

                    let motorEl = document.getElementById('dispMotor');
                    motorEl.innerHTML = data.motor ? 
                        '<span class="status-pill status-on">RUNNING</span>' : 
                        '<span class="status-pill status-off">STOPPED</span>';

                    let irEl = document.getElementById('dispIr');
                    irEl.innerHTML = data.ir_obstacle ? 
                        '<span class="status-pill status-warn">TRIPPED (OBSTACLE)</span>' : 
                        '<span class="status-pill status-on">CLEAR</span>';

                    let logHtml = '';
                    data.logs.forEach(l => { logHtml += `<div class="log-line">> ${l}</div>`; });
                    document.getElementById('logContainer').innerHTML = logHtml;
                })
                .catch(err => {
                    document.getElementById('connStatus').innerText = 'OFFLINE';
                    document.getElementById('connStatus').style.borderColor = 'red';
                });
        }

        setInterval(updateUI, 1500);

        function sendMqttCmd(key, val) {
            fetch(`/api/command?${key}=${val}`)
                .then(r => updateUI());
        }

        function updateThreshold() {
            let val = document.getElementById('inputThresh').value;
            fetch(`/api/config?threshold=${val}`)
                .then(r => updateUI());
        }
    </script>
</body>
</html>
)rawliteral";

// -----------------------------------------------------------------------------
// Web API Handlers (Bridge Web SCADA to MQTT Topics)
// -----------------------------------------------------------------------------
void handleRoot() {
  server.send(200, "text/html", index_html);
}

void handleTelemetryApi() {
  String json = "{";
  json += "\"temp\":" + String(currentTemp, 1) + ",";
  json += "\"hum\":" + String(currentHum, 1) + ",";
  json += "\"motor\":" + String(isMotorOn ? "true" : "false") + ",";
  json += "\"relay\":" + String(isRelayOn ? "true" : "false") + ",";
  json += "\"alarm\":" + String(isAlarmOn ? "true" : "false") + ",";
  json += "\"ir_obstacle\":" + String(isIrDetected ? "true" : "false") + ",";
  json += "\"power\":" + String(powerOutput, 2) + ",";
  json += "\"pressure\":" + String(pressure, 2) + ",";
  json += "\"threshold\":" + String(tempThreshold, 1) + ",";
  json += "\"logs\":[";
  for (int i = 0; i < 5; i++) {
    json += "\"" + eventLog[i] + "\"";
    if (i < 4) json += ",";
  }
  json += "]}";

  server.send(200, "application/json", json);
}

void handleCommandApi() {
  if (server.hasArg("motor")) {
    int m = server.arg("motor").toInt();
    String payload = "{\"motor\":" + String(m) + "}";
    mqttClient.publish(TOPIC_COMMANDS, payload.c_str());
    addLog("[HMI PUBLISH] plant/commands -> " + payload);
  }
  if (server.hasArg("emergency")) {
    String payload = "{\"emergency\":1}";
    mqttClient.publish(TOPIC_COMMANDS, payload.c_str());
    addLog("[HMI PUBLISH CRITICAL] plant/commands -> " + payload);
  }
  server.send(200, "text/plain", "OK");
}

void handleConfigApi() {
  if (server.hasArg("threshold")) {
    float t = server.arg("threshold").toFloat();
    tempThreshold = t;
    String payload = "{\"threshold\":" + String(t, 1) + "}";
    mqttClient.publish(TOPIC_CONFIG, payload.c_str());
    addLog("[HMI PUBLISH] plant/config -> " + payload);
  }
  server.send(200, "text/plain", "OK");
}

// -----------------------------------------------------------------------------
// Setup Routine
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("\n========================================================");
  Serial.println("   ICS LABORATORY - HMI SUPERVISORY NODE (LEVEL 2)       ");
  Serial.println("   PROTOCOL: MQTT v3.1.1 (TCP 1883) & SCADA Web HMI     ");
  Serial.println("========================================================");

  // Configure Wi-Fi Access Point for the SCADA Subnet
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  Serial.printf("[AP] SCADA Network Started: SSID='%s' | Gateway IP: %s\n", 
                ssid, WiFi.softAPIP().toString().c_str());

  // Configure MQTT Client
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);

  // Setup Web Server Endpoints
  server.on("/", handleRoot);
  server.on("/api/telemetry", handleTelemetryApi);
  server.on("/api/command", handleCommandApi);
  server.on("/api/config", handleConfigApi);
  server.begin();
  Serial.println("[HTTP] SCADA Web Dashboard listening on port 80");
}

// -----------------------------------------------------------------------------
// Main Execution Loop
// -----------------------------------------------------------------------------
void loop() {
  if (!mqttClient.connected()) {
    reconnectMqtt();
  }
  mqttClient.loop();
  server.handleClient();
}
