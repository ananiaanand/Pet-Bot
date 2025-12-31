#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Servo.h>
#include <SoftwareSerial.h>

// DEBUG MACRO - Add at top
#define DEBUG
#ifdef DEBUG
#define DPRINT(x) Serial.print(x)
#define DPRINTLN(x) Serial.println(x)
#else
#define DPRINT(x)
#define DPRINTLN(x)
#endif

// Pin definitions - ARDUINO UNO COMPATIBLE
#define TOUCH_PIN 2      // Digital pin for touch sensor (using tactile switch)
#define SWITCH_PIN 3
#define SERVO_PIN 9      // PWM pin for servo
#define BUZZER_PIN 5     // PWM pin for buzzer
#define OLED_SDA A4      // I2C pins for Uno
#define OLED_SCL A5
#define VOICE_RX 10      // SoftwareSerial RX for voice module
#define VOICE_TX 11      // SoftwareSerial TX

// Display dimensions
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// Touch threshold (for digital switch)
#define TOUCH_THRESHOLD 500  // ms hold time

// Timing constants
#define DEBOUNCE_DELAY 50
#define SINGLE_CLICK_MAX 500
#define DOUBLE_CLICK_MAX 600
#define WEATHER_DISPLAY_TIME 10000
#define POMODORO_WORK_TIME 1500000UL  // 25 minutes in ms
#define POMODORO_BREAK_TIME 300000UL  // 5 minutes in ms
#define SERVO_TRANSITION_TIME 500
#define DISPLAY_REFRESH 500
#define BEEP_DURATION 100
#define BEEP_INTERVAL 300
#define DISPLAY_REFRESH 500

// Face bitmaps - 8x8 pixels
static const unsigned char PROGMEM neutralFace[] = {
  0b00111100,
  0b01000010,
  0b10011001,
  0b10000001,
  0b10000001,
  0b10000001,
  0b01000010,
  0b00111100
};

static const unsigned char PROGMEM happyFace[] = {
  0b00111100,
  0b01000010,
  0b10011001,
  0b10000001,
  0b10000001,
  0b10111101,
  0b01000010,
  0b00111100
};

static const unsigned char PROGMEM sadFace[] = {
  0b00111100,
  0b01000010,
  0b10011001,
  0b10000001,
  0b10000001,
  0b10000001,
  0b01011010,
  0b00111100
};

static const unsigned char PROGMEM surprisedFace[] = {
  0b00111100,
  0b01000010,
  0b10100101,
  0b10000001,
  0b10011001,
  0b10000001,
  0b01000010,
  0b00111100
};

// State machine
enum State { 
  IDLE_FACE, 
  MOVING_FORWARD, 
  MOVING_BACKWARD, 
  SHOW_WEATHER, 
  POMODORO_WORK, 
  POMODORO_BREAK, 
  BEEPING
};
State currentState = IDLE_FACE;

// Global objects
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Servo petServo;
SoftwareSerial voiceSerial(VOICE_RX, VOICE_TX);  // RX, TX

// Timers
unsigned long lastDisplayUpdate = 0;
unsigned long stateStartTime = 0;
unsigned long lastSwitchTime = 0;
unsigned long lastClickTime = 0;
unsigned long lastTouchTime = 0;
unsigned long lastBeepTime = 0;
unsigned long beepOnTime = 0;

// Servo control
int targetServoAngle = 90;
int currentServoAngle = 90;
unsigned long servoStartTime = 0;

// Switch handling
int switchClickCount = 0;
bool lastSwitchState = HIGH;

// Touch handling
bool touchDetected = false;
int touchCounter = 0;
bool showPurr = false;
bool buzzerOn = false;
int beepCount = 0;

// Pomodoro
int pomodoroPhase = 0; // 0=off, 1=work, 2=break

// Voice buffer
String voiceBuffer = "";

// Function prototypes
void handleTouch();
void transitionState(State newState);
void updateDisplay();
void handleVoiceCommands();
void handleSwitch();
void updatePomodoro();
void moveServoSmoothly();
void updateBuzzer();

