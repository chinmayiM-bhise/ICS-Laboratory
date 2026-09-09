"""
==============================================================================================
 PROJECT: ICS Laboratory - Converged IT/OT SOC Monitoring Engine
 MODULE: Hybrid Modbus/TCP & MQTT Passive OT Network Security Monitor (Level 2/3)
 AUTHOR: Chinmayi Bhise
 MAPPING: MITRE ATT&CK for ICS (T0888, T0836, T0855, T0859, T0815, T0814, T0802)
 TARGET SIEM: Wazuh Manager / XDR (Syslog UDP 514 & JSON Alert Pipeline)
==============================================================================================
"""

import json
import socket
import struct
import threading
import time
import os
import argparse
from datetime import datetime

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("[!] Error: paho-mqtt not installed. Run: pip install paho-mqtt")
    exit(1)

# Default Configuration
DEFAULT_MQTT_BROKER = "127.0.0.1"
DEFAULT_MQTT_PORT = 1883
DEFAULT_PLC_IP = "127.0.0.1"
DEFAULT_MODBUS_PORT = 502
DEFAULT_UDP_BACKDOOR_PORT = 8888
DEFAULT_WAZUH_SYSLOG_HOST = "127.0.0.1"
DEFAULT_WAZUH_SYSLOG_PORT = 514
DEFAULT_ALERT_LOG_PATH = "logs/ot_ics_alerts.log"

# Physical Safety Bounds (Operational Envelope)
MIN_SAFE_TEMP = 10.0
MAX_SAFE_TEMP = 45.0
MAX_COMMAND_FREQUENCY_WINDOW = 5.0
MAX_COMMAND_COUNT_IN_WINDOW = 3


