/*
  Rui Santos
  Complete project details at https://RandomNerdTutorials.com/esp8266-esp-now-wi-fi-web-server/
  
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files.
  
  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
*/

#include <espnow.h>
#include <ESP8266WiFi.h>
#include "ESPAsyncWebServer.h"
#include "ESPAsyncTCP.h"
#include <Arduino_JSON.h>
#include <ArduinoJson.h>

#include <BMP180advanced.h>
#include <GyverOLED.h>

#include <NTPClient.h>
#include <WiFiUdp.h>

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7200, 60000); // UTC+2 (для України)

GyverOLED<SSD1306_128x64, OLED_NO_BUFFER> oled;
BMP180advanced myBMP(BMP180_ULTRAHIGHRES);
#define MY_PERIOD 5000  // период в мс
uint32_t tmr1;

// Replace with your network credentials (STATION)
const char *ssid = "ASUS999";
const char *password = "2341515m";

// Structure example to receive data
// Must match the sender structure
typedef struct struct_message {
  int id;
  float temp;
  float hum;
  unsigned int readingId;
} struct_message;

// Создаем struct_message с именем incomingReadings
struct_message incomingReadings;
struct_message board1;
struct_message board2;
struct_message boardsStruct[2] = { board1, board2 };

JSONVar board;

AsyncWebServer server(80);
AsyncEventSource events("/events");

struct TempRecord {
  float temperature;
  String time;
};

TempRecord tempData[144];  // Буфер для 144 вимірювань (24 години)
int dataIndex = 0;
bool bufferFull = false;

unsigned long lastSyncTime = 0;  // Останній успішний запит часу
unsigned long deviceTime = 0;    // Відлік часу без інтернету (секунди)

// Функція отримання часу
String getFormattedTime() {
  unsigned long currentTime;
  
  if (WiFi.status() == WL_CONNECTED) {
    timeClient.update();
    currentTime = timeClient.getEpochTime();
    lastSyncTime = millis();
    deviceTime = currentTime;  // Оновлюємо локальний таймер
  } else {
    // Якщо інтернету немає, рахуємо час самі
    currentTime = deviceTime + (millis() - lastSyncTime) / 1000;
  }

  int hours = (currentTime % 86400L) / 3600;
  int minutes = (currentTime % 3600) / 60;
  char timeStr[6];
  sprintf(timeStr, "%02d:%02d", hours, minutes);
  return String(timeStr);
}




// Функція вимірювання температури
void measureTemperature() {
  //sensors.requestTemperatures();
  float temp = boardsStruct[0].temp;

  tempData[dataIndex].temperature = temp;
  tempData[dataIndex].time = getFormattedTime();

  dataIndex++;
  if (dataIndex >= 144) {
    dataIndex = 0;
    bufferFull = true;
  }
}



// Обробник для відправки JSON з температурою
String getTempJson() {
  DynamicJsonDocument doc(4096);
  JsonArray array = doc.createNestedArray("data");

  for (int i = 0; i < dataIndex; i++) {
    JsonObject obj = array.createNestedObject();
    obj["time"] = tempData[i].time;
    obj["temperature"] = tempData[i].temperature;
  }










  String json;
  serializeJson(doc, json);
  return json;
}



// Обробник для /history
String getHistoryJson() {
  DynamicJsonDocument doc(4096);
  JsonArray array = doc.createNestedArray("history");

  if (bufferFull) {
    for (int i = 0; i < 144; i++) {
      int index = (dataIndex + i) % 144;
      JsonObject obj = array.createNestedObject();
      obj["time"] = tempData[index].time;
      obj["temperature"] = tempData[index].temperature;
    }
  } else {
    for (int i = 0; i < dataIndex; i++) {
      JsonObject obj = array.createNestedObject();
      obj["time"] = tempData[i].time;
      obj["temperature"] = tempData[i].temperature;
    }
  }

  String json;
  serializeJson(doc, json);
  return json;
}

