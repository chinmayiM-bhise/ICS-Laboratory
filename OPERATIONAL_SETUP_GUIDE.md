# Mini Power Plant - Operational Setup Guide

This guide explains the step-by-step sequence to bring your "Industrial Plant" online after you have finished the hardware wiring.

## Step 1: Powering the Master (HMI Node)
The HMI Node must be powered on first because it hosts the Wi-Fi network that the PLC needs to find.
1. Connect **ESP8266 #2 (HMI)** to your laptop via USB.
2. Wait about 5-10 seconds for the internal web server to start.
3. **Verification:** Open your phone or laptop Wi-Fi settings. You should see a network named **`Mini_OT_SCADA`**. Connect to it using the password `admin123`.

## Step 2: Powering the Slave (PLC Node)
1. Ensure all sensors and the motor are wired according to the `HARDWARE_WIRING_GUIDE.md`.
2. Plug the **5V/2A Adapter** into the Micro-USB port of **ESP8266 #1 (PLC)**.
3. **Verification:** If you have the PLC connected to a serial monitor (115200 baud), you should see it say "Connected to SCADA Network!" and "Successfully synced data to HMI."

## Step 3: Accessing the SCADA Dashboard
1. Ensure your laptop is still connected to the `Mini_OT_SCADA` Wi-Fi.
2. Open a web browser and type: `http://192.168.4.1`
3. Enter the credentials:
   - **Username:** `admin`
   - **Password:** `admin123`
4. **Verification:** Look at the bottom-left corner of the dashboard. The "PLC CONNECTED" status should be **Green**.

## Step 4: First Test Run (Validation)
To ensure everything is working, perform these three tests:

### Test A: Telemetry Test
- Breathe on the DHT11 sensor or hold it between your fingers.
- You should see the **Temperature** and **Humidity** values rise on the dashboard within 2-3 seconds.

### Test B: Manual Control Test
- On the dashboard, click **"START MOTOR"**.
- You should hear the Relay "click" and the Motor should start spinning.
- Click **"STOP MOTOR"** to turn it off.

### Test C: Safety Logic Test (Vulnerability Demo)
- Place an object in front of the IR Obstacle sensor.
- The physical **Red LED** on the PLC should light up.
- On the dashboard, the "IR SENSOR" status should change to **"DETECTED"**.
- *Observation:* Note that even with the object detected, the motor continues to spin if you turned it on manually (demonstrating the "Missing Safety Interlock" vulnerability).

## Troubleshooting
- **Dashboard doesn't load:** Ensure you are connected to the `Mini_OT_SCADA` Wi-Fi and not your home Wi-Fi.
- **PLC not connecting:** Ensure the SSID and Password in `PLC_Node.ino` exactly match those in `HMI_Node.ino`.
- **Sensors show 0.0:** Check the wiring on the PLC. Ensure the GND wire is securely connected to the ESP8266 and the sensor.
