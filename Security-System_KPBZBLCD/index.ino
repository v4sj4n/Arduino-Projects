#include <Keypad.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

// ==========================================
// CONFIGURATION
// ==========================================

LiquidCrystal_I2C lcd(0x27, 16, 2); 

const byte ROWS = 4;
const byte COLS = 4;

char hexaKeys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte rowPins[ROWS] = {9, 8, 7, 6};
byte colPins[COLS] = {5, 4, 3, 2};

Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

const int BUTTON_PIN = 11; 
const int BUZZER_PIN = 12;

// ==========================================
// EEPROM MEMORY MAP
// ==========================================
const int ADDR_IS_SET      = 0;   
const int ADDR_LEN         = 1;   
const int ADDR_PASS        = 2;   

// Security Persistence
const int ADDR_LOCK_FLAG   = 20; // 1 = Locked
const int ADDR_REMAINING   = 25; // Time left (Long)
const int ADDR_TOTAL_DUR   = 30; // Total duration (Long) - to remember doubling

const byte MAGIC_BYTE = 0xBB;

// ==========================================
// STATE MANAGEMENT
// ==========================================

String masterKey = "";      
String enteredKey = "";     
bool setupMode = false;     

unsigned long lastInputTime = 0;   
const unsigned long TIMEOUT = 8000; 
const unsigned long MASK_DELAY = 500; 
bool waitingToMask = false; 

// --- Security Lockout Variables ---
int failedAttempts = 0;
bool isLockedOut = false;
unsigned long lockoutStartTime = 0;
unsigned long lastEEPROMSave = 0; // To limit writes slightly

const unsigned long BASE_LOCKOUT_DURATION = 30000; 
unsigned long currentLockoutDuration = BASE_LOCKOUT_DURATION;
unsigned long savedRemainingTime = 0; // Buffer for resume

// ==========================================
// AUDIO & HELPERS
// ==========================================

// Helper to write long integers (4 bytes) to EEPROM
void EEPROM_writeLong(int address, unsigned long value) {
  byte four = (value & 0xFF);
  byte three = ((value >> 8) & 0xFF);
  byte two = ((value >> 16) & 0xFF);
  byte one = ((value >> 24) & 0xFF);

  EEPROM.update(address, four);     // 'update' only writes if value changed (saves life)
  EEPROM.update(address + 1, three);
  EEPROM.update(address + 2, two);
  EEPROM.update(address + 3, one);
}

unsigned long EEPROM_readLong(int address) {
  long four = EEPROM.read(address);
  long three = EEPROM.read(address + 1);
  long two = EEPROM.read(address + 2);
  long one = EEPROM.read(address + 3);

  return ((four << 0) & 0xFF) + ((three << 8) & 0xFFFF) + ((two << 16) & 0xFFFFFF) + ((one << 24) & 0xFFFFFFFF);
}

void playClickSound() { tone(BUZZER_PIN, 3500, 20); }

void playSuccessSound() {
  tone(BUZZER_PIN, 2000, 100); delay(100);
  tone(BUZZER_PIN, 3000, 100); delay(100);
  noTone(BUZZER_PIN);
}

void playErrorSound() {
  tone(BUZZER_PIN, 200, 300); delay(300);
  tone(BUZZER_PIN, 150, 300); delay(300);
  noTone(BUZZER_PIN);
}

void playResetSound() {
  tone(BUZZER_PIN, 500, 100); delay(100);
  tone(BUZZER_PIN, 1000, 100); delay(100);
  tone(BUZZER_PIN, 1500, 100); delay(100);
  noTone(BUZZER_PIN);
}

// ==========================================
// FORWARD DECLARATIONS
// ==========================================
void updateDisplay();
void clearLcdLines();
void resetScreen();
void savePasswordToEEPROM();
void loadPasswordFromEEPROM();
void wipeEEPROM();
void checkPassword();
void saveLockoutProgress(unsigned long remaining);
void clearLockoutState();

