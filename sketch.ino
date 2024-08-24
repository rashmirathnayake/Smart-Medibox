#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHTesp.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

#define NTP_SERVER     "pool.ntp.org"
#define UTC_OFFSET     0
#define UTC_OFFSET_DST 0

#define SCREEN_WIDTH 128
#define SCREEN_HIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3c

#define LED 15
#define BUZZER 5
#define PB_CANCLE 26
#define PB_DOWN 25
#define PB_UP 32
#define PB_MENU 33

#define DHTpin 12

#define LDRleft 35
#define LDRright 34

#define SERVOPIN 4

struct tm date_time;
int hours = 0;
int mins = 0;
int seconds = 0;
int seconds_to_display = 0;
int timezone=25;

unsigned long timeNow = 0;
unsigned long timeLast = 0;

int n_alarms = 3;
boolean alarm_enabled[] = {true,true,true};
int alarm_hours[] = {6, 12, 18};
int alarm_minutes[] = {0, 0, 0};
boolean alarm_triggered[] = {false, false,false};

boolean stop_ringing = false;
boolean goto_menu = false;

int current_mode =0;
int max_modes = 5;
String modes[]={"1. Set Time Zone","2. Set Alarm 1","3.  Set Alarm 2","4.  Set Alarm 3","5.  Disable Alarms"};
String time_zones[][2]=
{{"-1200","<-12>12"},{"-1130","<-1130>11:30"},{"-1100","<-11>11"},{"-1030","<-1030>10:30"},{"-1000","<-10>10"},{"-0930","<-0930>9:30"},{"-0900","<-09>9"},{"-0830","<-0830>8:30"},{"-0800","<-08>8"},{"-0730","<-0730>7:30"},{"-0700","<-07>7"},{"-0630","<-0630>6:30"},{"-0600","<-06>6"},{"-0530","<-0530>5:30"},{"-0500","<-05>5"},{"-0430","<-0430>4:30"},{"-0400","<-04>4"},{"-0330","<-0330>3:30"},{"-0300","<-03>3"},{"-0230","<-0230>2:30"},{"-0200","<-02>2"},{"-0130","<-0130>1:30"},{"-0100","<-01>1"},{"-0030","<-0030>0:30"},{"+0000","<+00>-0"},{"+0030","<+0030>-0:30"},{"+0100","<+01>-1"},{"+0130","<+0130>-1:30"},{"+0200","<+02>-2"},{"+0230","<+0230>-2:30"},{"+0300","<+03>-3"},{"+0330","<+0330>-3:30"},{"+0400","<+04>-4"},{"+0430","<+0430>-4:30"},{"+0500","<+05>-5"},{"+0530","<+0530>-5:30"},{"+0600","<+06>-6"},{"+0630","<+0630>-6:30"},{"+0700","<+07>-7"},{"+0730","<+0730>-7:30"},{"+0800","<+08>-8"},{"+0830","<+0830>-8:30"},{"+0900","<+09>-9"},{"+0930","<+0930>-9:30"},{"+1000","<+10>-10"},{"+1030","<+1030>-10:30"},{"+1100","<+11>-11"},{"+1130","<+1130>-11:30"},{"+1200","<+12>-12"},{"+1230","<+1230>-12:30"},{"+1300","<+13>-13"},{"+1330","<+1330>-13:30"},{"+1400","<+14>-14"}};

const char* mqtt_server = "test.mosquitto.org";

int minimum_angle=30;
float gmm=0.75;
float angle=0;

void IRAM_ATTR interrupt_ringing() {
  stop_ringing=true;
}
void IRAM_ATTR interrupt_gotoMenu() {
  goto_menu=true;
}

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HIGHT, &Wire, OLED_RESET);
DHTesp dhtSensor;

WiFiClient espClient;
PubSubClient client(espClient);
Servo servo;

