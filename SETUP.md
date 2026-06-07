# Hardware Setup & Installation Guide

Complete step-by-step guide to building and installing the Fish Tank Automation System.

## Parts List

| Component | Qty | Notes |
|-----------|-----|-------|
| ESP32 Microcontroller | 1 | Any ESP32 dev board (e.g., LOLIN32, DevKitC) |
| DHT11 Temperature & Humidity Sensor | 1 | Digital sensor with 4-pin header |
| DS18B20 Waterproof Temp Sensor | 1 | Comes in waterproof probe or module form |
| HC-SR04 Ultrasonic Distance Sensor | 1 | Ping sensor for water level detection |
| 5V Relay Module (2-channel) | 1 | For controlling pump and light |
| Resistors | 2x 4.7kΩ | Pull-up resistors for DHT11 and DS18B20 (optional but recommended) |
| Jumper Wires | ~30 | Male-to-male, male-to-female |
| Breadboard or Perfboard | 1 | For prototyping or permanent build |
| USB Cable (Micro-USB) | 1 | For ESP32 programming and power |

### Optional Components

- Capacitor (100µF) across relay module power for stability
- Heat shrink tubing for wire protection
- 3D-printed enclosure for ESP32 + sensor housing


## Step-by-Step Assembly

### 1. Prepare the ESP32

- Ensure you have the latest CH340 USB drivers installed (for programming)
- Connect ESP32 to computer via USB cable
- Verify it shows up in Device Manager / System Information

### 2. Wire the DHT11 Sensor

DHT11 has 4 pins: VCC, Data, NC (no connect), GND

```
DHT11 Pin      ESP32 Pin
─────────────────────────
VCC      ──>  3.3V
Data     ──>  GPIO 5
GND      ──>  GND
(NC not connected)
```

**Optional**: Add a 4.7kΩ resistor from Data pin to 3.3V for better signal integrity.

### 3. Wire the DS18B20 Sensor

DS18B20 has 3 pins (left to right when facing flat side): GND, Data, VCC

```
DS18B20 Pin    ESP32 Pin
──────────────────────────
GND      ──>  GND
Data     ──>  GPIO 4
VCC      ──>  3.3V
```

**Required**: Add a 4.7kΩ pull-up resistor from Data pin (GPIO 4) to 3.3V.

### 4. Wire the HC-SR04 Ultrasonic Sensor

HC-SR04 has 4 pins: VCC, TRIG, ECHO, GND

```
HC-SR04 Pin    ESP32 Pin
──────────────────────────
VCC      ──>  5V (important: needs 5V, not 3.3V!)
TRIG     ──>  GPIO 15
ECHO     ──>  GPIO 16
GND      ──>  GND
```

### 5. Wire the Relay Module

Most 2-channel relay modules have: VCC, GND, IN1, IN2, and relay terminals.

```
Relay Pin      ESP32 Pin      Purpose
─────────────────────────────────────
VCC      ──>  5V
GND      ──>  GND
IN1      ──>  GPIO 6         (Pump Control)
IN2      ──>  GPIO 7         (Light Control)

Relay NO (normally open):
  Relay 1  ──> connects pump
  Relay 2  ──> connects light
```

### 6. Verify Connections

Before uploading code:

- ✓ All sensors have GND connection
- ✓ DHT11 and DS18B20 on 3.3V rail
- ✓ HC-SR04 on 5V rail
- ✓ Relay module on 5V rail
- ✓ All GPIO connections match the code pin definitions
- ✓ No exposed wires touching (short circuit risk)

## Software Installation

### 1. Install Arduino IDE 2

Download from: https://www.arduino.cc/en/software

Or use Web Editor: https://create.arduino.cc/editor

### 2. Install ESP32 Board Support

