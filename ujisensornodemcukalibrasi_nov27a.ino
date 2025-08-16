#define trigPin D1
#define echoPin D2

void setup() {
  Serial.begin(9600);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
}

void loop() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH);
  
  float temperature = 29.0; // Masukkan suhu udara
  float speedOfSound = 331.3 + (0.606 * temperature); // dalam m/s
  speedOfSound = speedOfSound * 100 / 1000000; // Konversi ke cm/us
  float distance = (duration * speedOfSound) / 2;

  float calibrationFactor = 1.0015;
  distance = distance * calibrationFactor;

  Serial.print("Jarak: ");
  Serial.print(distance);
  Serial.println(" cm");
  
  delay(500);
}