void setup() {
  pinMode(LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  pinMode(PB_CANCLE, INPUT);
  pinMode(PB_DOWN, INPUT);
  pinMode(PB_UP, INPUT);
  pinMode(PB_MENU, INPUT);
  digitalWrite(LED, LOW);

  pinMode(LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  // pinMode(LDRleft, INPUT);
  // pinMode(LDRright, INPUT);

  attachInterrupt(digitalPinToInterrupt(PB_CANCLE), interrupt_ringing, RISING);
  attachInterrupt(digitalPinToInterrupt(PB_MENU), interrupt_gotoMenu, RISING);


  dhtSensor.setup(DHTpin,DHTesp::DHT22);
  servo.attach(SERVOPIN, 500, 2400);
  Serial.begin(115200);
  Serial.println("Began");
  if (! display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 Allocation failed!"));
    for (;;);
  }
  display.display();
  display.clearDisplay();
  delay(4000);
  if(WiFi.status() != WL_CONNECTED){
  WiFi.begin("Wokwi-GUEST", "", 6);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    print_line("Connecting to WIFI..",0,20,1);
  }}
  print_line("welcome to medboxy", 10, 10, 1);
  delay(2000);

  configTime(UTC_OFFSET, UTC_OFFSET_DST, NTP_SERVER);
  setenv("TZ","<+00>-0",1);  
  tzset();
  Serial.println("Setup done");

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);



}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  
  display.clearDisplay();
  update_time_with_check_alarm();
  if(goto_menu){
    delay(200);
    display.clearDisplay();
    go_to_menu();
    goto_menu=false;
  }
  
  get_max_intensity();
  check_temp();
  if(angle>180){
    angle=180;
  }
  servo.write(angle);
  delay(15);

}

void print_line(String line, int column, int row, int textSize) {
  display.setTextSize(textSize);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(column, row);
  display.print(line);
  display.display();
}

void print_time_now(void) {

  char date[80];
  strftime(date, 80, "%A, %Y-%m-%d \n      %H:%M:%S", &date_time);
  print_line(date, 0, 0, 1);
}

void update_time(void) {
  struct tm timeinfo;
  getLocalTime(&timeinfo);

  char timeHour[3];
  strftime(timeHour,3,"%H",&timeinfo);
  hours=atoi(timeHour); 
  
  char timeMins[3];
  strftime(timeMins,3,"%M",&timeinfo);
  mins=atoi(timeMins);

  char timeSeconds[3];
  strftime(timeSeconds,3,"%S",&timeinfo);
  seconds=atoi(timeSeconds);

  date_time= timeinfo;

}
void update_time_with_check_alarm() {
  update_time();
  print_time_now();

  if (alarm_enabled) {
    for (int i = 0; i < n_alarms; i++) {
      if (alarm_triggered[i] == false && alarm_hours[i] == hours && alarm_minutes[i] == mins) {
        digitalWrite(LED, HIGH);
        ring_alarm();
        digitalWrite(LED, LOW);
        alarm_triggered[i] = true;
      }
    }
  }

}
void ring_alarm() {
  display.clearDisplay();
  print_line("MEDICINE TIME!", 0, 10, 1);
  stop_ringing=false;
  while (true) {
    tone(BUZZER, 500);
    delay(250);
    noTone((BUZZER));
    delay(100);
    if (stop_ringing) {
      delay(200);
      break;
    }
  }
}

int wait_for_input(){
  while(true){
    if(digitalRead(PB_MENU)==HIGH){
      delay(200);
      Serial.println(PB_MENU);
      return PB_MENU;
    }
    else if(digitalRead(PB_UP)==HIGH){
      delay(200);
      Serial.println(PB_UP);
      return PB_UP;
    }
    else if(digitalRead(PB_DOWN)==HIGH){
      delay(200);
      Serial.println(PB_DOWN);
      return PB_DOWN;
    }
    else if(digitalRead(PB_CANCLE)==HIGH){
      delay(200);
      Serial.println(PB_CANCLE);
      return PB_CANCLE;
    }
    update_time();
  }
}

void go_to_menu(){
 
  while(digitalRead(PB_CANCLE)==LOW){
  display.clearDisplay();
  print_line("Menu",50,0,2);
  print_line(modes[current_mode],0,30,1);
    int pressed = wait_for_input();
    if(pressed==PB_UP){
      delay(200);
      current_mode +=1;
      current_mode = current_mode % max_modes;
    }
    else if(pressed == PB_DOWN){
      delay(200);
      current_mode -=1;
      if(current_mode <0){
        current_mode = max_modes -1;
      }
    }
    else if(pressed==PB_MENU){
      delay(200);
      run_mode(current_mode);
    }
    else if(pressed==PB_CANCLE){
      delay(200);
      break;
    }
  }
}

