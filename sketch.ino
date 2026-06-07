#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>
#include <math.h>
#include <ESPmDNS.h>
// WiFi credentials
const char* ssid = "YOUR_SSID";
const char* wifi_password = "YOUR_PASSWORD";

// ================= LOGIN =================
const char* LOGIN_PASSWORD = "volt";

// Pin definitions
#define DHT_PIN 5
#define TRIG_PIN 15
#define ECHO_PIN 16
#define RELAY1 6
#define RELAY2 7
#define DS18B20_PIN 4

#ifndef RGB_BUILTIN
#define RGB_BUILTIN 48
#endif
#define RGB_BRIGHTNESS 64
#define SCALE(v) ((v * RGB_BRIGHTNESS) / 255)

// CUSTOM ORANGE
#define ORANGE_R 240
#define ORANGE_G 112
#define ORANGE_B 14

#define DHTTYPE DHT11

#define TEMP_MIN 20.0
#define TEMP_MAX 25.0
#define WATER_TEMP_MIN 21.0
#define WATER_TEMP_MAX 28.0
#define HUMIDITY_MAX 80.0

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 5 * 3600;
const int daylightOffset_sec = 0;

DHT dht(DHT_PIN, DHTTYPE);
OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);
WebServer server(80);
Preferences preferences;

// GLOBALS
float humidity=0,temperature=0,waterTemp=-127,waterLevel=0;
int minWaterLevel=10,scheduleOnHour=8,scheduleOffHour=19;
bool pumpState=true,lightState=true,waterWarning=false,tempWarning=false,
     humidityWarning=false,waterTempWarning=false,sensorError=false,
     autoMode=true,scheduledOnTriggered=false,scheduledOffTriggered=false;

int rainbowHue=0;
unsigned long lastRainbowUpdate=0,lastSensorRead=0,lastTimeSync=0;

// ================= AUTH =================
bool isAuthenticated() {
  // Primary check: Cookie header
  if (server.hasHeader("Cookie")) {
    String cookie = server.header("Cookie");
    if (cookie.indexOf("ESPSESSION=1") >= 0) return true;
  }

  // Fallback: allow explicit query param used after JS redirect
  if (server.hasArg("esp") && server.arg("esp") == "1") return true;

  return false;
}


void redirectLogin(){
 server.sendHeader("Location","/login");
 server.send(302,"text/plain","");
}

// ================= RGB =================
void setRGBColor(int r,int g,int b){ rgbLedWrite(RGB_BUILTIN,r,g,b); }

void updateRGBStatus(){
 if(sensorError||tempWarning||humidityWarning||waterTempWarning||waterWarning){
   setRGBColor(SCALE(ORANGE_R),SCALE(ORANGE_G),SCALE(ORANGE_B));
   return;
 }
 if(millis()-lastRainbowUpdate>20){
   rainbowHue+=2;if(rainbowHue>=360)rainbowHue=0;
   float h=rainbowHue/60.0;
   float x=RGB_BRIGHTNESS*(1-abs(fmod(h,2)-1));
   int r,g,b;
   if(h<1){r=RGB_BRIGHTNESS;g=x;b=0;}
   else if(h<2){r=x;g=RGB_BRIGHTNESS;b=0;}
   else if(h<3){r=0;g=RGB_BRIGHTNESS;b=x;}
   else if(h<4){r=0;g=x;b=RGB_BRIGHTNESS;}
   else if(h<5){r=x;g=0;b=RGB_BRIGHTNESS;}
   else{r=RGB_BRIGHTNESS;g=0;b=x;}
   setRGBColor(r,g,b);
   lastRainbowUpdate=millis();
 }
}

// ================= LOGIN UI =================
void handleLoginPage() {
  // If already authenticated, go to dashboard
  if (isAuthenticated()) {
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
    return;
  }

  String h = "<html><body style='font-family:Arial;text-align:center;margin-top:60px;'>";
  h += "<h2>Fish Tank Login</h2><form action='/doLogin'>";
  h += "<input type='password' name='p' placeholder='Password'><br><br>";
  h += "<button type='submit'>Login</button></form></body></html>";
  server.send(200, "text/html", h);
}

