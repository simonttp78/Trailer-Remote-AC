#include <WiFi.h>                                     // needed to connect to WiFi
#include <ESPAsyncWebServer.h>                        // needed to create a simple webserver (make sure tools -> board is set to ESP32, otherwise you will get a "WebServer.h: No such file or directory" error)
//#include <WebSocketsServer.h>                       // needed for instant communication between client and server through Websockets
#include <ArduinoJson.h>                              // needed for JSON encapsulation (send multiple variables with one string)
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoOTA.h>

// Data wire is connected to GPIO 4
#define ONE_WIRE_BUS 4
#define BTN_UP_PIN 2
#define BTN_DOWN_PIN 15


// SSID and password of Wifi connection:
const char* ssid = "Woof-Trail";
const char* password = "Flyball-Woof!!!";
unsigned int uiLastProgress = 0; // last % OTA progress value

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature sensor 
DallasTemperature sensors(&oneWire);

// Variables to store temperature values
String temperatureC = "";

// Timer variables
unsigned long lastTime = 0;  
unsigned long timerDelay = 3000;

// The String below "webpage" contains the complete HTML code that is sent to the client whenever someone connects to the webserver
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1" charset="utf-8">
  <link rel="stylesheet" href="https://use.fontawesome.com/releases/v5.7.2/css/all.css" integrity="sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr" crossorigin="anonymous">
  <style>
    html {
     font-family: Arial;
     display: inline-block;
     margin: 0px auto;
     text-align: center;
    }
    h2 { font-size: 2.5rem; }
    p { font-size: 3.0rem; }
    .units { font-size: 1.5rem; }
    .ds-labels{
      font-size: 2rem;
      vertical-align:middle;
      padding-bottom: 5px;
    }
    .buttonUPDOWN {
        background-color: #4CAF50; /* Green */
        color: white;
        padding: 15px 22px;
        text-align: center;
        text-decoration: none;
        display: inline-block;
        cursor: pointer;
        font-size: 24px;
    }
    .button1 {background-color: #008CBA; border: 2px solid blue;} /* Blue */
    .button2 {background-color: #FFBF00; border: 2px solid red;}  /* Amber */
  </style>
</head>

<body>
  <h2>Przyczepa WOOF</h2>
  <p>
    <i class="fas fa-thermometer-half" style="color:#059e8a;"></i> 
    <span class="ds-labels">Temperatura</span>
    <sup class="units">&deg;C</sup>
  </p>
  <p> 
    <span id='temp'></span>
  </p>
  <p>
  <button type='button' class="buttonUPDOWN button1" id='BTN_UP_SEND_BACK'> Wentylator &uarr;</button>
  </p>
  <p>
  <button type='button' class="buttonUPDOWN button2" id='BTN_DOWN_SEND_BACK'> Wentylator &darr;</button>
  </p>
</body>

<script>
  var Socket;
  document.getElementById('BTN_UP_SEND_BACK').addEventListener('click', button_up_send_back);
  document.getElementById('BTN_DOWN_SEND_BACK').addEventListener('click', button_down_send_back);
  function init() {
    Socket = new WebSocket('ws://' + window.location.hostname + '/ws');
    Socket.onmessage = function(event) {
      processCommand(event);
    };
  }
  function button_up_send_back() {
    var msg = {	Przycisk: 'UP' };
	Socket.send(JSON.stringify(msg));
  }
  function button_down_send_back() {
    var msg = {	Przycisk: 'DOWN' };
    Socket.send(JSON.stringify(msg));
  }
  function processCommand(event) {
	var obj = JSON.parse(event.data);
	document.getElementById('temp').innerHTML = obj.temp;
    console.log(obj.temp);
  }
  window.onload = function(event) {
    init();
  }
</script>
</html>
)rawliteral";

// The JSON library uses static memory, so this will need to be allocated:
// -> in the video I used global variables for "doc_tx" and "doc_rx", however, I now changed this in the code to local variables instead "doc" -> Arduino documentation recomends to use local containers instead of global to prevent data corruption

// We want to periodically send values to the clients, so we need to define an "interval" and remember the last time we sent data to the client (with "previousMillis")
//int interval = 1000;                                  // send data to the client every 1000ms -> 1s
//unsigned long previousMillis = 0;                     // we use the "millis()" command for time reference and this will output an unsigned long

// Initialization of webserver and websocket
AsyncWebServer server(80);                            // the server uses port 80 (standard port for websites
AsyncWebSocket ws("/ws");                             // the websocket uses /ws


String readDSTemperatureC() {
  // Call sensors.requestTemperatures() to issue a global temperature and Requests to all devices on the bus
  sensors.requestTemperatures(); 
  float tempC = sensors.getTempCByIndex(0);

  if(tempC == -127.00) {
    Serial.println("Failed to read from DS18B20 sensor");
    return "--";
  } else {
    Serial.print("Temperatura (st.C) ");
    Serial.println(tempC); 
  }
  return String(tempC);
}

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_DISCONNECT)
    Serial.println("Client " + String(client->id()) + " disconnected");
  else if (type == WS_EVT_CONNECT)
    Serial.println("Client " + String(client->id()) + " connected");
  else if (type == WS_EVT_DATA)
  {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    String msg = "";
    if (info->final && info->index == 0 && info->len == len)
    {
      // the whole message is in a single frame and we got all of it's data
      // log_d("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT) ? "text" : "binary", info->len);

      if (info->opcode == WS_TEXT)
      {
          data[len] = 0;
          msg = (char *)data;
      }
      else
      {
          char buff[3];
          for (size_t i = 0; i < info->len; i++)
          {
            sprintf(buff, "%02x ", (uint8_t)data[i]);
            msg += buff;
          }
      }
      //log_d("%s", msg.c_str());
    }
    else
    {
      // message is comprised of multiple frames or the frame is split into multiple packets
      if (info->index == 0)
      {
          if (info->num == 0)
            log_d("ws[%s][%u] %s-message start", server->url(), client->id(), (info->message_opcode == WS_TEXT) ? "text" : "binary");
          log_d("ws[%s][%u] frame[%u] start[%llu]", server->url(), client->id(), info->num, info->len);
      }

      log_d("ws[%s][%u] frame[%u] %s[%llu - %llu]: ", server->url(), client->id(), info->num, (info->message_opcode == WS_TEXT) ? "text" : "binary", info->index, info->index + len);

      if (info->opcode == WS_TEXT)
      {
          data[len] = 0;
          msg = (char *)data;
      }
      else
      {
          char buff[3];
          for (size_t i = 0; i < info->len; i++)
          {
            sprintf(buff, "%02x ", (uint8_t)data[i]);
            msg += buff;
          }
      }
      log_d("%s", msg.c_str());

      if ((info->index + len) == info->len)
      {
          log_d("ws[%s][%u] frame[%u] end[%llu]", server->url(), client->id(), info->num, info->len);
          if (info->final)
            log_d("ws[%s][%u] %s-message end", server->url(), client->id(), (info->message_opcode == WS_TEXT) ? "text" : "binary");
      }
    }
    // try to decipher the JSON string received
    StaticJsonDocument<200> doc;                    // create a JSON container
    DeserializationError error = deserializeJson(doc, msg);
    if (error)
    {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }
    else
    {
      // JSON string was received correctly, so information can be retrieved:
      const char* button = doc["Przycisk"];
      Serial.println("Received action from client: " + String(client->id()));
      Serial.println("Przycisk: " + String(button));
      if (String(button) == "UP")
      {
        digitalWrite(BTN_UP_PIN, HIGH);
        vTaskDelay(500);
        digitalWrite(BTN_UP_PIN, LOW);
      }
      else if (String(button) == "DOWN")
      {
        digitalWrite(BTN_DOWN_PIN, HIGH);
        vTaskDelay(500);
        digitalWrite(BTN_DOWN_PIN, LOW);
      }
    }
  }
}

