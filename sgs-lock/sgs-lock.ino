#include <SPI.h>
#include <string>
#include <Stepper.h>
#include <ArduinoJson.h>
#include <FlashStorage.h>
#include <WiFiWebServer.h>
#include <ArduinoMqttClient.h>
#include <ArduinoHttpClient.h>

#include <MFRC522v2.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h>

#include "arduino_secret.h"

#define SS_PIN 10
#define RST_PIN 9
#define RED_PIN 2
#define GREEN_PIN 3
#define BLUE_PIN 4
#define BUTTON_PIN 20
#define SWITCH_PIN 15
#define BUZZER_PIN 18
#define STEPPER_PIN_1 8
#define STEPPER_PIN_2 7
#define STEPPER_PIN_3 6
#define STEPPER_PIN_4 5

//lock config on flash memory
typedef struct {
  bool isReady;
  char ssid[30];
  char pass[30];
  bool isStandalone;
} WiFiConfig;

typedef struct {
  bool isReady;
  char serverAddress[30];
  char mqttUserName[30];
  char mqttPassword[30];
  int direction;
  int selectedModule;
  int port;
} PairingConfig;

typedef struct {
  char username[30];
  char password[30];
} AuthConfig;


const char* setupPage = 
#include "setup_page.h"
;

const char* adminPage = 
#include "admin_page.h"
;

const char* resetPage = 
#include "reset_page.h"
;

// Reserve a portion of flash memory to store a "WiFiConfig" and PairingConfig
// call it "wifi_store".
FlashStorage(wifi_store, WiFiConfig);
FlashStorage(pairing_store, PairingConfig);
FlashStorage(auth_store, AuthConfig);

WiFiConfig wiFiConfig;
PairingConfig pairingConfig;
AuthConfig authConfig;

//wifi config
char hostname[] = LOCK_HOST;
char ap_pass[] = AP_PASS;
String lockID;
int status = WL_IDLE_STATUS;

//RFID config
MFRC522DriverPinSimple ss_pin(10); // Configurable, see typical pin layout above.
MFRC522DriverSPI driver{ss_pin}; // Create SPI driver.
MFRC522 reader{driver};  // Create MFRC522 instance.

//MQTT config
WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

const char topic[]  = "sgs-lock/status";
int mqttPort = 1883;

const long interval = 1000;
unsigned long previousMillis = 0;

//API config
WiFiWebServer server(8080);

// initialize the stepper
const int STEP_PRE_REV = 32;
const int GEAR_RED = 64;
const int STEP_PRE_OUT_REV = STEP_PRE_REV * GEAR_RED;
Stepper myStepper(STEP_PRE_REV, STEPPER_PIN_1, STEPPER_PIN_3, STEPPER_PIN_2, STEPPER_PIN_4);
int lockStep = 0;
int direction = 1; // if door direction is left, set it as -1

//http conifg
String serverAddress = "";  // set after pairing
int port = 8080;

