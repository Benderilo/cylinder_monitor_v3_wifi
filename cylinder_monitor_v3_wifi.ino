#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>

// ==================== WIFI SETTINGS ====================
const char* AP_SSID = "TERMO-CONTROL";
const char* AP_PASS = "12345678";        // минимум 8 символов
WebServer server(80);
IPAddress apIP;

// ==================== DISPLAY SETTINGS ====================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ==================== BUTTON PINS ====================
#define BTN_LEFT   32   // K1
#define BTN_RIGHT  33   // K2
#define BTN_SELECT 34   // K3 (внешняя подтяжка)
#define BTN_BACK   35   // K4 (внешняя подтяжка)

// ==================== MAX6675 PINS ====================
#define CLK_PIN 18
#define DO_PIN  19

const int csPins[6] = {5, 17, 16, 4, 2, 15};

// ==================== ALARM SETTINGS ====================
const float MAX_SPREAD = 30.0;   // допустимый разброс между цилиндрами, °C

// ==================== STATE VARIABLES ====================
float temperatures[6] = {0, 0, 0, 0, 0, 0};
int   selectedCylinder = 0;
bool  detailView = false;
bool  infoView = false;          // экран с данными Wi-Fi (кнопка BACK)

bool  alarmActive = false;       // разброс превышен
bool  deviated[6] = {false};     // какие цилиндры выбиваются
float currentSpread = 0;
float avgTemp = 0;

// Таймеры
unsigned long lastTempRead = 0;
const unsigned long TEMP_INTERVAL = 300;
unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_INTERVAL = 100;
unsigned long lastDebug = 0;
const unsigned long DEBUG_INTERVAL = 1000;

// Мигание при тревоге
bool blinkState = false;
unsigned long lastBlink = 0;
const unsigned long BLINK_INTERVAL = 400;

// Debounce
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;
bool lastLeftState = HIGH, lastRightState = HIGH;
bool lastSelectState = HIGH, lastBackState = HIGH;

// ==================== DRAWING PARAMETERS ====================
const int barWidth = 16;
const int barSpacing = 3;
const int startX = 4;
const int maxBarHeight = 48;
const int barBaseY = 62;
const int maxTemp = 600;

