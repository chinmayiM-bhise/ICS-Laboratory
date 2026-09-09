# 🛡️ Wazuh SIEM Integration Guide: OT/ICS Telemetry & Detection

This guide documents how to ingest industrial OT telemetry and process anomaly alerts from the **ICS Laboratory** into an enterprise **Wazuh SIEM / XDR** deployment.

---

## 🏛️ Ingestion Architecture

```
[ESP8266 PLC Node] (Level 1) 
       │ 
       ▼ (MQTT over TCP 1883)
[HMI / Mosquitto Broker] (Level 2)
       │
       ▼ (Passive MQTT Telemetry Tap)
[soc_engine/ot_soc_sensor.py] (Level 2/3 NSM)
       │
       ├─► Local JSON Log File (/var/ossec/logs/ot_ics_alerts.log)
       └─► Syslog UDP 514 / Remote Ingestion
              │
              ▼
       [Wazuh Manager] (Level 3/4 Corporate SOC)
              ├─► Decoders: ot_ics_decoders.xml
              ├─► Rules: ot_ics_rules.xml
              └─► MITRE ATT&CK for ICS Dashboard
```

---

## 🚀 Step 1: Deploy Custom Decoders

Copy [`ot_ics_decoders.xml`](decoders/ot_ics_decoders.xml) to your Wazuh Manager:

```bash
sudo cp decoders/ot_ics_decoders.xml /var/ossec/etc/decoders/
sudo chown wazuh:wazuh /var/ossec/etc/decoders/ot_ics_decoders.xml
sudo chmod 660 /var/ossec/etc/decoders/ot_ics_decoders.xml
```

---

## 🚀 Step 2: Deploy Custom Rules

Copy [`ot_ics_rules.xml`](rules/ot_ics_rules.xml) to your Wazuh Manager:

```bash
sudo cp rules/ot_ics_rules.xml /var/ossec/etc/rules/
sudo chown wazuh:wazuh /var/ossec/etc/rules/ot_ics_rules.xml
sudo chmod 660 /var/ossec/etc/rules/ot_ics_rules.xml
```

---

## 🚀 Step 3: Configure Ingestion in `ossec.conf`

Open `/var/ossec/etc/ossec.conf` on the Wazuh Manager:

### Option A: Local Log Ingestion (Recommended)
Add this block inside the `<ossec_config>` section:

```xml
<localfile>
  <log_format>json</log_format>
  <location>/var/ossec/logs/ot_ics_alerts.log</location>
</localfile>
```

### Option B: Remote Syslog Ingestion (UDP Port 514)
If forwarding over network from the sensor host:

```xml
<remote>
  <connection>syslog</connection>
  <port>514</port>
  <protocol>udp</protocol>
  <allowed-ips>192.168.4.0/24</allowed-ips>
</remote>
```

---

## 🧪 Step 4: Test & Verify Rules with `wazuh-logtest`

Run the Wazuh rule testing utility to verify that sample OT alerts trigger the expected rules:

```bash
/var/ossec/bin/wazuh-logtest
```

Paste this sample alert into the prompt:

```json
{"timestamp":"2026-09-09T10:00:00Z","agent":"OT_SOC_SENSOR_01","rule_id":100201,"rule_level":12,"severity":"CRITICAL","alert_type":"Safety Interlock Violation - Motor Active During Obstacle Hazard","mitre_technique_id":"T0888","mitre_tactic":"Inhibit Response Function","ics_plant_id":"PLANT_ALPHA","details":{"motor_state":"RUNNING","ir_sensor":"TRIPPED_OBSTACLE","core_temperature":24.5}}
```

**Expected Output:**
```
**Phase 1: Completed pre-decoding.
**Phase 2: Completed decoding.
       decoder: 'ot-ics-sensor'
**Phase 3: Completed filtering (rules).
       id: '100201'
       level: '12'
       description: 'CRITICAL HAZARD: Physical Safety Interlock Breached - Actuator Active While Optical Safety Barrier Tripped (T0888)'
       groups: '['ot_ics', 'scada', 'power_plant']'
       mitre.id: '['T0888']'
```

---

## 🔄 Step 5: Restart Wazuh Manager

```bash
sudo systemctl restart wazuh-manager
```

Your Wazuh dashboard will now populate real-time alerts under **Security Events** and visualize them in the **MITRE ATT&CK Matrix** tab!
