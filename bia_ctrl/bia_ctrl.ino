


#include <SPI.h>
#include <Ethernet2.h>

#include <MsTimer2.h>
//#include <TimerOne.h>

// Enter a MAC address and IP address for your controller below.
// The IP address will be dependent on your local network.
// gateway and subnet are optional:

byte mac[] = 
{
  0x90, 0xA2, 0xDA, 0x0F, 0xA0, 0x4F
};


// telnet defaults to port 23
EthernetServer g_server(23);


// BIA Controller MODES
int bia_mode;
int safe_on;

int cmdnok;

int g_pin; // bia pin#
long g_aduty; // duty
long g_aperiod; // period

boolean shb_open_status;
boolean shb_close_status;
boolean shb_err_status;
boolean shr_open_status;
boolean shr_close_status;
boolean shr_err_status;

bool LedState;
bool PulseMode;

char statword [] ={"00000000"};

// setup
void setup()
{    
  Serial.begin(9600);
  // this check is only needed on the Leonardo:
  Serial.println("BIADuino v2.0");
  
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    // no point in carrying on, so do nothing forevermore:
    for(;;);}

  g_server.begin();

  Serial.println(Ethernet.localIP());
  delay(1000);
 
  LedState = false;
  PulseMode = false;

   // Set BIA :ode to IDLE
  bia_mode = 0;
  safe_on = 0;
  
  //////////////////////////////////// PARAM BIA /////////////////////////////////////////
  g_aduty = 0; // 0% duty 
  g_aperiod = 100; // 100000 = 100ms 1000 = 1ms
  g_pin = 9;
  
  
  //////////////////////////////////// PARAM SHUTTERS /////////////////////////////////////////
    // Config pin 7 en output (DF)
  pinMode(1,INPUT);
  pinMode(2,INPUT);
  pinMode(3,INPUT);
  pinMode(4,INPUT);
  pinMode(5,INPUT);
  pinMode(6,OUTPUT);
  pinMode(7,OUTPUT);
  pinMode(8,INPUT);

  pinMode(g_pin, OUTPUT);
  digitalWrite(g_pin, LOW);
  digitalWrite(6, LOW);
  digitalWrite(g_pin, LOW);
  
  MsTimer2::set(1000, Timer); // 500ms period
  MsTimer2::start();
}

//ISR(TIMER1_COMPA_vect)
void Timer()
{//timer1 interrupt 1Hz
  if (bia_mode == 2)
  {
    if (!PulseMode)
    {
      analogWrite(g_pin, g_aduty);
      return;
    }
    
    if (LedState)
    {
      Serial.print("ON ");
      Serial.println(g_aduty);
     analogWrite(g_pin, g_aduty);
     //digitalWrite(9, HIGH);
    }
    else
    {
      Serial.println("OFF");
      digitalWrite(g_pin, LOW);
    }    
    LedState = !LedState;
  }
}


void SetPeriod(int d)
{
  //OCR1A = d;
  
  MsTimer2::stop();  
  MsTimer2::set(d, Timer);
  MsTimer2::start();
}

void Command(EthernetClient g_client, String mycommand){

  cmdnok  = 2;
  
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
    statword[2]='1';}
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
    statword[5]='1'; }
  if (shr_err_status == HIGH){
    statword[5]='0'; }
    
  
  
    //////////////////////////////////// GESTION DU GRAFCET SANS LES ACTIONS /////////////////////////////////////////
  switch (bia_mode) {
    case 0:
    // We are in Idle state
      if (mycommand=="bia_on\r\n"){
          if (check_interlock(mycommand)==1){
            bia_mode = 2;
            cmdnok = 0;}
          else {
            g_client.write("intlk");
            cmdnok = 1;} 
      }
      
      if (mycommand=="shut_open\r\n"){
        if (check_interlock(mycommand)==1){
            bia_mode = 1;
            cmdnok = 0;}
          else {
          g_client.write("intlk");
          cmdnok = 1;}
      }  

      break;

    case 1:
    // We are in Shutter state
      if (mycommand=="shut_close\r\n"){
        bia_mode = 0;
        cmdnok = 0;}
    
      if (mycommand=="bia_on\r\n"){
        g_client.write("intlk"); 
        cmdnok  = 1;}
      break;

    case 2:
    // We are in BIA state
      if (mycommand=="bia_off\r\n"){
        bia_mode= 0;
        cmdnok = 0;}
    
      if (mycommand=="shut_open\r\n"){ 
        g_client.write("intlk");
        cmdnok = 1;}
      break;
    }
    //////////////////////////////////////////////// FIN EVOLUTION ETATS ///////////////////////////////////////////////////
    // Controller status command parsing
  
  
  if (mycommand=="init\r\n"){
    bia_mode = 0;
    cmdnok = 0;}
        
  if (mycommand=="statword\r\n"){
    g_client.write(statword); 
    g_client.write("\0");
    cmdnok = 1;}
    
  if (mycommand=="status\r\n"){ 
    g_client.print(bia_mode);
    cmdnok = 1;}

  long v;
