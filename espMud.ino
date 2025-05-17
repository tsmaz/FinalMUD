#include <WiFi.h>
#include <PubSubClient.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

// Wi-Fi and MQTT configuration - CHANGE THESE
const char* ssid = "YOUR-SSID";
const char* password = "WIFI-PASSWORD";
const char* mqtt_server = "MQTT-SERVER";

// LCD setup
LiquidCrystal_I2C lcd(0x27, 16, 2);

// MQTT client
WiFiClient espClient;
PubSubClient client(espClient);

void scrollText(String text) {
  lcd.clear();
  text = " " + text + " ";  // Padding for smooth scroll

  for (int i = 0; i < text.length() - 15; i++) {
    lcd.setCursor(0, 0);
    lcd.print(text.substring(i, i + 16));
    delay(400);  // Scroll speed
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, "game/room/description") == 0) {
    String description = "";
    for (unsigned int i = 0; i < length; i++) {
      description += (char)payload[i];
    }
    scrollText(description);
  }
}

void sendMove(char direction) {
  String dirStr(1, direction);
  client.publish("game/player/move", dirStr.c_str());
}

void reconnect() {
  while (!client.connected()) {
    Serial.println("Attempting MQTT connection...");
    String clientId = "ESP32-MUD-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("MQTT connected.");
      client.subscribe("game/room/description");
    } else {
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.println(" retrying in 5 seconds...");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Booting...");

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.println("\nWiFi connected.");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  Wire.begin(14, 13); // Adjust pins if necessary
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("MUD Ready");
  delay(2000);
  lcd.clear();
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (Serial.available() > 0) {
    char input = Serial.read();
    input = tolower(input);
    if (input == 'n' || input == 'e' || input == 's' || input == 'w') {
      Serial.printf("Sending move: %c\n", input);
      sendMove(input);
    }
  }
}
