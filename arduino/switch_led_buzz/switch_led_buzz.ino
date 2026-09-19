const int switchPin = 6;
const int buzzerPin = 7;
const int ledPin = 8;

void setup() {
  pinMode(switchPin, INPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
}

void loop() {
  if (digitalRead(switchPin) == HIGH) {
    digitalWrite(ledPin, HIGH);
    digitalWrite(buzzerPin, HIGH);

    delay(100);

    digitalWrite(ledPin, LOW);
    digitalWrite(buzzerPin, LOW);

    while (digitalRead(switchPin) == HIGH) {
      delay(10);
    }
  }
}