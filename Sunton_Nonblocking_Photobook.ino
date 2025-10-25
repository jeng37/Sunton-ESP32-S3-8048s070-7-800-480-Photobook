// ============================================================
// 🖼️ Sunton ESP32-S3 7" Slideshow Pro – STABILE VERSION (KORRIGIERT)
// ============================================================
#define CONFIG_ARDUINO_LOOP_STACK_SIZE 65536

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <FFat.h>
#include <Arduino_GFX_Library.h>
#include <JPEGDEC.h>
#include <ArduinoOTA.h>
#include <HTTPUpdateServer.h>
#include <ESPmDNS.h>
#include <esp_ota_ops.h>

// ============================================================
// ⚙️ Sicherheit & RAM
// ============================================================
#define CONFIG_ESP_COREDUMP_ENABLE 0
#define CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH 0
#define CONFIG_ESP_COREDUMP_ENABLE_TO_UART 0

// ============================================================
// 🧠 Eigenes Task-System mit großem Stack (64KB) auf Core0
// ============================================================
void mainLoopTask(void *param);
TaskHandle_t photoTaskHandle;

volatile bool forceImageChange = false; // NEU: Flag für Button-Klicks

// NEU: Mutex für geteilte Daten
SemaphoreHandle_t dataMutex;

#define GFX_BL 2

// ============================================================
// DISPLAY
// ============================================================
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
  41, 40, 39, 42,
  14, 21, 47, 48, 45,
  9, 46, 3, 8, 16, 1,
  15, 7, 6, 5, 4,
  0, 180, 30, 32,
  0, 12, 13, 20);

Arduino_RGB_Display *display = nullptr;
JPEGDEC jpeg;
WebServer server(80);
HTTPUpdateServer httpUpdater;

// ============================================================
// VARIABLEN
// ============================================================
String WIFI_SSID, WIFI_PASSWORD, HOST_IP, HOST_PATH, DEVICE_NAME;
int HOST_PORT = 8080, MAX_IMAGES = 50;
String LOCAL_IP, GATEWAY, SUBNET;
String logBuffer;
int currentImage = 1;
int totalImages = 0;
bool slideshowActive = true;
unsigned long lastSlide = 0;
int slideInterval = 10;
int ROTATION = 1;
bool drawingFrame = false;
bool configLoaded = false;


// ============================================================
// LOG (nicht rekursiv!)
// ============================================================
void logln(const String &msg) {
  // Warte auf den Mutex, um sicher auf den logBuffer zuzugreifen
  if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    Serial.println(msg);
    logBuffer += msg + "\n";
    if (logBuffer.length() > 8192)
      logBuffer = logBuffer.substring(logBuffer.length() - 8192);
    
    // Mutex wieder freigeben
    xSemaphoreGive(dataMutex);
  }
}

// ============================================================
// 🧠 RAM Buffer für direktes Bild-Rendering
// ============================================================
#define MAX_JPEG_SIZE 350000
uint8_t *imgBuffer = nullptr;
size_t imgSize = 0;

// ============================================================
// Hauptloop auf Core0 - KORRIGIERTE LOGIK
// ============================================================
void mainLoopTask(void *param) {
  logln("[Task] FotoLoop gestartet (Core " + String(xPortGetCoreID()) + ")");
  for (;;) {
    bool doFetch = false;
    unsigned long interval = 10000;
    int imageToFetch = 1;
    bool slideChanged = false; // Flag, ob Timer abgelaufen ist

    // Kritischer Abschnitt: Geteilte Variablen prüfen
    if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
      interval = slideInterval * 1000;
      
      // 1. Prüfen, ob Timer abgelaufen ist
      if (slideshowActive && millis() - lastSlide > interval) {
        currentImage++;
        if (currentImage > totalImages) currentImage = 1;
        lastSlide = millis();
        slideChanged = true;
      }

      // 2. Prüfen, ob ein Button-Klick eine Änderung erzwingt
      if (forceImageChange) {
        lastSlide = millis(); // Timer auch hier zurücksetzen
        forceImageChange = false;
        slideChanged = true;
      }

      // 3. Wenn eine Änderung ansteht, Bildnummer holen
      if (slideChanged) {
        imageToFetch = currentImage; // Bildnummer sicher holen
        doFetch = true;
      }
      
      xSemaphoreGive(dataMutex);
    }
    // Kritischer Abschnitt Ende

    if (doFetch) {
      if (WiFi.status() == WL_CONNECTED) {
        
        bool success = fetchImage(imageToFetch);
        
        // NEU: Wenn Bild fehlerhaft ist, SOFORT das nächste laden
        if (!success) {
           logln("[Task] Bild " + String(imageToFetch) + " konnte nicht angezeigt werden. Überspringe.");
           
           // Setze Timer, um sofort das NÄCHSTE Bild zu laden
           if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
              lastSlide = 0; // Signal für "sofort neuladen"
              xSemaphoreGive(dataMutex);
           }
        }
      }
    }
    vTaskDelay(20); // Gib dem System 20ms Zeit
  }
}


