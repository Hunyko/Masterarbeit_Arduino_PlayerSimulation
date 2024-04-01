#include <Arduino.h>
#include <EtherCard.h>

#define SS 10 // Slave Select Pin Nummer

// Define delay between actions in milliseconds
#define ACTION_DELAY 250
// Define Pin On/Off time in milliseconds
#define PIN_ON_OFF_TIME 1
// Define refill per shot and total refill time in seconds
#define REFILL_TIME_TOTAL 12
#define REFILL_TIME_ONE_SHOT 0.5
// Define maximum shots and lifecount
#define MAX_SHOTS 24
#define MAX_LIFECOUNT 200
// Define state machine states
#define ACTION_FIRING 0
#define ACTION_GETTING_HIT 1
#define ACTION_REFILLING 2
#define ACTION_WALKING 3
// Define pins
#define HIT_DETECTION_PIN 6
#define SHOT_PIN 5
#define PUMP_PIN 4
#define INTERRUPT_PIN_2 2
#define nRF_FLAG_PIN_1 7
#define nRF_FLAG_PIN_2 8
#define nRF_FLAG_PIN_3 9

// Needed for UDP
uint8_t Ethernet::buffer[700];
byte mymac[] = { 0x74,0x69,0x69,0x2D,0x30,0x31 };
static BufferFiller bfill;
uint16_t nSourcePort = 1234;
uint16_t nDestinationPort = 5678;
//IP Address Arduino
static byte myip[] = { 192,168,178,22 };
//Gateway ip address
static byte gwip[] = { 192,168,178,1 };
// IP address target
uint8_t ipDestinationAddress[IP_LEN] = {192,168,178,109};

uint16_t udp_counter = 0;

// Allow for different device configurations
bool watergun = false;
bool gamemaster = true;
bool use_udp = true;
bool static_ip = false;

// Some necessary global variables
int player_lifecount = MAX_LIFECOUNT;
int shots_left = MAX_SHOTS;
int shots_fired_total = 0;
float pump_cycles_total = 0.0;
bool is_refilling = false;
unsigned long refill_start_time = 0;
unsigned long refill_end_time = 0;
unsigned long last_action_time = 0;
int state = ACTION_WALKING;

// Function prototypes
void refill(int refill_duration, bool can_be_interrupted = true);
void fire();
void get_hit();
void walking();
void interrupt_by_nrf_handler();

// Setup
void setup() {
  randomSeed(analogRead(0));
  // Setup Interrupt Pins
  pinMode(INTERRUPT_PIN_2, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN_2), interrupt_by_nrf_handler, RISING);
  // Setup Input Pins
  pinMode(nRF_FLAG_PIN_1, INPUT_PULLUP);
  pinMode(nRF_FLAG_PIN_2, INPUT_PULLUP);
  pinMode(nRF_FLAG_PIN_3, INPUT_PULLUP);
  // Setup Output Pins
  pinMode(HIT_DETECTION_PIN, OUTPUT);
  pinMode(SHOT_PIN, OUTPUT);
  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(HIT_DETECTION_PIN, LOW);
  digitalWrite(SHOT_PIN, LOW);
  digitalWrite(PUMP_PIN, LOW);
  Serial.begin(115200);
  if (use_udp)
  {
    Serial.println("Warte auf EnC28J60 Startup.");
    delay(6000);
    Serial.println("Initialisierung des Ethernet Controllers");
    if (ether.begin(sizeof Ethernet::buffer, mymac, SS) == 0)
    {
      Serial.println( "Fehler: EnC28J60 nicht initalisiert.");
      while (true);
    }
    if (static_ip)
    {
      ether.staticSetup(myip, gwip);
      char payload[]= "Game started";
      ether.sendUdp(payload, sizeof(payload), nSourcePort, ipDestinationAddress, nDestinationPort);
    } else
    {
      Serial.println("Hole DHCP Adresse.");
      if (ether.dhcpSetup())
      {
        ether.printIp("IP Adresse: ", ether.myip);
        ether.printIp("Netmask: ", ether.netmask);
        ether.printIp("GW IP: ", ether.gwip);
        ether.printIp("DNS IP: ", ether.dnsip);
      }
      else
      {
        Serial.println("DHCP Adresse holen ist fehlgeschlagen.");
        while (true);
      }
    }
    char payload[]= "Game started";
    ether.sendUdp(payload, sizeof(payload), nSourcePort, ipDestinationAddress, nDestinationPort);
    Serial.println("Start Message sent via UDP.");
  }
  Serial.println("Player simulator started");
}

// Main loop
void loop() {
  // Looping through the state machine
  if (!gamemaster) {
    if (millis() - last_action_time >= ACTION_DELAY) {
        switch (state) {
        case ACTION_WALKING:
          walking();
          break;
        case ACTION_FIRING:
          fire();
          break;
        case ACTION_GETTING_HIT:
          get_hit();
          break;
        case ACTION_REFILLING:
          refill(REFILL_TIME_ONE_SHOT);
          break;
      }
      last_action_time = millis();
    }

    // Game over
    if (player_lifecount == 0) {
      Serial.println("Game over");
      while(1);
    }
  } else {
    if (millis() - last_action_time >= (4*ACTION_DELAY)){
      last_action_time = millis();
      digitalWrite(SHOT_PIN, HIGH);
      digitalWrite(SHOT_PIN, LOW);
    }
  }
}

