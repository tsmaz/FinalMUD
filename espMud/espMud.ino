#include <WiFi.h>
#include <PubSubClient.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

#define GRID_SIZE 4

// Wi-Fi and MQTT configuration - CHANGE THESE
const char* ssid = "Airplow";
const char* password = "tonytony";
const char* host_ip = "34.102.16.54";
WiFiClient sock; 

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
    delay(200);  // Scroll speed
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  // Handle receiving and printing room descriptions
  if (strcmp(topic, "game/room/description") == 0) {
    String description = "";
    for (unsigned int i = 0; i < length; i++) {
      description += (char)payload[i];
    }
    scrollText(description);
  }
  // Handle general server-to-client messages
  else if (strcmp(topic, "game/message/serverToClient") == 0) {
     String message = "";
    for (unsigned int i = 0; i < length; i++) {
      message += (char)payload[i];
    }
    scrollText(message);
  }
  // Handle receiving and printing grid
  else if (strcmp(topic, "game/dungeon/grid") == 0 && length == GRID_SIZE*GRID_SIZE) {
    uint8_t grid[GRID_SIZE][GRID_SIZE];
    for (unsigned int i = 0; i < length; ++i) {
      int row = i / GRID_SIZE;
      int col = i % GRID_SIZE;

      grid[row][col] = payload[i];
    }

    Serial.println("  0 1 2 3");
    Serial.println(" +--------+");
    for (int r = 0; r < GRID_SIZE; ++r) {
      Serial.print(r);
      Serial.print("|");
      for (int c = 0; c < GRID_SIZE; ++c) {
        char symbol;
        switch (grid[r][c]) {
          case 1: symbol = '*'; break;  // current
          case 2: symbol = 'S'; break;  // start
          case 3: symbol = 'C'; break;  // connector
          case 4: symbol = 'I'; break;  // item
          case 5: symbol = 'R'; break;  // room
          default: symbol = ' ';        // empty
        }
        Serial.print(symbol);
        Serial.print(' ');
      }
      Serial.println("|");
    }
    Serial.println(" +--------+");
  }
}

void sendMove(char direction) {
  sock.write(direction);
}

void reconnect() {
  while (!client.connected()) {
    Serial.println("Attempting MQTT connection...");
    String clientId = "ESP32-MUD-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("MQTT connected.");
      client.subscribe("game/room/description");
      client.subscribe("game/dungeon/grid");  
      client.subscribe("game/message/serverToClient");
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

  client.setServer(host_ip, 1883);
  client.setCallback(callback);

  Wire.begin(14, 13); // Adjust pins if necessary
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("MUD Ready");
  delay(5000);
  lcd.clear();
  reconnect();
  sock.connect(host_ip, 4000);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (Serial.available() > 0) {
    char input = Serial.read();
    input = tolower(input);
    if (input == 'n' || input == 'e' || input == 's' || input == 'w' || input == 'd') {
      Serial.printf("Sending move: %c\n", input);
      sendMove(input);
    }
    else if (input == 't') {
      Serial.printf("Testing LCD.");
      scrollText("Hello World!");
    }
  }
}
