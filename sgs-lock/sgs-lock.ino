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
#define BLUE_LED_PIN 4
#define BUTTON_PIN 21
#define SWITCH_PIN 16
#define BUZZER_PIN 1
#define STEPPER_PIN_1 20
#define STEPPER_PIN_2 19
#define STEPPER_PIN_3 18
#define STEPPER_PIN_4 17

//wifi config
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;
char hostname[] = LOCK_HOST;
int status = WL_IDLE_STATUS;
byte mac[6];                     // the MAC address of your WiFi Module

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

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  while (!Serial) {
    ;
  }

  //myStepper.setSpeed(5);

  
  //Init Wifi
  initWiFi();
  
  SPI.begin(); // Init SPI bus

  // Init MFRC522 
  rfid.PCD_Init(); // Init MFRC522 
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }

  Serial.println(F("This code scan the MIFARE Classsic NUID."));
  Serial.print(F("Using the following key:"));
  String keyValue = getHexValue(key.keyByte, MFRC522::MF_KEY_SIZE);
  Serial.print(keyValue);
  
  /*
  //Init server 
  server.begin();
  Serial.println("Server started");
  */

  // initialize the outputs:
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(BLUE_LED_PIN, OUTPUT);
  //pinMode(BUZZER_PIN, OUTPUT);

  // initialize the inputs:
  pinMode(BUTTON_PIN, INPUT);
  pinMode(SWITCH_PIN, INPUT);

}

//testing loop
void test_loop() {
  delay(5000);


  //test RGB LED
  Serial.println("Green Light");
  ledController("UNLOCK");
  delay(5000);
  ledController("");
  delay(1000);

  Serial.println("Red Light");
  ledController("LOCK");
  delay(5000);
  ledController("");
  delay(1000);

  Serial.println("Yellow Light");
  ledController("UNPAIR");
  delay(5000);
  ledController("");
  delay(1000);

  /*
  //test stepper motor
  myStepper.step(90);
  delay(1000);
  myStepper.step(-90);
  delay(1000);
  myStepper.step(180);
  delay(1000);
  myStepper.step(-180);
  */

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
  
  Serial.println("Testing Complete");
  Serial.println("----------------");

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

  String uid = rfidListener();
  //request verification from control hub if uid != NULL
  if (""!=uid){
    verification(uid, hostname);
  }

  doorSwitchListener();
  buttonListener();
  apiListener();
}



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

String rfidListener(){
  // Reset the loop if no new card present on the sensor/reader. This saves the entire process when idle.
  if ( ! rfid.PICC_IsNewCardPresent())
    return "";

  // Verify if the NUID has been readed
  if ( ! rfid.PICC_ReadCardSerial())
    return "";

  Serial.print(F("PICC type: "));
  MFRC522::PICC_Type piccType = rfid.PICC_GetType(rfid.uid.sak);
  Serial.println(rfid.PICC_GetTypeName(piccType));

  // Check is the PICC of Classic MIFARE type
  if (piccType != MFRC522::PICC_TYPE_MIFARE_MINI &&  
    piccType != MFRC522::PICC_TYPE_MIFARE_1K &&
    piccType != MFRC522::PICC_TYPE_MIFARE_4K) {
    Serial.println(F("Your tag is not of type MIFARE Classic."));
    return "";
  }

  String uid = getHexValue(rfid.uid.uidByte, rfid.uid.size);
  
  // Halt PICC
  rfid.PICC_HaltA();

  // Stop encryption on PCD
  rfid.PCD_StopCrypto1();

  return uid;
}

void verification(String uid, String requestedBy) {
  HttpClient client = HttpClient(wifiClient, serverAddress, port);

  Serial.println("making POST request");
  String contentType = "application/x-www-form-urlencoded";
  String postData = "uid=" + uid;
  String requestedBy = "requestedBy=" + requestedBy;

  client.post("/", contentType, postData);

  // read the status code and body of the response
  int statusCode = client.responseStatusCode();
  String response = client.responseBody();

  Serial.print("Status code: ");
  Serial.println(statusCode);
  Serial.print("Response: ");
  Serial.println(response);

  if(200 == statusCode){
    motorController("UNLOCK");
    ledController("UNLOCK");
    buzzerController("UNLOCK");
  }

}