// Function definitions
// Refill function
void refill(int refill_duration, bool can_be_interrupted) {
  Serial.print(millis());
  Serial.print(" - Refilling. Shots left: ");
  Serial.println(shots_left);
  Serial.print(" Pump cycles total: ");
  Serial.println(pump_cycles_total);
  if(shots_left == MAX_SHOTS) {
    state = ACTION_WALKING;
    is_refilling = false;
    goto output;
  }
  if(is_refilling == false){
    refill_start_time = millis();
    refill_end_time = refill_start_time + refill_duration * 1000;
    is_refilling = true;
  } else {
    if (millis() < refill_end_time) {
    } else
    {
      shots_left++;
      pump_cycles_total += 1.0/MAX_SHOTS;
      is_refilling = false;
      if (watergun) {
        digitalWrite(PUMP_PIN, HIGH);
        digitalWrite(PUMP_PIN, LOW);
        Serial.print("Added 1 shot. Shots left: ");
        Serial.println(shots_left);
      }
    }
  }
  if (can_be_interrupted == true) {
    int random_number = random(0, 100);
    if (random_number < 95) {
      state = ACTION_REFILLING;
    } else {
      state = ACTION_FIRING;
      is_refilling = false;
    }
  }
  output:
    return;
}

// Fire function
void fire() {
  if (shots_left > 0) {
    shots_left--;
    shots_fired_total++;
    Serial.print(millis());
    Serial.print(" - Firing. Shots left: ");
    Serial.print(shots_left);
    Serial.print(" Shots fired total: ");
    Serial.println(shots_fired_total);
    if (watergun) {
      digitalWrite(SHOT_PIN, HIGH);
      digitalWrite(SHOT_PIN, LOW);
    }
  } else {
    state = ACTION_WALKING;
  }
  int random_number = random(0, 100);
  if (random_number < 60) {
    state = ACTION_FIRING;
  } else {
    state = ACTION_WALKING;
  }

}
// Get hit function
void get_hit() {
  if (player_lifecount > 0) {
    player_lifecount--;
    Serial.print(millis());
    Serial.print(" - Getting hit. Lifecount: ");
    Serial.println(player_lifecount);
    if (shots_left > 0) {
      int random_number = random(0, 100);
      if (random_number < 85) {
        state = ACTION_FIRING;
      } else {
        state = ACTION_WALKING;
      }
    } else {
        state = ACTION_WALKING;
    }
    if (!watergun && !gamemaster) {
      digitalWrite(HIT_DETECTION_PIN, HIGH);
      digitalWrite(HIT_DETECTION_PIN, LOW);
    }
  }

}

// Walking function
void walking() {
  Serial.print(millis());
  Serial.println(" - Walking");
  int random_number = random(0, 100);
  if (shots_left == 0) {
    state = ACTION_REFILLING;
  } else {
    if (random_number < 85) {
      state = ACTION_WALKING;
    } else if (random_number < 97.5) {
      state = ACTION_FIRING;
    } else {
      state = ACTION_GETTING_HIT;
    }
  }
}

// Interrupt handler
void interrupt_by_nrf_handler(){
  bool flag_1 = digitalRead(nRF_FLAG_PIN_1);
  bool flag_2 = digitalRead(nRF_FLAG_PIN_2);
  bool flag_3 = digitalRead(nRF_FLAG_PIN_3);

  // check if any bit is set, if not return
  if (!flag_1 && !flag_2 && !flag_3) {
    return;
  }

  if (flag_1)
  {
    if (!flag_2){
      if (!flag_3) {
        if (use_udp)
        {
          //Send GAME MODIFICATION EVENT UDP
          char payload[]= "B3.1 - Game Mod.";
          ether.sendUdp(payload, sizeof(payload), nSourcePort, ipDestinationAddress, nDestinationPort);
          Serial.println("Interrupt by nRF - Game Modification");
          return;
        } else {
          Serial.println("Interrupt by nRF - Game Modification");
          return;
        }
      }
    }
  }
  else
  {
    if (!flag_2)
    {
      if (flag_3)
      {
        if (use_udp)
        {
          //Send HIT EVENT UDP
          char payload[]= "B3.1 - HIT";
          ether.sendUdp(payload, sizeof(payload), nSourcePort, ipDestinationAddress, nDestinationPort);
          Serial.println("Interrupt by nRF - HIT");
          return;
        } else {
          Serial.println("Interrupt by nRF - HIT");
          return;
        }
      }
    } else {
      if (!flag_3)
      {
        if (use_udp)
        {
          //Send HEARTBEAT EVENT UDP
          char payload[]= "B3.1- HEARTBEAT";
          ether.sendUdp(payload, sizeof(payload), nSourcePort, ipDestinationAddress, nDestinationPort);
          Serial.println("Interrupt by nRF - HEARTBEAT");
          return;
        } else {
          Serial.println("Interrupt by nRF - HEARTBEAT");
          return;
        }
      } else {
        if (use_udp)
        {
          //Send PLAYERSTATUS EVENT UDP
          char payload[]= "B3.1 - PLAYERSTATUS";
          ether.sendUdp(payload, sizeof(payload), nSourcePort, ipDestinationAddress, nDestinationPort);
          Serial.println("Interrupt by nRF - PLAYERSTATUS");
          return;
        } else {
          Serial.println("Interrupt by nRF - PLAYERSTATUS");
          return;
        }
      }
    } 
  }
}