///////////////////////////////////// PROGRAMMATION DU TEMPS DE CYCLE TIMER PWM BIA /////////////////////////////////////////
  if (mycommand.substring(0,10)=="set_period")
  {
    int mylen=mycommand.length()-2;
    String inter=mycommand.substring(10,mylen);
    v = (long)inter.toInt();
    
    if ((v > 0)&&(v<65536))
    {//v is in ms
      SetPeriod(v);
    }
    
    Serial.println(g_aperiod);
    cmdnok = 0;
  } 
  
  if (mycommand.substring(0,8)=="pulse_on")
  {
    PulseMode = true;
    cmdnok = 0;    
  }
  
  if (mycommand.substring(0,9)=="pulse_off")
  {
    PulseMode = false;
    cmdnok = 0;    
  }
  
  
    
  if (mycommand.substring(0,8)=="set_duty"){
    int mylen=mycommand.length()-2;
    String inter=mycommand.substring(8,mylen);
    v =inter.toInt();
    
    if ((v>0) && (v<256))
    {
      g_aduty = v;
    }
    
    Serial.println(g_aduty);
    cmdnok = 0;} 

  if (mycommand=="get_period\r\n"){
    g_client.print(g_aperiod);
    cmdnok = 1;} 
    
  if (mycommand=="get_duty\r\n"){
    g_client.print(g_aduty);
    cmdnok = 1;} 
    
  if (mycommand=="safe_on\r\n"){
    safe_on = 1;
    cmdnok = 1;}
    
  if (mycommand=="safe_off\r\n"){
    safe_on = 0;
    cmdnok = 1;}
    
   ////////////////////////////////////////////////////////////////////////////////////////////////ARRET SERVEUR /////////////////////////////////////////////////////////////////////////
  if (mycommand=="stop\r\n"){
    bia_mode = 0;
    g_client.write("ok\r\n");
    g_client.stop();
    return ;}
  
  /////////////////////////////////////////////////////////////////////////////////////////////// AFFICHAGE COMMANDE NOK ///////////////////////////////////////////////////////////////////
  if (cmdnok<2){
    g_client.write("ok\r\n");
  }
  else {
    g_client.write("nok\r\n");}
 
  if (cmdnok==1){
    return;}
  
    
///////////////////////////////////// ACTIONS FAITES SELON MODE ACTUEL BIA /////////////////////////////////////////////////////
  switch (bia_mode) {
    case 0:  ///////////////////////////////////ACTIONS FAITES PENDANT LE MODE 0 CAD IDLE: NI SHUTTER NI BIA//
      //Timer1.setPwmDuty(g_pin, 0); // FORCAGE PWM BIA A ZERO 
      digitalWrite(g_pin, LOW);
      
      digitalWrite(7,LOW);          // FORCAGE FERMETURE SHUTTER B
      digitalWrite(6,LOW);          // FORCAGE FERMETURE SHUTTER R
      break;

/////////////////////////////////////////////////////////////////// ACTIONS DU MODE 1 SHUTTER//
    case 1:
      //Timer1.setPwmDuty(g_pin, 0);  // FORCAGE BIA ETEINTE
      digitalWrite(g_pin, LOW);
      
       digitalWrite(7,HIGH);          // OUVERTURE SHUTTER B
       digitalWrite(6,HIGH);          // OUVERTURE SHUTTER R
      break;
    // //////////////////////////////////////////////////////////////////ACTIONS DU MODE BIA//
    case 2:
      //Timer1.setPeriod(g_aperiod);        // DEMARRE LE TIMER PWM A LA PERIODE PROGRAMM
      //Timer1.setPwmDuty(g_pin, g_aduty); // DEMARRE LE TIMER PWM AU TEMPS DE CYCLE PROGRAMME
      digitalWrite(7,LOW);               // FORCE FERMETURE SHUTTER B
      digitalWrite(6,LOW);               // FORCE FERMETURE SHUTTER R
      break;
  }

}

int check_interlock(String mycommand){
  if (safe_on==1){
    if (mycommand =="bia_on\r\n" && statword !="01001000") {return 0;}
    //if (mycommand =="shut_close\r\n" && statword !="01001000") {return 0;}     GET STATUS FROM PHOTODIODE
    }
  return 1;}
    
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////BOUCLE PRINCIPALE///////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void loop() {
//  Serial.println("Waiting...");
  EthernetClient g_client = g_server.available();
  if (g_client)
  {
    int taille=g_client.available();

    if (taille > 100)
      taille = 100;
      
    char data[taille+1];
    int i=0;
           
    while (i<taille)
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
  
 delay(10);
}
