/*
 ==============================================================================================
  PROJECT: ICS Laboratory - Mini Power Plant SCADA & OT Testbed
  NODE: Dual-Protocol PLC Field Controller (Purdue Model Level 1)
  COMMUNICATION PROTOCOLS: 
    1. Modbus/TCP Server (Industrial Standard on TCP Port 502)
    2. MQTT v3.1.1 Client (IIoT Telemetry on TCP Port 1883)
    3. UDP Maintenance Backdoor (Shadow Port 8888)
  
  REAL-WORLD OT CONTEXT & VULNERABILITY MATRIX:
  - T0888 (TRITON / TRISIS): Broken Safety Interlock (Loss of Safety)
  - T0836 (Stuxnet / Oldsmar): Setpoint Parameter Tampering (Modbus FC06 / MQTT plant/config)
  - T0855 (Industroyer): Unauthenticated Control Injection (Modbus FC05 / MQTT plant/commands)
  - T0802 (Reconnaissance): Modbus Register Enumeration (FC01/02/03/04) & MQTT Wildcard (#)
  - T0887: Cleartext Network Sniffing (Modbus Port 502 & MQTT Port 1883)
  - T0812: Default Hardcoded Wi-Fi & Static Credentials
  - T0859: Out-of-Band Hardware Backdoor via UDP listener (Port 8888)
  - T0814 (Aurora): Actuator Flapping / Mechanical Fatigue DoS
 ==============================================================================================
*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <WiFiUdp.h>

// --- Wi-Fi Configuration ---
const char* ssid     = "Mini_OT_SCADA";
const char* password = "admin123";

// --- MQTT Configuration (Level 2 Supervisory Broker) ---
const char* mqtt_server = "192.168.4.1";
const int   mqtt_port   = 1883;
const char* mqtt_client = "PLC_NODE_01";

// --- MQTT Topics ---
const char* TOPIC_TELEMETRY = "plant/telemetry";
const char* TOPIC_COMMANDS  = "plant/commands";
const char* TOPIC_CONFIG    = "plant/config";
const char* TOPIC_ALERTS    = "plant/alerts";

// --- Physical Pin Definitions (ESP8266 NodeMCU) ---
#define DHTPIN     D2    // GPIO4  - DHT11 Environmental Sensor
#define DHTTYPE    DHT11 // DHT11 Sensor
#define RELAY_PIN  D5    // GPIO14 - Actuator / Motor Relay
#define IR_PIN     D6    // GPIO12 - IR Obstacle Safety Interlock Sensor
#define GREEN_LED  D7    // GPIO13 - Normal Operation Indicator
#define RED_LED    D8    // GPIO15 - Safety Alarm / Tripped Indicator

// Sensor & Network Instances
DHT dht(DHTPIN, DHTTYPE);
WiFiClient espClient;
PubSubClient mqttClient(espClient);
WiFiServer modbusServer(502); // Standard Modbus/TCP Industrial Port
WiFiClient modbusClientSession;

// --- Plant State Variables ---
float currentTemp       = 24.0;
float currentHum        = 50.0;
bool  isIrDetected      = false;
bool  isMotorOn         = false;
bool  isAlarmActive     = false;
float safetyThreshold   = 30.0; // Default temperature setpoint (°C)
bool  hmiMotorOverride  = false; // Forced state commanded via Modbus/MQTT

// Telemetry Publication Timer
unsigned long lastTelemetryPublish = 0;
const unsigned long telemetryInterval = 2000; // Publish every 2 seconds

// --- VULNERABILITY: Out-of-Band Hardware Backdoor (UDP Port 8888) ---
WiFiUDP udpBackdoor;
const unsigned int backdoorPort = 8888;
char packetBuffer[255];
bool sabotageMode = false; // Logic bomb trigger

// -----------------------------------------------------------------------------
// Helper: Simple JSON string parser
// -----------------------------------------------------------------------------
float extractJsonFloat(String json, String key) {
  int keyIndex = json.indexOf("\"" + key + "\":");
  if (keyIndex == -1) keyIndex = json.indexOf(key + ":");
  if (keyIndex == -1) return -999.0;
  
  int colonIndex = json.indexOf(':', keyIndex);
  int commaIndex = json.indexOf(',', colonIndex);
  int braceIndex = json.indexOf('}', colonIndex);
  int endIndex   = (commaIndex != -1 && commaIndex < braceIndex) ? commaIndex : braceIndex;
  
  String valStr = json.substring(colonIndex + 1, endIndex);
  valStr.trim();
  valStr.replace("\"", "");
  return valStr.toFloat();
}

int extractJsonInt(String json, String key) {
  return (int)extractJsonFloat(json, key);
}

// -----------------------------------------------------------------------------
// MQTT Message Callback
// -----------------------------------------------------------------------------
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  String msg = String((char*)payload);
  String topicStr = String(topic);

  Serial.printf("[MQTT RECV] Topic: %s | Payload: %s\n", topic, msg.c_str());

  // 1. Process Commands (Topic: plant/commands) - VULN T0855
  if (topicStr == TOPIC_COMMANDS) {
    int motorCmd = extractJsonInt(msg, "motor");
    if (motorCmd == 1) {
      hmiMotorOverride = true;
      isMotorOn = true;
      Serial.println("[PLC ACTION] Motor override forced ON via MQTT");
    } else if (motorCmd == 0) {
      hmiMotorOverride = false;
      isMotorOn = false;
      Serial.println("[PLC ACTION] Motor override disabled via MQTT");
    }

    int eStop = extractJsonInt(msg, "emergency");
    if (eStop == 1) {
      hmiMotorOverride = false;
      isMotorOn = false;
      isAlarmActive = true;
      mqttClient.publish(TOPIC_ALERTS, "{\"alert\":\"EMERGENCY_SHUTDOWN_TRIPPED\",\"source\":\"OPERATOR_MQTT\"}");
      Serial.println("[PLC ALERT] Emergency Stop Activated!");
    }
  }

  // 2. Process Setpoint Configuration (Topic: plant/config) - VULN T0836
  if (topicStr == TOPIC_CONFIG) {
    float newThresh = extractJsonFloat(msg, "threshold");
    if (newThresh != -999.0) {
      safetyThreshold = newThresh;
      Serial.printf("[PLC CONFIG] Safety temperature threshold updated to: %.2f C\n", safetyThreshold);
      String ack = "{\"event\":\"THRESHOLD_UPDATED\",\"new_threshold\":" + String(safetyThreshold) + "}";
      mqttClient.publish(TOPIC_ALERTS, ack.c_str());
    }
  }
}

// -----------------------------------------------------------------------------
// Connect to MQTT Broker
// -----------------------------------------------------------------------------
void reconnectMqtt() {
  if (!mqttClient.connected()) {
    if (mqttClient.connect(mqtt_client)) {
      Serial.println("[MQTT] Connected to supervisory broker!");
      mqttClient.subscribe(TOPIC_COMMANDS);
      mqttClient.subscribe(TOPIC_CONFIG);
    }
  }
}

// -----------------------------------------------------------------------------
// Modbus/TCP Request Handler (Standard Industrial Protocol - TCP Port 502)
// Memory Map:
//   Coil 00001 (0x0000): Motor Relay State (0=OFF, 1=ON) [FC01, FC05]
//   Coil 00002 (0x0001): Emergency Shutdown Trip [FC05]
//   Discrete Input 10001 (0x0000): IR Obstacle Sensor (0=Clear, 1=Trip) [FC02]
//   Discrete Input 10002 (0x0001): Alarm Indicator [FC02]
//   Input Register 30001 (0x0000): Core Temperature in C * 10 [FC04]
//   Input Register 30002 (0x0001): Humidity in % * 10 [FC04]
//   Input Register 30003 (0x0002): Power in kW * 100 [FC04]
//   Input Register 30004 (0x0003): Pressure in Bar * 100 [FC04]
//   Holding Register 40001 (0x0000): Safety Temp Threshold in C * 10 [FC03, FC06]
// -----------------------------------------------------------------------------
void processModbusTcp() {
  if (!modbusClientSession || !modbusClientSession.connected()) {
    modbusClientSession = modbusServer.available();
  }

  if (modbusClientSession && modbusClientSession.available() >= 7) {
    uint8_t buffer[260];
    int len = modbusClientSession.read(buffer, sizeof(buffer));
    if (len < 8) return;

    // Parse MBAP Header (Bytes 0-6)
    uint16_t transactionId = (buffer[0] << 8) | buffer[1];
    uint16_t protocolId    = (buffer[2] << 8) | buffer[3]; // 0 = Modbus
    uint16_t length        = (buffer[4] << 8) | buffer[5];
    uint8_t  unitId        = buffer[6];

    if (protocolId != 0) return; // Not standard Modbus

    // Parse PDU (Bytes 7+)
    uint8_t functionCode = buffer[7];
    uint16_t startAddress = (buffer[8] << 8) | buffer[9];

    Serial.printf("[MODBUS TCP] Client: %s | FC: 0x%02X | Addr: %d\n",
                  modbusClientSession.remoteIP().toString().c_str(), functionCode, startAddress);

    // FC 01: Read Coils
    if (functionCode == 0x01) {
      uint16_t quantity = (buffer[10] << 8) | buffer[11];
      uint8_t coilByte = 0;
      if (isMotorOn) coilByte |= (1 << 0);

      uint8_t response[10];
      response[0] = buffer[0]; response[1] = buffer[1]; // Tx ID
      response[2] = 0x00;      response[3] = 0x00;      // Protocol ID
      response[4] = 0x00;      response[5] = 0x04;      // Length
      response[6] = unitId;                             // Unit ID
      response[7] = 0x01;                               // FC
      response[8] = 0x01;                               // Byte Count
      response[9] = coilByte;                           // Coil Status
      modbusClientSession.write(response, 10);
    }
    // FC 02: Read Discrete Inputs (Sensors)
    else if (functionCode == 0x02) {
      uint8_t inputByte = 0;
      if (isIrDetected)  inputByte |= (1 << 0); // Discrete Input 10001
      if (isAlarmActive) inputByte |= (1 << 1); // Discrete Input 10002

      uint8_t response[10];
      response[0] = buffer[0]; response[1] = buffer[1];
      response[2] = 0x00;      response[3] = 0x00;
      response[4] = 0x00;      response[5] = 0x04;
      response[6] = unitId;
      response[7] = 0x02;
      response[8] = 0x01;
      response[9] = inputByte;
      modbusClientSession.write(response, 10);
    }
    // FC 03: Read Holding Registers (Thresholds)
    else if (functionCode == 0x03) {
      uint16_t threshInt = (uint16_t)(safetyThreshold * 10);
      uint8_t response[11];
      response[0] = buffer[0]; response[1] = buffer[1];
      response[2] = 0x00;      response[3] = 0x00;
      response[4] = 0x00;      response[5] = 0x05;
      response[6] = unitId;
      response[7] = 0x03;
      response[8] = 0x02; // 2 bytes
      response[9] = (threshInt >> 8) & 0xFF;
      response[10] = threshInt & 0xFF;
      modbusClientSession.write(response, 11);
    }
    // FC 04: Read Input Registers (Process Telemetry)
    else if (functionCode == 0x04) {
      uint16_t tempInt = (uint16_t)(currentTemp * 10);
      uint16_t humInt  = (uint16_t)(currentHum * 10);
      uint16_t powerInt = isMotorOn ? 450 : 80;
      uint16_t presInt  = isMotorOn ? 820 : 310;

      uint8_t response[17];
      response[0] = buffer[0]; response[1] = buffer[1];
      response[2] = 0x00;      response[3] = 0x00;
      response[4] = 0x00;      response[5] = 0x0B; // Length 11 bytes
      response[6] = unitId;
      response[7] = 0x04;
      response[8] = 0x08; // 8 data bytes
      response[9]  = (tempInt >> 8) & 0xFF;  response[10] = tempInt & 0xFF;
      response[11] = (humInt >> 8) & 0xFF;   response[12] = humInt & 0xFF;
      response[13] = (powerInt >> 8) & 0xFF; response[14] = powerInt & 0xFF;
      response[15] = (presInt >> 8) & 0xFF;  response[16] = presInt & 0xFF;
      modbusClientSession.write(response, 17);
    }
    // FC 05: Force Single Coil (Motor Actuation) - VULN T0855 (Industroyer style)
    else if (functionCode == 0x05) {
      uint16_t coilVal = (buffer[10] << 8) | buffer[11];
      if (startAddress == 0) { // Coil 00001 (Motor Relay)
        if (coilVal == 0xFF00) {
          hmiMotorOverride = true;
          isMotorOn = true;
          Serial.println("[MODBUS FC05] Forced Motor ON via Modbus/TCP!");
        } else if (coilVal == 0x0000) {
          hmiMotorOverride = false;
          isMotorOn = false;
          Serial.println("[MODBUS FC05] Forced Motor OFF via Modbus/TCP!");
        }
      } else if (startAddress == 1) { // Coil 00002 (E-Stop)
        hmiMotorOverride = false;
        isMotorOn = false;
        isAlarmActive = true;
        Serial.println("[MODBUS FC05] E-Stop Triggered via Modbus/TCP!");
      }
      // Modbus echo response
      modbusClientSession.write(buffer, 12);
    }
    // FC 06: Preset Single Register (Threshold Setpoint) - VULN T0836 (Stuxnet/Oldsmar style)
    else if (functionCode == 0x06) {
      uint16_t regVal = (buffer[10] << 8) | buffer[11];
      if (startAddress == 0) { // Register 40001 (Threshold)
        safetyThreshold = (float)regVal / 10.0;
        Serial.printf("[MODBUS FC06] Safety Threshold Tampered to: %.1f C via Modbus/TCP!\n", safetyThreshold);
      }
      // Modbus echo response
      modbusClientSession.write(buffer, 12);
    }
  }
}

// -----------------------------------------------------------------------------
// Setup Routine
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("\n========================================================");
  Serial.println("   ICS LABORATORY - HYBRID OT CONTROLLER (LEVEL 1)      ");
  Serial.println("   PROTOCOLS: Modbus/TCP (Port 502) & MQTT (Port 1883)   ");
  Serial.println("========================================================");

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Safe default: Relay OFF

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(IR_PIN, INPUT);

  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(RED_LED, LOW);

  dht.begin();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.printf("[WIFI] Connecting to SCADA AP '%s'...", ssid);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WIFI] Connected!");
  Serial.printf("[WIFI] PLC Assigned IP: %s\n", WiFi.localIP().toString().c_str());

  // Start Modbus/TCP Server (Industrial Port 502)
  modbusServer.begin();
  Serial.println("[MODBUS/TCP] Server listening on standard TCP Port 502");

  // Setup MQTT Client
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);

  // VULNERABILITY T0859: Start Unauthenticated UDP Hardware Backdoor Listener
  udpBackdoor.begin(backdoorPort);
  Serial.printf("[BACKDOOR] Listening on UDP Port %d\n", backdoorPort);
}

// -----------------------------------------------------------------------------
// Main Industrial Execution Loop
// -----------------------------------------------------------------------------
void loop() {
  // 1. Process Modbus/TCP Industrial Requests
  processModbusTcp();

  // 2. Process MQTT Telemetry & Commands
  if (!mqttClient.connected()) {
    reconnectMqtt();
  }
  mqttClient.loop();

  // 3. Process UDP Backdoor (VULN T0859)
  int packetSize = udpBackdoor.parsePacket();
  if (packetSize) {
    int len = udpBackdoor.read(packetBuffer, 254);
    if (len > 0) packetBuffer[len] = 0;
    String cmd = String(packetBuffer);
    cmd.trim();

    if (cmd == "FORCE_MOTOR_ON") {
      hmiMotorOverride = true;
      isMotorOn = true;
      Serial.println("[BACKDOOR] Forced Motor ON directly at hardware register");
    } else if (cmd == "FORCE_MOTOR_OFF") {
      hmiMotorOverride = false;
      isMotorOn = false;
      Serial.println("[BACKDOOR] Forced Motor OFF directly at hardware register");
    } else if (cmd == "SABOTAGE") {
      sabotageMode = true;
      Serial.println("[BACKDOOR CRITICAL] Logic bomb armed: Thermal trips disabled!");
    }
  }

  // 4. Physical Sensors & Safety Logic
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (!isnan(h) && !isnan(t)) {
    currentHum = h;
    currentTemp = t;
  }

  isIrDetected = (digitalRead(IR_PIN) == LOW);
  isAlarmActive = false;

  if (isIrDetected) {
    isAlarmActive = true;
  }

  if (currentTemp > safetyThreshold && !sabotageMode) {
    isAlarmActive = true;
    isMotorOn = true;
  } else if (!hmiMotorOverride) {
    isMotorOn = false;
  }

  // VULNERABILITY T0888: Broken Safety Interlock (TRITON style)
  if (hmiMotorOverride) {
    isMotorOn = true;
  }

  // 5. Physical Actuators
  digitalWrite(RELAY_PIN, isMotorOn ? HIGH : LOW);

  if (isAlarmActive) {
    digitalWrite(RED_LED, HIGH);
    digitalWrite(GREEN_LED, LOW);
  } else {
    digitalWrite(RED_LED, LOW);
    digitalWrite(GREEN_LED, HIGH);
  }

  // 6. Publish Real-Time MQTT Telemetry
  if (millis() - lastTelemetryPublish >= telemetryInterval) {
    lastTelemetryPublish = millis();

    float powerKw  = isMotorOn ? 4.5 : 0.8;
    float pressure = isMotorOn ? 8.2 : 3.1;

    String payload = "{";
    payload += "\"plant_id\":\"PLANT_ALPHA\",";
    payload += "\"temp\":" + String(currentTemp, 1) + ",";
    payload += "\"hum\":" + String(currentHum, 1) + ",";
    payload += "\"motor\":" + String(isMotorOn ? 1 : 0) + ",";
    payload += "\"relay\":" + String(isMotorOn ? 1 : 0) + ",";
    payload += "\"alarm\":" + String(isAlarmActive ? 1 : 0) + ",";
    payload += "\"ir_obstacle\":" + String(isIrDetected ? 1 : 0) + ",";
    payload += "\"power\":" + String(powerKw, 2) + ",";
    payload += "\"pressure\":" + String(pressure, 2) + ",";
    payload += "\"threshold\":" + String(safetyThreshold, 1);
    payload += "}";

    mqttClient.publish(TOPIC_TELEMETRY, payload.c_str());
  }

  delay(20);
}