// Функція для пошуку максимальної температури
float getMaxTemperature() {
  if (dataIndex == 0) return NAN; // Якщо ще немає вимірювань

  float maxTemp = tempData[0].temperature;
  for (int i = 1; i < dataIndex; i++) {
    if (tempData[i].temperature > maxTemp) {
      maxTemp = tempData[i].temperature;
    }
  }
  return maxTemp;
}


// Функція для отримання мінімальної температури
float getMinTemperature() {
  if (dataIndex == 0 && !bufferFull) return NAN;  // Якщо немає вимірювань
  float minTemp = tempData[0].temperature;
  
  int limit = bufferFull ? 144 : dataIndex;
  for (int i = 1; i < limit; i++) {
    if (tempData[i].temperature < minTemp) {
      minTemp = tempData[i].temperature;
    }
  }
  return minTemp;
}

// Обробник для /minmax
String getMinMaxJson() {
  DynamicJsonDocument doc(256);
  doc["minTemp"] = getMinTemperature();
  doc["maxTemp"] = getMaxTemperature();

  String json;
  serializeJson(doc, json);
  return json;
}





// HTML + JavaScript для відображення графіка
const char indexgr_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Grafik Temp</title>
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
</head>
<body>
  <h2>TempTime</h2>
  <canvas id="tempChart"></canvas>
  
  <script>
    async function fetchTemperature() {
      const response = await fetch('/temperature');
      const data = await response.json();
      
      const labels = data.data.map(entry => entry.time);
      const values = data.data.map(entry => entry.temperature);

      new Chart(document.getElementById("tempChart"), {
        type: "line",
        data: {
          labels: labels,
          datasets: [{
            label: "Temp  (°C)",
            data: values,
            borderColor: "red",
            fill: false
          }]
        },
        options: {
          responsive: true,
          scales: {
            x: { title: { display: true, text: "Time" } },
            y: { title: { display: true, text: "Temp (°C)" }, suggestedMin: 0, suggestedMax: 40 }
          }
        }
      });
    }
    
    fetchTemperature();
    setInterval(fetchTemperature, 600000); // Оновлення кожні 10 хв
  </script>
</body>
</html>
)rawliteral";


















