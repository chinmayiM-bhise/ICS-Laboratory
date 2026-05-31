#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <DHT.h>
#include <WiFiUdp.h>

// --- Configuration ---
// Wi-Fi Credentials for the HMI Access Point
const char* ssid = "Mini_OT_SCADA";
const char* password = "admin123";

// IP Address of the HMI Node (Default for ESP8266 AP is usually 192.168.4.1)
const char* hmi_server = "http://192.168.4.1";

// --- Pin Definitions (From PLAN.pdf) ---
#define DHTPIN D2        // GPIO4
#define DHTTYPE DHT11    // DHT 11 Sensor
#define RELAY_PIN D5     // GPIO14
#define IR_PIN D6        // GPIO12
#define GREEN_LED D7     // GPIO13
#define RED_LED D8       // GPIO15

// Initialize DHT sensor
DHT dht(DHTPIN, DHTTYPE);

// --- Plant State Variables ---
float currentTemp = 0.0;
float currentHum = 0.0;
bool isIrDetected = false;
bool isMotorOn = false;    // Actual state of the motor/relay
bool isAlarmActive = false;

// Variables synced from HMI
float safetyThreshold = 30.0; 
bool hmiMotorOverride = false; // If HMI tells us to force the motor on/off
   
// Timing for network updates
unsigned long lastUpdate = 0;
const unsigned long updateInterval = 2000; // Update HMI every 2 seconds

// --- VULNERABILITY GLOBALS ---
WiFiUDP udpBackdoor;
unsigned int backdoorPort = 8888;
char packetBuffer[255]; 
bool sabotageMode = false; // Used for Logic Bomb
// -----------------------------

void setup() {
  Serial.begin(115200);
  Serial.println("\nStarting PLC Node...");

  // Initialize Pins
  // Setting to LOW at startup to ensure it stays OFF for this specific relay.
  digitalWrite(RELAY_PIN, LOW); 
  pinMode(RELAY_PIN, OUTPUT);
  
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(IR_PIN, INPUT);

  // Start with indicators
  digitalWrite(GREEN_LED, HIGH); // Green means normal
  digitalWrite(RED_LED, LOW);

  dht.begin();

  // Connect to the HMI Wi-Fi Network
  WiFi.begin(ssid, password);
  Serial.print("Connecting to HMI AP: ");
  Serial.print(ssid);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nConnected to SCADA Network!");
  Serial.print("PLC IP address: ");
  Serial.println(WiFi.localIP());

  // VULNERABILITY: Start Unauthenticated UDP Backdoor
  udpBackdoor.begin(backdoorPort);
  Serial.printf("Backdoor listening on UDP port %d\n", backdoorPort);
}

