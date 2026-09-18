/*
 *  ESP32-S3 Precision Seeder Monitor
 *  IR Break-Beam Seed Counting with Web Dashboard
 *  
 *  Hardware:
 *    - ESP32-S3 DevKit
 *    - IR Break-Beam Sensor Pair 1 -> GPIO 1 (ADC1_CH0)
 *    - IR Break-Beam Sensor Pair 2 -> GPIO 2 (ADC1_CH1)
 *    - WiFi Access Point Mode (fixed IP 192.168.4.1)
 *    
 *  Metrics:
 *    - Seed Count, Singulation %, Skips, Doubles, Seed Rate (seeds/m)
 *    
 *  Dashboard: http://192.168.4.1
 *  Connect to WiFi AP, then open the URL above in a browser.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// ==================== PIN CONFIGURATION ====================
#define SENSOR_1_PIN    1   // ADC1_CH0
#define SENSOR_2_PIN    2   // ADC1_CH1
#define LED_STATUS      8   // Built-in LED or external status LED

// ==================== ADC SETTINGS ====================
#define ADC_RESOLUTION      12
#define ADC_MAX_VALUE       4095
#define BEAM_THRESHOLD      3500   // Below this = beam broken (adjust for your sensors)
#define SENSOR_HYSTERESIS   200    // Hysteresis to prevent chatter

// ==================== SEED COUNTING ALGORITHM (SCA) ====================
// Timing thresholds in microseconds
#define T_LOW_US        1500    // < 1.5 ms = noise/dust
#define T_HIGH_US       4000    // >= 4.0 ms = double seed
#define T_DEBOUNCE_US   50000   // 50 ms debounce after seed exits
#define T_EXPECTED_US   200000  // 200 ms expected interval between seeds
#define SKIP_TIMEOUT_US 300000  // 300 ms without seed = skip detected

// ==================== SERIAL REPORTING INTERVAL ====================
#define SERIAL_REPORT_INTERVAL_MS  2000   // Print metrics every 2 seconds

// ==================== WIFI SETTINGS ====================
const char* ssid = "Precision Planter";
const char* password = "Jabari_2026";  // min 8 chars

// Fixed AP IP configuration
IPAddress local_IP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

WebServer server(80);

// ==================== STATE MACHINE ====================
enum SeedState {
  STATE_IDLE,
  STATE_IN_BEAM,
  STATE_DEBOUNCE
};

// ==================== GLOBAL DATA ====================
volatile unsigned long seedCount = 0;
volatile unsigned long singleCount = 0;
volatile unsigned long doubleCount = 0;
volatile unsigned long skipCount = 0;
volatile unsigned long noiseCount = 0;

volatile unsigned long lastSeedTime = 0;
volatile unsigned long lastSkipCheckTime = 0;
volatile bool skipFlag = false;

// For seed rate calculation
float planterSpeedKmh = 6.0;      // Default 6 km/h (user adjustable)
unsigned long sessionStartTime = 0;

// ADC averaging buffers
#define ADC_SAMPLES 8

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n========================================");
  Serial.println("  PRECISION SEEDER MONITOR - ESP32-S3");
  Serial.println("========================================");

  pinMode(LED_STATUS, OUTPUT);
  digitalWrite(LED_STATUS, LOW);

  analogReadResolution(ADC_RESOLUTION);
  analogSetAttenuation(ADC_11db);  // 0-3.3V full range

  sessionStartTime = millis();

  initWiFi();
  initWebServer();

  // ---- Print actual IP address to Serial Monitor ----
  IPAddress IP = WiFi.softAPIP();
  Serial.println();
  Serial.println("========================================");
  Serial.println("  NETWORK READY");
  Serial.println("========================================");
  Serial.print("  WiFi SSID     : ");
  Serial.println(ssid);
  Serial.print("  WiFi Password : ");
  Serial.println(password);
  Serial.print("  Dashboard URL : http://");
  Serial.println(IP);
  Serial.println("========================================");
  Serial.println("  Connect your laptop/phone to the WiFi");
  Serial.println("  above, then open the Dashboard URL.");
  Serial.println("========================================\n");
}

// ==================== WIFI ====================
void initWiFi() {
  // Force a fixed AP IP (192.168.4.1)
  if (!WiFi.softAPConfig(local_IP, gateway, subnet)) {
    Serial.println("AP Config Failed");
  }

  WiFi.softAP(ssid, password);
  delay(100);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
}

// ==================== WEB SERVER ====================
void initWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/data", HTTP_GET, handleApiData);
  server.on("/api/reset", HTTP_POST, handleReset);
  server.on("/api/speed", HTTP_POST, handleSpeed);
  server.begin();
  Serial.println("HTTP server started");
}

// ==================== MAIN LOOP ====================
void loop() {
  server.handleClient();
  processSensors();
  checkForSkips();
  blinkStatusLED();
  reportToSerial();   // periodic Serial Monitor reporting
}

// ==================== SENSOR PROCESSING ====================
void processSensors() {
  static SeedState state = STATE_IDLE;
  static unsigned long beamEnterTime = 0;
  static unsigned long debounceStart = 0;

  // Read both sensors with averaging
  int raw1 = averageAnalogRead(SENSOR_1_PIN);
  int raw2 = averageAnalogRead(SENSOR_2_PIN);

  bool sensor1Broken = (raw1 < BEAM_THRESHOLD);
  bool sensor2Broken = (raw2 < BEAM_THRESHOLD);

  // Require BOTH sensors to agree for robust detection (anti-dust)
  bool beamBroken = sensor1Broken && sensor2Broken;

  unsigned long now = micros();

  switch (state) {
    case STATE_IDLE:
      if (beamBroken) {
        state = STATE_IN_BEAM;
        beamEnterTime = now;
      }
      break;

    case STATE_IN_BEAM:
      if (!beamBroken) {
        unsigned long sustainTime = now - beamEnterTime;
        classifySeed(sustainTime);
        state = STATE_DEBOUNCE;
        debounceStart = now;
      }
      break;

    case STATE_DEBOUNCE:
      if ((now - debounceStart) >= T_DEBOUNCE_US) {
        state = STATE_IDLE;
      }
      break;
  }
}

// ==================== SEED CLASSIFICATION ====================
void classifySeed(unsigned long sustainTime) {
  if (sustainTime < T_LOW_US) {
    // Noise or dust particle
    noiseCount++;
    Serial.printf("[NOISE] Sustain: %lu us\n", sustainTime);
  }
  else if (sustainTime < T_HIGH_US) {
    // Single seed
    seedCount += 1;
    singleCount += 1;
    lastSeedTime = micros();
    lastSkipCheckTime = lastSeedTime;
    skipFlag = false;
    Serial.printf("[SINGLE] Count: %lu | Sustain: %lu us\n", seedCount, sustainTime);
  }
  else {
    // Double seed (or multiple)
    seedCount += 2;
    doubleCount += 1;
    lastSeedTime = micros();
    lastSkipCheckTime = lastSeedTime;
    skipFlag = false;
    Serial.printf("[DOUBLE] Count: %lu | Sustain: %lu us\n", seedCount, sustainTime);
  }
}

// ==================== SKIP DETECTION ====================
void checkForSkips() {
  if (seedCount == 0) return;

  unsigned long now = micros();
  unsigned long timeSinceLastSeed = now - lastSeedTime;

  // Detect skip if we've waited longer than expected but not yet flagged
  if (timeSinceLastSeed > SKIP_TIMEOUT_US && !skipFlag) {
    skipCount++;
    skipFlag = true;
    Serial.printf("[SKIP] Detected! Total skips: %lu\n", skipCount);
  }
}

// ==================== ADC HELPERS ====================
int averageAnalogRead(uint8_t pin) {
  long sum = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    sum += analogRead(pin);
  }
  return sum / ADC_SAMPLES;
}

// ==================== METRICS CALCULATION ====================
float calculateSingulation() {
  unsigned long totalEvents = singleCount + doubleCount + skipCount;
  if (totalEvents == 0) return 100.0;
  return ((float)singleCount / (float)totalEvents) * 100.0;
}

float calculateSeedRate() {
  float speedMps = planterSpeedKmh / 3.6;
  unsigned long elapsedSec = (millis() - sessionStartTime) / 1000;
  if (elapsedSec == 0 || speedMps <= 0) return 0.0;
  float distanceM = speedMps * elapsedSec;
  return (float)seedCount / distanceM;
}

float calculateSeedsPerMinute() {
  unsigned long elapsedMs = millis() - sessionStartTime;
  if (elapsedMs < 1000) return 0.0;
  float elapsedMin = elapsedMs / 60000.0;
  return (float)seedCount / elapsedMin;
}

// ==================== SERIAL REPORTING ====================
void reportToSerial() {
  static unsigned long lastReport = 0;
  unsigned long now = millis();

  if (now - lastReport < SERIAL_REPORT_INTERVAL_MS) return;
  lastReport = now;

  unsigned long seeds   = seedCount;
  unsigned long singles = singleCount;
  unsigned long doubles = doubleCount;
  unsigned long skips   = skipCount;
  unsigned long noise   = noiseCount;
  float singulation     = calculateSingulation();
  float seedRate        = calculateSeedRate();
  float seedsPerMin     = calculateSeedsPerMinute();
  unsigned long uptime  = (now - sessionStartTime) / 1000;

  Serial.println();
  Serial.println("+----------------------------------------------+");
  Serial.println("|            SEEDER LIVE METRICS               |");
  Serial.println("+----------------------------------------------+");
  Serial.printf("|  Total Seeds      : %-8lu                 |\n", seeds);
  Serial.printf("|  Singles          : %-8lu                 |\n", singles);
  Serial.printf("|  Doubles          : %-8lu                 |\n", doubles);
  Serial.printf("|  Skips            : %-8lu                 |\n", skips);
  Serial.printf("|  Noise Filtered   : %-8lu                 |\n", noise);
  Serial.printf("|  Singulation      : %6.1f %%                |\n", singulation);
  Serial.printf("|  Seed Rate        : %6.2f seeds/m           |\n", seedRate);
  Serial.printf("|  Seeds / Minute   : %6.1f                   |\n", seedsPerMin);
  Serial.printf("|  Planter Speed    : %5.1f km/h              |\n", planterSpeedKmh);
  Serial.printf("|  Uptime           : %-8lu s               |\n", uptime);
  Serial.println("+----------------------------------------------+");
  Serial.println();
}

// ==================== LED INDICATOR ====================
void blinkStatusLED() {
  static unsigned long lastBlink = 0;
  static bool ledState = false;
  unsigned long now = millis();

  unsigned long interval = (seedCount > 0) ? 500 : 2000;

  if (now - lastBlink >= interval) {
    lastBlink = now;
    ledState = !ledState;
    digitalWrite(LED_STATUS, ledState);
  }
}

// ==================== HTTP HANDLERS ====================
void handleRoot() {
  server.send(200, "text/html", dashboardHTML());
}

void handleApiData() {
  StaticJsonDocument<512> doc;
  doc["seedCount"] = seedCount;
  doc["singleCount"] = singleCount;
  doc["doubleCount"] = doubleCount;
  doc["skipCount"] = skipCount;
  doc["noiseCount"] = noiseCount;
  doc["singulation"] = calculateSingulation();
  doc["seedRate"] = calculateSeedRate();
  doc["seedsPerMin"] = calculateSeedsPerMinute();
  doc["planterSpeed"] = planterSpeedKmh;
  doc["uptimeSec"] = (millis() - sessionStartTime) / 1000;

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleReset() {
  seedCount = 0;
  singleCount = 0;
  doubleCount = 0;
  skipCount = 0;
  noiseCount = 0;
  lastSeedTime = 0;
  skipFlag = false;
  sessionStartTime = millis();
  Serial.println("[RESET] All counters cleared by user.");
  server.send(200, "application/json", "{\"status\":\"reset_ok\"}");
}

void handleSpeed() {
  if (server.hasArg("value")) {
    planterSpeedKmh = server.arg("value").toFloat();
    if (planterSpeedKmh < 0) planterSpeedKmh = 0;
    if (planterSpeedKmh > 20) planterSpeedKmh = 20;
    Serial.printf("[SPEED] Planter speed set to %.1f km/h\n", planterSpeedKmh);
  }
  server.send(200, "application/json", "{\"status\":\"speed_updated\",\"speed\":" + String(planterSpeedKmh) + "}");
}

// ==================== DASHBOARD HTML ====================
String dashboardHTML() {
  return R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>JABARI Precision Planter</title>
<style>
  :root {
    --bg: #F0F4F1;
    --card: #FFFFFF;
    --dark-green: #1B4332;
    --med-green: #2D6A4F;
    --light-green: #52B788;
    --accent: #D8F3DC;
    --yellow: #F4D03F;
    --red: #E74C3C;
    --text: #1B4332;
    --text-light: #6B8E7B;
    --shadow: rgba(27,67,50,0.08);
  }
  * { margin: 0; padding: 0; box-sizing: border-box; }
  body {
    font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;
    background: var(--bg);
    color: var(--text);
    min-height: 100vh;
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 16px;
    overflow: hidden;
  }
  .dashboard {
    width: 100%;
    max-width: 1360px;
    height: 100vh;
    max-height: 900px;
    display: flex;
    flex-direction: column;
    gap: 12px;
    padding: 4px 0;
    overflow: hidden;
  }
  .header-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    flex-shrink: 0;
    padding: 0 4px;
  }
  .title-block h1 {
    font-size: 1.4rem;
    font-weight: 700;
    color: var(--dark-green);
    letter-spacing: -0.3px;
    display: flex;
    align-items: center;
    gap: 6px;
  }
  .title-block h1 span { font-size: 1.6rem; }
  .title-block p {
    color: var(--text-light);
    font-size: 0.8rem;
    margin-top: 2px;
  }
  .status-bar {
    display: flex;
    align-items: center;
    gap: 8px;
    background: var(--card);
    padding: 8px 16px;
    border-radius: 100px;
    box-shadow: 0 2px 8px var(--shadow);
    font-size: 0.8rem;
    color: var(--text-light);
    white-space: nowrap;
  }
  .status-dot {
    width: 8px; height: 8px;
    border-radius: 50%;
    background: var(--light-green);
    animation: pulse 2s infinite;
  }
  @keyframes pulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.3; }
  }
  .main-grid {
    display: grid;
    grid-template-columns: 1.1fr 1.3fr 1.1fr;
    gap: 12px;
    flex: 1;
    min-height: 0;
    overflow: hidden;
  }
  .col {
    display: flex;
    flex-direction: column;
    gap: 12px;
    min-height: 0;
  }
  .card {
    background: var(--card);
    border-radius: 16px;
    padding: 16px 18px;
    box-shadow: 0 4px 16px var(--shadow);
    display: flex;
    flex-direction: column;
    transition: transform 0.15s;
    overflow: hidden;
    flex-shrink: 0;
  }
  .card.large {
    flex: 1;
    justify-content: center;
    align-items: center;
    text-align: center;
    background: linear-gradient(135deg, var(--dark-green), var(--med-green));
    color: white;
  }
  .card.large .label {
    color: var(--accent);
    font-size: 0.8rem;
    text-transform: uppercase;
    letter-spacing: 1.5px;
  }
  .card.large .value {
    font-size: 4rem;
    font-weight: 700;
    line-height: 1.1;
    color: white;
    margin: 6px 0 2px;
  }
  .card.large .sub {
    font-size: 0.95rem;
    opacity: 0.9;
    font-weight: 400;
  }
  .label {
    font-size: 0.7rem;
    color: var(--text-light);
    text-transform: uppercase;
    letter-spacing: 0.5px;
    margin-bottom: 4px;
    font-weight: 600;
  }
  .value {
    font-size: 2rem;
    font-weight: 700;
    color: var(--dark-green);
    line-height: 1.2;
  }
  .value .unit {
    font-size: 0.8rem;
    font-weight: 400;
    color: var(--text-light);
    margin-left: 4px;
  }
  .stat-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 8px;
  }
  .stat-card {
    flex: 1;
    background: var(--card);
    border-radius: 14px;
    padding: 14px 14px;
    box-shadow: 0 4px 16px var(--shadow);
    display: flex;
    flex-direction: column;
    justify-content: center;
    border-left: 4px solid transparent;
    overflow: hidden;
    min-height: 80px;
  }
  .stat-card.warning { border-left-color: var(--yellow); }
  .stat-card.alert { border-left-color: var(--red); }
  .stat-card.success { border-left-color: var(--light-green); }
  .stat-card.rate { border-left-color: var(--med-green); }
  .stat-card .value { font-size: 1.8rem; font-weight: 700; line-height: 1.2; }
  .progress-bar {
    width: 100%;
    height: 5px;
    background: var(--accent);
    border-radius: 3px;
    margin-top: 8px;
    overflow: hidden;
  }
  .progress-fill {
    height: 100%;
    background: var(--light-green);
    border-radius: 3px;
    transition: width 0.4s ease;
    width: 100%;
  }
  .controls-compact {
    background: var(--card);
    border-radius: 16px;
    padding: 14px 16px;
    box-shadow: 0 4px 16px var(--shadow);
    display: flex;
    flex-direction: column;
    gap: 10px;
    flex-shrink: 0;
    margin-top: auto;
  }
  .controls-compact h3 {
    font-size: 0.8rem;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--text-light);
    font-weight: 600;
  }
  .input-group {
    display: flex;
    gap: 8px;
    align-items: center;
  }
  .input-group input {
    flex: 1;
    padding: 8px 12px;
    border: 2px solid var(--accent);
    border-radius: 10px;
    font-size: 0.9rem;
    color: var(--dark-green);
    outline: none;
    background: white;
    min-width: 0;
  }
  .input-group input:focus { border-color: var(--light-green); }
  .btn {
    padding: 8px 16px;
    border: none;
    border-radius: 10px;
    font-size: 0.8rem;
    font-weight: 600;
    cursor: pointer;
    transition: all 0.15s;
    white-space: nowrap;
    flex-shrink: 0;
  }
  .btn-primary { background: var(--med-green); color: white; }
  .btn-primary:hover { background: var(--dark-green); }
  .btn-danger {
    background: var(--red);
    color: white;
    width: 100%;
    margin-top: 2px;
  }
  .btn-danger:hover { background: #C0392B; }
  .session-footer {
    display: flex;
    justify-content: space-between;
    font-size: 0.75rem;
    color: var(--text-light);
    padding: 0 4px 2px;
    flex-shrink: 0;
  }
  .session-footer b { color: var(--dark-green); font-weight: 600; }
  @media (max-width: 1000px) {
    .main-grid { grid-template-columns: 1fr 1.2fr 1fr; gap: 10px; }
    .card.large .value { font-size: 3.2rem; }
    .stat-card .value { font-size: 1.5rem; }
    .value { font-size: 1.6rem; }
    .title-block h1 { font-size: 1.2rem; }
  }
  @media (max-height: 720px) {
    .card { padding: 12px 14px; }
    .card.large .value { font-size: 2.8rem; }
    .stat-card { padding: 10px 12px; min-height: 68px; }
    .stat-card .value { font-size: 1.4rem; }
    .controls-compact { padding: 10px 14px; gap: 6px; }
    .btn { padding: 6px 12px; font-size: 0.75rem; }
    .input-group input { padding: 6px 10px; font-size: 0.8rem; }
  }
  @media (max-width: 700px) {
    body { overflow: auto; padding: 12px; align-items: flex-start; }
    .dashboard { height: auto; max-height: none; overflow: visible; }
    .main-grid { grid-template-columns: 1fr; }
    .col { gap: 10px; }
    .card.large .value { font-size: 2.8rem; }
    .session-footer { flex-wrap: wrap; gap: 6px; }
  }
</style>
</head>
<body>
<div class="dashboard">
  <div class="header-row">
    <div class="title-block">
      <h1><span>🌱</span> JABARI Precision Planter</h1>
      <p>Real-time seed monitoring · IEEE YESIST12</p>
    </div>
    <div class="status-bar">
      <div class="status-dot" id="statusDot"></div>
      <span id="connStatus">Live · Updating</span>
    </div>
  </div>

  <div class="main-grid">
    <div class="col">
      <div class="card success" style="border-left: 4px solid var(--light-green); padding-bottom: 12px;">
        <div class="label">Singulation</div>
        <div class="value"><span id="singulation">100</span><span class="unit">%</span></div>
        <div class="progress-bar">
          <div class="progress-fill" id="singBar" style="width:100%"></div>
        </div>
      </div>

      <div class="stat-row">
        <div class="stat-card warning">
          <div class="label">⚠ Skips</div>
          <div class="value" id="skipCount" style="color: var(--yellow);">0</div>
        </div>
        <div class="stat-card alert">
          <div class="label">⚡ Doubles</div>
          <div class="value" id="doubleCount" style="color: var(--red);">0</div>
        </div>
      </div>

      <div class="stat-card rate" style="flex: 1; justify-content: center;">
        <div class="label">Seed Rate</div>
        <div class="value"><span id="seedRate">0.00</span> <span class="unit">seeds/m</span></div>
        <div style="font-size:0.7rem; color:var(--text-light); margin-top: 4px;">based on planter speed</div>
      </div>

      <div class="controls-compact">
        <h3>⚙ Planter settings</h3>
        <div class="input-group">
          <input type="number" id="speedInput" value="6.0" min="0" max="20" step="0.5" placeholder="km/h">
          <span style="color:var(--text-light); font-size:0.75rem;">km/h</span>
          <button class="btn btn-primary" onclick="updateSpeed()">Set</button>
        </div>
        <button class="btn btn-danger" onclick="resetCounters()">↺ Reset all counters</button>
      </div>
    </div>

    <div class="col">
      <div class="card large" id="totalCard">
        <div class="label">Total Seeds</div>
        <div class="value" id="seedCount">0</div>
        <div class="sub"><span id="seedsPerMin">0</span> seeds/min</div>
        <div style="margin-top: 12px; font-size: 0.75rem; opacity: 0.7;">live count</div>
      </div>
    </div>

    <div class="col">
      <div class="card" style="justify-content: center; flex: 1;">
        <div class="label">Session uptime</div>
        <div class="value"><span id="uptime">0</span><span class="unit">s</span></div>
      </div>
      <div class="card" style="justify-content: center; flex: 1;">
        <div class="label">Noise filtered</div>
        <div class="value" id="noiseCount">0</div>
      </div>
      <div style="margin-top: auto; text-align: right; font-size: 0.65rem; color: var(--text-light); padding: 4px 6px;">
        ESP32-S3 · AP mode
      </div>
    </div>
  </div>

  <div class="session-footer">
    <span>🌾 Seeds: <b id="footerSeedCount">0</b></span>
    <span>📊 Singulation: <b id="footerSing">100%</b></span>
    <span>⏱️ Uptime: <b id="footerUptime">0</b>s</span>
    <span>🔇 Noise: <b id="footerNoise">0</b></span>
  </div>
</div>

<script>
let lastCount = 0;

async function fetchData() {
  try {
    const res = await fetch('/api/data');
    const d = await res.json();

    document.getElementById('seedCount').textContent = d.seedCount;
    document.getElementById('singulation').textContent = d.singulation.toFixed(1);
    document.getElementById('singBar').style.width = d.singulation + '%';
    document.getElementById('skipCount').textContent = d.skipCount;
    document.getElementById('doubleCount').textContent = d.doubleCount;
    document.getElementById('seedRate').textContent = d.seedRate.toFixed(2);
    document.getElementById('seedsPerMin').textContent = d.seedsPerMin.toFixed(1);
    document.getElementById('uptime').textContent = d.uptimeSec;
    document.getElementById('noiseCount').textContent = d.noiseCount;

    document.getElementById('footerSeedCount').textContent = d.seedCount;
    document.getElementById('footerSing').textContent = d.singulation.toFixed(1) + '%';
    document.getElementById('footerUptime').textContent = d.uptimeSec;
    document.getElementById('footerNoise').textContent = d.noiseCount;

    if (d.seedCount > lastCount) {
      const card = document.getElementById('totalCard');
      card.style.transform = 'scale(1.02)';
      setTimeout(() => card.style.transform = '', 150);
      lastCount = d.seedCount;
    }

    document.getElementById('connStatus').textContent = 'Live · Updating';
    document.getElementById('statusDot').style.background = '#52B788';
  } catch (e) {
    document.getElementById('connStatus').textContent = 'Reconnecting...';
    document.getElementById('statusDot').style.background = '#E74C3C';
  }
}

async function updateSpeed() {
  const val = document.getElementById('speedInput').value;
  await fetch('/api/speed?value=' + val, {method:'POST'});
  fetchData();
}

async function resetCounters() {
  if (!confirm('Reset all counters?')) return;
  await fetch('/api/reset', {method:'POST'});
  lastCount = 0;
  fetchData();
}

fetchData();
setInterval(fetchData, 500);
</script>
</body>
</html>
)rawliteral";
}