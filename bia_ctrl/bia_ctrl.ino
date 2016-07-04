
#include <Stepper.h>

#include <Dhcp.h>
#include <Dns.h>
#include <Ethernet.h>
#include <EthernetClient.h>
#include <EthernetServer.h>
#include <EthernetUdp.h>
#include <util.h>

// LED diming board
// by Enos 2013/11/20

#include <SPI.h>
#include <Ethernet.h>
#include "TimerOne.h"

// Enter a MAC address and IP address for your controller below.
// The IP address will be dependent on your local network.
// gateway and subnet are optional:

byte mac[] = 
{
  0x90, 0xA2, 0xDA, 0x0F, 0xA0, 0x4F
};


// telnet defaults to port 23
EthernetServer g_server(23);
EthernetClient g_client;

// BIA Controller MODES
int bia_mode;
int g_pin; // arduino pin#
int cmdnok;
String g_strcmd;

char str[8];
int shut_error;

boolean shb_open_status;
boolean shb_close_status;
boolean shb_err_status;
boolean shr_open_status;
boolean shr_close_status;
boolean shr_err_status;

long g_aduty; // duty
long g_aperiod; // period
long g_bduty; // duty
long g_bperiod; // period
byte shutstat = 0;
char statword [] ={"00000000"};

// setup
void setup()
{    
  Serial.begin(9600);
  // this check is only needed on the Leonardo:
   while (!Serial) {
    ; // wait for serial port to connect. Needed for Leonardo only
  }
  Serial.println("Search your Feelings");
  
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    // no point in carrying on, so do nothing forevermore:
    for(;;);}
  Serial.println(Ethernet.localIP());
  g_server.begin();
  delay(1000);
 
  g_aduty = 105; // 0% duty 
  g_aperiod = 100; // 100000 = 100ms 1000 = 1ms
  g_bduty = 105; 
  g_bperiod = 100000; // 100000 = 100ms
  
   // Set BIA :ode to IDLE
  bia_mode = 0;
  g_pin = 9;
  
  Timer1.initialize(300000); // the timer's period here (in microseconds)
  Timer1.pwm(9, 0); // setup pwm on pin 9, 0% duty cycle
  Timer1.setPwmDuty(9,0);
  
 
  g_strcmd = String("");
  g_strcmd = ""; // empty cmd buffer
  g_client.read();
  g_client.flush();
  
 
  // Config pin 7 en output (DF)
  pinMode(1,INPUT);
  pinMode(2,INPUT);
  pinMode(3,INPUT);
  pinMode(4,INPUT);
  pinMode(5,INPUT);
  pinMode(6,OUTPUT);
  pinMode(7,OUTPUT);
  pinMode(8,INPUT);
}

