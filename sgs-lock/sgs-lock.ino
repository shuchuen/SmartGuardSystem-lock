#include <SPI.h>
#include <string>
#include <MFRC522.h>
#include <Stepper.h>
#include <WiFiNINA.h>
#include <ArduinoHttpClient.h>
#include <ArduinoMqttClient.h>
#include "arduino_secret.h"


#define SS_PIN 10
#define RST_PIN 9
#define RED_LED_PIN 2
#define GREEN_LED_PIN 3
#define BUTTON_PIN 4
#define SWITCH_PIN 5
#define BUZZER_PIN 6
#define STEPPER_PIN_1 11
#define STEPPER_PIN_2 12
#define STEPPER_PIN_3 13
#define STEPPER_PIN_4 14

//wifi config
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;
char hostname[] = "lock.sgs.1";
int status = WL_IDLE_STATUS;

//RFID config
MFRC522 rfid(SS_PIN, RST_PIN); // Instance of the class
MFRC522::MIFARE_Key key;

//MQTT config
WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

//API config
WiFiServer server(8080);

// initialize the stepper
int stepsPerRevolution = 360;
Stepper myStepper(stepsPerRevolution, STEPPER_PIN_1, STEPPER_PIN_2, STEPPER_PIN_3, STEPPER_PIN_4);
int lockStep = 0;
int direction = 1; // if door direction is left, set it as -1

//http conifg
String serverAddress = "";  // set after pairing
int port = 8080;

//variables 
bool paired = false;
bool locked = true;
bool calibrated = false;
int buttonState = 0; 
int switchState = 0;



void initWiFi() {
  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    // don't continue
    while (true);
  }

  WiFi.setHostname(hostname);
  // attempt to connect to Wifi network:
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    status = WiFi.begin(ssid, pass);
    // wait 10 seconds for connection:
    delay(10000);
  }

  Serial.println("Connected to wifi");
}


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  while (!Serial) {
    ;
  }

  //Init Wifi
  initWiFi();

  // Init MFRC522 
  rfid.PCD_Init(); // Init MFRC522 
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }

  Serial.println(F("This code scan the MIFARE Classsic NUID."));
  Serial.print(F("Using the following key:"));
  String keyValue = getHexValue(key.keyByte, MFRC522::MF_KEY_SIZE);
  Serial.print(keyValue);

  //Init server 
  server.begin();
  Serial.println("Server started");

  // initialize the outputs:
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  // initialize the pushbutton pin as an input:
  pinMode(BUTTON_PIN, INPUT);
  pinMode(SWITCH_PIN, INPUT);

}

void loop() {

  while (!paired){
    //waiting for pairing
    ledController("UNPAIR");
    pairing();

  }

  while (!calibrated){
    //waiting for calibrate the stepper motor
    calibrateStepper();
  }

  rfidListener();
  doorSwitchListener();
  buttonListener();
}


void rfidListener(){
  // Reset the loop if no new card present on the sensor/reader. This saves the entire process when idle.
  if ( ! rfid.PICC_IsNewCardPresent())
    return;

  // Verify if the NUID has been readed
  if ( ! rfid.PICC_ReadCardSerial())
    return;

  Serial.print(F("PICC type: "));
  MFRC522::PICC_Type piccType = rfid.PICC_GetType(rfid.uid.sak);
  Serial.println(rfid.PICC_GetTypeName(piccType));

  // Check is the PICC of Classic MIFARE type
  if (piccType != MFRC522::PICC_TYPE_MIFARE_MINI &&  
    piccType != MFRC522::PICC_TYPE_MIFARE_1K &&
    piccType != MFRC522::PICC_TYPE_MIFARE_4K) {
    Serial.println(F("Your tag is not of type MIFARE Classic."));
    return;
  }

  String uid = getHexValue(rfid.uid.uidByte, rfid.uid.size);
  
  // Halt PICC
  rfid.PICC_HaltA();

  // Stop encryption on PCD
  rfid.PCD_StopCrypto1();
  
  //request verification from control hub
  verification(uid);
}

void verification(String uid) {
  HttpClient client = HttpClient(wifiClient, serverAddress, port);

  Serial.println("making POST request");
  String contentType = "application/x-www-form-urlencoded";
  String postData = "uid=" + uid;

  client.post("/", contentType, postData);

  // read the status code and body of the response
  int statusCode = client.responseStatusCode();
  String response = client.responseBody();

  Serial.print("Status code: ");
  Serial.println(statusCode);
  Serial.print("Response: ");
  Serial.println(response);

 if(200 == statusCode){
  updateStatus("UNLOCK");
  buzzerController("UNLOCK");
  ledController("UNLOCK");
 }

}

void calibrateStepper(){
  // read the state of the pushbutton value:
  buttonState = digitalRead(BUTTON_PIN);

  // check if the pushbutton is pressed. If it is, the buttonState is HIGH:
  while (buttonState == HIGH) {
    myStepper.step(-1*direction);
    lockStep += 1;

    if (buttonState == LOW) {
      calibrated = true;
    }
  }
  
}

void pairing(){
  // Check if a client has connected
  WiFiClient client = server.available();
  if (!client) {
    return;
  }

  // Wait until the client sends some data
  Serial.println("new client");
  while(!client.available()){
    delay(1);
  }

  String req = client.readStringUntil('\r');

  Serial.println(req);
  client.flush();
  // Match the request

  String s = "";
  if (req.indexOf("/pairing") != -1) {
    //handle the request
    s = "HTTP/1.1 200 OK";
    paired = true;
    direction = 1;
    serverAddress = "";
  }
  else {
    Serial.println("invalid request");
    client.stop();
    return;
  }

  client.flush();
  // Send the response to the client
  client.print(s);
  delay(1);
  client.stop();
  Serial.println("Client disonnected");

}

void updateStatus(String status){
  if("LOCK" == status){
    locked=true;
  } 
  else if ("UNLOCK" == status){
    locked=false;
  }

}

void doorSwitchListener(){
  
}

void buttonListener(){

}

void motorController(String mode){
  if("LOCK" == mode){
    myStepper.step(lockStep*direction);
  } 
  else if ("UNLOCK" == mode){
    myStepper.step(lockStep*direction*-1);
  }
}

void buzzerController(String mode){
  if("LOCK" == mode){

  } 
  else if ("UNLOCK" == mode){

  }
}

void ledController(String mode){
  if("LOCK" == mode){

  } else if ("UNLOCK" == mode){

  } else if("UNPAIR" == mode){

  }
}

void apiListener(){
  // Check if a client has connected
  WiFiClient client = server.available();
  if (!client) {
    return;
  }

  // Wait until the client sends some data
  Serial.println("new client");
  while(!client.available()){
    delay(1);
  }

  String req = client.readStringUntil('\r');

  Serial.println(req);
  client.flush();
  // Match the request

  String s = "";
  if (req.indexOf("/status") != -1) {
    //handle the request
    String status = "LOCK";
    updateStatus(status);
    String s = "HTTP/1.1 200 OK";
  }
  else {
    Serial.println("invalid request");
    client.stop();
    return;
  }

  client.flush();
  
  // Send the response to the client
  client.print(s);
  delay(1);
  client.stop();
  Serial.println("Client disonnected");
}


String getHexValue(byte *buffer, byte bufferSize) {
  String result = "";

  for (byte i = 0; i < bufferSize; i++) {
    result.concat(String(buffer[i] < 0x10 ? " 0" : " "));
    result.concat(String(buffer[i], HEX));
  }

  return result;
}