In Arduino IDE:
1. Go to **File → Preferences**
2. In "Additional Board Manager URLs", add: `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. Click **OK**
4. Go to **Tools → Board Manager**
5. Search "esp32" and install "ESP32 by Espressif Systems"

### 3. Install Required Libraries

In Arduino IDE, go to **Sketch → Include Library → Manage Libraries**:

Search for and install:
- `DHT sensor library` (by Adafruit)
- `OneWire` (by Jim Studt)
- `DallasTemperature` (by Miles Burton)

### 4. Configure Board Settings

In **Tools** menu, set:

```
Board:       ESP32 Dev Module (or your specific board)
Upload Speed: 115200
Flash Mode:   DIO
Flash Size:   4MB
Partition Scheme: Default 4MB with SPIFFS
```

### 5. Update WiFi Credentials

In `fish_tank_cleaned.ino`, find lines 11-13:

```cpp
const char* ssid = "Adeen";              // ← Change to YOUR WiFi SSID
const char* wifi_password = "03312226851"; // ← Change to YOUR WiFi password
const char* LOGIN_PASSWORD = "volt";     // ← Change this to a secure password!
```

**Replace with your actual WiFi details.**

### 6. Upload Firmware

1. Connect ESP32 via USB
2. Select the correct COM port in **Tools → Port**
3. Click **Upload** (or Sketch → Upload)
4. Wait for "Leaving... Hard resetting via RTS pin" message

### 7. Verify via Serial Monitor

1. Open **Tools → Serial Monitor**
2. Set baud rate to **115200**
3. Reset the ESP32 (press RST button)
4. You should see:

```
--- Fish Tank System Initializing ---
Connecting to WiFi: Adeen
........
[+] WiFi Connected!
IP Address: 192.168.X.XXX
mDNS responder started. Access at http://fish.local
HTTP Server started.
```

If WiFi fails, check your SSID and password are correct.

## First Run

1. **Find the IP Address**: From the Serial Monitor output (e.g., `192.168.1.100`)
2. **Open in Browser**: Type `http://fish.local` or the IP address
3. **Login**: Enter password `volt` (or whatever you set it to)
4. **Test Sensors**: Watch the readings update every 2 seconds
5. **Test Relays**: Click the Pump and Light toggles to verify they work

## Troubleshooting

### WiFi Won't Connect

- Double-check SSID and password (case-sensitive)
- Ensure 2.4GHz network (ESP32 doesn't support 5GHz)
- Verify no special characters in password that might break the string

### Sensor Readings Show "ERROR"

**DHT11:**
- Check VCC and GND are connected
- Verify GPIO 5 is wired correctly
- DHT11 takes ~2 seconds to warm up; readings should appear after a few seconds

**DS18B20:**
- Check the 4.7kΩ pull-up resistor is installed
- Verify GPIO 4 connection
- Multiple DS18B20s can be on one wire (already pulled up once)

**HC-SR04:**
- Verify VCC is 5V (not 3.3V!)
- Check TRIG (GPIO 15) and ECHO (GPIO 16) wiring
- Ensure nothing is blocking the sensor face
- Readings >400cm or 0cm = sensor error

### Serial Monitor Shows Gibberish

- Baud rate is wrong (should be 115200)
- Wrong COM port selected
- USB driver issue; reinstall CH340 drivers

### Can't Access Web Dashboard

- Check WiFi connection (should print IP in Serial Monitor)
- Firewall blocking port 80 (local network only)
- Try IP address instead of `fish.local` if mDNS isn't working
- Make sure you're on the same WiFi network as the ESP32

### Relay Doesn't Activate

- Check relay module is powered (VCC and GND connected)
- Test relay manually by connecting IN1/IN2 to GND (should click)
- Verify GPIO 6 and 7 are not being used by WiFi/Serial
- Relay might be inverted (HIGH = OFF, LOW = ON); swap the logic if needed

## Next Steps

Once everything works:

1. Mount sensors permanently (waterproof the DS18B20 with aquarium sealant)
2. Connect actual pump and light to relay terminals
3. Test automation by setting a schedule
4. Monitor Serial logs for any warnings

See [README.md](./README.md) for usage and configuration details.
