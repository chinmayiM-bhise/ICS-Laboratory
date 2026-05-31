#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

const char* ssid = "Mini_OT_SCADA";
const char* password = "admin123";

ESP8266WebServer server(80);

// Global State
float currentTemp = 72.0;
float currentHum = 45.0;
bool isMotorOn = true;
bool isRelayOn = true;
bool isAlarmOn = false;
bool isIrDetected = false;
float powerOutput = 1.25;
float pressure = 3.5;
float tempThreshold = 85.0;

// --- VULNERABILITY GLOBALS ---
String plantName = "MINI POWER PLANT"; // Used for Reflected XSS
char firmwareVersion[16] = "1.1.0";    // Target for Buffer Overflow
// -----------------------------

// Simple Event Log (stores last 3 events)
String eventLog[3] = {"SYSTEM STARTUP", "NORMAL OPERATION", "-"};
int logIndex = 2;

void addLog(String event) {
    eventLog[0] = eventLog[1];
    eventLog[1] = eventLog[2];
    eventLog[2] = event;
}

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>MINI POWER PLANT CONTROL SYSTEM</title>
    <style>
        :root {
            --bg-color: #050a10;
            --panel-bg: rgba(20, 30, 48, 0.8);
            --accent-blue: #00d2ff;
            --accent-green: #00ff88;
            --accent-red: #ff3344;
            --text-main: #e0e6ed;
            --border-glow: 0 0 10px rgba(0, 210, 255, 0.3);
        }

        body {
            background: radial-gradient(circle at center, #101b2d 0%, var(--bg-color) 100%);
            color: var(--text-main);
            font-family: 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;
            margin: 0;
            padding: 10px;
            overflow-x: hidden;
            text-transform: uppercase;
            letter-spacing: 1px;
        }

        /* Login Overlay */
        #login-overlay {
            position: fixed;
            top: 0; left: 0; width: 100%; height: 100%;
            background: rgba(5, 10, 16, 0.95);
            z-index: 9999;
            display: flex;
            flex-direction: column;
            justify-content: center;
            align-items: center;
            backdrop-filter: blur(10px);
        }
        .login-box {
            background: var(--panel-bg);
            border: 1px solid var(--accent-blue);
            padding: 30px;
            border-radius: 5px;
            box-shadow: 0 0 20px rgba(0, 210, 255, 0.4);
            text-align: center;
            width: 300px;
        }
        .login-input {
            width: 90%;
            padding: 10px;
            margin: 10px 0;
            background: #111;
            border: 1px solid #334;
            color: white;
        }

        .header {
            text-align: center;
            padding: 15px;
            border-bottom: 2px solid var(--accent-blue);
            box-shadow: 0 5px 15px rgba(0, 210, 255, 0.2);
            margin-bottom: 20px;
        }

        .header h1 {
            margin: 0;
            font-size: 24px;
            background: linear-gradient(to right, #fff, var(--accent-blue));
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }

        .main-container {
            display: grid;
            grid-template-columns: 280px 1fr 300px;
            gap: 20px;
            max-width: 1400px;
            margin: 0 auto;
        }

        .panel {
            background: var(--panel-bg);
            border: 1px solid rgba(0, 210, 255, 0.2);
            border-radius: 5px;
            padding: 15px;
            box-shadow: var(--border-glow);
            backdrop-filter: blur(5px);
        }

        .panel-header {
            color: var(--accent-blue);
            font-size: 14px;
            font-weight: bold;
            border-bottom: 1px solid rgba(255,255,255,0.1);
            padding-bottom: 5px;
            margin-bottom: 15px;
            display: flex;
            justify-content: space-between;
        }

        .status-item {
            display: flex;
            align-items: center;
            justify-content: space-between;
            margin-bottom: 15px;
            padding: 10px;
            background: rgba(0,0,0,0.2);
            border-radius: 3px;
        }

        .status-label { font-size: 12px; color: #8899aa; }
        .status-value { font-size: 18px; font-weight: bold; }
        .val-blue { color: var(--accent-blue); }
        .val-green { color: var(--accent-green); }
        .val-red { color: var(--accent-red); }

        .schematic-box {
            position: relative;
            height: 400px;
            background: url('https://www.transparenttextures.com/patterns/carbon-fibre.png'), rgba(0,0,0,0.4);
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            border: 1px dashed rgba(0, 210, 255, 0.3);
        }

        .system-diagram {
            width: 80%;
            height: 60%;
            border: 2px solid rgba(0, 210, 255, 0.2);
            position: relative;
            display: flex;
            justify-content: space-around;
            align-items: center;
        }

        .pipe {
            position: absolute;
            background: linear-gradient(90deg, transparent, var(--accent-blue), transparent);
            height: 4px;
            width: 100%;
            opacity: 0.5;
            animation: flow 2s linear infinite;
        }

        @keyframes flow { from { background-position: -200px; } to { background-position: 200px; } }

        .btn {
            width: 100%;
            padding: 12px;
            margin-bottom: 10px;
            border: none;
            border-radius: 3px;
            color: white;
            font-weight: bold;
            cursor: pointer;
            text-transform: uppercase;
            transition: 0.3s;
        }

        .btn-blue { background: linear-gradient(to bottom, #0088cc, #005588); border-bottom: 4px solid #003355; }
        .btn-emergency { background: linear-gradient(to bottom, #ff4455, #aa1122); border-bottom: 4px solid #770011; }
        .btn-override { background: linear-gradient(to bottom, #225588, #113366); border-bottom: 4px solid #081a33; }

        .input-group {
            display: flex;
            justify-content: space-between;
            margin-top: 10px;
            background: rgba(0,0,0,0.3);
            padding: 5px;
            border: 1px solid #334;
        }
        .input-group input { width: 60px; background: transparent; border: none; color: white; text-align: right; }
        .input-group button { background: var(--accent-blue); border: none; padding: 5px 10px; color: black; cursor: pointer; font-weight: bold;}

        .bottom-grid {
            display: grid;
            grid-template-columns: repeat(4, 1fr);
            gap: 15px;
            margin-top: 20px;
            max-width: 1400px;
            margin-left: auto;
            margin-right: auto;
        }

        .log-entry { font-size: 10px; color: #aaa; margin-bottom: 4px; border-bottom: 1px solid #222; padding-bottom: 2px;}

        .blink { animation: blinker 1.5s linear infinite; }
        @keyframes blinker { 50% { opacity: 0.2; } }
    </style>
</head>
<body>
    <!-- VULNERABILITY 1: Hardcoded Client-Side Login -->
    <div id="login-overlay">
        <div class="login-box">
            <h2 style="color: var(--accent-blue); margin-top: 0;">SCADA AUTHENTICATION</h2>
            <input type="text" id="user" class="login-input" placeholder="USERNAME" value="admin">
            <input type="password" id="pass" class="login-input" placeholder="PASSWORD">
            <button class="btn btn-blue" onclick="checkLogin()" style="margin-top: 15px;">LOGIN</button>
            <div id="login-error" style="color: var(--accent-red); font-size: 12px; margin-top: 10px; display: none;">ACCESS DENIED</div>
        </div>
    </div>

    <div class="header">
        <h1><span id="pName"></span> <span style="color:rgba(255,255,255,0.5)">CONTROL SYSTEM</span></h1>
    </div>

    <div class="main-container">
        <!-- Left Panel: Live Status -->
        <div class="panel">
            <div class="panel-header"><span>LIVE STATUS</span> [OK]</div>
            
            <div class="status-item">
                <span class="status-label">BOILER TEMP:</span>
                <span class="status-value val-blue" id="temp">-- &deg;C</span>
            </div>
            <div class="status-item">
                <span class="status-label">HUMIDITY:</span>
                <span class="status-value val-blue" id="hum">-- %</span>
            </div>
            <div class="status-item">
                <span class="status-label">TURBINE MOTOR:</span>
                <span class="status-value" id="motor">--</span>
            </div>
            <div class="status-item">
                <span class="status-label">SAFETY RELAY:</span>
                <span class="status-value" id="relay">--</span>
            </div>
            <div class="status-item">
                <span class="status-label">IR SENSOR:</span>
                <span class="status-value" id="ir">--</span>
            </div>
        </div>

        <!-- Middle Panel: Schematic -->
        <div class="panel">
            <div class="panel-header">SYSTEM SCHEMATIC</div>
            <div class="schematic-box">
                <div style="font-size:10px; color:var(--accent-blue); margin-bottom: 10px;">VIRTUAL REPRESENTATION</div>
                <div class="system-diagram">
                    <div style="border:1px solid #445566; padding: 10px; box-shadow: 0 0 10px #ff5500;">BOILER</div>
                    <div class="pipe"></div>
                    <div style="border:1px solid #445566; padding: 10px;">TURBINE</div>
                    <div style="border:1px solid #445566; padding: 10px;">GENERATOR</div>
                </div>
                <div style="margin-top: 20px; color: var(--accent-green); font-size: 12px;" class="blink">SYSTEM FLOW ACTIVE</div>
            </div>
        </div>

        <!-- Right Panel: Alerts & Controls -->
        <div class="panel">
            <div class="panel-header">ALERTS & CONTROLS</div>
            
            <div id="alarm-banner" style="background: rgba(255, 51, 68, 0.2); padding: 10px; border: 1px solid var(--accent-red); margin-bottom: 15px; font-size: 11px; text-align: center; display: none;">
                <span class="val-red blink">WARNING: HIGH TEMPERATURE DETECTED</span>
            </div>

            <button class="btn btn-emergency" onclick="sendCommand('emergency', 1)">EMERGENCY SHUTDOWN</button>
            
            <div style="display: flex; gap: 10px;">
                <button class="btn btn-override" onclick="sendCommand('motor', 1)">START MOTOR</button>
                <button class="btn btn-override" onclick="sendCommand('motor', 0)">STOP MOTOR</button>
            </div>

            <!-- VULNERABILITY 4: Unsafe Configuration -->
            <div style="margin-top: 20px; border-top: 1px solid #334; padding-top: 15px;">
                <span class="status-label">SAFETY THRESHOLD (&deg;C)</span>
                <div class="input-group">
                    <input type="number" id="threshInput" value="85">
                    <button onclick="setThreshold()">UPDATE</button>
                </div>
                <div style="font-size: 10px; color: #667; margin-top: 5px;">CURRENT: <span id="curThresh">--</span>&deg;C</div>
            </div>
        </div>
    </div>

    <!-- Bottom Panel mapping to Sample.png features -->
    <div class="bottom-grid">
        <div class="panel">
            <div class="panel-header">NETWORK STATUS</div>
            <div style="font-size: 12px; margin-bottom: 8px;"><span class="val-green">&check;</span> PLC CONNECTED</div>
            <div style="font-size: 12px;"><span class="val-green">&check;</span> SCADA ONLINE</div>
        </div>
        
        <!-- VULNERABILITY 6: Inadequate Logging -->
        <div class="panel">
            <div class="panel-header">DATA LOGS</div>
            <div id="logsContainer">
                <!-- Logs injected here -->
            </div>
        </div>

        <div class="panel">
            <div class="panel-header">POWER OUTPUT</div>
            <div class="status-value val-blue"><span id="pwr">1.25</span> MW</div>
        </div>

        <div class="panel">
            <div class="panel-header">SECURE CONNECTION</div>
            <div style="font-size: 12px; margin-bottom: 8px;"><span class="val-red">&cross;</span> ENCRYPTION DISABLED</div>
            <div style="font-size: 12px;"><span class="val-red">&cross;</span> PLAIN HTTP TRAFFIC</div>
        </div>
    </div>

    <script>
        // Highly insecure hardcoded login check
        function checkLogin() {
            if(document.getElementById('user').value === 'admin' && document.getElementById('pass').value === 'admin123') {
                document.getElementById('login-overlay').style.display = 'none';
            } else {
                document.getElementById('login-error').style.display = 'block';
            }
        }

        setInterval(function() {
            fetch('/data')
                .then(response => response.json())
                .then(data => {
                    // Reflected XSS Vulnerability: Using innerHTML for plant name
                    document.getElementById('pName').innerHTML = data.plantName;
                    
                    document.getElementById('temp').innerText = data.temperature.toFixed(1) + ' °C';
                    document.getElementById('hum').innerText = data.humidity.toFixed(1) + ' %';
                    
                    const motor = document.getElementById('motor');
                    motor.innerText = data.motor ? 'RUNNING' : 'STOPPED';
                    motor.className = data.motor ? 'status-value val-green' : 'status-value val-red';
                    
                    const relay = document.getElementById('relay');
                    relay.innerText = data.relay ? 'ACTIVE' : 'INACTIVE';
                    relay.className = data.relay ? 'status-value val-green' : 'status-value val-red';

                    const ir = document.getElementById('ir');
                    ir.innerText = data.ir ? 'DETECTED' : 'CLEAR';
                    ir.className = data.ir ? 'status-value val-red blink' : 'status-value val-green';

                    document.getElementById('alarm-banner').style.display = data.alarm ? 'block' : 'none';
                    document.getElementById('curThresh').innerText = data.threshold.toFixed(1);

                    // Update Logs
                    let logHtml = '';
                    data.logs.forEach(log => {
                        logHtml += `<div class="log-entry">> ${log}</div>`;
                    });
                    document.getElementById('logsContainer').innerHTML = logHtml;
                });
        }, 1000);

        function sendCommand(cmd, val) {
            fetch('/command?' + cmd + '=' + val);
        }

        function setThreshold() {
            let val = document.getElementById('threshInput').value;
            fetch('/command?thresh=' + val);
        }
    </script>
</body>
</html>
)rawliteral";

void handleRoot() { server.send(200, "text/html", index_html); }

void handleData() {
    String json = "{";
    json += "\"plantName\":\"" + plantName + "\",";
    json += "\"version\":\"" + String(firmwareVersion) + "\",";
    json += "\"temperature\":" + String(currentTemp) + ",";
    json += "\"humidity\":" + String(currentHum) + ",";
    json += "\"motor\":" + String(isMotorOn ? "true" : "false") + ",";
    json += "\"relay\":" + String(isRelayOn ? "true" : "false") + ",";
    json += "\"alarm\":" + String(isAlarmOn ? "true" : "false") + ",";
    json += "\"ir\":" + String(isIrDetected ? "true" : "false") + ",";
    json += "\"power\":" + String(powerOutput) + ",";
    json += "\"pressure\":" + String(pressure) + ",";
    json += "\"threshold\":" + String(tempThreshold) + ",";
    
    // Send logs
    json += "\"logs\":[\"" + eventLog[0] + "\",\"" + eventLog[1] + "\",\"" + eventLog[2] + "\"]";
    json += "}";
    server.send(200, "application/json", json);
}

void handleCommand() {
    if (server.hasArg("motor")) {
        isMotorOn = (server.arg("motor") == "1");
        addLog(isMotorOn ? "CMD: MOTOR STARTED" : "CMD: MOTOR STOPPED");
    }
    if (server.hasArg("emergency")) {
        isMotorOn = false;
        isRelayOn = false;
        isAlarmOn = true;
        addLog("CMD: EMERGENCY SHUTDOWN!");
    }
    if (server.hasArg("thresh")) {
        tempThreshold = server.arg("thresh").toFloat();
        addLog("CMD: THRESHOLD SET TO " + String(tempThreshold));
    }
    // VULNERABILITY: Reflected XSS via /command?set_name=
    if (server.hasArg("set_name")) {
        plantName = server.arg("set_name");
        addLog("SYSTEM: PLANT NAME UPDATED");
    }
    server.send(200, "text/plain", "OK");
}

// VULNERABILITY: Buffer Overflow (DoS) via unsafe strcpy
void handleFirmwareCheck() {
    char versionBuffer[16];
    if (server.hasArg("version")) {
        // Unsafe copy! Sending >16 chars will crash the ESP
        strcpy(versionBuffer, server.arg("version").c_str());
        server.send(200, "text/plain", "Firmware version recorded: " + String(versionBuffer));
    } else {
        server.send(400, "text/plain", "Missing version param");
    }
}

// VULNERABILITY: Path Traversal / Info Disclosure
void handleDownloadLog() {
    if (server.hasArg("file")) {
        String fileName = server.arg("file");
        if (fileName == "../../config.sys") {
            String secret = "WiFi_SSID: " + String(ssid) + "\nWiFi_PASS: " + String(password);
            server.send(200, "text/plain", secret);
        } else {
            server.send(404, "text/plain", "Log file not found");
        }
    }
}

void handleUpdate() {
    if (server.hasArg("t")) currentTemp = server.arg("t").toFloat();
    if (server.hasArg("h")) currentHum = server.arg("h").toFloat();
    if (server.hasArg("m")) isMotorOn = (server.arg("m") == "1");
    if (server.hasArg("r")) isRelayOn = (server.arg("r") == "1");
    if (server.hasArg("a")) isAlarmOn = (server.arg("a") == "1");
    if (server.hasArg("i")) isIrDetected = (server.arg("i") == "1");
    server.send(200, "text/plain", "OK");
}

void setup() {
    Serial.begin(115200);
    WiFi.softAP(ssid, password);
    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.on("/update", handleUpdate);
    server.on("/command", handleCommand);
    server.on("/firmware_check", handleFirmwareCheck);
    server.on("/download_log", handleDownloadLog);
    server.begin();
    Serial.println("SCADA Dashboard Online");
}

void loop() { server.handleClient(); }