// ==================== WEB PAGE (dark theme) ====================
const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>TERMO CONTROL</title>
<style>
  :root{
    --bg:#0e1116; --card:#161b23; --line:#232a35;
    --txt:#e6ebf2; --dim:#8a94a3;
    --ok:#ffb347; --ok2:#ff7a1a;
    --bad:#ff3b3b; --err:#5a6472;
  }
  *{box-sizing:border-box; margin:0; padding:0;}
  body{
    background:var(--bg); color:var(--txt);
    font-family:system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;
    min-height:100vh; padding:20px;
  }
  .wrap{max-width:760px; margin:0 auto;}
  h1{
    font-size:20px; letter-spacing:3px; font-weight:600;
    text-align:center; margin-bottom:4px;
  }
  .sub{
    text-align:center; color:var(--dim); font-size:13px; margin-bottom:18px;
  }
  #banner{
    display:none; background:#3a1414; border:1px solid var(--bad);
    color:#ffb3b3; border-radius:10px; padding:12px 16px;
    text-align:center; font-size:15px; margin-bottom:18px;
    animation:pulse 1s infinite;
  }
  #banner.show{display:block;}
  @keyframes pulse{
    0%,100%{box-shadow:0 0 0 0 rgba(255,59,59,.45);}
    50%{box-shadow:0 0 0 8px rgba(255,59,59,0);}
  }
  .panel{
    background:var(--card); border:1px solid var(--line);
    border-radius:14px; padding:22px 16px 14px;
  }
  .bars{
    display:flex; justify-content:space-around;
    align-items:flex-end; height:260px;
  }
  .col{
    display:flex; flex-direction:column; align-items:center;
    justify-content:flex-end; height:100%; width:13%;
  }
  .val{font-size:15px; font-weight:600; margin-bottom:6px;}
  .track{
    width:100%; max-width:56px; flex:1;
    background:#0a0d12; border:1px solid var(--line);
    border-radius:8px; position:relative; overflow:hidden;
  }
  .fill{
    position:absolute; bottom:0; left:0; right:0; height:0%;
    background:linear-gradient(to top,var(--ok2),var(--ok));
    border-radius:7px 7px 0 0;
    transition:height .4s ease, background .3s;
  }
  .col.bad .fill{
    background:linear-gradient(to top,#a00,var(--bad));
    animation:blink .8s infinite;
  }
  .col.bad .val{color:var(--bad);}
  @keyframes blink{50%{opacity:.45;}}
  .col.err .fill{height:4% !important; background:var(--err);}
  .col.err .val{color:var(--err);}
  .num{margin-top:8px; color:var(--dim); font-size:14px;}
  .stats{
    display:flex; justify-content:space-around;
    margin-top:18px; padding-top:14px;
    border-top:1px solid var(--line);
    color:var(--dim); font-size:13px; text-align:center;
  }
  .stats b{display:block; color:var(--txt); font-size:17px; margin-top:2px;}
  .stats .sp-bad b{color:var(--bad);}
  footer{text-align:center; color:var(--dim); font-size:11px; margin-top:16px;}
</style>
</head>
<body>
<div class="wrap">
  <h1>TERMO CONTROL</h1>
  <div class="sub">Мониторинг 6 цилиндров</div>

  <div id="banner"></div>

  <div class="panel">
    <div class="bars" id="bars"></div>
    <div class="stats">
      <div>СРЕДНЯЯ<b id="avg">--</b></div>
      <div id="spbox">РАЗБРОС<b id="spread">--</b></div>
      <div>МАКС<b id="max">--</b></div>
    </div>
  </div>
  <footer>ESP32 &middot; MAX6675 &middot; обновление 1 с</footer>
</div>

<script>
const MAXT = 600;
const barsEl = document.getElementById('bars');

// Создаём 6 колонок один раз
for(let i = 0; i < 6; i++){
  barsEl.insertAdjacentHTML('beforeend',
    `<div class="col" id="c${i}">
       <div class="val" id="v${i}">--</div>
       <div class="track"><div class="fill" id="f${i}"></div></div>
       <div class="num">${i+1}</div>
     </div>`);
}

async function update(){
  try{
    const r = await fetch('/data');
    const d = await r.json();
    let maxT = null;

    for(let i = 0; i < 6; i++){
      const t = d.t[i];
      const col = document.getElementById('c'+i);
      const val = document.getElementById('v'+i);
      const fill = document.getElementById('f'+i);
      col.className = 'col';

      if(t < -50){
        col.classList.add('err');
        val.textContent = 'ERR';
      }else{
        const pct = Math.min(100, Math.max(2, t / MAXT * 100));
        fill.style.height = pct + '%';
        val.textContent = Math.round(t) + '\u00B0';
        if(d.dev[i]) col.classList.add('bad');
        if(maxT === null || t > maxT) maxT = t;
      }
    }

    document.getElementById('avg').textContent =
      d.avg > -50 ? Math.round(d.avg) + '\u00B0C' : '--';
    document.getElementById('spread').textContent =
      d.spread >= 0 ? Math.round(d.spread) + '\u00B0C' : '--';
    document.getElementById('max').textContent =
      maxT !== null ? Math.round(maxT) + '\u00B0C' : '--';
    document.getElementById('spbox').className = d.alarm ? 'sp-bad' : '';

    const banner = document.getElementById('banner');
    if(d.alarm){
      banner.textContent = '\u26A0 РАЗБРОС ТЕМПЕРАТУР ' +
        Math.round(d.spread) + '\u00B0C (допуск ' + d.lim + '\u00B0C)';
      banner.classList.add('show');
    }else{
      banner.classList.remove('show');
    }
  }catch(e){ /* пропускаем неудачный запрос */ }
}
update();
setInterval(update, 1000);
</script>
</body>
</html>
)HTML";

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("  6 CYLINDER MONITOR v3.0 + WiFi"));
  Serial.println(F("========================================"));

  // --- Display ---
  Wire.begin(21, 22);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("ERROR: SSD1306 not found!"));
    while(1) delay(1000);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(15, 15);
  display.print(F("TERMO"));
  display.setCursor(20, 40);
  display.print(F("CONTROL"));
  display.display();

  // --- MAX6675 ---
  pinMode(CLK_PIN, OUTPUT);
  digitalWrite(CLK_PIN, LOW);
  pinMode(DO_PIN, INPUT);
  for(int i = 0; i < 6; i++) {
    pinMode(csPins[i], OUTPUT);
    digitalWrite(csPins[i], HIGH);
  }

  // --- Buttons ---
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT);
  pinMode(BTN_BACK, INPUT);

  // --- WiFi AP ---
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  apIP = WiFi.softAPIP();   // по умолчанию 192.168.4.1
  Serial.print(F("AP SSID: ")); Serial.println(AP_SSID);
  Serial.print(F("AP IP:   ")); Serial.println(apIP);

  // --- Web server ---
  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.onNotFound([](){ server.send(404, "text/plain", "Not found"); });
  server.begin();

  delay(1500);
  showWifiScreen();
  display.display();
  delay(3000);

  Serial.println(F("System ready!"));
}