// callback function that will be executed when data is received
void OnDataRecv(uint8_t *mac_addr, uint8_t *incomingData, uint8_t len) {
  // Copies the sender mac address to a string
  char macStr[18];
  Serial.print("Packet received from: ");
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.println(macStr);
  memcpy(&incomingReadings, incomingData, sizeof(incomingReadings));

  boardsStruct[incomingReadings.id - 1].temp = incomingReadings.temp;
  boardsStruct[incomingReadings.id - 1].hum = incomingReadings.hum;

  board["id"] = incomingReadings.id;
  board["temperature"] = incomingReadings.temp;
  board["humidity"] = incomingReadings.hum;
  board["readingId"] = String(incomingReadings.readingId);
  String jsonString = JSON.stringify(board);
  events.send(jsonString.c_str(), "new_readings", millis());

  /* Serial.printf("Board ID %u: %u bytes\n", incomingReadings.id, len);
  Serial.printf("t value: %4.2f \n", incomingReadings.temp);
  Serial.printf("h value: %4.2f \n", incomingReadings.hum);
  Serial.printf("readingID value: %d \n", incomingReadings.readingId);
  Serial.println();
  */
}

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>ESP-NOW DASHBOARD</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="https://use.fontawesome.com/releases/v5.7.2/css/all.css" integrity="sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr" crossorigin="anonymous">
  <link rel="icon" href="data:,">
  <style>
    html {font-family: Arial; display: inline-block; text-align: center;}
    h1 {  font-size: 2rem;}
    body {  margin: 0;}
    .topnav { overflow: hidden; background-color: #2f4468; color: white; font-size: 1.7rem; }
    .content { padding: 20px; }
    .card { background-color: white; box-shadow: 2px 2px 12px 1px rgba(140,140,140,.5); }
    .cards { max-width: 700px; margin: 0 auto; display: grid; grid-gap: 2rem; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); }
    .reading { font-size: 2.8rem; }
    .timestamp { color: #bebebe; font-size: 1rem; }
    .card-title{ font-size: 1.2rem; font-weight : bold; }
    .card.temperature { color: #B10F2E; }
    .card.humidity { color: #50B8B4; }
  </style>
</head>
<body>
  <div class="topnav">
    <h1>ESP-NOW DASHBOARD</h1>
  </div>
  <div class="content">
    <div class="cards">
      <div class="card temperature">
        <p class="card-title"><i class="fas fa-thermometer-half"></i> BOARD #1 - TEMPERATURE</p><p><span class="reading"><span id="t1"></span> &deg;C</span></p><p class="timestamp">Last Reading: <span id="rt1"></span></p>
      </div>
      <div class="card humidity">
        <p class="card-title"><i class="fas fa-tint"></i> BOARD #1 - HUMIDITY</p><p><span class="reading"><span id="h1"></span> &percnt;</span></p><p class="timestamp">Last Reading: <span id="rh1"></span></p>
      </div>
      <div class="card temperature">
        <p class="card-title"><i class="fas fa-thermometer-half"></i> BOARD #2 - TEMPERATURE</p><p><span class="reading"><span id="t2"></span> &deg;C</span></p><p class="timestamp">Last Reading: <span id="rt2"></span></p>
      </div>
      <div class="card humidity">
        <p class="card-title"><i class="fas fa-tint"></i> BOARD #2 - HUMIDITY</p><p><span class="reading"><span id="h2"></span> &percnt;</span></p><p class="timestamp">Last Reading: <span id="rh2"></span></p>
      </div>
    </div>
  </div>
<script>
function getDateTime() {
  var currentdate = new Date();
  var datetime = currentdate.getDate() + "/"
  + (currentdate.getMonth()+1) + "/"
  + currentdate.getFullYear() + " at "
  + currentdate.getHours() + ":"
  + currentdate.getMinutes() + ":"
  + currentdate.getSeconds();
  return datetime;
}
if (!!window.EventSource) {
 var source = new EventSource('/events');
 
 source.addEventListener('open', function(e) {
  console.log("Events Connected");
 }, false);
 source.addEventListener('error', function(e) {
  if (e.target.readyState != EventSource.OPEN) {
    console.log("Events Disconnected");
  }
 }, false);
 
 source.addEventListener('message', function(e) {
  console.log("message", e.data);
 }, false);
 
 source.addEventListener('new_readings', function(e) {
  console.log("new_readings", e.data);
  var obj = JSON.parse(e.data);
  document.getElementById("t"+obj.id).innerHTML = obj.temperature.toFixed(2);
  document.getElementById("h"+obj.id).innerHTML = obj.humidity.toFixed(2);
  document.getElementById("rt"+obj.id).innerHTML = getDateTime();
  document.getElementById("rh"+obj.id).innerHTML = getDateTime();
 }, false);
}
</script>
</body>
</html>)rawliteral";


// HTML сторінка у PROGMEM
const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="uk">
<head>
  <meta charset="utf-8">
  <title>Історія температури</title>
  <script>
    async function fetchHistory() {
      const response = await fetch('/history');
      const data = await response.json();
      
      let table = "<table border='1'><tr><th>Час</th><th>Температура (°C)</th></tr>";
      data.history.forEach(entry => {
        table += `<tr><td>${entry.time}</td><td>${entry.temperature}°C</td></tr>`;
      });
      table += "</table>";

      document.getElementById("history").innerHTML = table;
    }

    async function fetchMinMax() {
      const response = await fetch('/minmax');
      const data = await response.json();

      document.getElementById("minTemp").innerText = `Мінімальна: ${data.minTemp}°C`;
      document.getElementById("maxTemp").innerText = `Максимальна: ${data.maxTemp}°C`;
    }

    window.onload = function() {
      fetchHistory();
      fetchMinMax();
    };
  </script>
</head>
<body>
  <h2>Історія температури</h2>
  <div id="history">Завантаження...</div>

  <h3>Мінімальна та максимальна температура</h3>
  <p id="minTemp">Завантаження...</p>
  <p id="maxTemp">Завантаження...</p>
</body>
</html>
)rawliteral";




void setup() {
  // Initialize Serial Monitor
  Serial.begin(115200);
  oled.init();  // инициализация
  oled.setContrast(125);
  oled.clear();      // очистка
  oled.setScale(1);  // масштаб текста (1..4)
  oled.home();       // курсор в 0,0

  // Set the device as a Station and Soft Access Point simultaneously
  WiFi.mode(WIFI_AP_STA);

  // Set device as a Wi-Fi Station
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Setting as a Wi-Fi Station..");
  }
  Serial.print("Station IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Wi-Fi Channel: ");
  Serial.println(WiFi.channel());
  myBMP.begin();
  // Init ESP-NOW
  if (esp_now_init() != 0) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

 if (WiFi.status() == WL_CONNECTED) {
    timeClient.begin();
    timeClient.update();
    deviceTime = timeClient.getEpochTime();  // Початковий час
    lastSyncTime = millis();
  }



  // Once ESPNow is successfully Init, we will register for recv CB to
  // get recv packer info
  esp_now_register_recv_cb(OnDataRecv);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });

  events.onConnect([](AsyncEventSourceClient *client) {
    if (client->lastId()) {
      Serial.printf("Client reconnected! Last message ID that it got is: %u\n", client->lastId());
    }
    // send event with message "hello!", id current millis
    // and set reconnect delay to 1 second
    client->send("hello!", NULL, millis(), 10000);
  });
  server.addHandler(&events);


  // Віддає HTML-сторінку
  server.on("/gr", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", indexgr_html);
  });

  server.on("/history", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", getHistoryJson());
  });

  server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", getTempJson());
  });
  // --- Маршрут для температури ---
  server.on("/temp", HTTP_GET, [](AsyncWebServerRequest *request) {
    //float temperature = getTemperature();
    float board1temp = boardsStruct[0].temp;
    String json = "{\"temp\": " + String(board1temp, 2) + "}";
    // request->send(200, "application/json", json); // Відповідь у форматі JSON
    request->send(200, "application/json; charset=utf-8", json);
  });

   // Обробник для /minmax
  server.on("/minmax", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", getMinMaxJson());
  });
  // Обробник для /hi (повертає HTML-сторінку)
  server.on("/hi", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html; charset=utf-8", htmlPage); 
  });
  server.begin();
}

