// Finger gun glove. Same wiring as switch_led_buzz.ino, now talking to the tracker.
//
//   glove -> PC   "READY" once at boot, "T" on every trigger press
//   PC -> glove   'F' shot fired   'R' reload   'H' got hit   '?' -> replies "FG1"
//
// 115200 baud. Pressing the switch still buzzes and flashes by itself, exactly like
// switch_led_buzz.ino, so the glove works with no PC attached. The PC sends 'F' for the
// shots it detects from the camera (thumb drop, recoil kick), so every shot feels the same.
//
// Nothing here uses delay(): while the buzzer sounds, presses and commands still get read.

const int switchPin = 6;
const int buzzerPin = 7;
const int ledPin = 8;

const unsigned long DEBOUNCE_MS = 30;

// A pattern is on, off, on, off... durations in ms, ending with 0.
const unsigned int FIRE[] = {100, 0};
const unsigned int RELOAD[] = {40, 60, 40, 0};
const unsigned int HIT[] = {300, 0};

const unsigned int *pattern = NULL;
int patternStep = 0;
unsigned long stepStarted = 0;

bool pressed = false;
unsigned long lastChange = 0;

void setOutputs(bool on) {
  digitalWrite(ledPin, on ? HIGH : LOW);
  digitalWrite(buzzerPin, on ? HIGH : LOW);
}

void play(const unsigned int *p) {
  pattern = p;
  patternStep = 0;
  stepStarted = millis();
  setOutputs(true);
}

void updatePattern() {
  if (pattern == NULL) return;
  if (millis() - stepStarted < pattern[patternStep]) return;
  patternStep++;
  if (pattern[patternStep] == 0) {
    pattern = NULL;
    setOutputs(false);
    return;
  }
  stepStarted = millis();
  setOutputs(patternStep % 2 == 0);   // even steps sound, odd steps are the gaps
}

void readSwitch() {
  bool reading = digitalRead(switchPin) == HIGH;
  unsigned long now = millis();
  // Report on the first HIGH sample, then ignore the contact bounce that follows.
  // Waiting for the signal to settle first would add that wait to every shot.
  if (reading != pressed && now - lastChange >= DEBOUNCE_MS) {
    pressed = reading;
    lastChange = now;
    if (pressed) {
      Serial.println("T");
      play(FIRE);
    }
  }
}

void readCommands() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == 'F') play(FIRE);
    else if (c == 'R') play(RELOAD);
    else if (c == 'H') play(HIT);
    else if (c == '?') Serial.println("FG1");
  }
}

void setup() {
  pinMode(switchPin, INPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  setOutputs(false);
  Serial.begin(115200);
  Serial.println("READY");
}

void loop() {
  readSwitch();
  readCommands();
  updatePattern();
}