void set_time_zone(){
  int temp_timezone=timezone;
  
  while(true){
  display.clearDisplay();
  print_line("Select Time Zone",30,0,1);
  print_line(time_zones[temp_timezone][0],20,30,2);
    int pressed = wait_for_input();
    if(pressed==PB_UP){
      delay(200);
      temp_timezone +=1;
      temp_timezone = temp_timezone % 97;
    }
    else if(pressed == PB_DOWN){
      delay(200);
      temp_timezone -=1;
      if(temp_timezone <0){
        temp_timezone =96 ;
      }
    }
    else if(pressed==PB_MENU){
      delay(200);
      timezone = temp_timezone;
      break;
    }
    else if(pressed==PB_CANCLE){
      delay(200);
      return;
    }
  }
  setenv("TZ",time_zones[timezone][1].c_str(),1);  
  tzset();

  
}

void set_alarm(int mode){
  int temp_hours=alarm_hours[mode-1];
  int temp_mins = alarm_minutes[mode-1];


  while(true){
  display.clearDisplay();
  print_line("Enter hours",50,0,1);
  print_line(String(temp_hours),50,20,2);
    int pressed = wait_for_input();
    if(pressed==PB_UP){
      delay(200);
      temp_hours +=1;
      temp_hours = temp_hours % 24;
    }
    else if(pressed == PB_DOWN){
      delay(200);
      temp_hours -=1;
      if(temp_hours <0){
        temp_hours = 23;
      }
    }
    else if(pressed==PB_MENU){
      delay(200);
      alarm_hours[mode-1] = temp_hours;
      break;
    }
    else if(pressed==PB_CANCLE){
      delay(200);
      return;
    }
  }

  while(true){
  display.clearDisplay();
  print_line("Enter minutes",50,0,1);
  print_line(String(temp_mins),50,20,2);
    int pressed = wait_for_input();
    if(pressed==PB_UP){
      delay(200);
      temp_mins +=1;
      temp_mins = temp_mins % 60;
    }
    else if(pressed == PB_DOWN){
      delay(200);
      temp_mins -=1;
      if(temp_mins <0){
        temp_mins = 59;
      }
    }
    else if(pressed==PB_MENU){
      delay(200);
      alarm_minutes[mode-1] = temp_mins;
      break;
    }
    else if(pressed==PB_CANCLE){
      delay(200);
      return;
    }
  }
  alarm_enabled[mode-1]=true;

}

void run_mode(int mode){
  if(mode == 0){
    set_time_zone();
  }else if(mode == 1 || mode == 2 || mode == 3){
    set_alarm(mode);
  }else if(mode == 4){
    alarm_enabled[0] = false;
    alarm_enabled[1] = false;
    alarm_enabled[2] = false;
  }

}

void check_temp(){
  TempAndHumidity data = dhtSensor.getTempAndHumidity();
  if(data.temperature > 32){
    print_line("Temp High",0,40,1);

  }else if(data.temperature <26){
    print_line("Temp Low",0,40,1);
  }

  if(data.humidity > 80){
    print_line("Humidity High",0,50,1);

  }else if(data.humidity <60){
    print_line("Humidity Low",0,50,1);
  }
}

//............................Advanced Featurs..........................................
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("publisher")) {
      Serial.println("connected");
      client.subscribe("MinumumAngle");
      client.subscribe("Gamma");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  char payloadAr[length];
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    payloadAr[i] = (char)payload[i];
  }
  Serial.println();
  if(strcmp(topic,"MinumumAngle")==0){
    minimum_angle=atoi(payloadAr);
  }
 if(strcmp(topic,"Gamma")==0){
    gmm=atof(payloadAr);
    
  }
}

void get_max_intensity(){
  int L=analogRead(LDRleft);
  int R=analogRead(LDRright);
  float l=1.0-(float)L / 4095.0;
  float r=1.0-(float)R / 4095.0;
  if(L>R){
    
    client.publish("LIGHT",String(r).c_str());
    client.publish("LEFT_RIGHT","RIGHT");
    angle= minimum_angle*0.5 + (180-minimum_angle)*r*gmm;
  }else{
    client.publish("LIGHT",String(l).c_str());
    client.publish("LEFT_RIGHT","LEFT");
    angle= minimum_angle*1.5 + (180-minimum_angle)*l*gmm;
  }
}