void setup() {
  Serial.begin(115200);
  voiceSerial.begin(9600);
  
  DPRINTLN("Pet Bot Initializing...");
  
  // Initialize pins
  pinMode(TOUCH_PIN, INPUT_PULLUP);
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  
  // Initialize OLED
  Wire.begin();
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    DPRINTLN("OLED init failed");
    while(1) { 
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      delay(100);
    }
  }
  
  // Initialize servo
  petServo.attach(SERVO_PIN);
  petServo.write(90);
  delay(500);
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  DPRINTLN("Pet Bot Ready");
  transitionState(IDLE_FACE);
}

void handleTouch() {
  static unsigned long lastTouchCheck = 0;
  unsigned long now = millis();
  
  if (now - lastTouchCheck < 50) return;  // Debounce
  lastTouchCheck = now;
  
  bool touchState = digitalRead(TOUCH_PIN);
  
  if (touchState == LOW) {  // Button pressed (pull-up logic)
    static unsigned long touchStart = 0;
    static bool touchActive = false;
    
    if (!touchActive) {
      touchStart = now;
      touchActive = true;
      DPRINTLN("Touch started");
    }
    
    // Detect long press (>500ms)
    if (now - touchStart > TOUCH_THRESHOLD && !touchDetected) {
      touchDetected = true;
      DPRINTLN("Long touch detected");
    }
  } else {
    touchDetected = false;
  }
}

void transitionState(State newState) {
  DPRINT("Transitioning from ");
  DPRINT(currentState);
  DPRINT(" to ");
  DPRINTLN(newState);
  
  // Cleanup current state
  switch(currentState) {
    case BEEPING:
      digitalWrite(BUZZER_PIN, LOW);
      buzzerOn = false;
      beepCount = 0;
      break;
    default:
      break;
  }
  
  currentState = newState;
  stateStartTime = millis();
  
  // Initialize new state
  switch(newState) {
    case IDLE_FACE:
      targetServoAngle = 90;
      showPurr = false;
      break;
    case MOVING_FORWARD:
      targetServoAngle = 0;
      servoStartTime = millis();
      break;
    case MOVING_BACKWARD:
      targetServoAngle = 180;
      servoStartTime = millis();
      break;
    case POMODORO_WORK:
      pomodoroPhase = 1;
      break;
    case POMODORO_BREAK:
      pomodoroPhase = 2;
      break;
    default:
      break;
  }
}

void updateDisplay() {
  display.clearDisplay();
  
  switch(currentState) {      
    case IDLE_FACE:
      if (showPurr) {
        display.drawBitmap(60, 25, neutralFace, 8, 8, SSD1306_WHITE);
        display.setCursor(40, 45);
        display.print("Purr...");
      } else {
        const unsigned char* face = neutralFace;
        if (touchCounter % 3 == 1) face = happyFace;
        else if (touchCounter % 3 == 2) face = surprisedFace;
        display.drawBitmap(60, 25, face, 8, 8, SSD1306_WHITE);
      }
      display.setCursor(0, 0);
      display.print("IDLE");
      break;
      
    case MOVING_FORWARD:
      display.drawBitmap(60, 25, happyFace, 8, 8, SSD1306_WHITE);
      display.setCursor(35, 45);
      display.print(">> Forward");
      display.setCursor(0, 0);
      display.print("Angle: ");
      display.print(currentServoAngle);
      break;
      
    case MOVING_BACKWARD:
      display.drawBitmap(60, 25, happyFace, 8, 8, SSD1306_WHITE);
      display.setCursor(35, 45);
      display.print("Backward <<");
      display.setCursor(0, 0);
      display.print("Angle: ");
      display.print(currentServoAngle);
      break;
      
    case SHOW_WEATHER:
      display.setCursor(0, 0);
      display.println("Weather Mode");
      display.setCursor(20, 30);
      display.setTextSize(2);
      display.println("Offline");
      display.setTextSize(1);
      display.setCursor(0, 55);
      display.print("Use switch for other modes");
      break;
      
    case POMODORO_WORK:
      display.drawBitmap(60, 5, sadFace, 8, 8, SSD1306_WHITE);
      display.setCursor(0, 25);
      display.println("Work Time");
      
      unsigned long elapsed = millis() - stateStartTime;
      int progress = (elapsed * 100) / POMODORO_WORK_TIME;
      progress = constrain(progress, 0, 100);
      
      display.setCursor(0, 45);
      display.print("Progress: ");
      display.print(progress);
      display.print("%");
      
      // Progress bar
      display.drawRect(0, 55, 128, 8, SSD1306_WHITE);
      display.fillRect(2, 57, (progress * 124) / 100, 4, SSD1306_WHITE);
      break;
      
    case POMODORO_BREAK:
      display.drawBitmap(60, 5, happyFace, 8, 8, SSD1306_WHITE);
      display.setCursor(0, 25);
      display.println("Break Time");
      
      elapsed = millis() - stateStartTime;
      int remaining = 100 - (elapsed * 100) / POMODORO_BREAK_TIME;
      remaining = constrain(remaining, 0, 100);
      
      display.setCursor(0, 45);
      display.print("Remaining: ");
      display.print(remaining);
      display.print("%");
      
      display.drawRect(0, 55, 128, 8, SSD1306_WHITE);
      display.fillRect(2, 57, (remaining * 124) / 100, 4, SSD1306_WHITE);
      break;
      
    case BEEPING:
      display.drawBitmap(60, 25, surprisedFace, 8, 8, SSD1306_WHITE);
      display.setCursor(40, 45);
      display.print("TIME UP!");
      display.setCursor(0, 0);
      display.print("Touch to stop");
      break;
  }
  
  display.display();
}

