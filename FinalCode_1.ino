/*---------A.Libraries and Functions---------*/

//---1. LCD
#include<LiquidCrystal.h>
LiquidCrystal lcd(7,8,9,10,11,12);

//---2. Wifi - ESP8266
#include <SoftwareSerial.h>
SoftwareSerial ser(5, 6); // RX, TX

//---3. Relay
#define THEFT_RELAY 2
#define OVERLOAD_RELAY 3

/*---------B.Global Variables Declarations---------*/

//---1. For LDR - To count pulses

// a. Flag Variable to check state of LED is ON or OFF
int sem=0; // LDR-2
int sem1=0; // LDR-1

// b. Count LED pulses
int count =0; // LDR-2
int count2 =0; // LDR-1

// c. Threshold value of voltage vary
const int threshold = 500;

//---2. For Theft and Overload Logic

// a. Flags
bool theftDetected = false;
bool overloadDetected = false;

bool theftSent = false;
bool overloadSent = false;
bool bothSent = false;

// b. System State to performAction in correct way
enum SystemState {
  NORMAL, // 0
  THEFT,  // 1
  OVERLOAD, // 2
  BOTH // 3
};

SystemState currentState = NORMAL;
SystemState previousState = NORMAL; // Flag

// c. For Overload - Overload Protection - Rate Based and Absolute (Window Based)
int diff = 0;
int prevCount = 0;
unsigned long prevTime = 0;
bool rateOverload = false;
bool absoluteOverload = false;  // Absolute Overload - "Window Based - Soon"
int overLoadThreshold = 160;
int overLoadTimeThreshold = 70;
bool update_Time = false;
bool update_Count = false;

enum OverloadMode {
  COUNT_BASED, // 0
  TIME_BASED, // 1
  HYBRID // 2
};

OverloadMode overloadMode = TIME_BASED; 

// d. For Theft - Theft Detection - Rate Based and "Window Based - Soon"
unsigned long theftTime = 0;
int theftThreshold = 50;

// e. Buzzer
unsigned long buzzerStart = 0;
bool buzzerActive = false;
unsigned long lastBeepTime = 0;
bool firstBeepDone = false;

//---3. For IoT 
String apiKey = "------"; 
unsigned long iotTime = 0;
int stateValue = 0;

String getStr;

//---4. Auto Recovery / Reset Mechansim after 30 Sec
unsigned long faultStartTime = 0;  // Check Fault Time - Kitne Time sei fault hai system mei

/*---------C.setup() funciton---------*/
void setup() {
  Serial.begin(9600);  // USB-B
  lcd.begin(16,2);

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Energy Meter");
  delay(2000);

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Testing Theft");
  delay(2000);

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Test Overload");
  delay(2000);
  lcd.clear();

  pinMode(A3, OUTPUT);
  digitalWrite(A3, LOW);

  pinMode(THEFT_RELAY, OUTPUT);
  digitalWrite(THEFT_RELAY, LOW);

  pinMode(OVERLOAD_RELAY, OUTPUT);
  digitalWrite(OVERLOAD_RELAY, LOW);

  analogReference(DEFAULT);

  ser.begin(115200);  // 115200 - ESP

  ser.println("AT+RST");
  delay(500);

  ser.println("AT+CWMODE=3");
  delay(500);

  ser.println("AT+CWJAP=\"Project\",\"987654321\"");
  delay(500);

}

/*---------D.Functions Declaration---------*/
void readSensors();
void detectTheft();
void detectOverload();
void decisionEngine();
void performActions();
void sendToIoT();

void sendSMS(String msg);
void buzzerBehaviour();
void faultRecovery();

/*---------E.loop() function---------*/
void loop() {

  //Step-1 : Detection
  readSensors();        // Read LDR counts
  detectTheft();        // sends theftDetected
  detectOverload();     // sends overloadDetected

  // Step-2 : Decision
  decisionEngine();     // Decides final state

  // Step-3 : Action + Communication
  performActions();     // Relay + Buzzer + GSM + LCD

  // Step-4 : Communication
  sendToIoT();          // Periodic Upload

  // 1. Buzzer Behaviour
  buzzerBehaviour();

  // 2. System Reset - After 30 Seconds System Starts Behaving Normal
  faultRecovery();

}

