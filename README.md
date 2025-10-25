# ESP32-S3 Sunton 7" WiFi Slideshow 🖼️

A stable, dual-core slideshow application for the Sunton ESP32-S3 7" (800x480) RGB display. It fetches JPEG images over WiFi from a configurable HTTP server and provides a web interface for control and configuration.

## Features

* Displays JPEG images in a slideshow format on the Sunton ESP32-S3 7" display.
* Fetches images sequentially (`1.jpg`, `2.jpg`, ...) via HTTP from a user-defined server (IP, Port, Path).
* Configuration stored persistently on FFat filesystem (`/config.txt`).
* WiFi Connectivity (STA Mode): Connects to your existing WiFi network.
* **Access Point (AP) Fallback:** Automatically starts a configuration AP ("ESP32-Slideshow-Setup") if the configured WiFi connection fails, allowing easy setup via a web browser.
* **Web Interface** accessible via the device's IP address for:
    * Slideshow control (Start, Stop, Next image, Previous image).
    * Adjusting the image display interval.
    * Viewing live system logs.
    * Editing the `config.txt` file directly.
    * Performing Over-the-Air (OTA) firmware updates (Password protected: user `admin`, password `rootlu`).
* **Stable Dual-Core Operation:**
    * Image fetching and display runs on **Core 0**.
    * Web server, OTA updates, and WiFi management run on **Core 1**, preventing the UI from becoming unresponsive during image downloads/display.
* Thread-safe access to shared data (log buffer, configuration variables) using Mutexes.
* Diagnostic boot screen displaying system status (FFat check, WiFi connection attempt).
* Configurable display rotation.
* Automatic skipping of images that fail to load or decode (e.g., corrupted or progressive JPEGs).

## Hardware Requirements

* **Sunton ESP32-S3 7" 800x480 RGB Display** (e.g., Model ESP32-8048S070)

## Software Dependencies

* Arduino IDE or PlatformIO
* ESP32 Arduino Core (Version 3.0.0 or later recommended)
* **Libraries:**
    * `Arduino_GFX_Library` (Install via Arduino Library Manager)
    * `JPEGDEC` (Install via Arduino Library Manager)
    * (Included with ESP32 Core: `WiFi`, `WebServer`, `HTTPClient`, `FFat`, `ArduinoOTA`, `HTTPUpdateServer`, `ESPmDNS`)

## Configuration (`config.txt`)

The device looks for a `config.txt` file in the FFat filesystem root. If not found, it creates a default one. You can edit this file via the Web UI (`/edit`).

**Example `config.txt`:**

```cpp
// ####### config start #######
String WIFI_SSID = "Your_WiFi_Name";
String WIFI_PASSWORD = "Your_WiFi_Password";
String HOST_IP = "192.168.1.100"; // IP of your image server
int HOST_PORT = 8080;             // Port of your image server
String HOST_PATH = "images";      // Subfolder on server (leave "" for root)
int MAX_IMAGES = 50;              // Max number of images to check for
int SLIDE_INTERVAL = 15;          // Seconds between slides
int ROTATION = 0;                 // Display rotation (0, 1, 2, 3 - 0&3=Landscape, 1&2=Portrait)
String DEVICE_NAME = "ESP32-Slideshow"; // Hostname for OTA
String LOCAL_IP = "192.168.1.150"; // Optional: Static IP for the display
String GATEWAY = "192.168.1.1";    // Optional: Static Gateway
String SUBNET = "255.255.255.0";   // Optional: Static Subnet Mask
// ####### config end #######
````

  * **Image Server:** You need an HTTP server (like Python's `http.server`, Apache, Nginx, etc.) serving JPEG files named `1.jpg`, `2.jpg`, etc., in the specified `HOST_PATH`.
  * **Static IP:** Leave `LOCAL_IP`, `GATEWAY`, `SUBNET` empty (`""`) to use DHCP.

## Setup & Installation (Arduino IDE)

1.  **Install Libraries:** Use the Arduino Library Manager to install `Arduino_GFX_Library` and `JPEGDEC`.
2.  **Board Settings:**
      * Select Board: "ESP32S3 Dev Module".
      * PSRAM: "OPI PSRAM" (Enabled).
      * Flash Size: "16MB".
      * **Partition Scheme:** "Custom" (Ensure `partitions.csv` is in the sketch folder).
      * **Arduino Runs On: "Core 1"** (Crucial for stability\!).
      * Events Run On: "Core 1".
3.  **Files:** Place `WebUI_and_Config_Block.h` and `partitions.csv` in the same directory as your main `.ino` sketch file.
4.  **Compile & Upload.**

## First Boot & WiFi Setup (AP Mode)

1.  On the first boot, or if the configured WiFi details are incorrect, the device will fail the WiFi check on the boot screen.
2.  It will then start an Access Point named **`ESP32-Slideshow-Setup`**.
3.  Connect your phone or computer to this WiFi network using the password **`12345678`**.
4.  Open a web browser and navigate to **`http://192.168.4.1`**.
5.  You should see the Web UI. Click on **"⚙️ Config bearbeiten"** (Edit Config).
6.  Enter your correct WiFi `WIFI_SSID` and `WIFI_PASSWORD`.
7.  Configure your image server details (`HOST_IP`, `HOST_PORT`, `HOST_PATH`).
8.  Click **"Speichern & Neu starten"** (Save & Restart).
9.  The device will save the configuration and reboot. It should now connect to your WiFi network.

## Usage

Once connected to your WiFi, find the device's IP address (shown on the boot screen or via your router's client list). Access this IP address in your web browser to use the control interface.

## Troubleshooting

  * **Stuck on an image / Display Freezes:** This is often caused by JPEG images saved in **Progressive** format. The `JPEGDEC` library requires **Baseline (Standard)** JPEGs. Re-save your images ensuring the "Progressive" option is disabled.
  * **Web UI Unresponsive:** Ensure the "Arduino Runs On" setting in the IDE is set to "Core 1".
  * **Cannot Connect to WiFi:** Double-check SSID and Password via the AP mode configuration. Ensure your WiFi is 2.4GHz.
  * **Flashing/Flickering:** Usually caused by both tasks running on the same core. Verify the "Arduino Runs On: Core 1" setting.
