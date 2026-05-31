# ICS Laboratory: IoT Security & SCADA Vulnerability Demo

## Project Overview

This repository ([ICS-Laboratory](https://github.com/chinmayiM-bhise/ICS-Laboratory)) hosts a deliberately vulnerable Mini Power Plant SCADA (Supervisory Control and Data Acquisition) system designed to demonstrate critical security flaws common in Industrial Internet of Things (IIoT) and Operational Technology (OT) environments. 

Built using ESP8266 microcontrollers, the system simulates a real-world scenario with an HMI (Human-Machine Interface) master node and a PLC (Programmable Logic Controller) slave node managing physical sensors and actuators (simulated motors/relays).

## Architecture

*   **HMI Node (Master):** Hosts a Wi-Fi Access Point (`Mini_OT_SCADA`) and serves a web-based dashboard for monitoring and control.
*   **PLC Node (Slave):** Connects to the HMI network, reads sensor data (Temperature, Humidity, IR Obstacle), controls the relay/motor, and syncs data with the HMI.

## Highlighted Vulnerabilities

This project practically demonstrates several high-impact security vulnerabilities:

1.  **Insecure Client-Side Authentication Bypass:** Flawed login mechanisms relying solely on front-end validation.
2.  **Hardcoded Credentials:** Sensitive Wi-Fi and admin credentials exposed in the source code.
3.  **Unauthenticated API Endpoints:** Missing authorization checks allowing direct command execution.
4.  **Cleartext Communication (Sniffing):** Sensitive telemetry and commands transmitted via unencrypted HTTP.
5.  **Broken Safety Interlocks:** "Production over Safety" design flaws where manual overrides ignore physical safety sensors.
6.  **CSRF (Cross-Site Request Forgery):** State-changing actions performed via HTTP GET requests.
7.  **Insufficient Input Validation:** Lack of bounds checking on critical safety thresholds.
8.  **Information Disclosure:** Unprotected endpoints leaking system logs and internal states.
9.  **Unauthenticated UDP Backdoor:** A hidden listener on the PLC allowing direct manipulation, bypassing the HMI entirely.

## Documentation & Guides

*   [Hardware Wiring Guide](HARDWARE_WIRING_GUIDE.md): Instructions for assembling the physical components.
*   [Operational Setup Guide](OPERATIONAL_SETUP_GUIDE.md): Steps to power on the system and access the dashboard.
*   [Vulnerability Demonstration Guide](VULNERABILITY_DEMO_GUIDE.md): Step-by-step instructions on how to exploit the vulnerabilities listed above.
*   [Vulnerability Report](VULNERABILITY_REPORT.md): A detailed report summarizing the security flaws.

## Disclaimer

**This project is for educational and ethical testing purposes only.** The vulnerabilities demonstrated here are dangerous and should never be implemented in production systems. Always practice responsible disclosure and prioritize security in real-world OT/IIoT deployments.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