// ==================== MAIN LOOP ====================
void loop() {
  unsigned long now = millis();

  server.handleClient();

  if(now - lastTempRead >= TEMP_INTERVAL) {
    lastTempRead = now;
    readAllTemperatures();
    computeAlarm();
  }

  handleButtons();

  if(now - lastBlink >= BLINK_INTERVAL) {
    lastBlink = now;
    blinkState = !blinkState;
  }

  if(now - lastDisplayUpdate >= DISPLAY_INTERVAL) {
    lastDisplayUpdate = now;
    display.clearDisplay();
    if(infoView)        showWifiScreen();
    else if(detailView) showDetailView();
    else                showBargraph();
    display.display();
  }

  if(now - lastDebug >= DEBUG_INTERVAL) {
    lastDebug = now;
    debugOutput();
  }
}

// ==================== WEB HANDLERS ====================
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleData() {
  String json = "{\"t\":[";
  for(int i = 0; i < 6; i++) {
    json += String(temperatures[i], 1);
    if(i < 5) json += ",";
  }
  json += "],\"dev\":[";
  for(int i = 0; i < 6; i++) {
    json += deviated[i] ? "1" : "0";
    if(i < 5) json += ",";
  }
  json += "],\"alarm\":";
  json += alarmActive ? "true" : "false";
  json += ",\"spread\":" + String(currentSpread, 1);
  json += ",\"avg\":" + String(avgTemp, 1);
  json += ",\"lim\":" + String(MAX_SPREAD, 0);
  json += "}";
  server.send(200, "application/json", json);
}

// ==================== ALARM LOGIC ====================
void computeAlarm() {
  float tMin = 99999, tMax = -99999, sum = 0;
  int valid = 0;

  for(int i = 0; i < 6; i++) {
    deviated[i] = false;
    if(temperatures[i] > -50) {
      if(temperatures[i] < tMin) tMin = temperatures[i];
      if(temperatures[i] > tMax) tMax = temperatures[i];
      sum += temperatures[i];
      valid++;
    }
  }

  if(valid < 2) {           // сравнивать не с чем
    alarmActive = false;
    currentSpread = -1;
    avgTemp = (valid == 1) ? sum : -100;
    return;
  }

  avgTemp = sum / valid;
  currentSpread = tMax - tMin;
  alarmActive = (currentSpread > MAX_SPREAD);

  if(alarmActive) {
    // Выбиваются те, кто дальше половины допуска от среднего
    for(int i = 0; i < 6; i++) {
      if(temperatures[i] > -50 &&
         fabs(temperatures[i] - avgTemp) > MAX_SPREAD / 2.0) {
        deviated[i] = true;
      }
    }
  }
}

// ==================== READ MAX6675 ====================
float readThermocouple(int csPin) {
  digitalWrite(csPin, LOW);
  delayMicroseconds(10);

  uint16_t raw = 0;
  for(int bit = 15; bit >= 0; bit--) {
    if(digitalRead(DO_PIN)) raw |= (1 << bit);
    digitalWrite(CLK_PIN, HIGH);
    delayMicroseconds(1);
    digitalWrite(CLK_PIN, LOW);
    delayMicroseconds(1);
  }
  digitalWrite(csPin, HIGH);

  if(raw & 0x04) return NAN;   // обрыв термопары
  if(raw == 0)   return NAN;   // модуль не отвечает
  return (raw >> 3) * 0.25;
}

