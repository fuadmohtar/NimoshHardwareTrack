/*
 * Smart Attendance System
 * 
 * This system reads RFID cards, checks for motion detection,
 * and records attendance in Google Sheets
 */

// Include all the necessary libraries
#include <SPI.h>
#include <MFRC522.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <LiquidCrystal_I2C.h>

// Pin definitions for our components
#define RST_PIN       D3     // RFID reset pin
#define SS_PIN        D4     // RFID chip select pin
#define BUZZER        D8     // Buzzer for feedback
#define MOTION_SENSOR D0     // Motion sensor pin

// RFID reader setup
MFRC522 mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;
MFRC522::StatusCode status;

// We're reading data from block 2 on the RFID card
int blockNum = 2;
byte bufferLen = 18;
byte readBlockData[18];  // This will store the card data

String card_holder_name;
const String sheet_url = "YOUR_GOOGLE_SCRIPT_URL_HERE";  // Replace with your actual URL

// Your WiFi credentials - change these!
#define WIFI_SSID "Your_WiFi_Name"
#define WIFI_PASSWORD "Your_WiFi_Password"

// LCD display setup (try 0x27 if 0x3F doesn't work)
LiquidCrystal_I2C lcd(0x3F, 16, 2);

// Track if we've read a card and are waiting for motion
bool cardRead = false;
unsigned long motionDetectionStartTime = 0;
const unsigned long MOTION_TIMEOUT = 10000;  // Wait up to 10 seconds for motion

void setup() {
  // Start serial communication for debugging
  Serial.begin(9600);
  Serial.println("Starting attendance system...");

  // Initialize the LCD display
  lcd.begin(); 
  lcd.backlight();
  lcd.clear();
  
  // Show a startup message on the LCD
  lcd.setCursor(0, 0);
  lcd.print("Initializing...");
  
  // Simple progress indicator
  for (int i = 0; i <= 5; i++) {
    lcd.setCursor(i, 1);
    lcd.print(".");
    delay(300);
  }

  // Connect to WiFi
  Serial.println();
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  // Wait until we're connected
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(250);
  }
  
  Serial.println("");
  Serial.println("Awesome! WiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Setup our pins
  pinMode(BUZZER, OUTPUT);        // Buzzer is an output
  pinMode(MOTION_SENSOR, INPUT);  // Motion sensor is an input

  // Start the SPI communication for the RFID reader
  SPI.begin();
  
  // Initialize the RFID reader
  mfrc522.PCD_Init();
  
  // Let the user know we're ready
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("System Ready!");
  delay(1000);
}

void loop() {
  // If we've already read a card, check for motion
  if (cardRead) {
    checkForMotion();
    return;
  }
  
  // Normal operation - look for RFID cards
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Scan your card");
  
  // Initialize the RFID reader on each loop
  mfrc522.PCD_Init();
  
  // Check if a new card is present
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;  // No card found, try again
  }
  
  // Try to read the card serial number
  if (!mfrc522.PICC_ReadCardSerial()) {
    return;  // Failed to read, try again
  }
  
  // If we get here, we've found and read a card!
  Serial.println("Reading data from RFID card...");
  ReadDataFromBlock(blockNum, readBlockData);
  
  // Show what we read from the card
  Serial.print("Card data: ");
  for (int j = 0; j < 16; j++) {
    Serial.write(readBlockData[j]);
  }
  Serial.println();
  
  // Make some beeps to confirm card read
  beep(200);
  delay(200);
  beep(200);
  
  // Show the card holder's name on the LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Hello " + String((char*)readBlockData) + "!");
  lcd.setCursor(0, 1);
  lcd.print("Wait for motion");
  
  // Now wait for motion detection
  cardRead = true;
  motionDetectionStartTime = millis();
  Serial.println("Card read. Waiting for motion...");
}

// Check if motion is detected after card read
void checkForMotion() {
  // Check if motion is detected
  if (digitalRead(MOTION_SENSOR) == HIGH) {
    Serial.println("Motion detected! Sending data...");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Motion detected!");
    lcd.setCursor(0, 1);
    lcd.print("Sending data...");
    
    // Send the data to Google Sheets
    sendDataToSheet();
    
    // Reset for next card
    cardRead = false;
  }
  // Check if we've waited too long for motion
  else if (millis() - motionDetectionStartTime > MOTION_TIMEOUT) {
    Serial.println("No motion detected. Resetting...");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Timeout!");
    lcd.setCursor(0, 1);
    lcd.print("Please try again");
    
    // Beep to indicate timeout
    beep(1000);
    delay(2000);
    
    // Reset for next card
    cardRead = false;
  }
}

// Send the attendance data to Google Sheets
void sendDataToSheet() {
  // Make sure we're still connected to WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Oops! Lost WiFi connection.");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi disconnected");
    beep(500);
    delay(2000);
    cardRead = false;
    return;
  }
  
  // Create a secure client for HTTPS
  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();  // Don't check the certificate
  
  // Build the URL with the card data
  card_holder_name = sheet_url + String((char*)readBlockData);
  card_holder_name.trim();
  Serial.println("Sending to: " + card_holder_name);
  
  // Create an HTTP client
  HTTPClient https;
  
  // Try to connect to Google Sheets
  if (https.begin(*client, card_holder_name)) {
    Serial.println("Connecting to Google Sheets...");
    
    // Send the GET request
    int httpCode = https.GET();
    
    // Check if it worked
    if (httpCode > 0) {
      Serial.printf("Success! Server response: %d\n", httpCode);
      
      // Show success on LCD
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Attendance");
      lcd.setCursor(0, 1);
      lcd.print("Recorded!");
      
      // Happy beeps!
      beep(100);
      delay(100);
      beep(100);
      delay(100);
      beep(100);
      
      delay(2000);
    } else {
      // Something went wrong
      Serial.printf("Error: %s\n", https.errorToString(httpCode).c_str());
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Error sending");
      lcd.setCursor(0, 1);
      lcd.print("data!");
      
      // Sad beep
      beep(1000);
      delay(2000);
    }
    
    // Close the connection
    https.end();
  } else {
    Serial.println("Couldn't connect to Google Sheets");
  }
}

// Read data from the RFID card
void ReadDataFromBlock(int blockNum, byte readBlockData[]) {
  // Set up the default key (most cards use FFFFFFFFFFFF)
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }
  
  // Authenticate with the card
  status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, blockNum, &key, &(mfrc522.uid));
  
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Authentication failed: ");
    Serial.println(mfrc522.GetStatusCodeName(status));
    return;
  }
  
  // Try to read the data
  status = mfrc522.MIFARE_Read(blockNum, readBlockData, &bufferLen);
  
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Reading failed: ");
    Serial.println(mfrc522.GetStatusCodeName(status));
    return;
  }
  
  Serial.println("Card read successfully!");
}

// Simple beep function
void beep(int duration) {
  digitalWrite(BUZZER, HIGH);
  delay(duration);
  digitalWrite(BUZZER, LOW);
}