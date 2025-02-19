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

// Define input pins for SOC1 and SOC2
//const int soc1_pin = A0;  // Analog pin for SOC1
//const int soc2_pin = A1;  // Analog pin for SOC2

// Define output pin for duty cycle
const int duty_cycle_pin = 4; // Digital output pin for duty cycle

// Define output pin for PWM signal (for half-bridge control)
const int pwm_pin = 6; // Digital PWM pin (Pin 6 supports PWM on Arduino Uno)

// Define variables for frequency and duty cycle calculation
const float pwm_frequency = 500.0; // 500 Hz PWM signal
const int pwm_resolution = 255; // 8-bit PWM resolution (0 to 255)

// Define output pins for S1 and S2
const int s1_pin = 9;     // PWM output pin for S1
const int s2_pin = 10;    // PWM output pin for S2

// Variables to store the reference duty cycle (normalized)
float duty_cycle_normalized; // Value of the duty cycle in the range of -1 to +1

int duty_cycle; // Output variable (duty cycle)

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
  // Initialize serial communication
  Serial.begin(9600);

  // Set input and output pins
  pinMode(currentSensorPin1, INPUT);
  pinMode(currentSensorPin2, INPUT);
  pinMode(duty_cycle_pin, OUTPUT);

  // Set output pins for S1 and S2
  pinMode(s1_pin, OUTPUT);
  pinMode(s2_pin, OUTPUT);
  
  pinMode(pwm_pin, OUTPUT); //input as well
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
    float current1=((sensorValue1-sensorOffset)*1000*(5.0/1023.0))/sensorScale;
    float current2=((sensorValue1-sensorOffset)*1000*(5.0/1023.0))/sensorScale;
        
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
    
    // Display Coulomb SOC
    Serial.print("Current 1 (Coulomb Counting): "); Serial.print(current1); Serial.println(" A");
    Serial.print("Charge Consumed 1: "); Serial.print(chargeConsumed1); Serial.println(" mAh");
    Serial.print("SOC 1 (Coulomb Counting): "); Serial.print(SOC_Coulomb1); Serial.println(" %");
    Serial.println("__________________________________________________________________");
    Serial.print("Current 2 (Coulomb Counting): "); Serial.print(current2); Serial.println(" A");
    Serial.print("Charge Consumed 2: "); Serial.print(chargeConsumed2); Serial.println(" mAh");
    Serial.print("SOC 2 (Coulomb Counting): "); Serial.print(SOC_Coulomb2); Serial.println(" %");
  }

  // Recalibrate SOC 1 using OCV every OCVCheckInterval (e.g., every 60 seconds)
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
            
    // Display OCV-based SOC 1
    Serial.println("__________________________________________________________________");
    Serial.println("Battery Voltage 1: "); Serial.print(batteryVoltage1); Serial.println(" V");
    Serial.println("SOC 1 (OCV): "); Serial.print(SOC_OCV1); Serial.println(" %");
  }
  else {
    // Measure Open Circuit Voltage (OCV) to estimate SOC 1
    float batteryVoltage1 = analogRead(A2) * (25.0 / 1024.0);  // Assuming voltage divider scales to 0-5V range
    
    // Display OCV-based SOC 1
    Serial.println("__________________________________________________________________");
    Serial.println("Battery Voltage 1: "); Serial.print(batteryVoltage1); Serial.println(" V");
    Serial.println("SOC 1 (OCV): "); Serial.print(SOC_OCV1); Serial.println(" %");
  }
  
  // Recalibrate SOC 2 using OCV every OCVCheckInterval (e.g., every 60 seconds)
  if (currentMillis2 - lastOCVCheckTime2 >= OCVCheckInterval2) {
    lastOCVCheckTime2 = currentMillis2;

    // Measure Open Circuit Voltage (OCV) to estimate SOC
    float batteryVoltage2 = analogRead(A3) * (25.0 / 1024.0);  // Assuming voltage divider scales to 0-5V range
    
    // Estimate SOC based on voltage-to-SOC mapping (simplified for example)
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
    
    // Display OCV-based SOC 2
    Serial.println("Battery Voltage 2: "); Serial.print(batteryVoltage2); Serial.println(" V");
    Serial.println("SOC 2 (OCV): "); Serial.print(SOC_OCV2); Serial.println(" %");
  }
  else {
    // Measure Open Circuit Voltage (OCV) to estimate SOC
    float batteryVoltage2 = analogRead(A3) * (25.0 / 1024.0);  // Assuming voltage divider scales to 0-5V range

    // Display OCV-based SOC 2
    Serial.println("Battery Voltage 2: "); Serial.print(batteryVoltage2); Serial.println(" V");
    Serial.println("SOC 2 (OCV): "); Serial.print(SOC_OCV2); Serial.println(" %");
  }
  
  // Optionally, we can update the final SOC by averaging the two methods
  float soc1 = (SOC_Coulomb1 + SOC_OCV1) / 2;
  float soc2 = (SOC_Coulomb2 + SOC_OCV2) / 2;

  // Display the final combined SOC
  Serial.println("__________________________________________________________________");
  Serial.print("Final SOC 1: "); Serial.print(soc1); Serial.println(" %");
  Serial.print("Final SOC 2: "); Serial.print(soc2); Serial.println(" %");
  Serial.println("__________________________________________________________________");
  
  //delay(1);  // Wait 1 millisecond before reading again

  
  // Read analog inputs for SOC1 and SOC2
  //int soc1 = analogRead(soc1_pin);  // Read SOC1 value (0 to 1023)
  //int soc2 = analogRead(soc2_pin);  // Read SOC2 value (0 to 1023)

  // Read inputs from digital pins (assumes LOW or HIGH values)
  int soc_df_value = abs(soc1-soc2); // Read the input for soc_df
  int soc_avg_value = ((soc1+soc2)/2); // Read the input for soc_avg

  // Fuzzify the inputs
  SocLevels soc_df_level = fuzzySocDf(soc_df_value);
  SocLevels soc_avg_level = fuzzySocAvg(soc_avg_value);

  // Calculate duty cycle using fuzzy rules
  duty_cycle = fuzzyRule(soc_df_level, soc_avg_level);

  // Output the duty cycle (map the duty cycle to a PWM range 0-255 for output)
  analogWrite(duty_cycle_pin, map(duty_cycle, 0, 100, 0, 255)); // Map duty cycle to PWM range

  // Normalize the duty cycle to the range [-1, 1] (for linear operation)
  duty_cycle_normalized = map(duty_cycle, 0, 100, -255, 255) / 255.0;

  // Calculate the PWM signal's duty cycle value based on normalized reference signal
  int pwm_value = (duty_cycle_normalized + 1) * (pwm_resolution / 2);

  // Generate the PWM signal at 500 Hz
  analogWrite(pwm_pin, pwm_value);

  // Read the PWM signal (assuming it's a value between 0 and 255)
  int pwm_signal = pwm_value / 4;  // Read PWM and scale to 0-255 range (Arduino analog input is 0-1023)

  // Output result to serial monitor for debugging
  Serial.print("SOC DF: ");
  Serial.print(soc_df_value);
  Serial.print(", SOC AVG: ");
  Serial.println(soc_avg_value);
  Serial.println("__________________________________________________________________");
  //Serial.print(", Duty Cycle: ");
  //Serial.println(duty_cycle);

  // Wait for a bit before the next loop
  delay(500); // Adjust the delay time as needed

  // Debugging: print the normalized duty cycle and PWM value to the serial monitor
  Serial.print("Duty Cycle: ");
  Serial.print(duty_cycle);
  Serial.print("%, Normalized Duty Cycle: ");
  Serial.print(duty_cycle_normalized);
  Serial.print(", PWM Value: ");
  Serial.println(pwm_value);
  Serial.println("__________________________________________________________________");

  // Wait for a short time before updating the PWM signal (to keep the frequency constant)
  delay(1); // Adjust delay to control the update rate (ensuring 500Hz PWM)

  // Debugging: Print SOC1, SOC2, and PWM signal to the serial monitor
  Serial.print("SOC1: ");
  Serial.print(soc1);
  Serial.print(", SOC2: ");
  Serial.print(soc2);
  Serial.print(", PWM Signal: ");
  Serial.println(pwm_signal);
  Serial.println("__________________________________________________________________");

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

  Serial.println("__________________________________________________________________");
  if (soc1==100 && soc2==100 ) {
    Serial.println("Cells are fully charged and balanced!");
  } else if (soc1 < 100 || soc2<100) {
    Serial.println("Cells are not fully charged and balanced!");
    Serial.println("Balancing");
  } else if (soc1==soc2 && (soc1!=100 || soc2!=100)) {
    Serial.println("Cells are not fully charged but balanced");
  } else {
  }
  Serial.println("__________________________________________________________________");

  // Wait for a bit before the next loop
  delay(1000); // Adjust the delay time as needed
}
