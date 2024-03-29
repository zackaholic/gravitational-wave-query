
#include <EEPROM.h>
#include <Regexp.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h> 
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>

//////////////Wifi/////////////////
/* Set these to your desired credentials. */
const char *ssid = "SSID";  
const char *password = "passpasspass";

//Web/Server address to read/write from 
const char *host = "gracedb.ligo.org";
const int httpsPort = 443;  //HTTPS= 443

//SHA1 finger print of certificate use web browser to view and copy
const char fingerprint[] PROGMEM = "C9 33 A8 37 35 1F F1 7B 1C 4E 2B F3 07 73 61 45 58 4D 69 19";


/////////////Regex/////////////////

char line [100]; 
char match [20];  //oversized to hold result
char savedID [20];

///////////////////////////////////

byte error = 0;
long queryDelay = 1000 * 60 * 20; //20min
long lastServerQuery = 0;
byte enablePin = 13;
byte manualControlPin = 2;
int motorSpeed = 0;
int motorSpeedIncrement = 255;
byte state = 2;

/*
 * error codes:
 */
//=======================================================================
//                    Power on setup
//=======================================================================

void setup() {
  delay(1000);
  
  pinMode(13, OUTPUT);
  analogWriteFreq(20000);
  analogWrite(13, 0);
  
  Serial.begin(115200);
  WiFi.mode(WIFI_OFF);        //Prevents reconnection issue (taking too long to connect)
  delay(1000);
  WiFi.mode(WIFI_STA);        //Only Station No AP, This line hides the viewing of ESP as wifi hotspot
  
  WiFi.begin(ssid, password);     //Connect to your WiFi router
  Serial.println("");

  Serial.print("Connecting");
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  //If connection successful show IP address in serial monitor
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());  //IP address assigned to your ESP
  
  EEPROM.begin(20);
  
  readID(savedID, 20);
}

//=======================================================================
//                    Main Program Loop
//=======================================================================

void loop() {
  switch (state) {
    case 1:
      //manual control
      if (digitalRead(manualControlPin) == LOW) {
        delay(20);
        if (digitalRead(manualControlPin) == LOW) {
          //wait for release
          while(digitalRead(manualControlPin) == LOW);
          motorSpeed += motorSpeedIncrement;
          if (motorSpeed > 1023) {
            motorSpeed = 0;
            state = 2;
          }
          analogWrite(enablePin, motorSpeed);
        }
      }
      break;
    case 2:
      //poll for motor
      if (digitalRead(manualControlPin) == LOW) {
        delay(20);
        if (digitalRead(manualControlPin) == LOW) {
          //wait for release
          while(digitalRead(manualControlPin) == LOW);
          motorSpeed += motorSpeedIncrement;
          analogWrite(enablePin, motorSpeed);
          state = 1;      
        }
      }
      //poll for event
      if (millis() - lastServerQuery > queryDelay) {
        lastServerQuery = millis();
        
        WiFiClientSecure httpsClient;    //Declare object of class WiFiClient
      
        Serial.println(host);
      
        Serial.printf("Using fingerprint '%s'\n", fingerprint);
        httpsClient.setFingerprint(fingerprint);
        httpsClient.setTimeout(15000); // 15 Seconds
        delay(1000);
        
        Serial.print("HTTPS Connecting");
        int r=0; //retry counter
        while((!httpsClient.connect(host, httpsPort)) && (r < 30)){
            delay(100);
            Serial.print(".");
            r++;
        }
        if(r==30) {
          Serial.println("Connection failed");
          error = 2;
        }
        else {
          Serial.println("Connected to web");
        }
        
        String Link;
      
        //GET Data
        Link = "/latest/";
      
        Serial.print("requesting URL: ");
        Serial.println(host+Link);
      
        httpsClient.print(String("GET ") + Link + " HTTP/1.1\r\n" +
                     "Host: " + host + "\r\n" +               
                     "Connection: close\r\n\r\n");
      
        Serial.println("request sent");
                        
        while (httpsClient.connected()) {
          String line = httpsClient.readStringUntil('\n');
          if (line == "\r") {
            Serial.println("headers received:");
            break;
          }
        }
      
        Serial.println("reply was:");
        Serial.println("==========");
      
        while(httpsClient.available()){        
          httpsClient.readBytesUntil('\n', line, 100);
      
          //stop parsing after first match
          if (parseLine(line) == 0) {
            break;
          }
        }
        httpsClient.flush();
        Serial.println("==========");
        Serial.println("closing connection");
      }    
      break;
  }

}
//=======================================================================

void simulateWave() {
  float scale = 1.07;//1 + (float)random(7, 8 ) / 100;

   //startup voltage
  float rate = 500;
  analogWrite(enablePin, (int)rate);
  delay(100);
  rate = 200;
  analogWrite(enablePin, (int)rate);
  for (int i = 0; i < 500; i++) {
    rate *= scale;
    if (rate > 1023) {
      rate = 1023;
      analogWrite(enablePin, (int)rate);
      delay(200);
      break;
    }
    analogWrite(enablePin, (int)rate);
    delay(100);
  }
  analogWrite(enablePin, 0);
}

int parseLine(char *buff) {
  //called until a match is found. return 0 or 1 to indicate a match
  //so https stream can be flushed. Only the first match is important
  // match state object
  MatchState ms;
  byte matchFound = 0;
  ms.Target (buff);  // set its address

//  char result = ms.Match("<td><a href=\"/superevents/(%a%d+%a*)");
  char result = ms.Match("<a href=\"/superevents/(%a%d+%a*)/view/");
  if (result > 0) {
    matchFound = 1;
    ms.GetCapture(match, 0);
    
    Serial.print("New ID: ");
    Serial.println(match);
    Serial.print("Stored ID: ");
    Serial.println(savedID);
  
    if (strcmp(match, savedID) == 0) {
      Serial.println("Ignoring repeat ID");
    } else {
      Serial.println("New ID, simulating wave");
      storeID(match);
      strcpy(savedID, match);
      simulateWave();
    }
  }
  if (matchFound == 1) {
    return 0;
  } else {
    return 1;
  }
}

void storeID(char *buff) {
  int i;
  //write into memory, including null terminator
  for (i = 0; i < strlen(buff) + 1; i++) {
    EEPROM.write(i, buff[i]);
  }
  EEPROM.commit();
}

void readID(char *buff, int len) {
  int i = 0;
  do {
    buff[i] = EEPROM.read(i);
    i++;
  } while (i < len);
}