// ==========================================
// MAIN LOGIC
// ==========================================

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  
  lcd.init();
  lcd.backlight();

  // Factory Reset
  if (digitalRead(BUTTON_PIN) == LOW) {
    lcd.setCursor(0,0);
    lcd.print("FACTORY RESET...");
    playResetSound();
    wipeEEPROM();
    delay(2000);
  }

  // Load Password
  if (EEPROM.read(ADDR_IS_SET) == MAGIC_BYTE) {
    loadPasswordFromEEPROM();
    setupMode = false;
  } else {
    setupMode = true; 
  }

  // --- RESUME CHECK ---
  if (EEPROM.read(ADDR_LOCK_FLAG) == 1) {
    isLockedOut = true;
    
    // Read how much time was left when power died
    savedRemainingTime = EEPROM_readLong(ADDR_REMAINING);
    // Read what the total penalty level was (30s? 60s?)
    currentLockoutDuration = EEPROM_readLong(ADDR_TOTAL_DUR);

    // Sanity check
    if (savedRemainingTime == 0 || savedRemainingTime > 4000000) savedRemainingTime = BASE_LOCKOUT_DURATION;
    if (currentLockoutDuration < BASE_LOCKOUT_DURATION) currentLockoutDuration = BASE_LOCKOUT_DURATION;

    // Start immediately
    lockoutStartTime = millis();
    failedAttempts = 2; // Resume on thin ice
    
    lcd.setCursor(0, 0);
    lcd.print("SYSTEM RELOCKED");
    playErrorSound();
    delay(1000);
  }
  
  resetScreen();
}

void loop() {
  unsigned long currentMillis = millis();

  // --- 0. SECURITY LOCKOUT CHECK ---
  if (isLockedOut) {
    // Logic: If we just booted, we use 'savedRemainingTime' as the limit.
    // Otherwise, we use 'currentLockoutDuration'.
    // To simplify: We treat 'savedRemainingTime' as the active timer duration.
    
    unsigned long activeDuration = (savedRemainingTime > 0) ? savedRemainingTime : currentLockoutDuration;
    long elapsed = currentMillis - lockoutStartTime;
    long remainingMillis = activeDuration - elapsed;
    
    // Safety clamp
    if (remainingMillis < 0) remainingMillis = 0;

    // --- EEPROM CHECKPOINT (Every 1 second) ---
    if (currentMillis - lastEEPROMSave >= 1000) {
      lastEEPROMSave = currentMillis;
      saveLockoutProgress(remainingMillis);
    }

    // --- DISPLAY FIX (Added +1s buffer for visual rounding) ---
    long displaySeconds = (remainingMillis / 1000) + 1;
    // Special case: if it hits 0 but loop hasn't finished, show 0
    if (remainingMillis <= 100) displaySeconds = 0;

    lcd.setCursor(0, 1);
    lcd.print("LOCKED: " + String(displaySeconds) + "s   ");
    
    // Timer Finished?
    if (elapsed >= activeDuration) {
      isLockedOut = false;
      savedRemainingTime = 0; // Clear resume buffer
      clearLockoutState();    // Clear EEPROM

      // Exponential Backoff: Set attempts to 2 (1 strike left)
      failedAttempts = 2; 
      currentLockoutDuration = currentLockoutDuration * 2; // Next time is double

      resetScreen();
      playSuccessSound(); 
    }
    return; // Block input
  }

  char customKey = customKeypad.getKey();

  // --- 1. KEYPAD INPUT ---
  if (customKey) {
    lastInputTime = currentMillis; 
    waitingToMask = true; 

    if (enteredKey.length() >= 16) {
      playErrorSound(); 
    } else {
      playClickSound(); 
      enteredKey += customKey;
      updateDisplay(); 
    }
  }

  // --- 2. MASKING TIMER ---
  if (waitingToMask && (currentMillis - lastInputTime >= MASK_DELAY)) {
    waitingToMask = false;
    updateDisplay(); 
  }

  // --- 3. INACTIVITY TIMEOUT ---
  if (enteredKey.length() > 0 && (currentMillis - lastInputTime >= TIMEOUT)) {
    enteredKey = "";
    waitingToMask = false;
    lcd.setCursor(0, 1);
    lcd.print("TIMEOUT CLEAR   ");
    delay(1000);
    resetScreen();
  }

  // --- 4. SUBMIT BUTTON ---
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50); 
    if (digitalRead(BUTTON_PIN) == LOW) {
       waitingToMask = false;
       
       if (setupMode) {
         if (enteredKey.length() >= 4) savePasswordToEEPROM();
         else {
           lcd.setCursor(0,1);
           lcd.print("Too Short! >3");
           playErrorSound();
           delay(1000);
           resetScreen();
         }
       } else {
         checkPassword();
       }
       while(digitalRead(BUTTON_PIN) == LOW); 
    }
  }
}

