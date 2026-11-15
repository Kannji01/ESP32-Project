# 🎵 ESP32 Smart Desk Companion: Spotify Controller & PC Hardware Monitor

A compact, multi-functional desk gadget powered by an **ESP32** and a **TFT display** with dual **KY-040 rotary encoders**. Designed to sit right under your monitor, this device acts as a dedicated hardware interface for controlling Spotify playback, monitoring real-time PC temperatures and resource usage, and displaying an internet-synchronized clock.

---

## 💡 Motivation (Why I Built This)

When gaming, coding, or working in full-screen applications, constantly using `Alt+Tab` or switching windows just to check what song is playing, adjust the Spotify volume, or keep an eye on CPU/GPU temperatures breaks focus and interrupts workflow. 

I built this project to have a **dedicated, always-on physical dashboard** on my desk. It allows me to effortlessly control my music and monitor my PC's vital stats at a glance, with tactile rotary controls, without ever leaving the task at hand.

---

## ✨ Key Features

### 1. 🎧 Spotify Player & Controller
* **Two-Way Volume Synchronization:** Adjust Spotify volume smoothly using Encoder 1, or change it on your PC/Phone and watch the display update automatically.
* **Tactile Playback Control:** Press Encoder 1 to Play/Pause instantly, with a visual `"Stopped"` status indicator overlaid on the progress bar.
* **Custom Vinyl Animation:** Features a smooth, rotating vinyl record animation that spins while music is playing and pauses when playback stops.
* **Smart Text Scrolling & Diacritics Support:** Long track titles automatically slide across the screen and reset with an ellipsis (`...`). Includes a custom renderer for Romanian diacritics (`ă, â, î, ș, ț`).
* **Gradient Progress Bar:** Real-time song progress bar with a smooth Red-to-Yellow color fade and elapsed/total time indicators.

### 2. 🖥️ Real-Time PC Hardware Monitor
* **Live Temperatures:** Displays real-time **CPU** and **GPU** temperatures in Celsius ($^\circ\text{C}$).
* **Resource Usage Bars:** Three dedicated gradient progress bars showing live **CPU %**, **GPU %**, and **RAM %** utilization.
* **Zero-Config Background Sync:** Powered by a lightweight Python companion script (`pc_monitor.py`) that streams telemetry over USB Serial automatically when the PC boots.

### 3. 🕒 NTP System Clock
* **Automatic Time Sync:** Fetches accurate local time and date via Wi-Fi using NTP (`pool.ntp.org`), complete with automatic Daylight Saving Time adjustment.

### 4. 🧭 Intuitive Menu System
* Use **Encoder 2** to open the mode selection menu at any time, scroll through the screens (*Spotify Player*, *Temp & Usage PC*, *Time & Date*), and click to switch views seamlessly.

---

## 🛠️ Hardware Requirements & Pinout

### Components
* **Microcontroller:** ESP32 Development Board
* **Display:** 1.8" SPI TFT Display (160x128 resolution, ST7735 driver)
* **Controls:** 2x KY-040 Rotary Encoder Modules

### Rotary Encoders Pinout

| Component | Pin Name | ESP32 GPIO | Function |
| :--- | :--- | :--- | :--- |
| **Encoder 1** | `CLK` | **GPIO 32** | Rotate: Adjust Spotify Volume ($\pm 5\%$) |
| **Encoder 1** | `DT` | **GPIO 33** | Direction sensing |
| **Encoder 1** | `SW` | **GPIO 25** | Press: Play / Pause Track |
| **Encoder 2** | `CLK` | **GPIO 26** | Rotate: Navigate Menu Up / Down |
| **Encoder 2** | `DT` | **GPIO 27** | Direction sensing |
| **Encoder 2** | `SW` | **GPIO 14** | Press: Open Menu / Confirm Selection |
| **Both Encoders** | `+` (VCC) | **3.3V** | Power supply |
| **Both Encoders** | `GND` | **GND** | Common ground |

*(Note: Configure your TFT display pins directly inside the `TFT_eSPI` library's `User_Setup.h` file according to your wiring).*

---

## 🚀 Installation & Setup

### Step 1: ESP32 Firmware (Arduino IDE)
1. Install the following libraries in Arduino IDE:
   * `TFT_eSPI` by Bodmer
   * `ArduinoJson` by Benoit Blanchon
2. Open `image.h` and replace the placeholders with your personal credentials:
   ```cpp
   const char* client_id     = "YOUR_SPOTIFY_CLIENT_ID";
   const char* client_secret = "YOUR_SPOTIFY_CLIENT_SECRET";
   const char* refresh_token = "YOUR_SPOTIFY_REFRESH_TOKEN";
   const char* ssid          = "YOUR_WIFI_SSID";
   const char* password      = "YOUR_WIFI_PASSWORD";
   ```
   *(Make sure your Spotify Refresh Token was generated with the `user-read-playback-state` and `user-modify-playback-state` scopes).*
3. Open `project.ino`, select your ESP32 board and COM port, and upload the sketch.

### Step 2: PC Hardware Monitor Script (Python)
1. Install Python 3 on your PC and install the required packages via Command Prompt:
   ```bash
   pip install pyserial psutil wmi
   ```
2. Open `pc_monitor.py` and set `SERIAL_PORT` to match your ESP32's COM port (e.g., `'COM3'`):
   ```python
   SERIAL_PORT = 'COM3'
   ```
3. **For Accurate CPU/GPU Temperatures:**
   * **NVIDIA GPUs** are detected automatically via `nvidia-smi`.
   * For **CPU and AMD/Intel GPUs**, run [LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor) or [OpenHardwareMonitor](https://openhardwaremonitor.org/) minimized in the background so the script can read hardware sensors via WMI.
4. **Run Automatically on Windows Startup (Optional):**
   * Rename `pc_monitor.py` to `pc_monitor.pyw` (this runs the script silently without a console window).
   * Press `Win + R`, type `shell:startup`, press Enter, and place a shortcut to `pc_monitor.pyw` inside the Startup folder.

---

## 📂 Repository Structure

* `project.ino` — Main ESP32 firmware (UI rendering, Spotify Web API client, interrupt-driven encoder logic, and serial parser).
* `image.h` — Configuration header for Wi-Fi credentials, Spotify OAuth keys, and the root SSL certificate.
* `pc_monitor.py` — Python companion script for streaming real-time PC temperatures and usage over USB Serial.

---

## 📜 License

This project is open-source and available under the MIT License. Feel free to fork, modify, and customize the UI colors to match your own desk setup!
