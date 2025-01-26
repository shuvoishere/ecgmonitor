#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <ArduinoJson.h>

// WiFi Credentials
const char* ssid = "XPS";
const char* password = "00000000";

// Pins
const int ecgPin = 34;   // Analog pin for AD8232 ECG signal
const int pulsePin = 35; // Analog pin for Pulse Sensor
const int loPlusPin = 25; // LO+ pin
const int loMinusPin = 26; // LO- pin

// OLED Display Settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Web Server
WebServer server(80);

// ECG Settings
#define SAMPLE_RATE 250  // 250 Hz sampling rate
#define BUFFER_SIZE 250  // Circular buffer size
float ecgBuffer[BUFFER_SIZE];
int bufferIndex = 0;
float heartRate = 0;
unsigned long lastPeakTime = 0;

// Pulse Sensor Settings
float pulseRate = 0.0;
float pulseBuffer[BUFFER_SIZE];
int pulseBufferIndex = 0;

// Flags and Variables
bool electrodeConnected = true;
float filteredValue = 0.0;
float pulseFilteredValue = 0.0;

// Function Prototypes
float filterECG(float rawValue);
float filterPulse(float rawValue);
void detectPeakAndCalculateHR(float signal);
void detectPulseRate(float signal);
void updateDisplay();
void handleRoot();
void handleGetData();
void handleGetPrediction();