//variables
bool isReady = false; 
bool isPaired = false;
bool isLocked = false;
bool isCalibrated = false;
int buttonState = 0; 
int switchState = 0;
int selectedModule = -1; // 0: default, 1: RFID, 2: Keypad
bool isStandalone = false;


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  //while (!Serial) {
  //  ;
  //}

  wiFiConfig = wifi_store.read();

  if(wiFiConfig.isReady) {
    isReady = wiFiConfig.isReady;
    isStandalone = wiFiConfig.isStandalone;

    char ssid[30] = {};
    char pass[30] = {};
    strcpy(ssid, wiFiConfig.ssid);
    strcpy(pass, wiFiConfig.pass);

    pairingConfig = pairing_store.read();

    if(pairingConfig.isReady){
      isPaired = pairingConfig.isReady;
      serverAddress = String(pairingConfig.serverAddress);
      port = pairingConfig.port;
      selectedModule = pairingConfig.selectedModule;
      direction = pairingConfig.direction;

      setMqttConnection(pairingConfig);
    }

    myStepper.setSpeed(700);

    //Init Wifi
    initWiFi(ssid, pass);
    SPI.begin(); // Init SPI bus

    // Init MFRC522
    if (!isStandalone)
      reader.PCD_Init(); // Init MFRC522  
    
    //Init server
    if(!isPaired && !isStandalone){
      server.on(F("/pairing"), HTTP_POST, pairingHandler);
    } 
    
    if(isStandalone){
      server.on(F("/admin"), HTTP_GET, adminHandler);
      server.on(F("/reset"), HTTP_GET, resetGetHandler);
    }

    server.on(F("/status"), HTTP_POST, statusHandler);
    server.on(F("/reset"), HTTP_POST, resetHandler);
    server.onNotFound(notFound);
    server.begin();

    Serial.print(F("HTTP server started @ "));
    Serial.println(WiFi.localIP());
    
    // initialize the outputs:
    pinMode(RED_PIN, OUTPUT);
    pinMode(GREEN_PIN, OUTPUT);
    pinMode(BLUE_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);

    // initialize the inputs:
    pinMode(BUTTON_PIN, INPUT);
    pinMode(SWITCH_PIN, INPUT);
  } else {
    createWiFiAP();
    Serial.print(F("Access Point IP: "));
    Serial.println(WiFi.localIP());
    
    server.on(F("/"), rootHandler);
    server.onNotFound(notFound);
    server.begin();
  }

}

void loop() {

  if(!isReady){
    server.handleClient();
    return;
  }

  if (!isPaired && !isStandalone){
    //waiting for pairing
    ledController("UNPAIR");
    server.handleClient();
    return;
  } 

  ledController("OFF");

  if (!isCalibrated){
    //waiting for calibrate the stepper motor
    calibrateStepper();
    return;
  }

  if(!isStandalone){
    String uid = rfidListener();
    //request verification from control hub if uid != NULL
    if (""!=uid){
      verification(uid, lockID);
    }
  }

  doorSwitchListener();
  buttonListener();
  if(!isStandalone){
    publish_status();
  }
  server.handleClient();
  ledController("OFF");
}

void initWiFi(const char* ssid, const char* pass) {
  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println(F("Communication with WiFi module failed!"));
    // don't continue
    while (true);
  }

  WiFi.setHostname(hostname);
  // attempt to connect to Wifi network:
  Serial.print(F("Attempting to connect to SSID: "));
  Serial.println(ssid);
  status = WiFi.begin(ssid, pass);
  // wait 10 seconds for connection:
  delay(10000);

  //Cannot connect the Wifi, restart the board to collect another wifi credential
  if (status != WL_CONNECTED) {
    wiFiConfig.isReady = false;
    wifi_store.write(wiFiConfig);
    NVIC_SystemReset();
  }

  byte mac[6];
  WiFi.macAddress(mac);

  for(int i=sizeof(mac)-1 ; i>=0 ; i--){
    lockID.concat(String(mac[i], HEX));
  }

  Serial.println(F("Connected to wifi"));
}

void createWiFiAP() {
  Serial.println(F("Creating access point"));
  IPAddress selfIp = IPAddress (192, 168, 10, 1);
  uint8_t ap_channel = 2;
  WiFi.config(selfIp);
  status = WiFi.beginAP(LOCK_HOST, AP_PASS, ap_channel);

  if (status != WL_AP_LISTENING)
  {
    Serial.println(F("Create access point failed"));
    // don't continue
    while (true);
  }
}

String rfidListener(){
  if(!isLocked){
    return "";
  }

  // Reset the loop if no new card present on the sensor/reader. 
  // This saves the entire process when idle.
  if (reader.PICC_IsNewCardPresent() && reader.PICC_ReadCardSerial()){

    String uid = getHexValue(reader.uid.uidByte, reader.uid.size);
    Serial.print(F("uid ="));
    Serial.println(uid);

    // Halt PICC.
    reader.PICC_HaltA();
    // Stop encryption on PCD.
    reader.PCD_StopCrypto1();

    return uid;
  }
  
  return "";
}