void handleVoiceCommands() {
  while (voiceSerial.available()) {
    char c = voiceSerial.read();
    if (c == '\n' || c == '\r') {
      if (voiceBuffer.length() > 0) {
        voiceBuffer.toLowerCase();
        voiceBuffer.trim();
        
        DPRINT("Voice Cmd: ");
        DPRINTLN(voiceBuffer);
        
        if ((voiceBuffer.indexOf("forward") >= 0 || voiceBuffer.indexOf("move forward") >= 0) &&
            (currentState != POMODORO_WORK && currentState != POMODORO_BREAK && currentState != BEEPING)) {
          transitionState(MOVING_FORWARD);
        }
        else if ((voiceBuffer.indexOf("backward") >= 0 || voiceBuffer.indexOf("move backward") >= 0) &&
                 (currentState != POMODORO_WORK && currentState != POMODORO_BREAK && currentState != BEEPING)) {
          transitionState(MOVING_BACKWARD);
        }
        else if (voiceBuffer.indexOf("stop") >= 0 && 
                 (currentState == MOVING_FORWARD || currentState == MOVING_BACKWARD)) {
          transitionState(IDLE_FACE);
        }
        else if (voiceBuffer.indexOf("status") >= 0) {
          DPRINTLN("Current State: ");
          DPRINTLN(currentState);
        }
        
        voiceBuffer = "";
      }
    } else {
      voiceBuffer += c;
    }
  }
}

void handleSwitch() {
  bool currentSwitchState = digitalRead(SWITCH_PIN);
  unsigned long now = millis();
  
  if (now - lastSwitchTime < DEBOUNCE_DELAY) return;
  
  // Rising edge (button release) detection
  if (lastSwitchState == LOW && currentSwitchState == HIGH) {
    unsigned long clickDuration = now - lastSwitchTime;
    if (clickDuration > DEBOUNCE_DELAY && clickDuration < 500) {
      switchClickCount++;
      
      if (switchClickCount == 1) {
        lastClickTime = now;
      } else if (switchClickCount == 2) {
        if (now - lastClickTime <= DOUBLE_CLICK_MAX) {
          // Double click - Pomodoro
          if (currentState == IDLE_FACE || currentState == SHOW_WEATHER) {
            transitionState(POMODORO_WORK);
          }
          switchClickCount = 0;
          DPRINTLN("Double click - Pomodoro");
          return;
        } else {
          // Too slow, reset to single click
          switchClickCount = 1;
          lastClickTime = now;
        }
      }
      
      lastSwitchTime = now;
    }
  }
  
  // Single click timeout
  if (switchClickCount == 1 && (now - lastClickTime > SINGLE_CLICK_MAX)) {
    if (currentState == IDLE_FACE || currentState == POMODORO_WORK || currentState == POMODORO_BREAK) {
      transitionState(SHOW_WEATHER);
      DPRINTLN("Single click - Weather");
    }
    switchClickCount = 0;
  }
  
  // Reset long press
  if (switchClickCount > 0 && (now - lastClickTime > 1000)) {
    switchClickCount = 0;
  }
  
  lastSwitchState = currentSwitchState;
}