void handleDoLogin() {
  // Check provided password
  if (server.hasArg("p") && server.arg("p") == LOGIN_PASSWORD) {
    // Instruct server to set cookie (helps non-JS clients)
    server.sendHeader("Set-Cookie", "ESPSESSION=1; Max-Age=86400; Path=/");

    // Also send an HTML page that sets the cookie via JS and then navigates to the dashboard.
    // This guarantees the browser has the cookie before it requests "/".
    String html = "<!doctype html><html><head><meta charset='utf-8'><title>Logging in</title></head><body>";
    html += "<p>Logging in — please wait...</p>";
    html += "<script>";
    // set cookie client-side as well (redundant but robust)
    html += "document.cookie = 'ESPSESSION=1; path=/; max-age=86400';";
    // navigate to dashboard and add esp=1 as an extra fallback param
    html += "window.location = '/?esp=1';";
    html += "</script></body></html>";

    server.send(200, "text/html", html);
  } else {
    // Wrong password: show friendly message
    String html = "<html><body style='text-align:center;margin-top:50px;'>";
    html += "<h3>Wrong password!</h3>";
    html += "<a href='/login'>Try Again</a>";
    html += "</body></html>";
    server.send(200, "text/html", html);
  }
}


void handleLogout() {
  // Expire the cookie
  server.sendHeader("Set-Cookie", "ESPSESSION=0; Max-Age=0; Path=/");
  redirectLogin(); // redirect to login page
}


// ================= SETUP =================
// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(1000); 
  Serial.println("\n--- Fish System Initializing ---");

  preferences.begin("fishsystem", false);
  minWaterLevel = preferences.getInt("minLevel", 10);
  scheduleOnHour = preferences.getInt("schedOn", 8);
  scheduleOffHour = preferences.getInt("schedOff", 19);

  pinMode(RELAY1, OUTPUT); pinMode(RELAY2, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT); pinMode(ECHO_PIN, INPUT);
  
  dht.begin();
  ds18b20.begin();
  
  digitalWrite(RELAY1, HIGH);
  digitalWrite(RELAY2, HIGH);

  // --- WIFI CONNECTION ---
  Serial.printf("Connecting to WiFi: %s\n", ssid);
  WiFi.begin(ssid, wifi_password);

  unsigned long startAttemptTime = millis();
  const unsigned long wifiTimeout = 20000; 

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < wifiTimeout) {
    delay(1000);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[!] WiFi Connection Failed. Proceeding in Offline Mode.");
  } else {
    Serial.println("\n[+] WiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // --- NEW: mDNS SETUP ---
    // This allows you to use http://fish.local instead of the IP address
    if (!MDNS.begin("fish")) { 
      Serial.println("Error setting up MDNS responder!");
    } else {
      Serial.println("mDNS responder started. Access at http://fish.local");
    }
  }

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // ROUTES
  server.on("/login", handleLoginPage);
  server.on("/doLogin", handleDoLogin);
  server.on("/logout", handleLogout);
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/pump", handlePump);
  server.on("/light", handleLight);
  server.on("/setLevel", handleSetLevel);
  server.on("/setSchedule", handleSetSchedule);
  server.on("/toggleAuto", handleToggleAuto);

  // --- NEW: COOKIE HEADER FIX ---
  // This tells the server to actually "listen" for the authentication cookies
  const char * headerkeys[] = {"Cookie"};
  size_t headerkeyssize = sizeof(headerkeys) / sizeof(char *);
  server.collectHeaders(headerkeys, headerkeyssize);

  server.begin();
  Serial.println("HTTP Server started.");
}

// ================= LOOP =================
void loop(){
 server.handleClient();
 updateRGBStatus();

 if(millis()-lastSensorRead>2000){
  sensorError=false;
  readTemp();readWaterTemp();checkWaterLevel();
  checkEnvironmentalRanges();checkAutoSchedule();
  lastSensorRead=millis();
 }
 if(millis()-lastTimeSync>3600000){
  configTime(gmtOffset_sec,daylightOffset_sec,ntpServer);
  lastTimeSync=millis();
 }
}

// ====== KEEP ALL YOUR SENSOR + LOGIC FUNCTIONS EXACTLY SAME ======
void readTemp() {
  humidity = dht.readHumidity();
  temperature = dht.readTemperature();
  
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("ERROR: Failed to read from DHT11 sensor!");
    sensorError = true;
    return;
  }
  
  Serial.print("DHT11 - Temp: ");
  Serial.print(temperature);
  Serial.print("°C, Humidity: ");
  Serial.print(humidity);
  Serial.println("%");
}

