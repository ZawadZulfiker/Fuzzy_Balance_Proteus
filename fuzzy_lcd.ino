#include <LiquidCrystal.h>

// Initialize the LiquidCrystal library with the numbers of the interface pins
LiquidCrystal lcd(13, 12, 11, 10, 9, 8);

const int currentSensorPin1 = A0;  // Pin where the ACS712 sensor1 is connected
const int currentSensorPin2 = A1;  // Pin where the ACS712 sensor2 is connected
float batteryCapacity1 = 1200.0;   // Battery1 capacity in mAh
float batteryCapacity2 = 1200.0;   // Battery2 capacity in mAh
float chargeConsumed1 = 0.0;       // in mAh
float chargeConsumed2 = 0.0;       // in mAh
float SOC_Coulomb1 = 100.0;        // Start at 100% based on Coulomb Counting
float SOC_Coulomb2 = 100.0;        // Start at 100% based on Coulomb Counting
float SOC_OCV1 = 100.0;            // Start at 100% based on OCV
float SOC_OCV2 = 100.0;            // Start at 100% based on OCV
unsigned long previousTime1 = 0;
unsigned long interval1 = 1000;       // Sampling interval (1 millisecond)
unsigned long lastOCVCheckTime1 = 0;
unsigned long OCVCheckInterval1 = 60000; // Recalibrate OCV every 60 seconds
unsigned long previousTime2 = 0;
unsigned long interval2 = 1000;       // Sampling interval (1 millisecond)
unsigned long lastOCVCheckTime2 = 0;
unsigned long OCVCheckInterval2 = 60000; // Recalibrate OCV every 60 seconds

// ACS712 sensor parameters (example for 5A version)
float sensorOffset = 512.0;       // Output at 0A (midpoint, 2.5V = 512 ADC reading)
float sensorScale = 66.0;

// Define output pin for duty cycle
const int duty_cycle_pin = 4; // Digital output pin for duty cycle

// Define output pin for PWM signal (for half-bridge control)
const int pwm_pin = 6; // Digital PWM pin (Pin 6 supports PWM on Arduino Uno)

// Define output pins for S1 and S2
const int s1_pin = 3;     // PWM output pin for S1
const int s2_pin = 5;    // PWM output pin for S2

// Variables to store the reference duty cycle (normalized)
float duty_cycle_normalized; // Value of the duty cycle in the range of -1 to +1

int duty_cycle; // Output variable (duty cycle)
const float pwmFrequency = 500.0; // Frequency of PWM in Hz

// Fuzzy logic membership functions
enum SocLevels { VL, L, M, H, VH }; // Membership levels

// Fuzzy membership function for soc_df and soc_avg
SocLevels fuzzySocDf(int soc_df_value) {
  if (soc_df_value == LOW) return VL;
  else if (soc_df_value == HIGH) return L; // Assuming L for HIGH and VL for LOW, adjust if needed
}

SocLevels fuzzySocAvg(int soc_avg_value) {
  if (soc_avg_value == LOW) return VL;
  else if (soc_avg_value == HIGH) return L; // Assuming L for HIGH and VL for LOW, adjust if needed
}

// Fuzzy rule evaluation function
int fuzzyRule(SocLevels soc_df_level, SocLevels soc_avg_level) {
  int duty_cycle = 0;

  switch (soc_df_level) {
    case VL:
      switch (soc_avg_level) {
        case VL: duty_cycle = 10; break;
        case L: duty_cycle = 20; break;
        case M: duty_cycle = 40; break;
        case H: duty_cycle = 60; break;
        default: duty_cycle = 70; break;
      }
      break;
    case L:
      switch (soc_avg_level) {
        case VL: duty_cycle = 20; break;
        case L: duty_cycle = 30; break;
        case M: duty_cycle = 40; break;
        case H: duty_cycle = 60; break;
        default: duty_cycle = 70; break;
      }
      break;
    case M:
      switch (soc_avg_level) {
        case VL: duty_cycle = 40; break;
        case L: duty_cycle = 50; break;
        case M: duty_cycle = 60; break;
        case H: duty_cycle = 80; break;
        default: duty_cycle = 90; break;
      }
      break;
    case H:
      switch (soc_avg_level) {
        case VL: duty_cycle = 60; break;
        case L: duty_cycle = 70; break;
        case M: duty_cycle = 80; break;
        case H: duty_cycle = 100; break;
        default: duty_cycle = 100; break;
      }
      break;
    case VH:
      switch (soc_avg_level) {
        case VL: duty_cycle = 70; break;
        case L: duty_cycle = 80; break;
        case M: duty_cycle = 90; break;
        case H: duty_cycle = 100; break;
        default: duty_cycle = 100; break;
      }
      break;
  }

  return duty_cycle;
}