void verification(String uid, String requestedBy) {
  HttpClient client = HttpClient(wifiClient, serverAddress, port);

  Serial.println(F("making POST request"));
  String contentType = "application/json";

  StaticJsonDocument<96> msgJson;
  msgJson["cardId"] = uid;
  msgJson["deviceId"] = requestedBy;

  String msg;
  serializeJson(msgJson, msg);
  client.post("/lock/verify", contentType, msg);

  // read the status code and body of the response
  int statusCode = client.responseStatusCode();
  String response = client.responseBody();

  Serial.print(F("Status code: "));
  Serial.println(statusCode);
  Serial.print(F("Response: "));
  Serial.println(response);

  if(200 == statusCode){
    motorController(F("UNLOCK"));
    ledController(F("UNLOCK"));
    buzzerController(F("UNLOCK"));
  }

}

void calibrateStepper(){
  if(isCalibrated == true){
    return;
  }

  // read the state of the pushbutton value:
  buttonState = digitalRead(BUTTON_PIN);

  // check if the pushbutton is pressed. If it is, the buttonState is HIGH:
  while (buttonState == HIGH) {
    myStepper.step(-1*direction);
    lockStep += 1;

    buttonState = digitalRead(BUTTON_PIN);
    if (buttonState == LOW) {
      isCalibrated = true;
      isLocked = false;
    }
  }
  
}

void updateStatus(String status){
  if(F("LOCK") == status){
    isLocked=true;
  } 
  else if (F("UNLOCK") == status){
    isLocked=false;
  }

}

void doorSwitchListener(){

  if(isLocked){
    return;
  }

  switchState = digitalRead(SWITCH_PIN);
  unsigned long previousMillis = 0;

  while (switchState == LOW && !isLocked) {
    Serial.println("Door opened");

    if(previousMillis == 0){
      previousMillis = millis();
    }

    unsigned long currentMillis = millis();

    //warning after 5 seconds
    if(currentMillis - previousMillis >= 5000){
      ledController(F("WARN"));
      buzzerController(F("WARN"));
    }

    buttonListener();
    if(!isStandalone){
      publish_status();
    }
    switchState = digitalRead(SWITCH_PIN);
    if (switchState == HIGH || isLocked) {
      Serial.println("Door closed");
      previousMillis = 0;
      break;
    } 
  }

  if (switchState == HIGH && !isLocked) {
    Serial.println(F("Door closed but not lock yet"));
    //auto lock after 3 seconds
    delay(3000); 
    switchState = digitalRead(SWITCH_PIN);
    if (switchState == HIGH && !isLocked){
      motorController(F("LOCK"));
      ledController(F("LOCK"));
      buzzerController(F("LOCK"));
      Serial.println(F("Door locked"));
    }
  }
}

void buttonListener(){
  buttonState = digitalRead(BUTTON_PIN);
  unsigned long previousMillis = 0;
  while (buttonState == HIGH) {
    buttonState = digitalRead(BUTTON_PIN);
    unsigned long currentMillis = millis();

    if(previousMillis == 0){
      previousMillis = millis();
    } else {
      if (currentMillis - previousMillis >= 10000) { //reset after Pressed the button >= 10s
        resetDevice();
        NVIC_SystemReset();
      }  
    }

    if(buttonState == LOW){ //Pressed and released the button
      previousMillis = 0;
      if(!isLocked){
        motorController(F("LOCK"));
        buzzerController(F("LOCK"));
        ledController(F("LOCK"));
        Serial.println(F("Door locked"));
      } else {
        motorController(F("UNLOCK"));
        buzzerController(F("UNLOCK"));
        ledController(F("UNLOCK"));
        Serial.println(F("Door unlocked"));
      }
    }

  }
}

void motorController(String mode){
  if(F("LOCK") == mode){
    myStepper.step(lockStep*direction);
    updateStatus(F("LOCK"));
  } 
  else if (F("UNLOCK") == mode){
    myStepper.step(lockStep*direction*-1);
    updateStatus(F("UNLOCK"));
  }
}