void loop() {

  static unsigned long lastUpdate = 0;

  static unsigned long lastMeasure = 0;
  if (millis() - lastMeasure >= 600000) {  // Кожні 10 хв (600000 мс)
    lastMeasure = millis();
    measureTemperature();
  }


  if (millis() - tmr1 >= MY_PERIOD) {  // ищем разницу
    tmr1 = millis();                   // сброс таймера
    float board1temp = boardsStruct[0].temp;
    float board1hum = boardsStruct[0].hum;
    float board2temp = boardsStruct[1].temp;
    float board2hum = boardsStruct[1].hum;
    Serial.printf("t value: %4.2f \n", board1temp);
    Serial.printf("h value: %4.2f \n", board1hum);
    Serial.printf("t1 value: %4.2f \n", board2temp);
    Serial.printf("h2 value: %4.2f \n", board2hum);

    oled.setCursor(32, 2);
    oled.print("Temp:");
    oled.print(myBMP.getTemperature());  // записываем в буфер памяти дисплея нашу цифру

    oled.setCursor(32, 3);
    oled.print("PRS:");
    oled.print(myBMP.getPressure_mmHg());

    oled.setCursor(32, 4);
    oled.print("Out:");
    oled.print(board1temp);

    oled.setCursor(32, 5);
    oled.print("Hum:");
    oled.print(board1hum);

    oled.setCursor(32, 6);
    oled.print("Temp:");
    oled.print(board2temp);

    oled.setCursor(32, 7);
    oled.print("Hum:");
    oled.print(board2hum);
  }

  static unsigned long lastEventTime = millis();
  static const unsigned long EVENT_INTERVAL_MS = 5000;
  if ((millis() - lastEventTime) > EVENT_INTERVAL_MS) {
    events.send("ping", NULL, millis());
    lastEventTime = millis();
  }
}