void setup() {
  // Initialize LCD
  lcd.begin(16, 2); // Set up the LCD's number of columns and rows
  lcd.print("Initializing...");
  delay(2000);
  lcd.clear();

  // Set input and output pins
  pinMode(currentSensorPin1, INPUT);
  pinMode(currentSensorPin2, INPUT);
  pinMode(duty_cycle_pin, OUTPUT);

  // Set output pins for S1 and S2
  pinMode(s1_pin, OUTPUT);
  pinMode(s2_pin, OUTPUT);

  pinMode(pwm_pin, OUTPUT); // input as well

  // Start Serial communication
  Serial.begin(9600);
}

void loop() {
  unsigned long currentMillis1 = millis();
  unsigned long currentMillis2 = millis();

  // Measure current from ACS current sensor every millisecond
  if (currentMillis1 - previousTime1 >= interval1) {
    previousTime1 = currentMillis1;

    // Read the analog value from the ACS current sensor
    int sensorValue1 = analogRead(currentSensorPin1);
    int sensorValue2 = analogRead(currentSensorPin2);

    // Convert the analog reading to a voltage (range 0-5V)
    float current1 = ((sensorValue1 - sensorOffset) * 1000 * (5.0 / 1023.0)) / sensorScale;
    float current2 = ((sensorValue2 - sensorOffset) * 1000 * (5.0 / 1023.0)) / sensorScale;

    // Integrate current over time to calculate charge consumption in mAh
    chargeConsumed1 += (current1 / 1000.0) * (interval1 / 1000.0); // in Ah, integrating over milliseconds
    if (chargeConsumed1 > batteryCapacity1) {
      chargeConsumed1 = batteryCapacity1;
    }
    chargeConsumed2 += (current2 / 1000.0) * (interval1 / 1000.0); // in Ah, integrating over milliseconds
    if (chargeConsumed2 > batteryCapacity2) {
      chargeConsumed2 = batteryCapacity2;
    }

    // Calculate SOC from Coulomb Counting (in percentage)
    SOC_Coulomb1 = (1 - (chargeConsumed1 / batteryCapacity1)) * 100;
    if (SOC_Coulomb1 < 0) SOC_Coulomb1 = 0;  // Prevent negative SOC
    SOC_Coulomb2 = (1 - (chargeConsumed2 / batteryCapacity2)) * 100;
    if (SOC_Coulomb2 < 0) SOC_Coulomb2 = 0;  // Prevent negative SOC

    // Update LCD with current and SOC values
    lcd.setCursor(0, 0);
    lcd.print("I1:"); lcd.print(current1, 1); lcd.print("A ");
    lcd.setCursor(8, 0);
    lcd.print("SOC1:"); lcd.print(SOC_Coulomb1, 1); lcd.print("%");

    lcd.setCursor(0, 1);
    lcd.print("I2:"); lcd.print(current2, 1); lcd.print("A ");
    lcd.setCursor(8, 1);
    lcd.print("SOC2:"); lcd.print(SOC_Coulomb2, 1); lcd.print("%");
    delay(2000);  // Allow time for display
  }

  if (currentMillis1 - lastOCVCheckTime1 >= OCVCheckInterval1) {
    lastOCVCheckTime1 = currentMillis1;

    // Measure Open Circuit Voltage (OCV) to estimate SOC 1
    float batteryVoltage1 = analogRead(A2) * (25.0 / 1024.0);  // Assuming voltage divider scales to 0-5V range

    // Estimate SOC 1 based on voltage-to-SOC mapping (simplified for example)
    if (batteryVoltage1 >= 4.2) {
      SOC_OCV1 = 100;
    } else if (batteryVoltage1 >= 4.0 && batteryVoltage1 < 4.2) {
      SOC_OCV1 = 80.0 + (100.0 - 80.0) * (batteryVoltage1 - 4.0) / 0.2; // Linear from 80% to 100%
    } else if (batteryVoltage1 >= 3.7 && batteryVoltage1 < 4.0) {
      SOC_OCV1 = 20.0 + (80.0 - 20.0) * (batteryVoltage1 - 3.7) / 0.1; // Linear from 80% to 100%
    } else if (batteryVoltage1 >= 3.6 && batteryVoltage1 < 3.7) {
      SOC_OCV1 = 0.0 + (20.0 - 0.0) * (batteryVoltage1 - 3.6) / 0.1; // Linear from 80% to 100%
    } else {
      SOC_OCV1 = 0;
    }

    // Display OCV-based SOC 1 on LCD
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Voltage1:"); lcd.print(batteryVoltage1, 2); lcd.print("V");
    lcd.setCursor(0, 1);
    lcd.print("SOC1:"); lcd.print(SOC_OCV1, 1); lcd.print("%");
  }

  // Recalibrate SOC 2 using OCV every OCVCheckInterval (e.g., every 60 seconds)
  if (currentMillis2 - lastOCVCheckTime2 >= OCVCheckInterval2) {
    lastOCVCheckTime2 = currentMillis2;

    // Measure Open Circuit Voltage (OCV) to estimate SOC 2
    float batteryVoltage2 = analogRead(A3) * (25.0 / 1024.0);  // Assuming voltage divider scales to 0-5V range

    // Estimate SOC 2 based on voltage-to-SOC mapping (simplified for example)
    if (batteryVoltage2 >= 4.2) {
      SOC_OCV2 = 100;
    } else if (batteryVoltage2 >= 4.0 && batteryVoltage2 < 4.2) {
      SOC_OCV2 = 80.0 + (100.0 - 80.0) * (batteryVoltage2 - 4.0) / 0.2; // Linear from 80% to 100%
    } else if (batteryVoltage2 >= 3.7 && batteryVoltage2 < 4.0) {
      SOC_OCV2 = 20.0 + (80.0 - 20.0) * (batteryVoltage2 - 3.7) / 0.1; // Linear from 20% to 80%
    } else if (batteryVoltage2 >= 3.6 && batteryVoltage2 < 3.7) {
      SOC_OCV2 = 0.0 + (20.0 - 0.0) * (batteryVoltage2 - 3.6) / 0.1; // Linear from 0% to 20%
    } else {
      SOC_OCV2 = 0;
    }

    // Display OCV-based SOC 2 on LCD
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Voltage2:"); lcd.print(batteryVoltage2, 2); lcd.print("V");
    lcd.setCursor(0, 1);
    lcd.print("SOC2:"); lcd.print(SOC_OCV2, 1); lcd.print("%");
  }

  // Optionally, we can update the final SOC by averaging the two methods
  float soc1 = (SOC_Coulomb1 + SOC_OCV1) / 2;
  float soc2 = (SOC_Coulomb2 + SOC_OCV2) / 2;

  // Display the final combined SOC on LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Final SOC1:"); lcd.print(soc1, 1); lcd.print("%");
  lcd.setCursor(0, 1);
  lcd.print("Final SOC2:"); lcd.print(soc2, 1); lcd.print("%");

  // Read analog inputs for SOC1 and SOC2
  int soc1_raw = analogRead(currentSensorPin1);  // Read SOC1 value (0 to 1023)
  int soc2_raw = analogRead(currentSensorPin2);  // Read SOC2 value (0 to 1023)

  // Read inputs from digital pins (assumes LOW or HIGH values)
  int soc_df_value = abs(soc1_raw - soc2_raw); // Read the input for soc_df
  int soc_avg_value = ((soc1_raw + soc2_raw) / 2); // Read the input for soc_avg

  // Fuzzify the inputs
  SocLevels soc_df_level = fuzzySocDf(soc_df_value);
  SocLevels soc_avg_level = fuzzySocAvg(soc_avg_value);

  // Calculate duty cycle using fuzzy rules
  duty_cycle = fuzzyRule(soc_df_level, soc_avg_value);

  // Output the duty cycle (map the duty cycle to a PWM range 0-255 for output)
  analogWrite(duty_cycle_pin, map(duty_cycle, 0, 100, 0, 255)); // Map duty cycle to PWM range

  // Normalize the duty cycle to the range [-1, 1] (for linear operation)
  duty_cycle_normalized = map(duty_cycle, 0, 100, -255, 255) / 255.0;

  // Calculate the PWM signal's duty cycle value based on normalized reference signal
  int pwm_value = (duty_cycle_normalized + 1) * (255 / 2);

  // Generate the PWM signal at 500 Hz
  analogWrite(pwm_pin, pwm_value);

  // Read the PWM signal (assuming it's a value between 0 and 255)
  int pwm_signal = pwm_value / 4;  // Read PWM and scale to 0-255 range (Arduino analog input is 0-1023)

  // Display debugging information on LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Duty:"); lcd.print(duty_cycle, 1); lcd.print("%");
  lcd.setCursor(0, 1);
  lcd.print("PWM:"); lcd.print(pwm_value);
  delay(1500);

  lcd.setCursor(0, 1);
  lcd.print("SOC DF:"); lcd.print(soc_df_value);
  lcd.setCursor(8, 1);
  lcd.print("AVG:"); lcd.print(soc_avg_value);

  // Wait for the display to update
  delay(1500);

  // Debugging output to Serial Monitor
    lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Duty Cycle::"); lcd.print(duty_cycle, 1);
  lcd.setCursor(0, 1);
  lcd.print("Normalized DC:"); lcd.print(duty_cycle_normalized, 1);
  delay(1500);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("PWM Freq:"); lcd.print(pwmFrequency, 0); lcd.print("Hz");
  lcd.setCursor(0, 1);
  lcd.print("PWM Value:"); lcd.print(pwm_value, 1);
  delay(1500);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SOC1:"); lcd.print(soc1, 1);
  lcd.setCursor(8, 0);
  lcd.print(" SOC2:"); lcd.print(soc2);

  lcd.setCursor(0, 1);
  lcd.print("PWM Signal:"); lcd.print(pwm_signal);
 

  // Implement the conditions for S1 and S2
  if (soc2 < soc1) {
    analogWrite(s1_pin, pwm_signal);  // Set S1 to PWM
    analogWrite(s2_pin, 0);           // Set S2 to 0
  } else if (soc1 < soc2) {
    analogWrite(s1_pin, 0);           // Set S1 to 0
    analogWrite(s2_pin, pwm_signal);  // Set S2 to PWM
  } else {
    analogWrite(s1_pin, 0);           // Set S1 to 0
    analogWrite(s2_pin, 0);           // Set S2 to 0
  }

  if (soc1 == 100 && soc2 == 100) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Cells fully charged");
    lcd.setCursor(0, 1);
    lcd.print("charged");
  } else if (soc1 < 100 || soc2 < 100) {
    lcd.clear();
    lcd.print("Balancing cells");
  } else if (soc1 == soc2 && (soc1 != 100 || soc2 != 100)) {
    lcd.clear();
    lcd.print("Cells balanced");
  }

  // Wait for a bit before the next loop
  delay(1000); // Adjust the delay time as needed
}
