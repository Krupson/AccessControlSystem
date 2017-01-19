#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>
#include <elapsedMillis.h>
#include <stdarg.h>

const int redPin = 6;
const int greenPin = 5;
const int bluePin = 3;
const int relayPin = A1;
const int buzzerPin = A5;

const int rstPin = A0;
const int ssPin = 10;

const int unusedPins[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,A0,A1,A2,A3,A4,A5};

const bool DEBUG = false;
const bool RELAY_ON = true;

MFRC522 reader(ssPin, rstPin);

byte masterCard[] = {0x13, 0x53, 0xC2, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

enum MODE_ENUM {READY, MASTER_ADD, MASTER_CLEAR, ACCESS_GRANTED, ACCESS_DENIED, NEED_AUTH, OTHER};

MODE_ENUM currentMode = READY;
bool isMaster = false, isLastReadMaster = false, adminAccess = false;
elapsedMillis masterTimeout = 0, actionTime = 0, loginTime = 0;

byte cardNotPresentCount = 0;

void setup() {
  Serial.begin(9600);
  
  //for(int x = 0; x < sizeof(unusedPins) / sizeof(int); x++) {
  for(int x = 0; x <= 19; x++) {
    if(isInUse(12, x, 0, 1, redPin, greenPin, bluePin, relayPin, buzzerPin, rstPin, ssPin, 11, 12, 13)) continue;
    
    pinMode(x, INPUT_PULLUP);
    Serial.print("Unused pin: ");
    Serial.println(x);
    //digitalWrite(x, LOW);
  }
  
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
  pinMode(relayPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);

  digitalWrite(redPin, LOW);
  digitalWrite(greenPin, LOW);
  digitalWrite(bluePin, LOW);
  digitalWrite(relayPin, !RELAY_ON);
  digitalWrite(buzzerPin, LOW);
  
  SPI.begin();
  reader.PCD_Init();
  if(DEBUG) reader.PCD_DumpVersionToSerial();

  if(DEBUG) {
    Serial.println("EEPROM DUMP:");
    Serial.print("Cards count: ");
    Serial.println(EEPROM.read(0));
    for(int x = 1; x < EEPROM.length(); x++) {
      if((x - 1) % 10 == 0) {
        Serial.print((x - 1) / 10);
        Serial.print(":\t\t");
      }
      Serial.print(EEPROM.read(x), HEX);
      if(x % 10 == 0) {
        Serial.println();
      } else {
        Serial.print("\t");
      }
    }
    Serial.println();
  }

  for(int x = 0; x < 6; x++) {
    if(x%2 == 0) setLedColor(0,255,255);
    else setLedColor(0,0,0);
    delay(150);
  }

  setLedColor(0,0,0);
}

void loop() {
  if(adminAccess && loginTime >= 30*1000){//600000) {
    adminAccess = false;
    Serial.println("AUTO_LOGGED_OUT");
  }
  
  if(currentMode != READY) {
    switch(currentMode) {
      case ACCESS_GRANTED: performAccessGranted(); break;
      case ACCESS_DENIED: performAccessDenied(); break;
      case MASTER_ADD: performMasterAdd(); break;
      case MASTER_CLEAR: performMasterClear(); break;
      case NEED_AUTH: performMasterAuth(); break;
    }
    return;
  }

  processSerialCmds();
  
  if(!reader.PICC_IsNewCardPresent() || !reader.PICC_ReadCardSerial()) {
    cardNotPresentCount++;
    if(cardNotPresentCount >= 2) {
      if(isLastReadMaster && masterTimeout < 2000) {
        currentMode = MASTER_ADD;
        actionTime = 0;
        if(DEBUG) Serial.println("MODE: MASTER_ADD");
        isMaster = false;
        isLastReadMaster = false;
      }
      masterTimeout = 0;
    }
    
    return;
  }

  cardNotPresentCount = 0;

  byte cardId[10] = {0};
  
  if(readUid(cardId, reader.uid)) {
    isMaster = checkIfMaster(cardId);

    if(DEBUG && isMaster) {
        Serial.print("Got master for: ");
        Serial.println(masterTimeout);
    }

    if(!isMaster) {
      if(checkIfValidCard(cardId)) {
        currentMode = ACCESS_GRANTED;
        actionTime = 0;
        if(DEBUG) Serial.println("MODE: ACCESS_GRANTED");
      } else {
        currentMode = ACCESS_DENIED;
        actionTime = 0;
        if(DEBUG) Serial.println("MODE: ACCESS_DENIED");
      }
    }
    
    if(isMaster && isLastReadMaster && masterTimeout >= 2000) {
      currentMode = MASTER_CLEAR;
      actionTime = 0;
      if(DEBUG) Serial.println("MODE: MASTER_CLEAR");
      isMaster = false;
    } else if(isMaster && isLastReadMaster) {
      
    } else if(!isMaster && isLastReadMaster) {
      currentMode = MASTER_ADD;
      actionTime = 0;
      if(DEBUG) Serial.println("MODE: MASTER_ADD");
      isMaster = false;
    } else if(isMaster && !isLastReadMaster) {
      masterTimeout = 0;
    }
    
    isLastReadMaster = isMaster;
  }
}

bool compareUids(byte * uid1, byte * uid2) {
  for(byte x = 0; x < 10; x++) {
    if(uid1[x] != uid2[x]) {
      return false;
    }
  }
  return true;
}

bool checkIfMaster(byte * uid) {
  return compareUids(uid, masterCard);
}

bool readUid(byte * result, MFRC522::Uid uid) {
  if(!uid.sak) return false;
  
  for(byte x = 0; x < 10; x++) {
    if(x < uid.size) {
      result[x] = uid.uidByte[x];
    } else {
      result[x] = 0;
    }

    if(DEBUG) {
      Serial.print(result[x], HEX);
      Serial.print(" ");
    }
  }

  if(DEBUG) Serial.println();
  return true;
}

bool checkIfValidCard(byte * uid) {
  int cardsCount = EEPROM.read(0);
  byte currentCard[10] = {0};
  for(int x = 0; x < cardsCount; x++) {
    readCardFromEEPROM(x, currentCard);
    if(compareUids(currentCard, uid)) return true;
  }

  return false;
  
  static bool lol = true;
  lol = !lol;
  return lol;
}

void performAccessGranted() {
  if(actionTime <= 200) {
    digitalWrite(buzzerPin, HIGH);
    digitalWrite(relayPin, RELAY_ON);
    digitalWrite(greenPin, HIGH);
  } else if(actionTime <= 1000) {
    digitalWrite(buzzerPin, HIGH);
    digitalWrite(relayPin, !RELAY_ON);
    digitalWrite(greenPin, HIGH);
  } else {
    digitalWrite(greenPin, LOW);
    digitalWrite(buzzerPin, LOW);
    currentMode = READY;
  }
}

void performAccessDenied() {
  for(int x = 1; x <= 6; x++) {
    if(actionTime < 100 * x && actionTime >= 100 * (x - 1)) {
      digitalWrite(buzzerPin, x%2);
    }
  }

  digitalWrite(buzzerPin, LOW);
  
  if(actionTime <= 1000) {
    digitalWrite(redPin, HIGH);
  } else {
    digitalWrite(redPin, LOW);
    currentMode = READY;
  }
}

void performMasterAdd() {
  if(actionTime <= 10000) {
    for(int x = 1; x <= 50; x++) {
      if(actionTime < 200 * x && actionTime >= 200 * (x - 1)) {
        digitalWrite(greenPin, !(x%2));
      }
    }

    if(reader.PICC_IsNewCardPresent() && reader.PICC_ReadCardSerial()) {
      byte cardId[10] = {0};
      if(readUid(cardId, reader.uid) && !checkIfMaster(cardId)) {
        currentMode = READY;
        digitalWrite(buzzerPin, HIGH);
        digitalWrite(greenPin, LOW);

        int responseLed;
        if(addToEEPROM(cardId)) { 
           responseLed = greenPin;
        } else {
           responseLed = redPin;
        }

        for(int x = 0; x < 10; x++) {
          digitalWrite(responseLed, !digitalRead(responseLed));
          delay(100);
        }
        digitalWrite(buzzerPin, LOW);
        digitalWrite(responseLed, LOW); 
      }
    }
    
  } else {
    setLedColor(0, 0, 0);
    currentMode = READY;
  }
}

void performMasterClear() {
  if(actionTime <= 3000) {
    for(int x = 1; x <= 6; x++) {
      if(actionTime < 500 * x && actionTime >= 500 * (x - 1)) {
        digitalWrite(redPin, !(x%2));
        digitalWrite(buzzerPin, !(x%2));
      }
    }
  } else {
    clearEEPROM();
    digitalWrite(redPin, LOW);
    digitalWrite(buzzerPin, LOW);
    currentMode = READY;
  }
}

void performMasterAuth() {
  if(adminAccess) {
    currentMode = READY;
    Serial.println("ALREADY_LOGGED_IN");
    return;
  }
  if(actionTime <= 10000) {
    for(int x = 1; x <= 50; x++) {
      if(actionTime < 200 * x && actionTime >= 200 * (x - 1)) {
        if(x%2 == 0) setLedColor(255, 255, 100);
        else setLedColor(0, 0, 0);
      }
    }

    if(reader.PICC_IsNewCardPresent() && reader.PICC_ReadCardSerial()) {
      byte cardId[10] = {0};
      setLedColor(0, 0, 0);
      if(readUid(cardId, reader.uid)) {
        if(checkIfMaster(cardId)) {
          currentMode = READY;
          adminAccess = true;
          loginTime = 0;
          Serial.println("LOGGED_IN");
        } else {
          currentMode = READY;
          Serial.println("AUTH_FAILED");
        }
      }
    }
    
  } else {
    setLedColor(0, 0, 0);
    currentMode = READY;
    Serial.println("AUTH_TIMEOUT");
  }
}

bool addToEEPROM(byte * uid) {
  int cardsLimit = (EEPROM.length() - 1) / 10;
  byte cardsCount = EEPROM.read(0);
  if(cardsCount >= cardsLimit) {
    if(DEBUG) Serial.println("Failed adding card to EEPROM. Out of memory.");
    return false;
  }

  EEPROM.write(0, cardsCount + 1);
  for(byte x = 0; x < 10; x++) {
    EEPROM.write(1 + (10 * cardsCount) + x, uid[x]);
  }
  if(DEBUG) Serial.println("Card added to EEPROM");
  return true;
}

void readCardFromEEPROM(int id, byte * result) {
  if(1 + (id * 11) > EEPROM.length()) return;
  for(int x = 0; x < 10; x++) {
    result[x] = EEPROM.read(1 + (id * 10) + x);
  }
}

void clearEEPROM() {
  for (int x = 0; x < EEPROM.length(); x++) {
    if(EEPROM.read(x) != 0) {
      EEPROM.write(x, 0);
    }
  }
  if(DEBUG) Serial.println("EEPROM cleared");
}

void processSerialCmds() {
  String cmd = "";
  elapsedMillis timeout = 0;
  while(timeout <= 100) {
    if(Serial.available()) {
      cmd += (char) Serial.read();
      timeout = 0;
    }
  }
  cmd.trim();
  if(cmd.equals("")) return;

  if(cmd.equals("LOGOUT")) {
    if(adminAccess) {
      adminAccess = false;
      Serial.println("LOGGED_OUT");
      return;
    }
  }

  if(cmd.equals("LOGIN")) {
    actionTime = 0;
    currentMode = NEED_AUTH;
    return;
  }

  if(cmd.equals("ADD")) {
    currentMode = MASTER_ADD;
  } else if(cmd.equals("CLEAR")) {
    currentMode = MASTER_CLEAR;
  } else if(cmd.equals("OPEN")) {
    currentMode = ACCESS_GRANTED;
  } else if(cmd.startsWith("SET_T_BUZ_OK ")) {
    if(!adminAccess) {
      currentMode = NEED_AUTH;
    } else {
      currentMode = OTHER;
      Serial.println(cmd.substring(13));
    }
  } else if(cmd.startsWith("SET_T_LED ")) {
    if(!adminAccess) {
      currentMode = NEED_AUTH;
    } else {
      currentMode = OTHER;
      Serial.println(cmd.substring(10));
    }
  } else if(cmd.startsWith("SET_T_REL ")) {
    if(!adminAccess) {
      currentMode = NEED_AUTH;
    } else {
      currentMode = OTHER;
      Serial.println(cmd.substring(10));
    }
  } else if(cmd.startsWith("REMOVE ")) {
    if(!adminAccess) {
      currentMode = NEED_AUTH;
    } else {
      currentMode = OTHER;
      Serial.println(cmd.substring(7));
    }
  }

  if(currentMode != READY && !adminAccess) {
    currentMode = NEED_AUTH;
    Serial.println("NEED_AUTH");
  } else if(currentMode != READY) {
    loginTime = 0;
  }
  
  actionTime = 0;
}

void getSettings() {
  // T_BUZ_OK, T_LED, T_REL
}

void setLedColor(byte r, byte g, byte b) {
  analogWrite(redPin, r);
  analogWrite(greenPin, g);
  analogWrite(bluePin, b);
}

bool isInUse(int count, int currentPin, ...) {
  va_list ap;
  va_start(ap, count);
  for(int x = 0; x < count; x++) {
    if(va_arg(ap, int) == currentPin) {
      return true;
    }
  }
  
  va_end(ap);
  return false;
}