// Setup
void setup() {
  Serial.begin(115200);

  // WiFi Setup
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Web Server Setup
  server.on("/", handleRoot);
  server.on("/getData", handleGetData);
  server.on("/getPrediction", handleGetPrediction);  // Add this route
  server.begin();

  // ECG Pin Setup
  pinMode(ecgPin, INPUT);
  pinMode(loPlusPin, INPUT);
  pinMode(loMinusPin, INPUT);

  // Pulse Sensor Pin Setup
  pinMode(pulsePin, INPUT);

  // OLED Setup
  if (!display.begin(0x3C)) {
    Serial.println("OLED initialization failed!");
    while (true);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.display();

 // Display Welcome Message
  display.setCursor(0, 0);
  display.println("Welcome to Smart Health Monitoring");
  display.display();
  delay(2000);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.printf("Connected to %s", ssid);
  display.display();
  delay(2000);
}
// Loop
void loop() {
  // Web Server Handling
  server.handleClient();

  // Detect if electrodes are connected
  if (digitalRead(loPlusPin) == HIGH || digitalRead(loMinusPin) == HIGH) {
    electrodeConnected = false;
    heartRate = 0; // Reset heart rate
  } else {
    electrodeConnected = true;

    // Read ECG Signal
    int rawECG = analogRead(ecgPin);
    filteredValue = filterECG(rawECG / 4095.0 * 3.3);  // Normalize to voltage (3.3V)

    // Validate and process ECG signal
    if (filteredValue > 0.1) { // Ignore weak or invalid signals
      // Store in Circular Buffer
      ecgBuffer[bufferIndex] = filteredValue;
      bufferIndex = (bufferIndex + 1) % BUFFER_SIZE;

      // Detect R-Wave and Calculate Heart Rate
      detectPeakAndCalculateHR(filteredValue);
    }
  }

  // Read Pulse Sensor Signal
  int rawPulse = analogRead(pulsePin);
  pulseFilteredValue = filterPulse(rawPulse / 4095.0 * 3.3);  // Normalize to voltage (3.3V)

  // Validate and process Pulse signal
  if (pulseFilteredValue > 0.1) { // Ignore weak or invalid signals
    // Store in Circular Buffer
    pulseBuffer[pulseBufferIndex] = pulseFilteredValue;
    pulseBufferIndex = (pulseBufferIndex + 1) % BUFFER_SIZE;

    // Detect Pulse Rate
    detectPulseRate(pulseFilteredValue);
  }

  // Update OLED Display
  updateDisplay();
}

// ECG Signal and Pulse Filtering
float filterECG(float rawValue) {
  static float prevInput1 = 0, prevInput2 = 0, prevOutput1 = 0, prevOutput2 = 0;
  float b0 = 0.2929, b1 = 0, b2 = -0.2929; // Example coefficients for 0.5–4 Hz
  float a1 = -1.7038, a2 = 0.7071;

  float filtered = b0 * rawValue + b1 * prevInput1 + b2 * prevInput2
                   - a1 * prevOutput1 - a2 * prevOutput2;

  prevInput2 = prevInput1;
  prevInput1 = rawValue;
  prevOutput2 = prevOutput1;
  prevOutput1 = filtered;

  return filtered;
}

float filterPulse(float rawValue) {
  static float prevInput1 = 0, prevInput2 = 0, prevOutput1 = 0, prevOutput2 = 0;
  float b0 = 0.2929, b1 = 0, b2 = -0.2929;
  float a1 = -1.7038, a2 = 0.7071;

  float filtered = b0 * rawValue + b1 * prevInput1 + b2 * prevInput2
                   - a1 * prevOutput1 - a2 * prevOutput2;

  prevInput2 = prevInput1;
  prevInput1 = rawValue;
  prevOutput2 = prevOutput1;
  prevOutput1 = filtered;

  return filtered;
}


// Detect R-Wave and Calculate Heart Rate
void detectPeakAndCalculateHR(float signal) {
  static float dynamicThreshold = 0.5; // Initial value
  static unsigned long lastPeakTime = 0;

  // Update threshold based on moving average
  static float sum = 0;
  static int count = 0;
  sum += signal;
  count++;
  if (count >= SAMPLE_RATE) { // Update every second
    dynamicThreshold = sum / count + 0.1; // Adjust as needed
    sum = 0;
    count = 0;
  }

  // Detect peak
  if (signal > dynamicThreshold && (millis() - lastPeakTime) > 600) { // Refractory period
    unsigned long currentTime = millis();
    heartRate = 60000.0 / (currentTime - lastPeakTime); // Calculate BPM
    lastPeakTime = currentTime;
  }
}

// Detect Pulse Rate
void detectPulseRate(float signal) {
  static float threshold = 0.8; // Adjust threshold for pulse detection
  static unsigned long lastPulseTime = 0;

  // Signal validation
  if (signal < 0.103) { // Ignore very low amplitude signals
    pulseRate = 0;
    return;
  }

  // Detect pulse
  if (signal > threshold && (millis() - lastPulseTime) > 510) { // Refractory period
    unsigned long currentTime = millis();
    pulseRate = 58300.0 / (currentTime - lastPulseTime); // Calculate BPM
    lastPulseTime = currentTime;
  }
}


// Update OLED Display
void updateDisplay() {
  display.clearDisplay();

  if (!electrodeConnected) {
    display.setCursor(0, 0);
    display.println("Electrodes not connected!");
    display.display();
    return;
  }
  if (filteredValue < 0.1 && pulseFilteredValue < 0.1) { // No valid input
    display.setCursor(0, 0);
    display.println("No Signal Detected!");
    display.display();
    return;
  }
  // Display Heart Rates
  display.setCursor(0, 0);
  display.printf("ECG HR: %.1f BPM\n", heartRate);
  display.printf("Pulse HR: %.1f BPM\n", pulseRate);

  display.display();
}


void handleGetPrediction() {
  String prediction = "Normal";  // Replace with actual prediction logic
  if (heartRate < 60) {
    prediction = "Bradycardia";
  } else if (heartRate > 100) {
    prediction = "Tachycardia";
  }
  
  String json = "{\"prediction\":\"" + prediction + "\"}";
  server.send(200, "application/json", json);
}


// Serve ECG and Pulse Data to Website
void handleGetData() {
  String json = "{";
  json += "\"ecgValue\":" + String(filteredValue) + ",";
  json += "\"heartRate\":" + String(heartRate) + ",";
  json += "\"pulseValue\":" + String(pulseFilteredValue) + ",";
  json += "\"pulseRate\":" + String(pulseRate);
  json += "}";
  server.send(200, "application/json", json);
}

// Serve HTML Page
void handleRoot() {
 String html = "<!DOCTYPE html><html><head><title>Smart Health Monitoring System using ECG</title>";
html += "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script></head><body>";

// Add CSS for styling
html += "<style>";
html += "body { font-family: Arial, sans-serif; margin: 0; padding: 0; display: flex; flex-direction: column; align-items: center; }";
html += "h1 { text-align: center; background-color: #4CAF50; color: white; padding: 20px; border-radius: 10px; box-shadow: 0px 4px 6px rgba(0,0,0,0.2); width: 80%; margin-top: 20px; }";
html += ".container { display: flex; flex-wrap: wrap; justify-content: center; margin-top: 20px; width: 90%; }";
html += "</style>";

// Add centered heading
html += "<h1>IoT Based Smart ECG Monitoring and Diagnostic System</h1>";
html += "<div class='container'>";


  // ECG Section
  html += "<div style='width: 45%; padding: 10px; border: 1px solid black; background-color: #ffe6e6;'>";
  html += "<h2>ECG Graph</h2><canvas id='ecgChart' width='400' height='200'></canvas>";
  html += "<p>Heart Rate: <span id='ecgHeartRate'></span> BPM</p>";
  html += "</div>";

  // Pulse Sensor Section
  html += "<div style='width: 45%; padding: 10px; border: 1px solid black; background-color: #e6f7ff;'>";
  html += "<h2>Pulse Sensor Graph</h2><canvas id='pulseChart' width='400' height='200'></canvas>";
  html += "<p>Pulse Rate: <span id='pulseHeartRate'></span> BPM</p>";
  html += "</div>";

  // Snapshot and Save Data Section
  html += "<div style='width: 45%; padding: 10px; border: 1px solid black; background-color: #e6ffe6;'>";
  html += "<h2>Snapshot & Save Data</h2>";
  html += "<button onclick='takeSnapshot()'>Take Snapshot</button>";
  html += "<button onclick='saveData()'>Save ECG Data</button>";
  html += "</div>";

 // Additional Features Section with Prediction
 html += "<div style='width: 45%; padding: 10px; border: 1px solid black; background-color: #f0e6ff;'>";
 html += "<h2>Diagnosis</h2>";
 html += "<p>Heart Status: <span id='prediction'>Loading...</span></p>";
 html += "</div>";


  html += "</div>";
  html += "<script>";
  html += "const ecgCtx = document.getElementById('ecgChart').getContext('2d');";
  html += "const ecgChart = new Chart(ecgCtx, { type: 'line', data: { labels: [], datasets: [{ label: 'ECG Signal', borderColor: 'red', data: [], tension: 0.1 }] }, options: { responsive: true, animation: false } });";
  html += "const pulseCtx = document.getElementById('pulseChart').getContext('2d');";
  html += "const pulseChart = new Chart(pulseCtx, { type: 'line', data: { labels: [], datasets: [{ label: 'Pulse Signal', borderColor: 'blue', data: [], tension: 0.1 }] }, options: { responsive: true, animation: false } });";
  html += "setInterval(() => { fetch('/getData').then(res => res.json()).then(data => { ";
  html += "if (ecgChart.data.labels.length > 100) { ecgChart.data.labels.shift(); ecgChart.data.datasets[0].data.shift(); }";
  html += "if (pulseChart.data.labels.length > 100) { pulseChart.data.labels.shift(); pulseChart.data.datasets[0].data.shift(); }";
  html += "ecgChart.data.labels.push((Date.now() / 1000).toFixed(2)); ecgChart.data.datasets[0].data.push(data.ecgValue);";
  html += "pulseChart.data.labels.push((Date.now() / 1000).toFixed(2)); pulseChart.data.datasets[0].data.push(data.pulseValue);";
  html += "ecgChart.update(); pulseChart.update();";
  html += "document.getElementById('ecgHeartRate').innerText = data.heartRate.toFixed(1);";
  html += "document.getElementById('pulseHeartRate').innerText = data.pulseRate.toFixed(1);";
  html += "}); }, 100);";
  // Snapshot function
  html += "function takeSnapshot() {";
  html += "  const ecgSnapshot = ecgChart.data.datasets[0].data;";
  html += "  const pulseSnapshot = pulseChart.data.datasets[0].data;";
  html += "  const snapshotData = { ecg: ecgSnapshot, pulse: pulseSnapshot };";
  html += "  const blob = new Blob([JSON.stringify(snapshotData, null, 2)], { type: 'json' });";
  html += "  const url = URL.createObjectURL(blob);";
  html += "  const a = document.createElement('a');";
  html += "  a.href = url; a.download = 'snapshot.json'; a.click();";
  html += "  alert('Snapshot downloaded as snapshot.json');";
  html += "}";

  // Save ECG Data function
  html += "function saveData() {";
  html += "  fetch('/getData').then(res => res.json()).then(data => {";
  html += "    const ecgData = data.ecgValue;";
  html += "    const blob = new Blob([ecgData.toString()], { type: 'csv' });";
  html += "    const url = URL.createObjectURL(blob);";
  html += "    const a = document.createElement('a');";
  html += "    a.href = url; a.download = 'ecg_data.csv'; a.click();";
  html += "    alert('ECG data saved as ecg_data.csv');";
  html += "  });";
  html += "}";
  
  html += "setInterval(() => {";
  html += "  fetch('/getPrediction')";
  html += "    .then(res => res.json())";
  html += "    .then(data => {";
  html += "      document.getElementById('prediction').innerText = data.prediction;";
  html += "    });";
  html += "}, 1000);";  // Update every second

  html += "</script></body></html>";
  server.send(200, "text/html", html);
}