void readWaterTemp() {
  ds18b20.requestTemperatures();
  waterTemp = ds18b20.getTempCByIndex(0);
  
  if (waterTemp != -127) {
    Serial.print("DS18B20 - Water Temp: ");
    Serial.print(waterTemp);
    Serial.println("°C");
  } else {
    Serial.println("DS18B20 - Not connected (expected)");
  }
}

void printLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.print("Current time: ");
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

void checkAutoSchedule() {
  if (!autoMode) return;
  
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;
  
  int hour = timeinfo.tm_hour;
  int minute = timeinfo.tm_min;
  
  // Reset trigger flags when not at scheduled time
  if (hour != scheduleOnHour) {
    scheduledOnTriggered = false;
  }
  if (hour != scheduleOffHour) {
    scheduledOffTriggered = false;
  }
  
  // Turn ON at scheduled hour (only trigger once)
  if (hour == scheduleOnHour && minute == 0 && !scheduledOnTriggered) {
    pumpState = true;
    lightState = true;
    digitalWrite(RELAY1, HIGH);
    digitalWrite(RELAY2, HIGH);
    scheduledOnTriggered = true;
    Serial.print("Auto: Turned pump and light ON at ");
    Serial.print(scheduleOnHour);
    Serial.println(":00");
  }
  
  // Turn OFF at scheduled hour (only trigger once)
  if (hour == scheduleOffHour && minute == 0 && !scheduledOffTriggered) {
    pumpState = false;
    lightState = false;
    digitalWrite(RELAY1, LOW);
    digitalWrite(RELAY2, LOW);
    scheduledOffTriggered = true;
    Serial.print("Auto: Turned pump and light OFF at ");
    Serial.print(scheduleOffHour);
    Serial.println(":00");
  }
}

void checkWaterLevel() {
  // First measurement
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  float reading1 = (duration * 0.0343) / 2;
  
  if (reading1 == 0 || reading1 > 400) {
    waterLevel = 0;
    waterWarning = false;
    Serial.println("ERROR: HC-SR04 ultrasonic sensor reading failed!");
    sensorError = true;
    return;
  }
  
  // Wait 100ms for sensor to settle
  delay(100);
  
  // Second measurement (double-check)
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  duration = pulseIn(ECHO_PIN, HIGH, 30000);
  float reading2 = (duration * 0.0343) / 2;
  
  if (reading2 == 0 || reading2 > 400) {
    waterLevel = reading1; // Use first reading if second fails
    Serial.println("HC-SR04 - Second reading failed, using first reading");
  } else {
    // Average the two readings for accuracy
    waterLevel = (reading1 + reading2) / 2;
    Serial.print("HC-SR04 - Averaged readings: ");
    Serial.print(reading1);
    Serial.print(" cm & ");
    Serial.print(reading2);
    Serial.print(" cm = ");
  }
  
  Serial.print("Water Level: ");
  Serial.print(waterLevel);
  Serial.println(" cm");
  
  // CRITICAL: Higher distance = Lower water level
  // So if distance is GREATER than minWaterLevel, water is too LOW
  if (waterLevel > minWaterLevel) {
    waterWarning = true;
    Serial.print("WARNING: Water level too low! Distance: ");
    Serial.print(waterLevel);
    Serial.print(" cm (should be under ");
    Serial.print(minWaterLevel);
    Serial.println(" cm)");
  } else {
    waterWarning = false;
  }
}