void readAllTemperatures() {
  for(int i = 0; i < 6; i++) {
    float temp = readThermocouple(csPins[i]);
    temperatures[i] = isnan(temp) ? -100 : temp;
  }
}

// ==================== BUTTON HANDLING ====================
void handleButtons() {
  bool leftState   = digitalRead(BTN_LEFT);
  bool rightState  = digitalRead(BTN_RIGHT);
  bool selectState = digitalRead(BTN_SELECT);
  bool backState   = digitalRead(BTN_BACK);

  if(millis() - lastDebounceTime >= debounceDelay) {

    if(leftState == LOW && lastLeftState == HIGH) {
      lastDebounceTime = millis();
      if(!infoView) {
        selectedCylinder = (selectedCylinder - 1 + 6) % 6;
        Serial.print(F("<< LEFT  | Cylinder: "));
        Serial.println(selectedCylinder + 1);
      }
    }

    if(rightState == LOW && lastRightState == HIGH) {
      lastDebounceTime = millis();
      if(!infoView) {
        selectedCylinder = (selectedCylinder + 1) % 6;
        Serial.print(F(">> RIGHT | Cylinder: "));
        Serial.println(selectedCylinder + 1);
      }
    }

    if(selectState == LOW && lastSelectState == HIGH) {
      lastDebounceTime = millis();
      infoView = false;
      detailView = !detailView;
      Serial.print(F("** SELECT | Mode: "));
      Serial.println(detailView ? F("DETAIL") : F("GENERAL"));
    }

    // BACK: из детального -> общий; из общего -> экран Wi-Fi и обратно
    if(backState == LOW && lastBackState == HIGH) {
      lastDebounceTime = millis();
      if(detailView) {
        detailView = false;
        Serial.println(F("<< BACK  | Return to general view"));
      } else {
        infoView = !infoView;
        Serial.println(infoView ? F("<< BACK  | WiFi info") : F("<< BACK  | General view"));
      }
    }
  }

  lastLeftState = leftState;
  lastRightState = rightState;
  lastSelectState = selectState;
  lastBackState = backState;
}

// ==================== DRAW: WIFI INFO ====================
void showWifiScreen() {
  display.setTextSize(1);
  display.setCursor(38, 0);
  display.print(F("Wi-Fi  AP"));
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  display.setCursor(0, 18);
  display.print(F("SSID: "));
  display.print(AP_SSID);

  display.setCursor(0, 30);
  display.print(F("PASS: "));
  display.print(AP_PASS);

  display.setCursor(0, 44);
  display.print(F("http://"));
  display.setCursor(0, 54);
  display.setTextSize(1);
  display.print(apIP);
}

