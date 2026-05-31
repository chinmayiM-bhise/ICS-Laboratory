# Mini Power Plant - Hardware & Wiring Guide

This guide contains every single physical connection required for the project. Follow this precisely to ensure the system works safely and as programmed.

## 1. Component List
- 1x ESP8266 NodeMCU (HMI Node)
- 1x ESP8266 NodeMCU (PLC Node)
- 1x DHT11 Temperature & Humidity Sensor
- 1x IR Obstacle Sensor (HW-201)
- 1x 5V Relay Module (1-Channel or 2-Channel)
- 1x DC Motor (Turbine/Fan)
- 1x Green LED (Normal Status)
- 1x Red LED (Alarm Status)
- 2x 220Ω Resistors (for LEDs)
- 1x 10kΩ Resistor (for DHT11 if not on a breakout module)
- Breadboard and Jumper Wires

---

## 2. PLC Node (ESP8266 #1) Connections
This is where all sensors and actuators are connected.

### A. Power Supply
1. **Micro-USB Port:** Plug in the 5V/2A Wall Adapter.
2. **Vin Pin:** This will now provide 5V out. Use this for the Relay and Motor power.
3. **GND Pin:** Connect to the breadboard's ground rail. All components MUST connect to this rail.

### B. Sensors
**1. DHT11 (Temperature):**
- **VCC:** Connect to **3.3V** pin on ESP8266.
- **GND:** Connect to **GND** rail.
- **DATA:** Connect to **D2 (GPIO4)**.

**2. IR Obstacle Sensor:**
- **VCC:** Connect to **3.3V** pin on ESP8266.
- **GND:** Connect to **GND** rail.
- **OUT:** Connect to **D6 (GPIO12)**.

### C. Indicators (LEDs)
**1. Green LED (Status):**
- **Long Leg (+):** To **D7 (GPIO13)** through a 220Ω resistor.
- **Short Leg (-):** To **GND** rail.

**2. Red LED (Alarm):**
- **Long Leg (+):** To **D8 (GPIO15)** through a 220Ω resistor.
- **Short Leg (-):** To **GND** rail.

### D. Actuators (Relay & Motor)
**1. Relay Module:**
- **VCC:** Connect to **Vin** (5V).
- **GND:** Connect to **GND** rail.
- **IN1:** Connect to **D5 (GPIO14)**.

**2. DC Motor:**
- **Wire 1:** Connect to the **Vin** (5V) pin of the ESP8266.
- **Wire 2:** Connect to the **Common (COM)** terminal of the Relay.
- **Relay NO (Normally Open) Terminal:** Connect to the **GND** rail of the breadboard.
*(Result: When the relay clicks, it completes the circuit to Ground, spinning the motor.)*

---

## 3. HMI Node (ESP8266 #2) Connections
This node is kept simple as it primarily handles software.

1. **Micro-USB Port:** Plug into Laptop USB for power and serial monitoring.
2. **On-board LED:** (Handled by code, no external wiring required).

---

## 4. Final Verification
- **Double check GND:** Ensure there is a single, continuous ground path for all PLC components.
- **Short Circuit Check:** Ensure no stray wires are touching.
- **Voltage Check:** Do NOT connect sensors to 5V unless they are specifically rated for it (DHT11 and IR are best on 3.3V). The Relay and Motor MUST use the 5V/Vin pin.