void buzzerController(String mode){
  if(F("LOCK") == mode){
    newTone(BUZZER_PIN, 1, 500);
  } else if (F("UNLOCK") == mode){
    newTone(BUZZER_PIN, 2, 250);
  } else if (F("WARN") == mode){
    newTone(BUZZER_PIN, 3, 100);
  }
}

void ledController(String mode){
  if(F("LOCK") == mode){
    //show red light
    rgbColor(255, 0, 0);
    delay(500);
    rgbColor(0, 0, 0);
  } else if (F("UNLOCK") == mode){
    //show green light
    rgbColor(0, 255, 0);
    delay(500);
    rgbColor(0, 0, 0);
  } else if("UNPAIR" == mode){
    // show yellow light
    rgbColor(255, 255, 0);
    delay(500);
    rgbColor(0, 0, 0);

  } else if(F("WARN") == mode){
    rgbColor(255, 0, 0);
    delay(250);
    rgbColor(255, 255, 0);
    delay(250);

  } else if(F("RESET") == mode){
    rgbColor(0, 0, 255);
    delay(250);
    rgbColor(0, 255, 255);
    delay(250);
    rgbColor(0, 0, 255);
    delay(250);
    rgbColor(0, 255, 255);
    delay(250);

  } else if("OFF" == mode){
    rgbColor(0, 0, 0);

  } else {
    rgbColor(255, 255, 255);

  }
}

void pairingHandler(){
  Serial.println(F("Received pairing request"));
  
  if(isPaired){
    server.send(403, F("application/json"), F("{\"message\":\"Already paired, please reset the device before pair again.\"}"));
    return;
  }

  if(!server.hasArg("plain")){
    server.send(204, F("application/json"), F("{\"message\":\"Empty request\"}"));
    return;
  }
  
  String requestBody = server.arg("plain");
  Serial.println("requestBody:" + requestBody);
  //DynamicJsonDocument bodyJSON(2048);
  StaticJsonDocument<512> bodyJSON;

  DeserializationError error = deserializeJson(bodyJSON, requestBody);
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());

    server.send(400, F("application/json"), F("{\"message\":\"Invalid Format\"}"));
    return;
  }
  
  serverAddress = (const char*)bodyJSON["serverAddress"]; // "192.192.192.192"
  port = bodyJSON["serverPort"]; // 8080
  direction = bodyJSON["doorDirection"]; // 1
  selectedModule = bodyJSON["selectedModule"]; // 1
  const char* mqttUsername = bodyJSON["mqttUsername"]; 
  const char* mqttPassword = bodyJSON["mqttPassword"]; 

  Serial.print(F("Received serverAddress: "));
  Serial.println(serverAddress);
  Serial.print(F("Received serverPort: "));
  Serial.println(String(port));
  Serial.print(F("Received doorDirection: "));
  Serial.println(String(direction));
  Serial.print(F("Received selectedModule: "));
  Serial.println(String(selectedModule));
  Serial.print(F("Received mqttUsername: "));
  Serial.println(mqttUsername);
  Serial.print(F("Received mqttPassword: "));
  Serial.println(mqttPassword);

  mqttClient.setId(lockID);
  mqttClient.setUsernamePassword(mqttUsername, mqttPassword);

  Serial.print(F("Attempting to connect to the MQTT broker: "));
  Serial.println(serverAddress);

  int strLen = serverAddress.length() + 1; 
  char serverAddressArray[strLen];
  serverAddress.toCharArray(serverAddressArray, strLen);

  if (!mqttClient.connect(serverAddressArray, mqttPort)) {
    Serial.print(F("MQTT connection failed! Error code = "));
    Serial.println(mqttClient.connectError());
    server.send(403, F("application/json"), F("{\"message\":\"Failed to connect MQTT broker, please check and pair again\"}"));
    return;
  }  

  Serial.println(F("You're connected to the MQTT broker!"));

  if(serverAddress != "" && direction != 0 && selectedModule != -1){
    isPaired = true;

    pairingConfig = pairing_store.read();
    pairingConfig.isReady = true;
    strcpy(pairingConfig.serverAddress, serverAddressArray);
    strcpy(pairingConfig.mqttUserName, mqttUsername);
    strcpy(pairingConfig.mqttPassword, mqttUsername);
    pairingConfig.direction = direction;
    pairingConfig.selectedModule = selectedModule;
    pairingConfig.port = port;
    pairing_store.write(pairingConfig);

    
    DynamicJsonDocument msgJson(1024);
    msgJson["deviceID"] = lockID;

    String msg;
    serializeJson(msgJson, msg);

    server.send(200, F("application/json"), msg);
  } else {
    server.send(400, F("application/json"), F("{\"message\":\"Invalid Format\"}"));
  }
}