void updatePomodoro() {
  unsigned long now = millis();
  unsigned long elapsed = now - stateStartTime;
  
  switch(currentState) {
    case POMODORO_WORK:
      if (elapsed >= POMODORO_WORK_TIME) {
        DPRINTLN("Work time complete, starting beep");
        transitionState(BEEPING);
      }
      break;
      
    case POMODORO_BREAK:
      if (elapsed >= POMODORO_BREAK_TIME) {
        DPRINTLN("Break time complete");
        transitionState(IDLE_FACE);
      }
      break;
  }
}

void moveServoSmoothly() {
  unsigned long now = millis();
  if (currentServoAngle != targetServoAngle) {
    unsigned long elapsed = now - servoStartTime;
    if (elapsed >= SERVO_TRANSITION_TIME) {
      currentServoAngle = targetServoAngle;
    } else {
      int progress = (elapsed * 100) / SERVO_TRANSITION_TIME;
      currentServoAngle = 90 + ((targetServoAngle - 90) * progress) / 100;
    }
    currentServoAngle = constrain(currentServoAngle, 0, 180);
    petServo.write(currentServoAngle);
  }
}

void updateBuzzer() {
  unsigned long now = millis();
  
  if (currentState != BEEPING) {
    if (buzzerOn) {
      digitalWrite(BUZZER_PIN, LOW);
      buzzerOn = false;
    }
    return;
  }
  
  if (beepCount < 3) {
    if (now - lastBeepTime >= BEEP_INTERVAL && !buzzerOn) {
      digitalWrite(BUZZER_PIN, HIGH);
      buzzerOn = true;
      beepOnTime = now;
      beepCount++;
      lastBeepTime = now;
      DPRINT("Beep ");
      DPRINTLN(beepCount);
    }
  }
  
  if (buzzerOn && (now - beepOnTime >= BEEP_DURATION)) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerOn = false;
  }
}

void loop() {
  unsigned long now = millis();
  
  // Handle touch
  handleTouch();
  if (touchDetected) {
    touchDetected = false;
    
    if (currentState == BEEPING) {
      // Stop beep, start break
      transitionState(POMODORO_BREAK);
      DPRINTLN("Touch stopped beep");
    } else if (currentState == IDLE_FACE) {
      touchCounter++;
      lastTouchTime = now;
      DPRINT("Pet touched, counter: ");
      DPRINTLN(touchCounter);
    }
  }
  
  // Handle touch hold for purr
  if (currentState == IDLE_FACE) {
    if (now - lastTouchTime > 500 && !showPurr) {
      showPurr = true;
      DPRINTLN("Purr mode on");
    } else if (now - lastTouchTime > 2000) {
      showPurr = false;
      touchCounter = 0;
      DPRINTLN("Purr mode off");
    }
  }
  
  // State timeouts
  switch(currentState) {
    case SHOW_WEATHER:
      if (now - stateStartTime >= WEATHER_DISPLAY_TIME) {
        transitionState(IDLE_FACE);
        DPRINTLN("Weather display timeout");
      }
      break;
  }
  
  // Update systems
  handleVoiceCommands();
  handleSwitch();
  updatePomodoro();
  moveServoSmoothly();
  updateBuzzer();
  
  // Update display
  if (now - lastDisplayUpdate >= DISPLAY_REFRESH) {
    updateDisplay();
    lastDisplayUpdate = now;
  }
  
  // Simple heartbeat
  static unsigned long lastHeartbeat = 0;
  if (now - lastHeartbeat >= 1000) {
    lastHeartbeat = now;
    DPRINT("State: ");
    DPRINT(currentState);
    DPRINT(" | Servo: ");
    DPRINT(currentServoAngle);
    DPRINT(" | TouchCtr: ");
    DPRINTLN(touchCounter);
  }
  
  // Non-blocking delay
  static unsigned long lastLoopTime = 0;
  if (now - lastLoopTime < 10) {
    delay(1);  // Small delay to prevent CPU hogging
  }
  lastLoopTime = now;
}