void checkEnvironmentalRanges() {
  
  // Check DHT11 temperature range
  if (!isnan(temperature) && temperature > 0) {
    if (temperature < TEMP_MIN || temperature > TEMP_MAX) {
      if (!tempWarning) {
        Serial.print("WARNING: Air temperature out of range! ");
        Serial.print(temperature);
        Serial.print("°C (desired: ");
        Serial.print(TEMP_MIN);
        Serial.print("-");
        Serial.print(TEMP_MAX);
        Serial.println("°C)");
      }
      tempWarning = true;
    } else {
      if (tempWarning) {
        Serial.println("Air temperature back in range");
      }
      tempWarning = false;
    }
  }
  
  // Check humidity range
  if (!isnan(humidity) && humidity > 0) {
    if (humidity > HUMIDITY_MAX) {
      if (!humidityWarning) {
        Serial.print("WARNING: Humidity too high! ");
        Serial.print(humidity);
        Serial.print("% (should be under ");
        Serial.print(HUMIDITY_MAX);
        Serial.println("%)");
      }
      humidityWarning = true;
    } else {
      if (humidityWarning) {
        Serial.println("Humidity back in range");
      }
      humidityWarning = false;
    }
  }
  
  // Check DS18B20 water temperature range (only if sensor is connected)
  if (waterTemp != -127) {
    if (waterTemp < WATER_TEMP_MIN || waterTemp > WATER_TEMP_MAX) {
      if (!waterTempWarning) {
        Serial.print("WARNING: Water temperature out of range! ");
        Serial.print(waterTemp);
        Serial.print("°C (desired: ");
        Serial.print(WATER_TEMP_MIN);
        Serial.print("-");
        Serial.print(WATER_TEMP_MAX);
        Serial.println("°C)");
      }
      waterTempWarning = true;
    } else {
      if (waterTempWarning) {
        Serial.println("Water temperature back in range");
      }
      waterTempWarning = false;
    }
  } else {
    waterTempWarning = false;  // Don't warn if sensor not connected
  }
}

// (readTemp, readWaterTemp, checkWaterLevel, checkEnvironmentalRanges,
//  checkAutoSchedule, printLocalTime etc. — unchanged)