// ============================================================
// 💾 CONFIG SYSTEM (FFat)
// ============================================================
bool loadConfig() {
  if (!FFat.exists("/config.txt")) {
    logln("[Init] Erstelle Standard-config.txt ...");
    File f = FFat.open("/config.txt", "w");
    if (!f) return false;
    f.println("// ####### config start #######");
    f.println("String WIFI_SSID = \"Top-1\";");
    f.println("String WIFI_PASSWORD = \"12345679\";");
    f.println("String HOST_IP = \"192.168.8.174\";");
    f.println("int HOST_PORT = 8080;");
    f.println("String HOST_PATH = \"\";");
    f.println("int MAX_IMAGES = 50;");
    f.println("int SLIDE_INTERVAL = 10;");
    f.println("int ROTATION = 1;");
    f.println("String DEVICE_NAME = \"ESP32-Slideshow_1\";");
    // Standards für IP sind jetzt leer (DHCP)
    f.println("String LOCAL_IP = \"192.168.8.180\";");
    f.println("String GATEWAY = \"192.168.8.1\";");
    f.println("String SUBNET = \"255.255.255.0\";");
    f.println("// ####### config end #######");
    f.close();
  }

  File f = FFat.open("/config.txt", "r");
  if (!f) { logln("[X] config.txt konnte nicht geöffnet werden!"); return false; }

  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line.startsWith("//") || line.isEmpty()) continue;
    if (line.startsWith("String WIFI_SSID")) WIFI_SSID = line.substring(line.indexOf('"') + 1, line.lastIndexOf('"'));
    else if (line.startsWith("String WIFI_PASSWORD")) WIFI_PASSWORD = line.substring(line.indexOf('"') + 1, line.lastIndexOf('"'));
    else if (line.startsWith("String HOST_IP")) HOST_IP = line.substring(line.indexOf('"') + 1, line.lastIndexOf('"'));
    else if (line.startsWith("int HOST_PORT")) HOST_PORT = line.substring(line.indexOf('=') + 1, line.indexOf(';')).toInt();
    else if (line.startsWith("String HOST_PATH")) HOST_PATH = line.substring(line.indexOf('"') + 1, line.lastIndexOf('"'));
    else if (line.startsWith("int MAX_IMAGES")) MAX_IMAGES = line.substring(line.indexOf('=') + 1, line.indexOf(';')).toInt();
    else if (line.startsWith("String DEVICE_NAME")) DEVICE_NAME = line.substring(line.indexOf('"') + 1, line.lastIndexOf('"'));
    else if (line.startsWith("String LOCAL_IP")) LOCAL_IP = line.substring(line.indexOf('"') + 1, line.lastIndexOf('"'));
    else if (line.startsWith("String GATEWAY")) GATEWAY = line.substring(line.indexOf('"') + 1, line.lastIndexOf('"'));
    else if (line.startsWith("String SUBNET")) SUBNET = line.substring(line.indexOf('"') + 1, line.lastIndexOf('"'));
    else if (line.startsWith("int SLIDE_INTERVAL")) slideInterval = line.substring(line.indexOf('=') + 1, line.indexOf(';')).toInt();
    else if (line.startsWith("int ROTATION")) ROTATION = line.substring(line.indexOf('=') + 1, line.indexOf(';')).toInt();
  }
  f.close();

  if (slideInterval <= 0) slideInterval = 10;
  if (ROTATION < 0 || ROTATION > 3) ROTATION = 1;

  logln("[OK] config.txt gelesen.");
  logln("[CFG] Intervall=" + String(slideInterval) + "s, ROTATION=" + String(ROTATION));
  configLoaded = true;
  return true;
}

