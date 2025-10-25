// ============================================================
// 🌐 WEBUI (Original) – stabilisierte Version
// ============================================================
String webHTML() {
  return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset='UTF-8'>
<title>ESP32 Slideshow Pro</title>
<style>
html, body {
  height: 100%;
  margin: 0;
  padding: 0;
  background: #0b0b0b;
  color: #8aff8a;
  font-family: 'Segoe UI', sans-serif;
  text-align: center;
  overflow: hidden;
}
h2 { margin-top: 10px; color: #7fffd4; }
#preview {
  margin-top: 5px;
  max-width: 60vw;
  max-height: 40vh;
  border: 3px solid #2a2;
  border-radius: 14px;
  box-shadow: 0 0 25px #0f08;
  background: #000;
  object-fit: contain;
  transition: opacity 0.4s ease-in-out;
}
.button-row {
  display: flex; justify-content: center;
  flex-wrap: wrap; gap: 8px; margin-top: 8px;
}
button {
  background: #222; color: #8aff8a;
  border: 1px solid #555; border-radius: 8px;
  padding: 8px 14px; cursor: pointer;
  transition: all 0.2s;
}
button:hover { background: #333; transform: scale(1.05); }
button.reboot { background: #400; color: #fff; }
input[type=range] {
  width: 45%; margin-top: 6px; accent-color: #0f0;
}
#weblog {
  background: #000; border-top: 2px solid #050;
  height: 18vh; overflow-y: auto; font-family: monospace;
  color: #6f6; padding: 5px; margin: 8px auto 40px auto;
  text-align: left; width: 90%; max-width: 900px;
  border-radius: 8px;
}
footer {
  background: #111; border-top: 1px solid #333; color: #8aff8a;
  padding: 6px; font-size: 13px; width: 100%;
  position: fixed; bottom: 0; left: 0;
  display: flex; justify-content: space-around; align-items: center;
}
#progress {
  display: none; position: fixed; top: 50%; left: 50%;
  transform: translate(-50%, -50%);
  background: #111; border: 2px solid #555;
  padding: 25px; border-radius: 10px; box-shadow: 0 0 10px #0f0;
}
progress { width: 300px; height: 20px; accent-color: #0f0; }
a { color: #5f5; text-decoration: none; }
a:hover { text-decoration: underline; }
.err { color: #f66; } .ok { color: #6f6; }
</style>
</head>
<body>
<h2>🖼️ ESP32 Slideshow Pro</h2>

<img id='preview' src='' alt='Slideshow Image'>
<p id='status'>Warte auf Status...</p>

<div class="button-row">
  <button onclick='send("prev")'>⏪ Zurück</button>
  <button onclick='send("start")'>▶️ Start</button>
  <button onclick='send("stop")'>⏸ Stop</button>
  <button onclick='send("next")'>⏩ Weiter</button>
  <button class='reboot' onclick='send("reboot")'>🔁 Reboot</button>
</div>

<label>⏱ Intervall: <span id='ival'>5</span>s</label><br>
<input type='range' min='1' max='20' value='5' id='slider'
        oninput='ival.innerText=this.value'
        onchange='setIntervalTime(this.value)'>

<h3 style='margin-top:10px'>
<a href='/edit'>⚙️ Config bearbeiten</a> |
<a href='#' onclick='showOTA()'>📤 OTA Update</a>
</h3>

<div id='progress'>
  <h3>Firmware-Upload</h3>
  <progress id='bar' value='0' max='100'></progress>
  <p id='pct'>0%</p>
</div>

<div id='weblog'><b>WebLog aktiv...</b></div>

<footer>
  <span id='wifi'>📡 WLAN: -</span>
  <span id='signal'>📶 Signal: -</span>
  <span id='ip'>🌐 IP: -</span>
</footer>

<script>
async function update(){
  try{
    let r=await fetch('/state.json');
    let j=await r.json();
    // Nur Bildquelle aktualisieren, wenn sie existiert (Host + Path)
    if (j.host && j.path) {
      let imgUrl='http://'+j.host+'/'+j.path+'/'+j.current+'.jpg';
      const preview=document.getElementById('preview');
      if(preview.src!=imgUrl){
        preview.style.opacity=0;
        setTimeout(()=>{preview.src=imgUrl;preview.style.opacity=1;},200);
      }
    } else {
      document.getElementById('preview').alt = 'Kein Host/Pfad konfiguriert';
    }
    document.getElementById('status').innerText=
      '📸 Bild '+j.current+' / '+j.total+' | Intervall: '+j.interval+'s';
    document.getElementById('slider').value=j.interval;
    document.getElementById('ival').innerText=j.interval;
    document.getElementById('wifi').innerText='📡 WLAN: '+j.ssid;
    document.getElementById('signal').innerText='📶 Signal: '+j.rssi+' dBm';
    document.getElementById('ip').innerText='🌐 IP: '+j.ip;
  }catch(e){}
  setTimeout(update,1500);
}
async function weblog(){
  try{
    let r=await fetch('/log.txt');
    let t=await r.text();
    const logDiv=document.getElementById('weblog');
    logDiv.innerHTML=t.replace(/\n/g,'<br>');
    logDiv.scrollTop=logDiv.scrollHeight;
  }catch(e){}
  setTimeout(weblog,1500);
}
async function send(cmd){await fetch('/control?cmd='+cmd);}
async function setIntervalTime(v){await fetch('/control?cmd=setinterval&val='+v);}
function showOTA(){
  const box=document.getElementById('progress');
  box.style.display='block';
  const input=document.createElement('input');
  input.type='file'; input.accept='.bin';
  input.onchange=()=>{const file=input.files[0]; if(!file)return; uploadOTA(file);};
  input.click();
}
async function uploadOTA(file){
  const bar=document.getElementById('bar');
  const pct=document.getElementById('pct');
  const xhr=new XMLHttpRequest();
  xhr.open('POST','/update',true);
  xhr.upload.onprogress=(e)=>{
    if(e.lengthComputable){
      const percent=Math.round((e.loaded/e.total)*100);
      bar.value=percent; pct.innerText=percent+'%';
    }
  };
  xhr.onload=()=>{
    pct.innerText='Neustart...';
    setTimeout(()=>{location.reload();},5000);
  };
  const formData=new FormData();
  formData.append('update',file,file.name);
  xhr.send(formData);
}
update(); weblog();
</script>
</body></html>
)rawliteral";
}

// ============================================================
// 🌐 WEB-SERVER & OTA-Setup (JETZT THREAD-SICHER)
// ============================================================
void setupWeb() {
  server.on("/", []() { server.send(200, "text/html", webHTML()); });

  server.on("/state.json", []() {
    String json = "{";
    
    // Kritischer Abschnitt: Geteilte Variablen lesen
    if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
      json += "\"current\":" + String(currentImage) + ",";
      json += "\"total\":" + String(totalImages) + ",";
      json += "\"interval\":" + String(slideInterval) + ",";
      if (HOST_IP.length() > 0) {
        json += "\"host\":\"" + HOST_IP + ":" + String(HOST_PORT) + "\",";
        json += "\"path\":\"" + HOST_PATH + "\",";
      }
      xSemaphoreGive(dataMutex);
    }
    // Kritischer Abschnitt Ende

    json += "\"ssid\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.SSID() : (WiFi.getMode() == WIFI_AP ? WiFi.softAPSSID() : "Getrennt")) + "\",";
    json += "\"ip\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI());
    json += "}";
    server.send(200, "application/json", json);
  });

  server.on("/log.txt", []() { 
    String logData;
    // Log-Puffer sicher kopieren
    if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
      logData = logBuffer;
      xSemaphoreGive(dataMutex);
    }
    server.send(200, "text/plain", logData); 
  });

server.on("/control", []() {
    String cmd = server.arg("cmd");
    String logMsg;

    // Kritischer Abschnitt: Geteilte Variablen SCHREIBEN
    if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
      if (cmd == "start") {
        slideshowActive = true;
        logMsg = "[Web] Befehl: Start";
      } 
      else if (cmd == "stop") {
        slideshowActive = false;
        logMsg = "[Web] Befehl: Stop";
      } 
      else if (cmd == "next") {
        currentImage++;
        if (currentImage > totalImages) currentImage = 1;
        forceImageChange = true; // NEU: Setze das Flag
        logMsg = "[Web] Befehl: Weiter (Bild " + String(currentImage) + ")";
      } 
      else if (cmd == "prev") {
        currentImage--;
        if (currentImage < 1) currentImage = totalImages;
        forceImageChange = true; // NEU: Setze das Flag
        logMsg = "[Web] Befehl: Zurück (Bild " + String(currentImage) + ")";
      } 
      else if (cmd == "setinterval") {
        slideInterval = server.arg("val").toInt();
        logMsg = "[CFG] Neuer Intervall: " + String(slideInterval) + "s";
      }
      xSemaphoreGive(dataMutex);
    }
    // Kritischer Abschnitt Ende

    // Loggen (logln ist jetzt sicher)
    if (logMsg.length() > 0) logln(logMsg);

    if (cmd == "setinterval") {
      if(configLoaded) saveConfig(); 
      logln("[CFG] Config gespeichert.");
    }
    
    if (cmd == "reboot") {
      logln("[SYS] Reboot durch Webinterface");
      server.send(200, "text/plain", "Rebooting...");
      delay(500);
      ESP.restart();
      return;
    }
    server.send(200, "text/plain", "OK");
  });

  server.on("/edit", []() {
    File f = FFat.open("/config.txt");
    String txt = f ? f.readString() : "Fehler: config.txt nicht gefunden!";
    f.close();
    String html = "<html><body style='background:#111;color:#8aff8a;font-family:sans-serif;'>"
                  "<h3>Config bearbeiten (ESP32-Slideshow)</h3>"
                  "<form method='post' action='/save'>"
                  "<textarea name='cfg' style='width:90%;height:60%;background:#222;color:#fff;border:1px solid #555;'>" + txt + "</textarea><br><br>"
                  "<button style='padding:10px 20px;font-size:1.1em;'>Speichern & Neu starten</button></form>"
                "<p>Nach dem Speichern startet das Geraet neu.</p>"
                "</body></html>";
    server.send(200, "text/html", html);
  });

  server.on("/save", []() {
    File f = FFat.open("/config.txt", "w");
    if (f) { 
      f.print(server.arg("cfg")); 
      f.close(); 
      logln("[Web] Config gespeichert. Lade neu...");
      loadConfig();
    } else {
      logln("[Web] FEHLER beim Speichern der Config!");
    }
    
    server.send(200, "text/html",
      String("<body style='background:#111;color:#8aff8a;font-family:sans-serif;font-size:1.5em;padding-top:50px;text-align:center;'>")
    + "Gespeichert!<br><br>Gerät startet in 5 Sekunden neu...<br>"
    + "Bitte verbinde dich danach mit deinem normalen WLAN."
    + "</body>");
      
    xTaskCreate([](void* param) {
        logln("[SYS] Neustart in 5s...");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        ESP.restart();
    }, "rebootTask", 2048, NULL, 5, NULL);
  });

  httpUpdater.setup(&server, "/update", "admin", "rootlu");
  server.begin();
  logln("[Web] HTTP-Server gestartet.");
}