// ==========================================
// IMPLEMENTATION
// ==========================================

void saveLockoutProgress(unsigned long remaining) {
  EEPROM.update(ADDR_LOCK_FLAG, 1); 
  EEPROM_writeLong(ADDR_REMAINING, remaining);
  EEPROM_writeLong(ADDR_TOTAL_DUR, currentLockoutDuration); // Keep track of the punishment level
}

void clearLockoutState() {
  EEPROM.update(ADDR_LOCK_FLAG, 0); 
  // We leave ADDR_TOTAL_DUR alone so backoff persists until successful login
}

void savePasswordToEEPROM() {
  EEPROM.write(ADDR_IS_SET, MAGIC_BYTE);
  byte len = enteredKey.length();
  EEPROM.write(ADDR_LEN, len);
  
  for (int i = 0; i < len; i++) {
    EEPROM.write(ADDR_PASS + i, enteredKey[i]);
  }
  
  masterKey = enteredKey; 
  setupMode = false;
  
  clearLcdLines();
  lcd.setCursor(0, 0);
  lcd.print("Saved to MEM!");
  playSuccessSound();
  delay(1500);
  resetScreen(); 
}

void loadPasswordFromEEPROM() {
  masterKey = "";
  byte len = EEPROM.read(ADDR_LEN);
  if (len > 16) { wipeEEPROM(); return; } 

  for (int i = 0; i < len; i++) {
    char c = (char)EEPROM.read(ADDR_PASS + i);
    masterKey += c;
  }
}

void wipeEEPROM() {
  for (int i = 0; i < 40; i++) EEPROM.write(i, 0); 
  setupMode = true;
  masterKey = "";
}

void checkPassword() {
  clearLcdLines();
  lcd.setCursor(0, 0);
  
  if (enteredKey == masterKey) {
    lcd.print("ACCESS GRANTED");
    
    // SUCCESS: Reset everything
    failedAttempts = 0;
    currentLockoutDuration = BASE_LOCKOUT_DURATION; 
    savedRemainingTime = 0;
    clearLockoutState(); 
    
    playSuccessSound();
  } else {
    lcd.print("ACCESS DENIED");
    playErrorSound(); 
    failedAttempts++;
    
    if (failedAttempts >= 3) {
      isLockedOut = true;
      savedRemainingTime = 0; // New lockout, so no saved time yet

      lcd.setCursor(0, 0);
      lcd.print("SYSTEM LOCKED!");
      
      // Save INITIAL state before alarm loop
      saveLockoutProgress(currentLockoutDuration);

      for(int i=0; i<3; i++) {
        tone(BUZZER_PIN, 1000); delay(100);
        tone(BUZZER_PIN, 2000); delay(100);
      }
      noTone(BUZZER_PIN);
      
      lockoutStartTime = millis(); 
    }
  }
  
  delay(1500); 
  resetScreen();
}

void updateDisplay() {
  lcd.setCursor(0, 1);
  String displayString = "";
  
  for (int i = 0; i < enteredKey.length(); i++) {
    if (i == enteredKey.length() - 1 && waitingToMask) {
       displayString += enteredKey[i]; 
    } else {
       displayString += '*'; 
    }
  }
  
  while (displayString.length() < 16) displayString += " ";
  lcd.print(displayString);
}

void clearLcdLines() {
  lcd.setCursor(0, 0); lcd.print("                ");
  lcd.setCursor(0, 1); lcd.print("                ");
}

void resetScreen() {
  enteredKey = "";
  waitingToMask = false;
  clearLcdLines();
  lcd.setCursor(0, 0);
  
  if (isLockedOut) return;

  if (setupMode) {
      lcd.print("SET NEW CODE:");
  } else {
      lcd.print("ENTER CODE:");
  }
}