// ============================================================
// 💾 CONFIG SPEICHERN
// ============================================================
bool saveConfig() {
  // Nimm den Mutex, bevor du Variablen liest/schreibst
  if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdFALSE) return false;

  File f = FFat.open("/config.txt", "w");
  if (!f) {
    xSemaphoreGive(dataMutex); // WICHTIG: Mutex bei Fehler freigeben
    return false;
  }
  
  f.println("// ####### config start #######");
  f.println("String WIFI_SSID = \"" + WIFI_SSID + "\";");
  f.println("String WIFI_PASSWORD = \"" + WIFI_PASSWORD + "\";");
  f.println("String HOST_IP = \"" + HOST_IP + "\";");
  f.println("int HOST_PORT = " + String(HOST_PORT) + ";");
  f.println("String HOST_PATH = \"" + HOST_PATH + "\";");
  f.println("int MAX_IMAGES = " + String(MAX_IMAGES) + ";");
  f.println("int SLIDE_INTERVAL = " + String(slideInterval) + ";"); // Liest geteilte Variable
  f.println("int ROTATION = " + String(ROTATION) + ";"); // Liest geteilte Variable
  f.println("String DEVICE_NAME = \"" + DEVICE_NAME + "\";");
  f.println("String LOCAL_IP = \"" + LOCAL_IP + "\";");
  f.println("String GATEWAY = \"" + GATEWAY + "\";");
  f.println("String SUBNET = \"" + SUBNET + "\";");
  f.println("// ####### config end #######");
  f.close();
  
  xSemaphoreGive(dataMutex); // Mutex nach Erfolg freigeben

  logln("[OK] Config gespeichert."); // logln() holt sich den Mutex selbst
  return true;
}

// ============================================================
// CountImages
// ============================================================
int countImages() {
  // Nur zählen, wenn WLAN verbunden ist
  if (WiFi.status() != WL_CONNECTED) {
    logln("[COUNT] WLAN nicht verbunden, überspringe Zählung.");
    return 0;
  }
 
  int found = 0;
  for (int i = 1; i <= MAX_IMAGES; i++) {
    HTTPClient http;
    String url = "http://" + HOST_IP + ":" + String(HOST_PORT) + "/" + HOST_PATH + "/" + String(i) + ".jpg";
    http.begin(url);
    int code = http.GET();
    http.end();
    if (code == 200) found++;
  }
  logln("[COUNT] Gefundene Bilder: " + String(found));
  return found;
}
// ============================================================
// JPEG-Anzeige - JETZT MIT RÜCKGABEWERT (bool)
// ============================================================
int drawMCU(JPEGDRAW *pDraw) {
  display->draw16bitRGBBitmap(pDraw->x, pDraw->y,
                              (uint16_t *)pDraw->pPixels,
                              pDraw->iWidth, pDraw->iHeight);
  return 1;
}

bool showImage() { // <-- Rückgabetyp auf bool geändert
  if (!imgBuffer || imgSize < 500) {
    logln("[IMG] Kein gültiges Bild!");
    return false; // <-- Misserfolg
  }
  drawingFrame = true;
  jpeg.close();
  delay(5);

  if (jpeg.openRAM(imgBuffer, imgSize, drawMCU)) {
    int jpgWidth = jpeg.getWidth();
    int jpgHeight = jpeg.getHeight();
    display->fillScreen(BLACK);
    jpeg.decode((display->width() - jpgWidth) / 2, (display->height() - jpgHeight) / 2, 0);
    jpeg.close();
    display->flush();
    logln("[IMG] Anzeige OK.");
    drawingFrame = false;
    return true; // <-- Erfolg
  } else {
    logln("[X] JPEG konnte nicht geöffnet werden!");
    drawingFrame = false;
    return false; // <-- Misserfolg
  }
}