// ==================== DRAW: GENERAL VIEW ====================
void showBargraph() {
  display.setTextSize(1);
  display.setCursor(0, 0);

  // При тревоге заголовок мигает предупреждением
  if(alarmActive && blinkState) {
    display.print(F("! dT>"));
    display.print((int)MAX_SPREAD);
    display.print(F(" !"));
  } else {
    display.print(F("CYLINDERS"));
  }

  float selectedTemp = temperatures[selectedCylinder];
  display.setCursor(80, 0);
  if(selectedTemp >= -50) {
    display.print(F("T:"));
    display.print((int)selectedTemp);
    display.print(F("C"));
  } else {
    display.print(F("ERR"));
  }

  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  for(int i = 0; i < 6; i++) {
    int x = startX + i * (barWidth + barSpacing);
    float temp = temperatures[i];
    int barHeight;
    bool isError = false;

    if(temp < -50) {
      barHeight = 2;
      isError = true;
    } else {
      barHeight = map(constrain((int)temp, 0, maxTemp), 0, maxTemp, 2, maxBarHeight);
    }

    int y = barBaseY - barHeight;
    bool isBad = deviated[i];

    if(i == selectedCylinder) {
      display.fillRect(x-1, barBaseY - maxBarHeight - 1, barWidth+2, maxBarHeight+2, SSD1306_WHITE);
      display.fillRect(x, barBaseY - maxBarHeight, barWidth, maxBarHeight, SSD1306_BLACK);
      display.fillRect(x, y, barWidth, barHeight, SSD1306_WHITE);
    } else if(isBad) {
      // Проблемный цилиндр: столбик мигает заливкой
      if(blinkState) display.fillRect(x, y, barWidth, barHeight, SSD1306_WHITE);
      else           display.drawRect(x, y, barWidth, barHeight, SSD1306_WHITE);
    } else {
      display.drawRect(x, y, barWidth, barHeight, SSD1306_WHITE);
    }

    if(isError) {
      display.drawLine(x, barBaseY - 15, x+barWidth, barBaseY - 35, SSD1306_WHITE);
      display.drawLine(x+barWidth, barBaseY - 15, x, barBaseY - 35, SSD1306_WHITE);
    }

    // Стрелка над выбивающимся цилиндром
    if(isBad && blinkState) {
      display.fillTriangle(x + barWidth/2 - 3, 12,
                           x + barWidth/2 + 3, 12,
                           x + barWidth/2, 16, SSD1306_WHITE);
    }

    display.setTextSize(1);
    display.setCursor(x + 5, 55);
    display.print(i + 1);
  }
}

// ==================== DRAW: DETAIL VIEW ====================
void showDetailView() {
  float temp = temperatures[selectedCylinder];

  display.setTextSize(1);
  display.setCursor(2, 0);
  display.print(F("<"));
  display.setCursor(120, 0);
  display.print(F(">"));

  char header[12];
  snprintf(header, sizeof(header), "CYLINDER %d", selectedCylinder + 1);
  int headerW = strlen(header) * 6;
  display.setCursor((SCREEN_WIDTH - headerW) / 2, 0);
  display.print(header);

  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  if(temp < -50) {
    display.setTextSize(2);
    display.setCursor((SCREEN_WIDTH - 5*12) / 2, 22);
    display.print(F("ERROR"));
    display.setTextSize(1);
    display.setCursor((SCREEN_WIDTH - 12*6) / 2, 44);
    display.print(F("thermocouple"));
    return;
  }

  int t = (int)temp;
  char buf[6];
  snprintf(buf, sizeof(buf), "%d", t);

  int numW   = strlen(buf) * 18;
  int degW   = 6;
  int cW     = 12;
  int totalW = numW + degW + cW;
  int x0     = (SCREEN_WIDTH - totalW) / 2;

  display.setTextSize(3);
  display.setCursor(x0, 20);
  display.print(buf);
  display.drawCircle(x0 + numW + 3, 23, 2, SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(x0 + numW + degW + 2, 24);
  display.print(F("C"));

  // Предупреждение, если этот цилиндр выбивается
  if(deviated[selectedCylinder] && blinkState) {
    display.setTextSize(1);
    display.setCursor(2, 24);
    display.print(F("!"));
    display.setCursor(122, 24);
    display.print(F("!"));
  }

  int barLen = map(constrain(t, 0, maxTemp), 0, maxTemp, 0, 118);
  display.drawRect(5, 52, 118, 8, SSD1306_WHITE);
  if(barLen > 0) display.fillRect(5, 52, barLen, 8, SSD1306_WHITE);
}

// ==================== DEBUG OUTPUT ====================
void debugOutput() {
  Serial.print(F("T: "));
  for(int i = 0; i < 6; i++) {
    if(temperatures[i] < -50) Serial.print(F("ERR"));
    else                      Serial.print(temperatures[i], 1);
    if(i < 5) Serial.print(F(" | "));
  }
  Serial.print(F("  dT="));
  if(currentSpread >= 0) Serial.print(currentSpread, 1);
  else                   Serial.print(F("--"));
  if(alarmActive) Serial.print(F(" !ALARM!"));
  Serial.print(F("  Clients: "));
  Serial.println(WiFi.softAPgetStationNum());
}
