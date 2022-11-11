#include <SPI.h>
#include <string>
#include <Stepper.h>
#include <ArduinoJson.h>
#include <ArduinoHttpClient.h>
#include <ArduinoMqttClient.h>
#include "arduino_secret.h"
#include <WiFiWebServer.h>
#include <MFRC522v2.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h>
#include <MFRC522Debug.h>

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

//wifi config
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;
char hostname[] = LOCK_HOST;
String lockID;
int status = WL_IDLE_STATUS;

//RFID config
//MFRC522 rfid(SS_PIN, RST_PIN); // Instance of the class
//MFRC522::MIFARE_Key key;
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
bool paired = false;
bool locked = false;
bool calibrated = false;
int buttonState = 0; 
int switchState = 0;
int selectedModule = -1; // 0: default, 1: RFID, 2: Keypad


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  while (!Serial) {
    ;
  }

  myStepper.setSpeed(700);

  //Init Wifi
  initWiFi();
  SPI.begin(); // Init SPI bus

  // Init MFRC522 
  reader.PCD_Init(); // Init MFRC522  
  
  //Init server  
  server.on(F("/pairing"), HTTP_POST, pairingHandler);
  server.on(F("/status"), HTTP_POST, statusHandler);
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

}

//testing loop
void test_loop() {
  
  digitalWrite(BUZZER_PIN, HIGH);

  //test RGB LED
  Serial.println("Green Light");
  ledController(F("UNLOCK"));
  //buzzerController(F("UNLOCK"));
  delay(5000);

  Serial.println("Red Light");
  ledController(F("LOCK"));
  //buzzerController(F("LOCK"));
  delay(5000);

  Serial.println("Yellow Light");
  ledController("UNPAIR");
  delay(5000);

  Serial.println("Warning Light");
  ledController(F("WARN"));
  //buzzerController(F("WARN"));
  delay(5000);
  

  /*
  Serial.println(F("LOCK"));
  buzzerController(F("LOCK"));
  delay(10000);

  Serial.println(F("UNLOCK"));
  buzzerController(F("UNLOCK"));
  delay(10000);

  Serial.println(F("WARN"));
  buzzerController(F("WARN"));
  delay(10000);
  */

  /*
  calibrateStepper();
  if(calibrated == true){
    Serial.println(lockStep);
    buttonListener();
  }
  */

  /*
  //test stepper motor
  myStepper.step(STEP_PRE_OUT_REV/4);
  delay(1000);
  myStepper.step(-STEP_PRE_OUT_REV/4);
  delay(1000);
  myStepper.step(STEP_PRE_OUT_REV/2);
  delay(1000);
  myStepper.step(-STEP_PRE_OUT_REV/2);
  */

  /*
  String uid = rfidListener();
  while (""==uid){
    uid = rfidListener();
    Serial.println("waiting");
    delay(5000);
  }
  Serial.println("Uid:" + uid);

  //test push button
  buttonState = digitalRead(BUTTON_PIN);
  while (buttonState == LOW) {
    Serial.println("Not Pressed");

    buttonState = digitalRead(BUTTON_PIN);
    if (buttonState == HIGH) {
      Serial.println("Pressed");
    }

    delay(1000);
  }
  */

  /*
  //test magnetic door switch
  switchState = digitalRead(SWITCH_PIN);
  while (switchState == LOW) {
    Serial.println("Door opened");

    switchState = digitalRead(SWITCH_PIN);
    if (switchState == HIGH) {
      Serial.println("Door closed");
    }

    delay(1000);
  }
  */

}

void loop() {

  if (!paired){
    //waiting for pairing
    ledController("UNPAIR");
    server.handleClient();
    return;
  } 

  ledController("OFF");

  if (!calibrated){
    //waiting for calibrate the stepper motor
    calibrateStepper();
    return;
  }

  String uid = rfidListener();
  //request verification from control hub if uid != NULL
  if (""!=uid){
    verification(uid, lockID);
  }

  doorSwitchListener();
  buttonListener();
  publish_status();
  server.handleClient();
  ledController("OFF");

}

void initWiFi() {
  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println(F("Communication with WiFi module failed!"));
    // don't continue
    while (true);
  }

  WiFi.setHostname(hostname);
  // attempt to connect to Wifi network:
  while (status != WL_CONNECTED) {
    Serial.print(F("Attempting to connect to SSID: "));
    Serial.println(ssid);
    status = WiFi.begin(ssid, pass);
    // wait 10 seconds for connection:
    delay(10000);
  }

  byte mac[6];
  WiFi.macAddress(mac);

  for(int i=sizeof(mac)-1 ; i>=0 ; i--){
    lockID.concat(String(mac[i], HEX));
  }

  Serial.println(F("Connected to wifi"));
}

String rfidListener(){
  if(!locked){
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
  String url = String(serverAddress + "/lock/verify");

  client.post(url, contentType, msg);

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
  if(calibrated == true){
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
      calibrated = true;
      locked = false;
    }
  }
  
}

void updateStatus(String status){
  if(F("LOCK") == status){
    locked=true;
  } 
  else if (F("UNLOCK") == status){
    locked=false;
  }

}

void doorSwitchListener(){

  if(locked){
    return;
  }

  switchState = digitalRead(SWITCH_PIN);
  int count = 0;

  while (switchState == LOW && count < 5 && !locked) {
    Serial.println("Door opened");
    buttonListener();
    switchState = digitalRead(SWITCH_PIN);
    if (switchState == HIGH || locked) {
      Serial.println("Door closed");
      count = 0;
      break;
    } else {
      count += 1;
    }
    delay(1000);
  }

  if (count == 5){ // warning after 5 seconds
    while (!locked && switchState == LOW){
      ledController(F("WARN"));
      buzzerController(F("WARN"));
      buttonListener();
      switchState = digitalRead(SWITCH_PIN);
      if (switchState == HIGH){
        Serial.println("Door closed");
        break;
      }
    }
  }

  if (switchState == HIGH && !locked) {
    Serial.println(F("Door closed but not lock yet"));
    //auto lock after 3 seconds
    delay(3000); 
    motorController(F("LOCK"));
    ledController(F("LOCK"));
    buzzerController(F("LOCK"));
    Serial.println(F("Door locked"));

  }
}

void buttonListener(){
  buttonState = digitalRead(BUTTON_PIN);
  while (buttonState == HIGH) {
    buttonState = digitalRead(BUTTON_PIN);
    if(buttonState == LOW){ //Pressed and released the button
      if(!locked){
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
  } else if("OFF" == mode){
    rgbColor(0, 0, 0);
  } else {
    rgbColor(255, 255, 255);
  }
}

void pairingHandler(){
  Serial.println(F("Received pairing request"));

  if(paired){
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
    paired = true;

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
  if(!paired || !calibrated){
    server.send(403, F("application/json"), F("{\"message\":\"Not paired or calibrated device.\"}"));
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

  if(F("LOCK") == newStatus){
    motorController(F("LOCK"));
    ledController(F("LOCK"));
    buzzerController(F("LOCK"));
    server.send(200, F("application/json"), F("{\"message\":\"Success\"}"));
  } else if(F("UNLOCK") == newStatus){
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

    if(locked){
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