// ============================================================
// Bild aus HTTP laden (RAM only) - KORRIGIERT
// ============================================================
bool fetchImage(int num) {
  // KORREKTUR: Pfad-Logik
  String pathSuffix = "/"; // Startet mit dem Root-Slash
  if (HOST_PATH.length() > 0) {
    pathSuffix += HOST_PATH + "/"; // Fügt "baustelle/" hinzu, falls vorhanden
  }
  
  String path = "http://" + HOST_IP + ":" + String(HOST_PORT) + pathSuffix + String(num) + ".jpg";
  logln("[DL] Lade Bild " + String(num) + " aus " + path);

  HTTPClient http;
  http.begin(path);
  int code = http.GET();

  if (code != HTTP_CODE_OK) {
    logln("[DL] FEHLER: HTTP " + String(code));
    http.end();
    return false;
  }

  int len = http.getSize();
  if (len <= 0 || len > MAX_JPEG_SIZE) {
    logln("[X] Ungültige Größe: " + String(len));
    http.end();
    return false;
  }

  if (imgBuffer) {
    free(imgBuffer);
    imgBuffer = nullptr;
  }

imgBuffer = (uint8_t*)malloc(len);
  if (!imgBuffer) {
    logln("[X] Kein RAM verfügbar (" + String(len) + " Bytes)");
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  size_t total = 0;
  while (http.connected() && total < len) {
    size_t avail = stream->available();
    if (avail) total += stream->readBytes(imgBuffer + total, avail);
    else delay(1); // Dieser delay(1) ist OK, da fetchImage auf Core 0 läuft
  }

  http.end();
  imgSize = total;

  if (imgSize < 500) {
    logln("[WARN] Bild zu klein (" + String(imgSize) + ")");
    free(imgBuffer);
    imgBuffer = nullptr;
    return false;
  }

  logln("[OK] Bild geladen (" + String(imgSize) + " Bytes)");
  return showImage(); // <-- GEÄNDERT: Gibt Erfolg/Misserfolg von showImage() zurück
}

// ============================================================
// 🧭 Boot-Diagnoseanzeige auf Display - MIT TITELSCHUTZ
// ============================================================
void showBootStatus(const String &msg, bool error = false) {
  static int line = 0; // Zählt Zeilen nur im Statusbereich
  const int titleAreaHeight = 40; // Platz für Titel + Leerzeile (ca. 2 Zeilen bei TextSize 2)
  const int lineHeight = 20;      // Höhe einer Statuszeile (geschätzt für TextSize 2)

  if (!display) return;

  display->setTextColor(error ? RED : GREEN);
  display->setTextSize(2); // Sicherstellen, dass die Größe stimmt

  // Berechne Y-Position UNTERHALB des Titelbereichs
  int y = titleAreaHeight + (line * lineHeight);

  // Prüfen, ob die AKTUELLE Zeile schon überläuft
  // (Wir prüfen y selbst, nicht y + lineHeight, um sicher zu sein)
  if (y >= display->height() - lineHeight) { // Wenn die Zeile am unteren Rand oder darüber hinaus ist
    // Lösche NUR den Statusbereich unterhalb des Titels
    display->fillRect(0, titleAreaHeight, display->width(), display->height() - titleAreaHeight, BLACK);
    line = 0; // Zeilenzähler für Statusbereich zurücksetzen
    y = titleAreaHeight + (line * lineHeight); // Y neu berechnen für die erste Zeile
    
    // Optional: Hier könnte man einen Trenner oder "..." ausgeben, ist aber nicht nötig
  }
  
  display->setCursor(10, y); // Cursor für die aktuelle Statusmeldung setzen
  display->println(msg);
  line++; // Zeilenzähler für die nächste Meldung erhöhen
}

// ============================================================
// 🧭 Vollständiger Bootcheck (FFat + config.txt + WiFi)
//    MODIFIZIERT mit AP-Fallback & Static IP
// ============================================================
bool runBootDiagnostics() {
  display->fillScreen(BLACK);
  display->setTextSize(2);
  display->setCursor(10, 10);
  display->setTextColor(CYAN);
  display->println("ESP32-S3 Slideshow Bootcheck:");
 
  showBootStatus("[1/3] FFat + config.txt ok", false);

  // WLAN Check
  showBootStatus("[2/3] WLAN verbinden (" + WIFI_SSID + ")...", false);
  
  // Statische IP setzen, WENN sie konfiguriert ist
  if (LOCAL_IP.length() > 7 && GATEWAY.length() > 7 && SUBNET.length() > 7) {
    IPAddress ip, gw, sn;
    ip.fromString(LOCAL_IP);
    gw.fromString(GATEWAY);
    sn.fromString(SUBNET);
    if (!WiFi.config(ip, gw, sn)) {
      logln("[WiFi] Statische IP-Konfiguration fehlgeschlagen");
      showBootStatus("  [X] Statische IP-Konfig. fehlgeschlagen!", true);
    } else {
      logln("[WiFi] Statische IP konfiguriert: " + LOCAL_IP);
      showBootStatus("  [OK] Statische IP gesetzt: " + LOCAL_IP, false);
    }
  } else {
    logln("[WiFi] Nutze DHCP.");
    showBootStatus("  [i] Nutze DHCP.", false);
  }

  WiFi.mode(WIFI_STA); // Explizit in den Station-Modus wechseln
  WiFi.begin(WIFI_SSID.c_str(), WIFI_PASSWORD.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) { // 10s Timeout
    delay(200);
    display->print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    showBootStatus("  [OK] Verbunden: " + WiFi.localIP().toString(), false);
    showBootStatus("------------------------------", false);
    showBootStatus("System OK - Starte in 5s ...", false);
    display->setTextColor(YELLOW);
    for (int i = 5; i >= 1; i--) { // 5s statt 10s
      display->setCursor(300, 420);
      display->fillRect(280, 400, 160, 40, BLACK);
      display->println(String(i) + "s");
      delay(1000);
    }
    return true;
  } else {
    // ===============================================
    // KORREKTUR: HALT ENTFERNT -> AP-MODUS STARTEN
    // ===============================================
    WiFi.disconnect(true); // Vorherige Versuche stoppen
    WiFi.mode(WIFI_AP);
    showBootStatus("  [X] WLAN nicht verbunden!", true);
    showBootStatus("------------------------------", true);
    showBootStatus("Starte Konfigurations-AP...", true);
        
    // Passwort muss mind. 8 Zeichen lang sein
    WiFi.softAP("ESP32-Slideshow-Setup", "12345678"); 
    delay(200); // Dem AP Zeit geben

    IPAddress apIP = WiFi.softAPIP();
    logln("[AP] AP gestartet. IP: " + apIP.toString());
    showBootStatus("AP: 'ESP32-Slideshow-Setup'", false);
    showBootStatus("Pass: '12345678'", false);
    showBootStatus("IP: " + apIP.toString() + " (o. 192.168.4.1)", false);
    showBootStatus("Bitte verbinden und IP im Browser", false);
    showBootStatus("oeffnen, um WLAN zu konfigurieren.", false);
    showBootStatus("Geraet startet nach Speichern neu.", false);

    // Nicht mehr anhalten, sondern weiterlaufen lassen,
    // damit der Webserver (auf der AP-IP) gestartet wird.
    // while (true) delay(1000);  // <-- WICHTIG: ENTFERNT!
    delay(10000); // 10s Zeit geben, um die Meldung zu lesen
    return false; // Zeigt an, dass die Verbindung fehlgeschlagen ist
  }
}


// ============================================================
// CONFIG + WEBUI (original)
// ============================================================
#include "WebUI_and_Config_Block.h"  // <-- Hier kommt dein kompletter HTML + Config-Teil rein


// ============================================================
// 🟢 Display Initialisierung stabil (keine Reboots mehr)
//    KORRIGIERTE SETUP-REIHENFOLGE
// ============================================================
void setup() {
  Serial.begin(115200);
  // NEU: Mutex erstellen
  dataMutex = xSemaphoreCreateMutex();
  // ============================================================
  // 🧠 FFat & Config einmalig prüfen / erzeugen
  // ============================================================
  logln("[BOOT] Prüfe FFat und config.txt ...");

  // FFat mounten oder formatieren, falls uninitialisiert
  if (!FFat.begin(false)) {
    logln("[WARN] FFat nicht gemountet, formatiere neu ...");
    if (!FFat.begin(true)) {
      logln("[X] FFat-Formatierung fehlgeschlagen!");
    } else {
      logln("[OK] FFat formatiert und gemountet.");
    }
  } else {
    logln("[OK] FFat gemountet.");
  }

  // Falls config.txt nicht existiert → nur dann erzeugen
  if (!FFat.exists("/config.txt")) {
    logln("[CREATE] config.txt fehlt – Standarddatei wird erstellt...");
    File f = FFat.open("/config.txt", "w");
    if (f) {
      f.println("// ####### config start #######");
      f.println("String WIFI_SSID = \"Top-1\";"); // <-- Standard-SSID
      f.println("String WIFI_PASSWORD = \"12345679\";"); // <-- Standard-Passwort
      f.println("String HOST_IP = \"192.168.8.174\";");
      f.println("int HOST_PORT = 8080;");
      f.println("String HOST_PATH = \"\";");
      f.println("int MAX_IMAGES = 50;");
      f.println("int SLIDE_INTERVAL = 10;");
      f.println("int ROTATION = 0;");
      f.println("String DEVICE_NAME = \"ESP32-Slideshow_1\";");
      f.println("String LOCAL_IP = \"192.168.8.180\";"); // Standardmäßig DHCP
      f.println("String GATEWAY = \"192.168.8.1\";");
      f.println("String SUBNET = \"255.255.255.0\";");
      f.println("// ####### config end #######");
      f.close();
      logln("[OK] Default config.txt erstellt.");
    } else {
      logln("[X] Fehler beim Erstellen von config.txt!");
    }
  } else {
    logln("[OK] config.txt bereits vorhanden.");
  }

  delay(300);
  logln("\n[BOOT] ESP32-S3 Slideshow startet...");

  // 🔹 Backlight + Display initialisieren (einmal!)
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, LOW);    // Backlight AUS beim Init
  delay(100);

  // Starte mit Rotation 0 (Landscape) für die Boot-Meldungen
  display = new Arduino_RGB_Display(800, 480, rgbpanel, 0, true); 
  display->begin();
  delay(200);                  // Panel init abwarten
  display->fillScreen(BLACK);
  delay(50);

  // PSRAM stabilisieren
  yield();
  //esp_task_wdt_reset();
  delay(200);

  // Backlight aktivieren, aber mit sanftem Übergang
  for (int i = 0; i < 3; i++) { delay(100); digitalWrite(GFX_BL, HIGH); }

  display->setTextSize(2);
  display->setTextColor(GREEN);
  display->setCursor(20, 200);

  // 🧠 Kritischer Punkt: hier langsam schreiben, mit Flush und Pause
  display->println("Display init OK starte Bootcheck...");
  display->flush();
  delay(500);
  yield();
  delay(500);

// ============================================================
  // KORREKTUR 1: Config laden VOR dem Bootcheck
  // ============================================================
  loadConfig();

  // ============================================================
  // Führe Bootcheck durch (nutzt jetzt geladene Config)
  // WIR MACHEN DAS HIER, WÄHREND NOCH LANDSCAPE (ROT 0) AKTIV IST.
  // ============================================================
  runBootDiagnostics(); // Diese Funktion startet ggf. den AP-Modus

  // ============================================================
  // ERST JETZT die Rotation aus der Config für die Slideshow setzen
  // ============================================================
  display->setRotation(ROTATION);
  logln("[Display] Rotation für Slideshow auf " + String(ROTATION) + " gesetzt.");

  // 🔹 Bildschirm für die Slideshow löschen
  display->fillScreen(BLACK);
  
  // loadConfig(); // <-- VON HIER ENTFERNT (war schon oben)

  // Log-Status nach dem Bootcheck
  if (WiFi.status() == WL_CONNECTED) {
      logln("[System] Bootcheck erfolgreich. Starte Dienste.");
// ... (Rest des Codes wie gehabt)
  } else {
      logln("[System] WLAN nicht verbunden. Starte Dienste im AP-Modus.");
  }


  // ============================================================
  // KORREKTUR 2: Überflüssigen WiFi-Block entfernt
  // ============================================================
  
  // 🔹 OTA + Webserver starten (Startet immer, auch im AP-Modus)
  ArduinoOTA.setHostname(DEVICE_NAME.c_str());
  ArduinoOTA.begin();
  setupWeb();

  // 🔹 Slideshow starten (nur wenn WLAN verbunden ist)
  if (WiFi.status() == WL_CONNECTED) {
    totalImages = countImages();
    if (totalImages > 0) fetchImage(1);
  } else {
    logln("[Slideshow] Gestoppt, da kein WLAN vorhanden ist.");
    display->setTextColor(RED);
    display->setTextSize(3);
    display->setCursor(100, 200);
    display->println("KEIN WLAN - BITTE KONFIGURIEREN");
    display->setTextSize(2);
    display->setCursor(100, 240);
    display->println("Verbinde mit AP 'ESP32-Slideshow-Setup'");
    display->setCursor(100, 270);
    display->println("Gehe zu http://192.168.4.1");
  }

  xTaskCreatePinnedToCore(mainLoopTask, "photoTask", 65536, NULL, 1, &photoTaskHandle, 0);
  logln("[System] photoTask auf Core 0 gestartet.");
}

void loop() {
  // Dieser Task läuft auf Core 1
  ArduinoOTA.handle();
  server.handleClient();
  
  // vTaskDelay(2) war zu aggressiv und verursachte Flackern.
  // 20ms (50x pro Sekunde) ist perfekt für eine WebUI.
  vTaskDelay(20); 
}