void statusHandler(){
  if(!isStandalone && (!isPaired || !isCalibrated)){
    server.send(403, F("application/json"), F("{\"message\":\"Not paired or calibrated device.\"}"));
    return;
  }

  authConfig = auth_store.read();
  if(isStandalone && !server.authenticate(authConfig.username, authConfig.password)){
    server.send(401, F("application/json"), F("{\"message\":\"Unauthorized request\"}"));
    return;
  }

  if(!server.hasArg("plain")){
    server.send(204, F("application/json"), F("{\"message\":\"Empty request\"}"));
    return;
  }
  
  String requestBody = server.arg("plain");
  DynamicJsonDocument bodyJSON(1024);

  DeserializationError error = deserializeJson(bodyJSON, requestBody);
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());

    server.send(400, F("application/json"), F("{\"message\":\"Invalid Format\"}"));
    return;
  } 

  String newStatus = bodyJSON["status"];
  Serial.print(F("Received newStatus: "));
  Serial.println(newStatus);

  if(F("LOCK") == newStatus && !isLocked){
    motorController(F("LOCK"));
    ledController(F("LOCK"));
    buzzerController(F("LOCK"));
    server.send(200, F("application/json"), F("{\"message\":\"Success\"}"));
  } else if(F("UNLOCK") == newStatus && isLocked){
    motorController(F("UNLOCK"));
    ledController(F("UNLOCK"));
    buzzerController(F("UNLOCK"));
    server.send(200, F("application/json"), F("{\"message\":\"Success\"}"));
  } else {
    server.send(400, F("application/json"), F("{\"message\":\"Invalid value\"}"));
  }
}

void notFound() {
  server.send(404, F("application/json"), F("{\"message\":\"Not found\"}"));
}

void rootHandler() {
  if (server.hasArg("ssid")&& server.hasArg("password")) {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    String mode = server.arg("mode");
    String standaloneUser = server.arg("standaloneUser");
    String standalonePass = server.arg("standalonePass");

    Serial.print(F("The received WiFi credential: "));
    Serial.println(ssid);
    Serial.print(F("The received mode: "));
    Serial.println(mode);

    wiFiConfig = wifi_store.read();
    ssid.toCharArray(wiFiConfig.ssid, sizeof(ssid));
    password.toCharArray(wiFiConfig.pass, sizeof(password));
    if(F("STANDALONE") == mode){

      if(standaloneUser.length() == 0 ||  standalonePass.length() == 0){
        server.send(200, F("text/html"), setupPage);
        return;
      }

      wiFiConfig.isStandalone = true;
      authConfig = auth_store.read();
      standaloneUser.toCharArray(authConfig.username, sizeof(standaloneUser));
      standalonePass.toCharArray(authConfig.password, sizeof(standalonePass));
      auth_store.write(authConfig);

    } else {
      wiFiConfig.isStandalone = false;
    }
    
    wiFiConfig.isReady = true;
    wifi_store.write(wiFiConfig);

    server.send(201, F("text/plain"), F("The device is going to restart. This access point will be disappear if the WiFi credential is correct."));
    
    NVIC_SystemReset();
  }
  else {
    // If one of the creds is missing, go back to form page
    server.send(200, F("text/html"), setupPage);
  }
}