class OTSocSensor:
    def __init__(self, broker="127.0.0.1", port=1883, plc_ip="127.0.0.1", modbus_port=502,
                 wazuh_host="127.0.0.1", wazuh_port=514, log_file="logs/ot_ics_alerts.log"):
        self.broker = broker
        self.port = port
        self.plc_ip = plc_ip
        self.modbus_port = modbus_port
        self.wazuh_host = wazuh_host
        self.wazuh_port = wazuh_port
        self.log_file = log_file

        self.last_telemetry = {}
        self.last_commands = []
        self.running = True

        os.makedirs(os.path.dirname(os.path.abspath(self.log_file)), exist_ok=True)
        self.syslog_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

        # MQTT Client
        self.mqtt_client = mqtt.Client(client_id="OT_SOC_HYBRID_SENSOR", protocol=mqtt.MQTTv311)
        self.mqtt_client.on_connect = self.on_connect
        self.mqtt_client.on_message = self.on_message

    def emit_alert(self, rule_id, rule_level, alert_name, mitre_id, mitre_tactic, details, severity="HIGH"):
        timestamp = datetime.utcnow().isoformat() + "Z"
        alert_event = {
            "timestamp": timestamp,
            "agent": "OT_SOC_SENSOR_01",
            "rule_id": rule_id,
            "rule_level": rule_level,
            "severity": severity,
            "alert_type": alert_name,
            "mitre_technique_id": mitre_id,
            "mitre_tactic": mitre_tactic,
            "ics_plant_id": "PLANT_ALPHA",
            "details": details
        }

        json_str = json.dumps(alert_event)

        colors = {
            "CRITICAL": "\033[91m",
            "HIGH": "\033[93m",
            "WARNING": "\033[96m",
            "RESET": "\033[0m"
        }
        color = colors.get(severity, "\033[92m")
        print(f"{color}[Wazuh Alert Lvl {rule_level}] [{mitre_id}] {alert_name}{colors['RESET']}")
        print(f"  └─ Details: {json.dumps(details)}")

        with open(self.log_file, "a", encoding="utf-8") as f:
            f.write(json_str + "\n")

        try:
            syslog_payload = f"<14>{json_str}".encode('utf-8')
            self.syslog_sock.sendto(syslog_payload, (self.wazuh_host, self.wazuh_port))
        except Exception:
            pass

    def on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            print(f"[*] [OT-NSM] Connected to MQTT Broker ({self.broker}:{self.port})")
            client.subscribe("plant/#")
        else:
            print(f"[!] MQTT connection failed with return code {rc}")

    def on_message(self, client, userdata, msg):
        topic = msg.topic
        payload_str = msg.payload.decode('utf-8', errors='ignore')

        # ----------------------------------------------------------------------
        # A. MQTT Telemetry Stream (Topic: plant/telemetry)
        # ----------------------------------------------------------------------
        if topic == "plant/telemetry":
            try:
                data = json.loads(payload_str)
                self.last_telemetry = data

                motor_on = data.get("motor", 0) == 1
                ir_obstacle = data.get("ir_obstacle", 0) == 1
                temp = float(data.get("temp", 0.0))
                thresh = float(data.get("threshold", 30.0))

                # RULE 100201: Safety Interlock Violation (T0888)
                if motor_on and ir_obstacle:
                    self.emit_alert(
                        rule_id=100201,
                        rule_level=12,
                        alert_name="Safety Interlock Violation - Motor Active During Obstacle Hazard",
                        mitre_id="T0888",
                        mitre_tactic="Inhibit Response Function",
                        severity="CRITICAL",
                        details={
                            "protocol": "MQTT",
                            "motor_state": "RUNNING",
                            "ir_sensor": "TRIPPED_OBSTACLE",
                            "core_temperature": temp,
                            "hazard_description": "Actuator energized despite active physical safety barrier."
                        }
                    )

                # RULE 100206: Thermal Trip Failure
                if temp > thresh and not motor_on:
                    self.emit_alert(
                        rule_id=100206,
                        rule_level=11,
                        alert_name="Thermal Trip Failure - Temperature Exceeds Threshold Without Cooling",
                        mitre_id="T0888",
                        mitre_tactic="Loss of Safety",
                        severity="HIGH",
                        details={"temperature": temp, "threshold": thresh, "motor_state": "STOPPED"}
                    )
            except json.JSONDecodeError:
                pass

        # ----------------------------------------------------------------------
        # B. MQTT Supervisory Commands (Topic: plant/commands)
        # ----------------------------------------------------------------------
        elif topic == "plant/commands":
            now = time.time()
            self.last_commands.append(now)
            self.last_commands = [t for t in self.last_commands if now - t <= MAX_COMMAND_FREQUENCY_WINDOW]

            # RULE 100204: Unauthorized Command Message (T0855)
            self.emit_alert(
                rule_id=100204,
                rule_level=10,
                alert_name="Supervisory Control Command Injected over Unauthenticated MQTT",
                mitre_id="T0855",
                mitre_tactic="Impair Process Control",
                severity="HIGH",
                details={"topic": topic, "payload": payload_str, "protocol": "MQTT"}
            )

            # RULE 100207: Actuator Flapping DoS (T0814)
            if len(self.last_commands) >= MAX_COMMAND_COUNT_IN_WINDOW:
                self.emit_alert(
                    rule_id=100207,
                    rule_level=12,
                    alert_name="Actuator Flapping DoS - Excessive High-Frequency Command Cycling",
                    mitre_id="T0814",
                    mitre_tactic="Denial of Service",
                    severity="CRITICAL",
                    details={"burst_count": len(self.last_commands), "protocol": "MQTT"}
                )

        # ----------------------------------------------------------------------
        # C. MQTT Setpoint Configuration (Topic: plant/config)
        # ----------------------------------------------------------------------
        elif topic == "plant/config":
            try:
                data = json.loads(payload_str)
                new_thresh = float(data.get("threshold", 30.0))
                if new_thresh > MAX_SAFE_TEMP or new_thresh < MIN_SAFE_TEMP:
                    self.emit_alert(
                        rule_id=100202,
                        rule_level=13,
                        alert_name="Critical Setpoint Parameter Tampered Beyond Physical Safety Limits",
                        mitre_id="T0836",
                        mitre_tactic="Modify Parameter",
                        severity="CRITICAL",
                        details={"commanded_threshold": new_thresh, "protocol": "MQTT"}
                    )
            except (json.JSONDecodeError, ValueError):
                pass

    # --------------------------------------------------------------------------
    # D. Modbus/TCP Operational Telemetry Poller & Anomaly Auditor
    # --------------------------------------------------------------------------
    def poll_modbus_registers(self, poll_interval=2.5):
        """
        Polls Modbus/TCP registers on the PLC to corroborate physical state across protocols.
        """
        while self.running:
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(2.0)
                sock.connect((self.plc_ip, self.modbus_port))

                # 1. Read Discrete Inputs (FC 02) to check IR Sensor 10001
                # TxID=1, Proto=0, Len=6, Unit=1, FC=2, Addr=0, Qty=2
                fc02_req = struct.pack(">HHHBBHH", 1, 0, 6, 1, 2, 0, 2)
                sock.sendall(fc02_req)
                resp = sock.recv(64)
                if len(resp) >= 10:
                    inputs = resp[9]
                    ir_detected = (inputs & (1 << 0)) != 0

                    # 2. Read Coils (FC 01) to check Motor Coil 00001
                    fc01_req = struct.pack(">HHHBBHH", 2, 0, 6, 1, 1, 0, 1)
                    sock.sendall(fc01_req)
                    coil_resp = sock.recv(64)
                    if len(coil_resp) >= 10:
                        coils = coil_resp[9]
                        motor_on = (coils & (1 << 0)) != 0

                        # RULE 100208: Modbus Safety Interlock Breach Corroboration (T0888)
                        if motor_on and ir_detected:
                            self.emit_alert(
                                rule_id=100201,
                                rule_level=12,
                                alert_name="Modbus/TCP Safety Interlock Violation - Motor Energized During Barrier Trip",
                                mitre_id="T0888",
                                mitre_tactic="Inhibit Response Function",
                                severity="CRITICAL",
                                details={
                                    "protocol": "Modbus/TCP (Port 502)",
                                    "coil_00001_motor": 1,
                                    "discrete_input_10001_ir": 1,
                                    "plc_target": f"{self.plc_ip}:502"
                                }
                            )

                    # 3. Read Holding Register 40001 (FC 03) to check Setpoint
                    fc03_req = struct.pack(">HHHBBHH", 3, 0, 6, 1, 3, 0, 1)
                    sock.sendall(fc03_req)
                    reg_resp = sock.recv(64)
                    if len(reg_resp) >= 11:
                        thresh_val = ((reg_resp[9] << 8) | reg_resp[10]) / 10.0
                        if thresh_val > MAX_SAFE_TEMP or thresh_val < MIN_SAFE_TEMP:
                            self.emit_alert(
                                rule_id=100202,
                                rule_level=13,
                                alert_name="Modbus/TCP Setpoint Register 40001 Tampered Outside Physical Limits",
                                mitre_id="T0836",
                                mitre_tactic="Modify Parameter",
                                severity="CRITICAL",
                                details={
                                    "protocol": "Modbus/TCP (Port 502)",
                                    "register_40001": thresh_val,
                                    "safe_max": MAX_SAFE_TEMP
                                }
                            )

                sock.close()
            except (socket.timeout, ConnectionRefusedError, OSError):
                pass

            time.sleep(poll_interval)

    # --------------------------------------------------------------------------
    # E. Out-of-Band UDP Backdoor Sniffer (Port 8888)
    # --------------------------------------------------------------------------
    def listen_udp_backdoor(self, port=8888):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            sock.bind(("0.0.0.0", port))
            while self.running:
                data, addr = sock.recvfrom(1024)
                raw_payload = data.decode('utf-8', errors='ignore').strip()
                self.emit_alert(
                    rule_id=100203,
                    rule_level=14,
                    alert_name="Direct-to-PLC Out-of-Band Maintenance Backdoor Triggered",
                    mitre_id="T0859",
                    mitre_tactic="Execution / Initial Access",
                    severity="CRITICAL",
                    details={
                        "source_ip": addr[0],
                        "source_port": addr[1],
                        "destination_port": port,
                        "raw_command": raw_payload
                    }
                )
        except Exception:
            pass

    def start(self):
        print("\n========================================================")
        print("   ICS-SENTINEL: DUAL MODBUS/TCP & MQTT SOC SENSOR       ")
        print("   PROTOCOLS: Modbus/TCP (Port 502) & MQTT (Port 1883)   ")
        print("   TARGET SIEM: Wazuh Manager (Level 3/4 SOC)            ")
        print("========================================================\n")

        # Start Modbus auditor thread
        modbus_thread = threading.Thread(target=self.poll_modbus_registers, daemon=True)
        modbus_thread.start()

        # Start UDP backdoor sniffer thread
        udp_thread = threading.Thread(target=self.listen_udp_backdoor, args=(DEFAULT_UDP_BACKDOOR_PORT,), daemon=True)
        udp_thread.start()

        try:
            self.mqtt_client.connect(self.broker, self.port, 60)
            self.mqtt_client.loop_forever()
        except KeyboardInterrupt:
            print("\n[*] Stopping OT SOC Sensor...")
            self.running = False
        except ConnectionRefusedError:
            print(f"[!] Cannot connect to MQTT broker at {self.broker}:{self.port}. Modbus & UDP sniffer remaining active...")
            while self.running:
                time.sleep(1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Hybrid Modbus/TCP & MQTT OT SOC Sensor")
    parser.add_argument("--broker", default=DEFAULT_MQTT_BROKER, help="MQTT Broker IP")
    parser.add_argument("--port", type=int, default=DEFAULT_MQTT_PORT, help="MQTT Port")
    parser.add_argument("--plc-ip", default=DEFAULT_PLC_IP, help="PLC IP for Modbus/TCP")
    parser.add_argument("--modbus-port", type=int, default=DEFAULT_MODBUS_PORT, help="Modbus Port")
    parser.add_argument("--wazuh-host", default=DEFAULT_WAZUH_SYSLOG_HOST, help="Wazuh Manager IP")
    parser.add_argument("--wazuh-port", type=int, default=DEFAULT_WAZUH_SYSLOG_PORT, help="Wazuh Syslog Port")
    parser.add_argument("--log-file", default=DEFAULT_ALERT_LOG_PATH, help="Path for JSON alert log")
    args = parser.parse_args()

    sensor = OTSocSensor(
        broker=args.broker,
        port=args.port,
        plc_ip=args.plc_ip,
        modbus_port=args.modbus_port,
        wazuh_host=args.wazuh_host,
        wazuh_port=args.wazuh_port,
        log_file=args.log_file
    )
    sensor.start()
