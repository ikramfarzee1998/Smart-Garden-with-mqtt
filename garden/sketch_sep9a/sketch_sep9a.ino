#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

// ===== WiFi & MQTT Config =====
const char* WIFI_SSID     = "ikrma";
const char* WIFI_PASSWORD = "12345ikram";
const char* MQTT_BROKER   = "test.mosquitto.org";
const int   MQTT_PORT     = 1883;

const char* MQTT_CONTROL_TOPIC   = "smartcity/control";    // For servo & pump commands
const char* MQTT_MOISTURE_TOPIC = "smartcity/moisture";   // For publishing moisture level

// ===== Hardware Pins =====
const int servoPin     = 14;  // PWM-capable pin for Servo
const int mosfetPin    = 23;  // Digital pin for MOSFET
const int moisturePin  = 33;  // ADC1 pin for Moisture Sensor

// ===== Globals =====
WiFiClient espClient;
PubSubClient client(espClient);
Servo myServo;

volatile int requestedServoAngle = -1;  // -1 means no new angle
volatile int pumpState = -1;            // -1 = no change, 0 = off, 1 = on

// ===== MQTT Callback =====
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  msg.trim();

  Serial.printf("MQTT [%s]: %s\n", topic, msg.c_str());

  if (msg == "pump:on") {
    pumpState = 1;

    // Detach servo to power it OFF
    if (myServo.attached()) {
      myServo.detach();
      Serial.println("⚡ Servo power OFF (detached)");
    }

    return;
  } else if (msg == "pump:off") {
    pumpState = 0;

    // Attach servo and move to 180°
    if (!myServo.attached()) {
      myServo.attach(servoPin);
      Serial.println("⚡ Servo power ON (attached)");
    }
    requestedServoAngle = 180;

    return;
  }

  if (msg.startsWith("servo:")) {
    int angle = msg.substring(6).toInt();
    if (angle >= 0 && angle <= 180) {
      if (myServo.attached()) {
        requestedServoAngle = angle;
      } else {
        Serial.println("⚠️ Cannot move servo — it is powered off (detached)");
      }
    } else {
      Serial.println("⚠️ Invalid servo angle (0–180 only)");
    }
  }
}

// ===== WiFi Connect =====
void connectToWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi connected");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

// ===== MQTT Connect =====
void connectToMQTT() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    String clientId = "ESP32Client-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println(" connected!");
      client.subscribe(MQTT_CONTROL_TOPIC);
      Serial.printf("Subscribed to topic: %s\n", MQTT_CONTROL_TOPIC);
    } else {
      Serial.printf("❌ MQTT failed (rc=%d). Retrying in 2s...\n", client.state());
      delay(2000);
    }
  }
}

// ===== Task: Servo Control =====
void taskServoControl(void *parameter) {
  while (true) {
    if (requestedServoAngle >= 0) {
      myServo.write(requestedServoAngle);
      Serial.printf("✅ Servo moved to %d°\n", requestedServoAngle);
      requestedServoAngle = -1;
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// ===== Task: Pump Control =====
void taskPumpControl(void *parameter) {
  while (true) {
    if (pumpState == 1) {
      digitalWrite(mosfetPin, HIGH);
      Serial.println("✅ Pump turned ON");
      pumpState = -1;
    } else if (pumpState == 0) {
      digitalWrite(mosfetPin, LOW);
      Serial.println("✅ Pump turned OFF");
      pumpState = -1;
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// ===== Task: Moisture Sensor Publisher =====
void taskMoisturePublish(void *parameter) {
  while (true) {
    int rawValue = analogRead(moisturePin);  // Range: 0–4095
    int moisturePercent = map(rawValue, 4095, 0, 0, 100); // Dry = 4095, Wet = 0

    // Updated logic: 0% = Dry, >0% = Wet
    String status = (moisturePercent == 0) ? "Dry" : "Wet";

    // Format: "45,Wet"
    char buf[32];
    snprintf(buf, sizeof(buf), "%d,%s", moisturePercent, status.c_str());
    client.publish(MQTT_MOISTURE_TOPIC, buf);

    Serial.printf("📤 Moisture: %d%% (%s) | Raw: %d\n", moisturePercent, status.c_str(), rawValue);
    vTaskDelay(2000 / portTICK_PERIOD_MS); // Every 2 seconds
  }
}

// ===== Setup =====
void setup() {
  Serial.begin(115200);

  // Pin setup
  pinMode(mosfetPin, OUTPUT);
  digitalWrite(mosfetPin, LOW); // Ensure pump is off

  pinMode(moisturePin, INPUT);

  myServo.attach(servoPin);
  myServo.write(90); // Center servo

  // WiFi & MQTT
  connectToWiFi();
  client.setServer(MQTT_BROKER, MQTT_PORT);
  client.setCallback(mqttCallback);
  connectToMQTT();

  // Create FreeRTOS tasks
  xTaskCreatePinnedToCore(taskServoControl,     "ServoTask",    2048, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(taskPumpControl,      "PumpTask",     2048, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(taskMoisturePublish,  "MoistureTask", 2048, NULL, 1, NULL, 1);
}

// ===== Loop =====
void loop() {
  if (!client.connected()) {
    connectToMQTT();
  }
  client.loop();
}