void adminHandler(){
  authConfig = auth_store.read();
  
  if(!server.authenticate(authConfig.username, authConfig.password)){
    server.requestAuthentication();
  }

  server.send(200, F("text/html"), adminPage);
}

void resetGetHandler(){
  authConfig = auth_store.read();
  
  if(!server.authenticate(authConfig.username, authConfig.password)){
    server.requestAuthentication();
  }

  server.send(200, F("text/html"), resetPage);
}

void resetHandler(){
  authConfig = auth_store.read();
  if(isStandalone && !server.authenticate(authConfig.username, authConfig.password)){
    server.send(401, F("application/json"), F("{\"message\":\"Unauthorized request\"}"));
    return;
  }

  resetDevice();
  server.send(200, F("text/plain"), F("The device is going to restart. Please setup the device from step 1."));
  NVIC_SystemReset();
}

void publish_status() {
  // call poll() regularly to allow the library to send MQTT keep alives which
  // avoids being disconnected by the broker
  mqttClient.poll();

  // to avoid having delays in loop, we'll use the strategy from BlinkWithoutDelay
  // see: File -> Examples -> 02.Digital -> BlinkWithoutDelay for more info
  unsigned long currentMillis = millis();
  
  if (currentMillis - previousMillis >= interval) {
    // save the last time a message was sent
    previousMillis = currentMillis;

    Serial.print(F("Sending message to topic: "));
    Serial.println(topic);

    StaticJsonDocument<96> doc;
    String jsonStr;
    doc["deviceID"] = lockID;

    if(isLocked){
      doc["status"] = F("LOCKED");
    } else {
      doc["status"] = F("UNLOCKED");
    }
    
    serializeJson(doc, jsonStr);

    // send message, the Print interface can be used to set the message contents
    mqttClient.beginMessage(topic);
    mqttClient.println(jsonStr);
    mqttClient.endMessage();
  }
}

//Utils methods
String getHexValue(byte *buffer, byte bufferSize) {
  String result = "";

  for (byte i = 0; i < bufferSize; i++) {
    result.concat(String(buffer[i] < 0x10 ? " 0" : " "));
    result.concat(String(buffer[i], HEX));
  }

  return result;
}

void rgbColor(int red, int green, int blue){
  analogWrite(RED_PIN, 255-red);
  analogWrite(GREEN_PIN, 255-green);
  analogWrite(BLUE_PIN, 255-blue);
}

void newTone(int buzzPin, int freq, int length){
  for(int i = 0; i< freq; i++){
    digitalWrite(buzzPin, HIGH);
    delay(length);
    digitalWrite(buzzPin, LOW);
    delay(1);
  }

}

void setMqttConnection(PairingConfig pairingConfig){
  mqttClient.setId(lockID);
  mqttClient.setUsernamePassword(pairingConfig.mqttUserName, pairingConfig.mqttPassword);

  Serial.print(F("Attempting to connect to the MQTT broker: "));
  Serial.println(pairingConfig.serverAddress);

  if (!mqttClient.connect(pairingConfig.serverAddress, mqttPort)) {
    Serial.print(F("MQTT connection failed! Error code = "));
    Serial.println(mqttClient.connectError());
    pairingConfig.isReady = false;
    pairing_store.write(pairingConfig);

    //reset the pairing status
    NVIC_SystemReset();
  } 

  Serial.println(F("You're connected to the MQTT broker!"));
}

void resetDevice(){
    //lock the door before reset the configs
  if(!isLocked)
    motorController(F("LOCK"));

  ledController(F("RESET"));

  wiFiConfig = wifi_store.read();
  wiFiConfig.isReady = false;
  wifi_store.write(wiFiConfig);

  pairingConfig = pairing_store.read();
  pairingConfig.isReady = false;
  pairing_store.write(pairingConfig);
}