/*---------F.Functions Definition---------*/
//---1. readSensors()
void readSensors(){
  
  Serial.print(analogRead(A4));
  Serial.print(" ");
  Serial.println(analogRead(A5));
  
  // a. LDR of Meter-2 (SubMeter - C1)
  int val1 = analogRead(A4);
  
  if((val1 < threshold) && (sem == 0)){
    sem = 1;
    count += 10;
  }

  if((val1 >= threshold) && (sem == 1)){
    sem = 0;
  }

  // b. LDR of Meter-1 (MainMeter - C2)
  int val2 = analogRead(A5);
  
  if((val2 < threshold) && (sem1 == 0)){
    sem1 = 1;
    count2 += 10;
  }

  if((val2 >= threshold) && (sem1 == 1)){
    sem1 = 0;
  }
}

//---2. detectTheft() - Window Based Theft Detection
void detectTheft() {
  
  if (millis() - theftTime >= 10000){
  
   if (count2 >= count + theftThreshold) {
      theftDetected = true;
    }

    else {
      theftDetected = false;
    }

    theftTime = millis();
  }
}

//---3. detectOverload()
void detectOverload() {

  // Condition-1: Rate-Based

   if (overloadMode == TIME_BASED || overloadMode == HYBRID) {
    
    if (millis() - prevTime >= 30000) {
    
      diff = count - prevCount;
      
      if (diff >= overLoadTimeThreshold) {
        rateOverload = true;
      } 
    
      prevCount = count;
      prevTime = millis();
      update_Time = true;
    }
   }
  

  // Condition 2: Absolute threshold - Window Based Overload Detection
  if (overloadMode == COUNT_BASED || overloadMode == HYBRID) {
    
    if (count > overLoadThreshold) {
      absoluteOverload = true;  // holdsState
      update_Count = true;
    }
  }
  

  // Final Decision
  overloadDetected = rateOverload || absoluteOverload;
}

//---4. decisionEngine()
void decisionEngine() {

  if (theftDetected && overloadDetected) {
    currentState = BOTH;
  }
  else if (theftDetected) {
    currentState = THEFT;
  }
  else if (overloadDetected) {
    currentState = OVERLOAD;
  }
  else {
    currentState = NORMAL;
  }
}

//---5. performAction() - Relay + Buzzer + GSM + LCD
void sendSMS(String msg) {
  
  Serial.println("AT+CMGF=1");
  delay(500);

  Serial.print("AT+CMGS=\"0123456789\"\r"); 
  delay(500);

  Serial.print(msg);
  delay(500);

  Serial.write(26);

}

void performActions() {
  
  // Run only when state changes
  if (currentState != previousState) {

    faultStartTime = millis(); // Start Time at which fault start happening

    lcd.clear();

    switch(currentState) {

      case NORMAL:
        digitalWrite(THEFT_RELAY, LOW);
        digitalWrite(OVERLOAD_RELAY, LOW);
        digitalWrite(A3, LOW);
  
        lcd.setCursor(0,0);
        lcd.print("Energy Meter");
  
        lcd.setCursor(0,1);
        lcd.print("C1:");
        lcd.print(count);
  
        lcd.setCursor(10,1);
        lcd.print("C2:");
        lcd.print(count2);
        break;

      case THEFT:
        digitalWrite(THEFT_RELAY, HIGH);
        digitalWrite(OVERLOAD_RELAY, HIGH);
        digitalWrite(A3, HIGH);
        sendSMS("Theft Detected at House No. 52");

        lcd.setCursor(0,0);
        lcd.print("THEFT DETECTED");
        delay(2000);

        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Theft Alert Sent");

        buzzerStart = millis();
        buzzerActive = true;
        firstBeepDone = false;
        
        break;

      case OVERLOAD:
        digitalWrite(OVERLOAD_RELAY, HIGH);
        digitalWrite(A3, HIGH);
        sendSMS("Overload Detected at House No. 52");

        lcd.setCursor(0,0);
        lcd.print("Overload");
        lcd.setCursor(0,1);
        lcd.print("Detected");
        delay(2000);

        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Overload Alert");
        lcd.setCursor(0,1);
        lcd.print("Sent");

        buzzerStart = millis();
        buzzerActive = true;
        firstBeepDone = false;
        
        break;

      case BOTH:
        digitalWrite(THEFT_RELAY, HIGH);
        digitalWrite(OVERLOAD_RELAY, HIGH);
        digitalWrite(A3, HIGH);
        sendSMS("Theft and Overload Detected at House No. 52");

        lcd.setCursor(0,0);
        lcd.print("Theft + Overload");
        lcd.setCursor(0,1);
        lcd.print("Detected");
        delay(2000);

        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Theft + Overload");
        lcd.setCursor(0,1);
        lcd.print("Alert Sent");

        buzzerStart = millis();
        buzzerActive = true;
        firstBeepDone = false;
        
        break;
    }

    previousState = currentState;
  }

  // Continuous display (no delay)
  if (currentState == NORMAL) {
    
    digitalWrite(THEFT_RELAY, LOW);
    digitalWrite(OVERLOAD_RELAY, LOW);
    digitalWrite(A3, LOW);

    lcd.setCursor(0,0);
    lcd.print("Energy Meter");
    
    lcd.setCursor(0,1);
    lcd.print("C1:");
    lcd.print(count);

    lcd.setCursor(10,1);
    lcd.print("C2:");
    lcd.print(count2);
  }

}

