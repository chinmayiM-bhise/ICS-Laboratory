# 🏭 Mini Power Plant - Operational Setup Guide
## Purdue Model Level 1 & Level 2 MQTT Integration

This guide details the exact operational sequence to bring the **Mini Power Plant SCADA & OT Testbed** online with **MQTT telemetry** and **Wazuh SOC monitoring**.

---

## 📦 Prerequisites & Arduino Libraries
Before flashing the microcontrollers, install these libraries via **Arduino IDE -> Library Manager**:
1. **`PubSubClient`** (by Nick O'Leary) - MQTT Client for ESP8266.
   * *Important:* In `PubSubClient.h`, verify `MQTT_MAX_PACKET_SIZE` is at least `256` (default is 256 in v2.8+).
2. **`DHT sensor library`** (by Adafruit) - Temperature & Humidity sensor.
3. **`Adafruit Unified Sensor`** - DHT dependency.
4. **`ESP8266 Board Package`** (v3.0.2 or later).

---

## 🚀 Step 1: Powering the Supervisory Station (HMI Node)
The HMI Node provides the supervisory Wi-Fi Access Point (`Mini_OT_SCADA`) and serves the SCADA dashboard:
1. Connect **ESP8266 #2 (HMI)** to your laptop or USB power.
2. The node will initialize the SoftAP network at `192.168.4.1`.
3. **Verification:**
   * Open your laptop Wi-Fi settings. Connect to SSID: **`Mini_OT_SCADA`**, Password: **`admin123`**.
   * Open a browser to **`http://192.168.4.1`** to access the Level 2 SCADA Dashboard.

---

## 🚀 Step 2: MQTT Broker Service (Level 2 Gateway)
In an industrial plant, MQTT telemetry flows through a Level 2 supervisory broker:
* **Option A (Laptop / Edge Gateway):** If your laptop is connected to `Mini_OT_SCADA`, start Mosquitto broker:
  ```bash
  mosquitto -p 1883 -v
  ```
* **Option B (Embedded):** If running standalone, the HMI node relays commands and telemetry between the web dashboard and MQTT topics.

---

## 🚀 Step 3: Powering the Field Controller (PLC Node)
1. Verify physical wiring against [`HARDWARE_WIRING_GUIDE.md`](HARDWARE_WIRING_GUIDE.md).
2. Plug 5V power into **ESP8266 #1 (PLC)**.
3. **Boot Sequence:**
   * Green LED illuminates (Normal Status).
   * PLC joins Wi-Fi network `Mini_OT_SCADA` and gets an IP (e.g. `192.168.4.2`).
   * PLC establishes MQTT connection to `192.168.4.1:1883`.
   * Subscribes to `plant/commands` and `plant/config`.
   * Begins publishing real sensor data every 2 seconds to `plant/telemetry`.
   * Initializes UDP backdoor listener on port `8888`.

---

## 🚀 Step 4: Launch Passive OT SOC Monitor
To monitor the plant and export alerts into Wazuh:
1. Open a terminal on a machine connected to the `Mini_OT_SCADA` network.
2. Start the passive OT sensor:
   ```bash
   python soc_engine/ot_soc_sensor.py --broker 192.168.4.1
   ```
3. The sensor begins passively inspecting MQTT packets and will alert on safety violations (e.g., motor active while obstacle sensor is tripped).

---

## 🧪 Step 5: System Operational Validation

### Test 1: Real-Time Telemetry Flow
* Observe the SCADA Dashboard at `http://192.168.4.1`.
* Breathe on the DHT11 sensor: watch temperature and humidity gauges update in real time.

### Test 2: MQTT Supervisory Actuation
* Click **"START MOTOR (OVERRIDE)"** on the dashboard.
* Relay clicks ON, DC Motor spins, and the motor card updates to `RUNNING`.

### Test 3: Safety Interlock Override Verification (TRITON T0888)
* Place an obstacle in front of the IR sensor.
* Red LED lights up, dashboard flags `TRIPPED (OBSTACLE)`.
* **Observation:** The motor continues spinning despite the obstacle hazard, proving the broken safety interlock vulnerability. The SOC sensor logs a **Critical Wazuh Rule 100201** alert!
