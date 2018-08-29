#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <SD.h>
#include <SPI.h>

using namespace std;

static const String INDEX_FILE_NAME = "INDEX.TXT";
static const String SAVE_FILE_NAME = "SAVE.TXT";
static const String STORE_FILE_NAME = "STORE.TXT";
static const char *SOFT_AP_NAME = "Rate_WiFi";
static const char *DEVICE_NAME = "Rater";
static const uint8_t LED_RED = D1;
static const uint8_t LED_GRN = D3;
static const uint8_t BUTTON = D2;
static const unsigned long WIFI_TIME = 10 * 60000;
static const unsigned long KILL_TIME = 1 * 1000;
static const unsigned long BUTTON_TIME = 3 * 1000;

WiFiServer server(80);
WiFiClient client;

int ssid_addr = 00;
int pswd_addr = 32;

void setup() {
  Serial.begin(9600);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GRN, OUTPUT);
  digitalWrite(LED_GRN, LOW);
  pinMode(BUTTON, INPUT_PULLUP);
  Serial.println();
  if (!SD.begin(SS)) {
    Serial.println("initialization failed!");
    return;
  }
  Serial.println("SD initialization done.");
}

void put(String _ssid, int addr) {
  int n = _ssid.length();
  for (int i = 0; i < n; i++) {
    EEPROM.write(addr++, _ssid[i]);
  }
}

String get(int addr) {
  string s = "";
  char c = ' ';
  for (int i = 0; c != '\0'; i++) {
    c = EEPROM.read(addr++);
    s[i] = c;
  }
  return s.c_str();
}

void save(String ssid, String password) {
  EEPROM.begin(512);
  put(ssid, ssid_addr);
  put(password, pswd_addr);
  EEPROM.commit();
}

// connect to router
void wifiConnect() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }
  digitalWrite(LED_GRN, LOW);
  File store = SD.open(STORE_FILE_NAME);
  if (store.available()) {
    String ssid = store.readStringUntil('\r');
    store.read();
    String pswd = store.readStringUntil('\r');
    store.flush();
    store.close();
    WiFi.mode(WIFI_STA);
    WiFi.hostname(DEVICE_NAME);
    while (WiFi.status() != WL_CONNECTED) {
      WiFi.begin(ssid.c_str(), pswd.c_str());
      delay(15000);
      Serial.print(".");
    }
    Serial.println();
    Serial.print("connected to router: ");
    Serial.println(ssid);
    Serial.print("assigned IP: ");
    Serial.println(WiFi.localIP());
    digitalWrite(LED_GRN, HIGH);
  }
}

unsigned long press_mem;

void longPress(function<void()> func) {
  if (!digitalRead(BUTTON)) {
    if (press_mem == 0) {
      press_mem = millis() + BUTTON_TIME;
      return;
    }
    if (press_mem < millis()) {
      func();
      press_mem = 0;
      return;
    }
  }
  if (press_mem < millis()) {
    press_mem = 0;
  }
}

unsigned long server_mem;

void keepAlive(bool kill = false) {
  if (kill) {
    server_mem = millis() + KILL_TIME;
    kill = true;
  } else {
    server_mem = millis() + WIFI_TIME;
  }
}

// start AP server
void accessPointServer() {
  keepAlive();
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  if (WiFi.softAP(SOFT_AP_NAME)) {
    Serial.println("wifi active");
    Serial.print("gateway: ");
    Serial.println(WiFi.softAPIP());
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_GRN, LOW);
    server.begin();
  }
}

void loop() {

  // listen for longpressing the button
  longPress(accessPointServer);
  // turn off AP wifi
  if (server_mem < millis()) {
    digitalWrite(LED_RED, LOW);
    wifiConnect();
  } else {
    // wifi is still on, listen for clients
    client = server.available();
    if (client) {
      Serial.println("new incoming request");
      int line = 0;
      String buf;
      String method;
      String src;
      int i;
      while (client.connected()) {
        if (i++ == 5000) {
          break;
        }
        while (client.available()) {
          buf = client.readStringUntil('\n');
          // first line contains method and routing (src)
          if (line++ == 0) {
            int first = buf.indexOf(' ');
            method = buf.substring(0, first);
            int second = buf.indexOf(' ', first + 1);
            src = buf.substring(first + 1, second);
          }
        }
        if (method == "GET" && src == "/") {
          // get index.html
          File index = SD.open(INDEX_FILE_NAME);
          if (index) {
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type: text/html");
            client.println("Connection: close");
            client.println();
            while (index.available()) {
              client.write(index.read());
            }
            index.close();
            client.println();
          } else {
            client.println("HTTP/1.1 500 Internal Server Error");
            client.println("Content-type: text/html");
            client.println();
            client.println("<h2>index could not be retrieved</h2>");
            client.println();
          }
          break;
        } else if (method == "POST" && src == "/save.html") {
          SD.remove(STORE_FILE_NAME);
          File store = SD.open(STORE_FILE_NAME, FILE_WRITE);
          if (store) {
            int and1 = buf.indexOf('&');
            store.println(buf.substring(buf.indexOf('=') + 1, and1));
            store.println(buf.substring(buf.indexOf('=', and1) + 1));
            store.flush();
            store.close();
          } else {
            client.println("HTTP/1.1 500 Internal Server Error");
            client.println("Content-type: text/html");
            client.println();
            client.println("<h2>could not save credentials</h2>");
            client.println();
            break;
          }
          // get save.html
          File save = SD.open(SAVE_FILE_NAME);
          if (save) {
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type: text/html");
            client.println("Connection: close");
            client.println();
            while (save.available()) {
              client.write(save.read());
            }
            save.close();
            client.println();
            keepAlive(true);
          } else {
            client.println("HTTP/1.1 500 Internal Server Error");
            client.println("Content-type: text/html");
            client.println();
            client.println("<h2>save could not be retrieved</h2>");
            client.println();
          }
          break;
        }
        client.flush();
        Serial.flush();
        buf = "";
        method = "";
        src = "";
      }
      // close the connection:
      client.stop();
      delay(1);
      Serial.println("client disconnected");
    }
  }
}