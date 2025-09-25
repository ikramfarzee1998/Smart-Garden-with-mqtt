#include <WiFi.h>
#include <PubSubClient.h>

// ===== WiFi & MQTT Config =====
const char* WIFI_SSID     = "ikrma";
const char* WIFI_PASSWORD = "12345ikram";
const char* MQTT_BROKER   = "test.mosquitto.org";
const int   MQTT_PORT     = 1883;

const char* MQTT_CONTROL_TOPIC   = "smartcity/control";   // For pump commands
const char* MQTT_MOISTURE_TOPIC  = "smartcity/moisture";  // For publishing moisture level

// ===== Hardware Pins =====
const int mosfetPin    = 23;  // Digital pin for MOSFET (Pump)
const int moisturePin  = 33;  // ADC1 pin for Moisture Sensor

// ===== Globals =====
WiFiClient espClient;
PubSubClient client(espClient);

bool pumpState = false;         // Current pump ON/OFF state
bool manualOverride = false;    // Manual override active?
bool manualPumpState = false;   // Manual pump desired state

// ===== MQTT Callback =====
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  msg.trim();

  Serial.printf("[MQTT CALLBACK] topic: %s, msg: '%s'\n", topic, msg.c_str());

  if (msg == "pump:on") {
    manualOverride = true;
    manualPumpState = true;
    digitalWrite(mosfetPin, HIGH);
    pumpState = true;
    Serial.println("Pump ON (Manual Override)");
  }
  else if (msg == "pump:off") {
    manualOverride = true;
    manualPumpState = false;
    digitalWrite(mosfetPin, LOW);
    pumpState = false;
    Serial.println("Pump OFF (Manual Override)");
  }
  else if (msg == "auto") {
    manualOverride = false;  // Cancel manual override, resume auto mode
    Serial.println("Manual override canceled. Auto mode ON.");
  }
}

// ===== WiFi Connect =====
void connectToWiFi() {
  Serial.print("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" connected!");
  Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
}

// ===== MQTT Connect =====
void connectToMQTT() {
  Serial.println("Connecting to MQTT...");
  while (!client.connected()) {
    String clientId = "ESP32Client-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println(" MQTT connected");
      client.subscribe(MQTT_CONTROL_TOPIC);
      Serial.printf("Subscribed to %s\n", MQTT_CONTROL_TOPIC);
    } else {
      Serial.printf("MQTT failed with rc=%d, retry in 2s\n", client.state());
      delay(2000);
    }
  }
}

// ===== Task: Moisture Sensor Publisher + Auto Pump Logic =====
void taskMoisturePublish(void *parameter) {
  while (true) {
    int rawValue = analogRead(moisturePin);
    int moisturePercent = map(rawValue, 4095, 0, 0, 100);
    moisturePercent = constrain(moisturePercent, 0, 100);

    String status;
    if (moisturePercent <= 10) {
      status = "Dry";
    }
    else if (moisturePercent > 10 && moisturePercent <= 20) {
      status = "Moist";
    }
    else {
      status = "Wet";
    }

    if (!manualOverride) {
      // Auto mode based on moisture
      if (status == "Dry" && !pumpState) {
        pumpState = true;
        digitalWrite(mosfetPin, HIGH);
        Serial.println("Auto: Pump ON (Dry)");
      }
      else if ((status == "Moist" || status == "Wet") && pumpState) {
        pumpState = false;
        digitalWrite(mosfetPin, LOW);
        Serial.println("Auto: Pump OFF (Moist/Wet)");
      }
    } else {
      // Manual override active, keep pump state as set by button
      if (pumpState != manualPumpState) {
        pumpState = manualPumpState;
        digitalWrite(mosfetPin, pumpState ? HIGH : LOW);
        Serial.printf("Manual override sync pump to: %s\n", pumpState ? "ON" : "OFF");
      }
    }

    client.publish(MQTT_MOISTURE_TOPIC, status.c_str());
    Serial.printf("Published moisture: %s | Raw: %d | %%: %d\n", status.c_str(), rawValue, moisturePercent);

    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(mosfetPin, OUTPUT);
  digitalWrite(mosfetPin, LOW);

  connectToWiFi();
  client.setServer(MQTT_BROKER, MQTT_PORT);
  client.setCallback(mqttCallback);
  connectToMQTT();

  xTaskCreatePinnedToCore(taskMoisturePublish, "MoistureTask", 2048, NULL, 1, NULL, 1);
}

void loop() {
  if (!client.connected()) {
    connectToMQTT();
  }
  client.loop();
}