void Command(EthernetClient g_client, String mycommand){

  cmdnok=1;
  
  
      ///// LECTURE EFFECTIVE DES STATUS SUR PORT IO ARDUINO //
  shb_open_status=digitalRead(8);         // BS ouvert//
  shb_close_status=digitalRead(5);   // BS ferme //
  shb_err_status=digitalRead(1); // BS error //
  shr_open_status=digitalRead(3);   // RS ouvert //
  shr_close_status=digitalRead(4);    // RS fermé //
  shr_err_status=digitalRead(2);    //RS error //


// MISE A JOUR DU MOT DE STATUS //
  if (shb_open_status == LOW){
    statword[0]='1'; }
  if (shb_open_status == HIGH){
    statword[0]='0'; }
      
  if (shb_close_status == LOW){
    statword[1]='1'; }
  if (shb_close_status == HIGH){
    statword[1]='0'; }
    
  if (shb_err_status == LOW){
    statword[2]='1';
    shut_error=1;}
  if (shb_err_status == HIGH){
    statword[2]='0'; } 
     
  if (shr_open_status == LOW){
    statword[3]='1'; }
  if (shr_open_status == HIGH){
    statword[3]='0'; }
    
  if (shr_close_status == LOW){
    statword[4]='1'; }
  if (shr_close_status == HIGH){
    statword[4]='0'; } 
    
  if (shr_err_status == LOW){
    statword[5]='1';
    shut_error=1; }
  if (shr_err_status == HIGH){
    statword[5]='0'; }
    
//  statword[0]='0';
//  statword[1]='1';
//  statword[2]='0';
//  statword[3]='0';
//  statword[4]='1';
//  statword[5]='0';
//  statword[6]='0';
//  statword[7]='0';
//  
//  shut_error=0;
  
  
  if (shut_error!=1){  
    //////////////////////////////////// GESTION DU GRAFCET SANS LES ACTIONS /////////////////////////////////////////
    switch (bia_mode) {
      case 0:
      // We are in Idle state
        if (mycommand=="bia_on\r\n"){
          if (shb_close_status==LOW && shr_close_status==LOW){
            bia_mode = 2;
            g_client.write("ok\r\n");
            cmdnok=0;}}
         
          
        if (mycommand.substring(0,5)=="open_"){
          bia_mode = 1;
          g_client.write("ok\r\n");
          cmdnok=0;}
        break;
  
      case 1:
      // We are in Shutter state
        if (mycommand=="close_shut\r\n"){
          bia_mode= 0;
          g_client.write("ok\r\n");
          cmdnok=0;}
      
        if (mycommand=="bia_on\r\n"){
          g_client.write("intlck\r\n"); 
          cmdnok=0;}
        break;
  
      case 2:
      // We are in BIA state
        if (mycommand=="bia_off\r\n"){
          bia_mode= 0;
          g_client.write("ok\r\n");
          cmdnok=0;}
      
        if (mycommand.substring(0,5)=="open_"){ 
          g_client.write("intlck\r\n");
          cmdnok=0;}
        break;
      }
    //////////////////////////////////////////////// FIN EVOLUTION ETATS ///////////////////////////////////////////////////
    // Controller status command parsing
  }
  else {bia_mode=0;
        g_client.write("safe_mode\r\n");}
  
  
  if (mycommand=="init\r\n"){
    bia_mode = 0;
    g_client.write("0\r\n");
    cmdnok=0;}
    
    

  if (mycommand=="ctrlstat\r\n"){ 
    g_strcmd = ""; // empty cmd buffer
    switch(bia_mode){
      case 0:
        g_client.write("0\r\n");
        cmdnok=0;
        break;
      case 1:
        g_client.write("1\r\n");
        cmdnok=0;
        break;
      case 2:
        g_client.write("2\r\n");
        cmdnok=0;
        break;
      }
    return;
  }

/////////////////////////////////////////////////// STATUS SHUTTERS //////////////////////////////////////////////////////
// Commande lecture status shutters
  if (mycommand=="shutstat\r\n"){  
    g_client.write(statword); 
    g_client.write("\0\r\n");
    cmdnok=0;
    return ;
  } // Fin commande lecture shutters/////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////// PROGRAMMATION DU TEMPS DE CYCLE TIMER PWM BIA /////////////////////////////////////////
  if (mycommand.substring(0,8)=="set_freq"){
    int mylen=mycommand.length()-2;
    String inter=mycommand.substring(8,mylen);
    g_aperiod = (long)inter.toInt()*1000;
    g_client.write("ok\r\n");
    cmdnok=0;
    return ;} 
  
  if (mycommand.substring(0,10)=="set_strobe"){
    int mylen=mycommand.length()-2;
    String inter=mycommand.substring(10,mylen);
    g_aperiod = (long)inter.toInt()*1000;
    g_client.write("ok\r\n");
    cmdnok=0;
    return ;}
   
//    Serial.println("set_strobe");
//    Serial.println(g_aperiod);
    
  if (mycommand.substring(0,12)=="set_bia_duty"){
    int mylen=mycommand.length()-2;
    String inter=mycommand.substring(12,mylen);
    g_aduty=inter.toInt();
    g_client.write("ok\r\n");
    cmdnok=0;
    return ;} 

    
///////////////////////////////////// ACTIONS FAITES SELON MODE ACTUEL BIA /////////////////////////////////////////////////////
  switch (bia_mode) {
    case 0:  ///////////////////////////////////ACTIONS FAITES PENDANT LE MODE 0 CAD IDLE: NI SHUTTER NI BIA//
      Timer1.setPwmDuty(g_pin, 0); // FORCAGE PWM BIA A ZERO 
      digitalWrite(7,LOW);          // FORCAGE FERMETURE SHUTTER B
      digitalWrite(6,LOW);          // FORCAGE FERMETURE SHUTTER R
      break;

/////////////////////////////////////////////////////////////////// ACTIONS DU MODE 1 SHUTTER//
    case 1:
      Timer1.setPwmDuty(g_pin, 0);  // FORCAGE BIA ETEINTE
      if (mycommand=="open_shut\r\n"){
        digitalWrite(7,HIGH);          // OUVERTURE SHUTTER B
        digitalWrite(6,HIGH);}          // OUVERTURE SHUTTER R
      if (mycommand=="open_shut_r\r\n"){
        digitalWrite(6,HIGH);}
      if (mycommand=="open_shut_b\r\n"){
        digitalWrite(7,HIGH);}
      if (mycommand=="close_shut_r\r\n"){
        digitalWrite(6,LOW);
        if (shb_close_status==LOW){
          Command(g_client,"close_shut\r\n");}}
      if (mycommand=="close_shut_b\r\n"){
        digitalWrite(7,LOW);
        if (shr_close_status==LOW){
          Command(g_client,"close_shut\r\n");}}

      break;
    // //////////////////////////////////////////////////////////////////ACTIONS DU MODE BIA//
    case 2:
      Timer1.setPeriod(g_aperiod);        // DEMARRE LE TIMER PWM A LA PERIODE PROGRAMM
      Timer1.setPwmDuty(g_pin, g_aduty); // DEMARRE LE TIMER PWM AU TEMPS DE CYCLE PROGRAMME
      digitalWrite(7,LOW);               // FORCE FERMETURE SHUTTER B
      digitalWrite(6,LOW);               // FORCE FERMETURE SHUTTER R
      break;
  }

  ////////////////////////////////////////////////////////////////////////////////////////////////ARRET SERVEUR /////////////////////////////////////////////////////////////////////////
  if (mycommand=="stop\r\n"){
    g_client.stop();
    return ;}
  
  /////////////////////////////////////////////////////////////////////////////////////////////// AFFICHAGE COMMANDE NOK ///////////////////////////////////////////////////////////////////
  if (cmdnok == 1){
    g_client.write("nok\r\n");} 

}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////BOUCLE PRINCIPALE///////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void loop() {
  EthernetClient g_client = g_server.available();
  if (g_client){
    int taille=g_client.available();
    char data[taille+1];
    int i=0;
    while (g_client.available()>0)
    {
      char c = g_client.read();
      if (c !=  - 1)
      {
       data[i]=c;
       i++;
        }
    }
    data[i]='\0';
    String mystring=(String)data;
    Command(g_client, mystring);
      
  }
 
}