// ================= ROOT =================
void handleRoot(){
 if(!isAuthenticated()) return redirectLogin();
 String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Fish Tank Monitor</title>";
  html += "<style>";
  html += "body{font-family:Arial;margin:0;padding:10px;background:#f5f5f5;}";
  html += ".header{background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);color:white;padding:20px;border-radius:10px;margin-bottom:20px;text-align:center;}";
  html += ".header h1{margin:0;}";
  html += ".card{background:white;padding:20px;border-radius:10px;margin:10px 0;box-shadow:0 2px 5px rgba(0,0,0,0.1);}";
  html += ".value{font-size:36px;font-weight:bold;color:#667eea;margin:10px 0;}";
  html += ".label{color:#666;font-size:14px;text-transform:uppercase;}";
  html += ".warning{background:#fff3cd;border-left:4px solid #ff9800;padding:15px;margin:10px 0;border-radius:5px;color:#856404;}";
  html += ".notification{position:fixed;top:20px;right:20px;background:#ff5722;color:white;padding:15px 20px;border-radius:8px;box-shadow:0 4px 12px rgba(0,0,0,0.3);z-index:1000;display:none;animation:slideIn 0.3s;}";
  html += "@keyframes slideIn{from{transform:translateX(400px);opacity:0;}to{transform:translateX(0);opacity:1;}}";
  html += ".grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;}";
  html += ".switch{position:relative;display:inline-block;width:60px;height:34px;vertical-align:middle;}";
  html += ".switch input{opacity:0;width:0;height:0;}";
  html += ".slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background-color:#ccc;transition:.4s;border-radius:34px;}";
  html += ".slider:before{position:absolute;content:'';height:26px;width:26px;left:4px;bottom:4px;background-color:white;transition:.4s;border-radius:50%;}";
  html += "input:checked+.slider{background-color:#4CAF50;}";
  html += "input:checked+.slider:before{transform:translateX(26px);}";
  html += ".control-row{display:flex;justify-content:space-between;align-items:center;margin:15px 0;}";
  html += ".control-label{font-size:18px;font-weight:bold;}";
  html += ".level-control{display:flex;gap:10px;align-items:center;margin-top:15px;}";
  html += ".level-control input{flex:1;padding:8px;font-size:16px;border:2px solid #ddd;border-radius:5px;}";
  html += ".level-control button{padding:8px 20px;background:#667eea;color:white;border:none;border-radius:5px;cursor:pointer;}";
  html += ".level-control button:hover{background:#764ba2;}";
  html += ".auto-badge{display:inline-block;padding:5px 10px;background:#4CAF50;color:white;border-radius:15px;font-size:12px;margin-left:10px;cursor:pointer;}";
  html += ".auto-badge.off{background:#f44336;}";
  html += "@media(max-width:600px){.grid{grid-template-columns:1fr;}.notification{right:10px;left:10px;}}";
  html += "</style>";
  html += "<script>";
  html += "let lastWaterWarning=false;";
  html += "let lastTempWarning=false;";
  html += "let lastHumidityWarning=false;";
  html += "let lastWaterTempWarning=false;";
  html += "function showNotification(msg){";
  html += "let n=document.getElementById('notification');";
  html += "n.innerText=msg;n.style.display='block';";
  html += "setTimeout(()=>{n.style.display='none';},5000);";
  html += "}";
  html += "function updateData(){fetch('/data').then(r=>r.json()).then(d=>{";
  html += "document.getElementById('temp').innerText=d.temp;";
  html += "document.getElementById('hum').innerText=d.humidity;";
  html += "document.getElementById('level').innerText=d.waterLevel;";
  html += "if(d.waterTemp==-127){document.getElementById('waterTemp').innerHTML='<span style=\"color:#ff9800;\">Sensor Error</span>';}else{document.getElementById('waterTemp').innerHTML=d.waterTemp+'°C';}";
  html += "document.getElementById('pumpSwitch').checked=d.pump;";
  html += "document.getElementById('lightSwitch').checked=d.light;";
  html += "let autoBadge=document.getElementById('autoBadge');";
  html += "if(d.autoMode){autoBadge.innerText='AUTO ON';autoBadge.className='auto-badge';}else{autoBadge.innerText='AUTO OFF';autoBadge.className='auto-badge off';}";
  
  // Handle water level warning
  html += "if(d.waterWarning){document.getElementById('warning').style.display='block';if(!lastWaterWarning){showNotification('⚠️ Water level too low!');}}else{document.getElementById('warning').style.display='none';}";
  html += "lastWaterWarning=d.waterWarning;";
  
  // Handle temperature warning
  html += "if(d.tempWarning && !lastTempWarning){showNotification('⚠️ Air temperature out of range!');}";
  html += "lastTempWarning=d.tempWarning;";
  
  // Handle humidity warning
  html += "if(d.humidityWarning && !lastHumidityWarning){showNotification('⚠️ Humidity too high!');}";
  html += "lastHumidityWarning=d.humidityWarning;";
  
  // Handle water temp warning
  html += "if(d.waterTempWarning && !lastWaterTempWarning){showNotification('⚠️ Water temperature out of range!');}";
  html += "lastWaterTempWarning=d.waterTempWarning;";
  
  // Handle sensor errors
  html += "if(d.sensorError){showNotification('❌ Sensor reading error!');}";
  
  html += "});}";
  
  // Control functions - always available
  html += "function togglePump(){fetch('/pump').then(()=>updateData());}";
  html += "function toggleLight(){fetch('/light').then(()=>updateData());}";
  html += "function toggleAuto(){fetch('/toggleAuto').then(()=>updateData());}";
  html += "function setLevel(){let val=document.getElementById('minLevel').value;fetch('/setLevel?value='+val).then(()=>{showNotification('Min water level set to '+val+' cm');updateData();});}";
  html += "function setSchedule(){let on=document.getElementById('schedOn').value;let off=document.getElementById('schedOff').value;fetch('/setSchedule?on='+on+'&off='+off).then(()=>{showNotification('Schedule updated: ON at '+on+':00, OFF at '+off+':00');});}";
  
  html += "setInterval(updateData,2000);updateData();";
  html += "</script>";
  html += "</head><body>";
  
  html += "<div id='notification' class='notification'></div>";
  
  html += "<div class='header'><h1>🐟 Fish Tank Monitor</h1></div>";
  
  html += "<div id='warning' class='warning' style='display:none;'>";
  html += "⚠️ <strong>Warning:</strong> Water level too low! Top-up required!";
  html += "</div>";
  
  html += "<div class='grid'>";
  html += "<div class='card'><div class='label'>Air Temperature</div><div class='value'><span id='temp'>--</span>°C</div></div>";
  html += "<div class='card'><div class='label'>Humidity</div><div class='value'><span id='hum'>--</span>%</div></div>";
  html += "</div>";
  
  html += "<div class='card'><div class='label'>Water Temperature</div><div class='value'><span id='waterTemp'>--</span></div></div>";
  
  html += "<div class='card'><div class='label'>Water Level (Distance from sensor)</div><div class='value'><span id='level'>--</span> cm</div>";
  html += "<div class='level-control'><input type='number' id='minLevel' value='" + String(minWaterLevel) + "' min='0' max='100'>";
  html += "<button onclick='setLevel()'>Set Min Level</button></div>";
  html += "<div style='margin-top:10px;font-size:12px;color:#666;'>💡 Tip: Lower distance = higher water level</div>";
  html += "</div>";
  
  html += "<div class='card'>";
  html += "<div class='control-row'><span class='control-label'>💧 Pump</span>";
  html += "<label class='switch'><input type='checkbox' id='pumpSwitch' onchange='togglePump()'><span class='slider'></span></label></div>";
  html += "<div class='control-row'><span class='control-label'>💡 Light</span>";
  html += "<label class='switch'><input type='checkbox' id='lightSwitch' onchange='toggleLight()'><span class='slider'></span></label></div>";
  html += "</div>";
  
  html += "<div class='card'>";
  html += "<div style='display:flex;justify-content:space-between;align-items:center;margin-bottom:15px;'>";
  html += "<span style='font-size:18px;font-weight:bold;'>⏰ Auto Schedule</span>";
  html += "<span id='autoBadge' class='auto-badge' onclick='toggleAuto()'>AUTO ON</span>";
  html += "</div>";
  html += "<div class='level-control'>";
  html += "<span style='min-width:80px;'>Turn ON at:</span>";
  html += "<input type='number' id='schedOn' value='" + String(scheduleOnHour) + "' min='0' max='23' style='width:80px;'>";
  html += "<span>:00</span>";
  html += "</div>";
  html += "<div class='level-control'>";
  html += "<span style='min-width:80px;'>Turn OFF at:</span>";
  html += "<input type='number' id='schedOff' value='" + String(scheduleOffHour) + "' min='0' max='23' style='width:80px;'>";
  html += "<span>:00</span>";
  html += "</div>";
  html += "<div class='level-control'>";
  html += "<button onclick='setSchedule()' style='width:100%;'>Save Schedule</button>";
  html += "</div>";
  html += "<div style='margin-top:10px;padding:10px;background:#e3f2fd;border-radius:5px;font-size:14px;text-align:center;'>";
  html += "Times are in Pakistan Standard Time (PKT)</div>";
  html += "</div>";
  
  html += "</body></html>";
 server.send(200,"text/html",html);
}