//---6.IOT 
void sendToIoT() {

  if (millis() - iotTime >= 15000) {

    iotTime = millis();
  
    stateValue = currentState;  // 0 = NORMAL, 1 = THEFT, 2 = OVERLOAD, 3 = BOTH

    // 1. Start connection - TCP
    ser.println("AT+CIPSTART=\"TCP\",\"184.106.153.149\",80");
    delay(1000);

    // 2. SINGLE REQUEST (important) - Create GET request 
    getStr = "GET /update?api_key=" + apiKey +
             "&field1=" + String(count) +
             "&field2=" + String(count2) +
             "&field3=" + String(stateValue) +
             "HTTP/1.1\r\nHost: api.thingspeak.com\r\nConnection: close\r\n\r\n";

    // 3. Send Length
    ser.println("AT+CIPSEND=" + String(getStr.length()));
    delay(1000);

    // 4. Send Data
    ser.print(getStr);
    delay(1500);

    // 5. CLOSE connection (VERY IMPORTANT)
    ser.println("AT+CIPCLOSE");
  }
}

//---7. Buzzer Beep Logic
void buzzerBehaviour(){
  
  // a. First 5 sec continuous beep - Buzzer auto OFF after 5 Secs
  if (buzzerActive && (firstBeepDone == false)) {
    if (millis() - buzzerStart > 5000) {
      digitalWrite(A3, LOW);
      buzzerActive = false;
      firstBeepDone = true;
      lastBeepTime = millis();  // Start Repeating Timer
    }
  }

  // b. Repeating beep after first beep - In every 3 secs 
  if (currentState != NORMAL && firstBeepDone) {
  
    if (millis() - lastBeepTime > 6000) {
  
      digitalWrite(A3, HIGH);
      buzzerStart = millis();
      buzzerActive = true;
      
      lastBeepTime = millis(); // Start Repeating Timer
    }
  }

  // c. Short Beep OFF
  if (buzzerActive && firstBeepDone && millis() - buzzerStart > 2000) {
    digitalWrite(A3, LOW);
    buzzerActive = false;
  }
}

//---8. Fault Recovery
void faultRecovery(){
  if (currentState != NORMAL && millis() - faultStartTime > 30000) {

    currentState = NORMAL;

    // Theft
    if (theftDetected){
      theftDetected = false;
      theftThreshold += 30;
    }
    
    // Overload
    if (overloadDetected) {
      overloadDetected = false;
      rateOverload = false;
      absoluteOverload = false;
  
      if (update_Count) {
        overLoadThreshold += 60;
        update_Count = false;
      }
  
      if (update_Time) {
         //overLoadTimeThreshold += 30;
         update_Time = false;
         prevCount = count;
         prevTime = millis();
      }
     
    }
    
  }
}