void loop() {
  // VULNERABILITY: Check for Unauthenticated UDP Backdoor Commands
  int packetSize = udpBackdoor.parsePacket();
  if (packetSize) {
    int len = udpBackdoor.read(packetBuffer, 255);
    if (len > 0) packetBuffer[len] = 0;
    String cmd = String(packetBuffer);
    
    if (cmd == "FORCE_MOTOR_ON") {
      hmiMotorOverride = true;
      Serial.println("BACKDOOR: Forced Motor ON");
    } else if (cmd == "FORCE_MOTOR_OFF") {
      hmiMotorOverride = false;
      isMotorOn = false;
      Serial.println("BACKDOOR: Forced Motor OFF");
    } else if (cmd == "SABOTAGE") {
      sabotageMode = true;
      Serial.println("BACKDOOR: Logic Bomb Triggered! Safety Disabled.");
    }
  }

  // 1. Read Physical Sensors
  // Read Temperature and Humidity
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (!isnan(h) && !isnan(t)) {
    currentHum = h;
    currentTemp = t;
  }

  // Read IR Obstacle Sensor (LOW means obstacle detected in most modules)
  isIrDetected = (digitalRead(IR_PIN) == LOW);


  // 2. Network Sync: Get Commands from HMI & Send Data to HMI
  if (millis() - lastUpdate >= updateInterval) {
    lastUpdate = millis();
    syncWithHMI();
  }


  // 3. PLC Logic & Safety Interlocks
  isAlarmActive = false;

  // Logic Rule A: Obstacle Detection (Safety Interlock - VULNERABILITY 5)
  // According to PLAN.pdf, we intentionally leave this "Missing" initially to show danger, 
  // but for the final demo we should implement an emergency stop. 
  // Let's implement the *vulnerable* version first: The IR sensor triggers an alarm, 
  // but DOES NOT stop the motor if the HMI forced it on or temp is high.
  if (isIrDetected) {
    isAlarmActive = true;
    Serial.println("ALARM: Obstacle Detected near machinery!");
  }

  // Logic Rule B: Temperature Control
  // VULNERABILITY: Logic Bomb (sabotageMode) bypasses safety shutdown
  if (currentTemp > safetyThreshold && !sabotageMode) {
    isAlarmActive = true;
    isMotorOn = true; // Turn on cooling fan
    Serial.println("WARNING: High Temp! Activating cooling.");
  } else {
    if (!hmiMotorOverride) isMotorOn = false; // Normal temp, fan off
  }

  // Logic Rule C: HMI Manual Override (VULNERABILITY 3)
  // The HMI command overrides the local temperature logic.
  if (hmiMotorOverride) {
    isMotorOn = true;
  }


  // 4. Actuate Physical Outputs
  if (isMotorOn) {
    digitalWrite(RELAY_PIN, HIGH); // Relay ON
  } else {
    digitalWrite(RELAY_PIN, LOW);  // Relay OFF
  }

  // Update Indicator LEDs
  if (isAlarmActive) {
    digitalWrite(RED_LED, HIGH);
    digitalWrite(GREEN_LED, LOW);
  } else {
    digitalWrite(RED_LED, LOW);
    digitalWrite(GREEN_LED, HIGH);
  }

  delay(100); // Small delay for stability
}

// Function to handle HTTP communication with the HMI Dashboard
void syncWithHMI() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
    HTTPClient http;

    // --- STEP 1: Fetch Commands & Thresholds from HMI ---
    // We request the /data endpoint to see what the HMI wants us to do
    http.begin(client, String(hmi_server) + "/data");
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      
      // Basic manual JSON parsing to extract 'threshold' and 'motor' command
      // In a real project, use ArduinoJson library.
      
      // Extract Threshold
      int threshIndex = payload.indexOf("\"threshold\":");
      if (threshIndex != -1) {
        int endIndex = payload.indexOf(",", threshIndex);
        if(endIndex == -1) endIndex = payload.indexOf("}", threshIndex);
        String threshStr = payload.substring(threshIndex + 12, endIndex);
        safetyThreshold = threshStr.toFloat();
      }

      // Extract Motor Override State
      int motorIndex = payload.indexOf("\"motor\":true");
      if (motorIndex != -1) {
        hmiMotorOverride = true;
      } else {
        hmiMotorOverride = false;
      }
    }
    http.end();


    // --- STEP 2: Send Real Sensor Data back to HMI ---
    // Construct the update URL: /update?t=25.5&h=50.0&m=1&r=1&a=1&i=0
    String updateUrl = String(hmi_server) + "/update";
    updateUrl += "?t=" + String(currentTemp);
    updateUrl += "&h=" + String(currentHum);
    updateUrl += "&m=" + String(isMotorOn ? "1" : "0");
    updateUrl += "&r=" + String(isMotorOn ? "1" : "0"); // Assuming relay matches motor state
    updateUrl += "&a=" + String(isAlarmActive ? "1" : "0");
    updateUrl += "&i=" + String(isIrDetected ? "1" : "0");
    
    // We will simulate power and pressure based on motor state to make the HMI look cool
    float simulatedPower = isMotorOn ? 4.5 : 1.25;
    float simulatedPressure = isMotorOn ? 8.2 : 3.5;
    updateUrl += "&p=" + String(simulatedPower);
    updateUrl += "&pres=" + String(simulatedPressure);

    http.begin(client, updateUrl);
    int postCode = http.GET(); // Using GET to push data for simplicity in this demo
    if(postCode > 0) {
      Serial.println("Successfully synced data to HMI.");
    } else {
      Serial.println("Failed to sync data to HMI.");
    }
    http.end();

  } else {
    Serial.println("Wi-Fi Disconnected. Waiting for HMI AP...");
  }
}