void calibrateStepper(){
  // read the state of the pushbutton value:
  buttonState = digitalRead(BUTTON_PIN);

  // check if the pushbutton is pressed. If it is, the buttonState is HIGH:
  while (buttonState == HIGH) {
    myStepper.step(-1*direction);
    lockStep += 1;

    buttonState = digitalRead(BUTTON_PIN);
    if (buttonState == LOW) {
      calibrated = true;
    }
  }
  
}

void pairing(){
  //TODO: update the logic
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
  switchState = digitalRead(SWITCH_PIN);
  int count = 0;

  while (switchState == LOW && count < 5) {
    Serial.println("Door opened");

    switchState = digitalRead(SWITCH_PIN);
    if (switchState == HIGH) {
      Serial.println("Door closed");
      return;
    } else {
      count += 1;
    }
    delay(1000);
  }

  if (count == 5){ // warning after 5 seconds
    while (switchState == LOW){
      ledController("WARN");
      buzzerController("WARN");
      switchState = digitalRead(SWITCH_PIN);
    }
    Serial.println("Door closed");
    return;
  }

  while (switchState == HIGH && !locked) {
    Serial.println("Door closed but not lock");

    switchState = digitalRead(SWITCH_PIN);
    if (switchState == LOW) {
      Serial.println("Door opened");
      return;
    } else {
      count += 1;
    }
    delay(1000);

    if (count == 3){ // auto lock after 3 seconds
      motorController("LOCK");
      ledController("LOCK");
      buzzerController("LOCK");
      Serial.println("Door locked");
    }
  }
}

void buttonListener(){
  buttonState = digitalRead(BUTTON_PIN);
  while (buttonState == HIGH) {
    buttonState = digitalRead(BUTTON_PIN);
    if(buttonState == LOW){ //Pressed and released the button
      if(!locked){
        motorController("LOCK");
        ledController("LOCK");
        buzzerController("LOCK");
        Serial.println("Door locked");
      } else {
        motorController("UNLOCK");
        ledController("UNLOCK");
        buzzerController("UNLOCK");
        Serial.println("Door unlocked");
      }
    }
  }
}

void motorController(String mode){
  if("LOCK" == mode){
    myStepper.step(lockStep*direction);
    updateStatus("LOCK");
  } 
  else if ("UNLOCK" == mode){
    myStepper.step(lockStep*direction*-1);
    updateStatus("UNLOCK");
  }
}

void buzzerController(String mode){
  if("LOCK" == mode){
    tone(BUZZER_PIN, 100, 2)
  } else if ("UNLOCK" == mode){
    tone(BUZZER_PIN, 50, 2)
  } else if ("WARN" == mode){
    tone(BUZZER_PIN, 10, 2)
  }
}

void ledController(String mode){
  if("LOCK" == mode){
    //show red light
    rgbColor(255, 0, 0);
  } else if ("UNLOCK" == mode){
    //show green light
    rgbColor(0, 255, 0);
  } else if("UNPAIR" == mode){
    // show yellow light
    rgbColor(255, 255, 0);
  } else if ("WARN" == mode){
    rgbColor(255, 0, 0);
    rgbColor(255, 255, 255);
    rgbColor(255, 0, 0);
    rgbColor(255, 255, 255);
  } else {
    rgbColor(255, 255, 255);
  }
}

void rgbColor(int red, int green, int blue){
  analogWrite(RED_LED_PIN, 255-red);
  analogWrite(GREEN_LED_PIN, 255-green);
  analogWrite(BLUE_LED_PIN, 255-blue);
}

void apiListener(){
  //TODO: update the logic
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