void setup() {
  Serial.begin(115200);                               // init serial port for debugging

  // Start up the DS18B20 library
  sensors.begin();

  temperatureC = readDSTemperatureC();

  pinMode(BTN_UP_PIN, OUTPUT);
  pinMode(BTN_DOWN_PIN, OUTPUT);
 
  // Connect to Wi-Fi network with SSID and password
  Serial.print("Setting AP (Access Point)…");
  // Remove the password parameter, if you want the AP (Access Point) to be open
  WiFi.softAP(ssid, password);

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  // OTA setup
   ArduinoOTA.setPassword(password);
   ArduinoOTA.setPort(3232);
   ArduinoOTA.onStart([](){
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
        type = "Firmware";
    else // U_SPIFFS
        type = "Filesystem";
    Serial.println("\n" + type + " update initiated."); });
    ArduinoOTA.onEnd([](){ 
        Serial.println("\nUpdate completed.\r\n"); });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total){
        uint16_t iProgressPercentage = (progress / (total / 100));
        if (uiLastProgress != iProgressPercentage)
        {
            Serial.printf("Progress: %u%%\r", iProgressPercentage);
            String sProgressPercentage = String(iProgressPercentage);
            while (sProgressPercentage.length() < 3)
            sProgressPercentage = " " + sProgressPercentage;
            uiLastProgress = iProgressPercentage;
        } });
    ArduinoOTA.onError([](ota_error_t error){
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed"); });
    ArduinoOTA.begin();

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });
  
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
 
  server.begin();                                     // start server
}

void loop() {
    if ((millis() - lastTime) > timerDelay) { // check if "interval" ms has passed since last time the clients were updated
        temperatureC = readDSTemperatureC();
        String jsonString = "";                           // create a JSON string for sending data to the client
        StaticJsonDocument<200> doc;                      // create a JSON container
        JsonObject object = doc.to<JsonObject>();         // create a JSON Object
        object["temp"] = temperatureC;                    // write data into the JSON object
        size_t len = measureJson(doc);
        AsyncWebSocketMessageBuffer *wsBuffer = ws.makeBuffer(len);
        serializeJson(doc, (char *)wsBuffer->get(), len + 1);
        ws.textAll(wsBuffer);
        Serial.println((char *)wsBuffer->get());                       // print JSON string to console for debug purposes (you can comment this out)
        lastTime = millis();
    }
    // Handle OTA update if incoming
    ArduinoOTA.handle();
}