// ================= DATA =================
void handleData(){
 //if(!isAuthenticated()) return redirectLogin();
 String json="{";
 json+="\"temp\":"+String(temperature,1)+",";
 json+="\"humidity\":"+String(humidity,1)+",";
 json+="\"waterTemp\":"+String(waterTemp,1)+",";
 json+="\"waterLevel\":"+String(waterLevel,1)+",";
 json+="\"pump\":"+String(pumpState?"true":"false")+",";
 json+="\"light\":"+String(lightState?"true":"false")+",";
 json+="\"autoMode\":"+String(autoMode?"true":"false")+",";
 json+="\"waterWarning\":"+String(waterWarning?"true":"false")+",";
 json+="\"tempWarning\":"+String(tempWarning?"true":"false")+",";
 json+="\"humidityWarning\":"+String(humidityWarning?"true":"false")+",";
 json+="\"waterTempWarning\":"+String(waterTempWarning?"true":"false")+",";
 json+="\"sensorError\":"+String(sensorError?"true":"false");
 json+="}";
 server.send(200,"application/json",json);
}

// ================= CONTROLS =================
void handlePump(){ if(!isAuthenticated())return redirectLogin();
 pumpState=!pumpState;digitalWrite(RELAY1,pumpState?HIGH:LOW);
 server.send(200,"text/plain","OK"); }

void handleLight(){ if(!isAuthenticated())return redirectLogin();
 lightState=!lightState;digitalWrite(RELAY2,lightState?HIGH:LOW);
 server.send(200,"text/plain","OK"); }

void handleSetLevel(){ if(!isAuthenticated())return redirectLogin();
 if(server.hasArg("value")){
  minWaterLevel=server.arg("value").toInt();
  preferences.putInt("minLevel",minWaterLevel);
 }
 server.send(200,"text/plain","OK"); }

void handleSetSchedule(){ if(!isAuthenticated())return redirectLogin();
 if(server.hasArg("on")&&server.hasArg("off")){
  scheduleOnHour=server.arg("on").toInt();
  scheduleOffHour=server.arg("off").toInt();
  preferences.putInt("schedOn",scheduleOnHour);
  preferences.putInt("schedOff",scheduleOffHour);
 }
 server.send(200,"text/plain","OK"); }

void handleToggleAuto(){ if(!isAuthenticated())return redirectLogin();
 autoMode=!autoMode;server.send(200,"text/plain","OK"); }
