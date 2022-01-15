
#include <SPI.h>
#include <Ethernet.h>
#include <CircularBuffer.h>

#include <MsTimer2.h>

    
    
// Enter a MAC address and IP address for your controller below.
// The IP address will be dependent on your local network.
// gateway and subnet are optional:

/* MAC ADDRESSES FOR BIA MODULES
dhcp-host=a8:61:0a:ae:2a:91,bsh-enu1
dhcp-host=a8:61:0a:ae:17:7c,bsh-enu2
dhcp-host=a8:61:0a:ae:2d:84,bsh-enu3
dhcp-host=a8:61:0a:ae:17:72,bsh-enu4
 
dhcp-host=a8:61:0a:ae:14:fc,bsh-enu5
dhcp-host=a8:61:0a:ae:13:25,bsh-enu6
*  
 */
    //Define firware version from git-tagged-version
    const char FIRMWARE_VERSION[] = "0.1.2-INSTRM-1504";
    
    //Uncomment the line below depending on the BSH
    #define ENU1
    //#define ENU2
    //#define ENU3
    //#define ENU4
    
    //#define ENU5
    //#define ENU6
         
    byte mac[] =
    {
      #ifdef ENU1
      0xA8,0x61,0x0A,0xAE,0x2A,0x91
      #endif

      #ifdef ENU2
      0xA8,0x61,0x0A,0xAE,0x17,0x7C
      #endif

      #ifdef ENU3
      0xA8,0x61,0x0A,0xAE,0x2D,0x84
      #endif

      #ifdef ENU4
      0xA8,0x61,0x0A,0xAE,0x17,0x72
      #endif

      #ifdef ENU5
      0xA8,0x61,0x0A,0xAE,0x14,0xFC
      #endif

      #ifdef ENU6
      0xA8,0x61,0x0A,0xAE,0x13,0x25
      #endif
    };
    
    
    String StateCmds[] =
    {
      "bia_on", 
      "bia_off",
      "shut_open", 
      "shut_close",
      "blue_open", 
      "blue_close",
      "red_open", 
      "red_close",
      "init"
    };
    #define STATECMDS_COUNT  9
    
    // INPUT PINS DEFAULT
    #define SHB_OPEN_PIN    3
    #define SHB_CLOSE_PIN   A4
    #define SHB_ERROR_PIN   2
    #define SHR_OPEN_PIN    8
    #define SHR_CLOSE_PIN   5
    #define SHR_ERROR_PIN   1

    #define PHR0_INPUT_PIN  A0
    #define PHR1_INPUT_PIN  A1

    //OUTPUT PINS DEFAULT
    #define LEDS_PIN        9
    #define SHB_PIN         7
    #define SHR_PIN         6
    
    // telnet defaults to port 23
    EthernetServer g_server(23);

    CircularBuffer<char, 100> commandBuffer;
    int bufferSize;
    char commandStr[100];
    const char EOL[] = "\r\n";
        
    // BIA Controller MODES
    int bia_mode;
    bool BIAIsOn;
    bool ledState;
    bool currentState;

    // Exposure variables
    bool doExposure;
    unsigned long openStartedAt;
    unsigned long fullyOpenAt;
    unsigned long closeStartedAt;
    unsigned long fullyClosedAt;

    const unsigned int scaling = 100;
    const unsigned int noStrobeDuty = 100;
    const unsigned int noStrobePeriod = 1000;
    unsigned int divFactor;
    unsigned int maxOnIter;
    unsigned int cycleIter;

    unsigned int g_aduty; // duty
    unsigned int g_aperiod; // period
    unsigned int g_apower; // power

    unsigned int g_sduty; // saved duty
    unsigned int g_speriod; // saved period
    
    boolean shb_open_status;
    boolean shb_close_status;
    boolean shb_err_status;
    boolean shr_open_status;
    boolean shr_close_status;
    boolean shr_err_status;
    
    #define STATUS_BCRC        0x52 //Both shutters closed no error
    #define STATUS_BCRO        0x54 //Blue CLOSED Red OPEN no error
    #define STATUS_BORC        0x62 //Blue OPEN Red CLOSED no error
    #define STATUS_BORO        0x64 //Blue OPEN Red OPEN no error
    
    const int ShutterMotionTimeout = 2000;   
    
    int StatWord;
    
    // setup
    void setup()
    {
      Serial.begin(9600);      
      Serial.print("ics_enu_bsh_version=");
      Serial.println(FIRMWARE_VERSION);
    
      if (Ethernet.begin(mac) == 0) 
      {
        Serial.println("Failed to configure Ethernet using DHCP");

        if (Ethernet.hardwareStatus() == EthernetNoHardware) {
          Serial.println("Ethernet shield was not found.  Sorry, can't run without hardware. ");}
           
        else if (Ethernet.linkStatus() == LinkOFF) {
          Serial.println("Ethernet cable is not connected.");}
        // no point in carrying on, so do nothing forevermore:
        for (;;);
      }
    
      g_server.begin();
    
      Serial.println(Ethernet.localIP());
      delay(1000);

    
      // Set BIA :ode to IDLE
      bia_mode = 0;

      //logic seems reversed for shutter command so we redefine the values here
      #define SHUTTER_LOW HIGH
      #define SHUTTER_HIGH LOW
    
      //////////////////////////////////// PARAM BIA /////////////////////////////////////////
      cycleIter = 0;
      divFactor = 1;
      maxOnIter = 1;
      g_apower = 0; // 0% power
      g_aduty = noStrobeDuty;
      g_aperiod = noStrobePeriod;

      g_sduty = g_aduty;
      g_speriod = g_aperiod;

      BIAIsOn = false;
      ledState = false;
      currentState = false;

      //////////////////////////////////// PARAM EXPOSURE //////////////////////////////////////
      doExposure = false;
      openStartedAt = 0;
      fullyOpenAt = 0;
      closeStartedAt = 0;
      fullyClosedAt = 0;

      //////////////////////////////////// PIN IO MODES ////////////////////////////////////////            
      //Shutter status pins on Bonn side are optocouplers so pullup is needed (was cabled on first hardware)
      
      pinMode(SHB_OPEN_PIN, INPUT_PULLUP);
      pinMode(SHB_CLOSE_PIN, INPUT_PULLUP);
      pinMode(SHB_ERROR_PIN, INPUT_PULLUP);
      
      pinMode(SHR_OPEN_PIN, INPUT_PULLUP);      
      pinMode(SHR_CLOSE_PIN, INPUT_PULLUP);
      pinMode(SHR_ERROR_PIN, INPUT_PULLUP);

      //Photoresistors
      pinMode(PHR0_INPUT_PIN, INPUT);//no need for pullup for these as it is cabled.
      pinMode(PHR1_INPUT_PIN, INPUT);//no need for pullup for these as it is cabled.

      //Outputs
      pinMode(SHR_PIN, OUTPUT);//RED Shutter command pin
      pinMode(SHB_PIN, OUTPUT);//BLUE Shutter command pin
      pinMode(LEDS_PIN, OUTPUT);//BIA LEDs command pin

      //Initial status
      digitalWrite(LEDS_PIN, LOW);//Start with BIA OFF, both shutters CLOSED
      digitalWrite(SHR_PIN, SHUTTER_LOW);     
      digitalWrite(SHB_PIN, SHUTTER_LOW);     
    
      MsTimer2::set(g_aperiod, Timer); // 1000ms period
      MsTimer2::start();
    }
    
    //ISR(TIMER1_COMPA_vect)
    void Timer()
    { //timer1 interrupt 1Hz
      if (BIAIsOn)
      {
        ledState = cycleIter < maxOnIter;
        if (ledState!=currentState){
          switchBiaLED(ledState);}

        cycleIter++;
        if (cycleIter>=divFactor)
          cycleIter = 0;
      }
    }

    
    int CalcGCD(unsigned int a, unsigned int b) {
      if (b == 0){
        return a;}
      else {
        return CalcGCD(b, a % b);}
    }

    bool SetBiaParameters(unsigned int duty, unsigned int period)
    {
      if ((duty < 0) || (duty > 100))
        return false;

      unsigned int gcd = CalcGCD(scaling, duty);
      if (period<(scaling/gcd))
        return false;

      divFactor = scaling / gcd;
      maxOnIter = duty / gcd;
      g_aduty = duty;
      g_aperiod = period;

      SetPeriod(period/divFactor);
      return true;

    }

    void switchBiaLED(bool reqState)
    {
      if (reqState) {
        analogWrite(LEDS_PIN, g_apower);}
      else {
        digitalWrite(LEDS_PIN, LOW);}

      currentState = reqState;
    }

    void resetExposureParam(){
      doExposure = false;
      openStartedAt = 0;
      fullyOpenAt = 0;
      closeStartedAt = 0;
      fullyClosedAt = 0;
    }

    
    int CommandCode(String comm)
    {
      int i;
      bool found;
    
      i = 0;
      found = false;
      while ((i < STATECMDS_COUNT) && (!CheckCommand(comm, StateCmds[i])))
        i++;
    
      return i;
    }
    
    bool StatusEvolve(int command, EthernetClient g_client)
    {
      bool cmdOk = false;
    
      switch (bia_mode)
      {
        case 0: // == everything OFF
          {
            switch (command)
            {
              case 0://bia_on, OK
                bia_mode = 10;
                cmdOk = true;
                break;
    
              case 1://bia_off while already off NOK
                break;
    
              case 2://shut_open : open both shutters OK
                bia_mode = 20;
                cmdOk = true;
                break;
    
              case 3://shut_close while they are already closed NOK
                break;
    
              case 4://blue_open : open blue shutter only OK
                bia_mode = 30;
                cmdOk = true;
                break;
    
              case 5://blue_close while blue shutter is already closed NOK
                break;
    
              case 6://red_open : open red shutter only OK
                bia_mode = 40;
                cmdOk = true;
                break;
    
              case 7://red_close : close red shutter while already closed NOK
                break;

              case 8://init : close both shutters, bia_off OK
                bia_mode = 0;
                cmdOk = true;
                break;

              case STATECMDS_COUNT://error / command not found NOK
                break;

            }
            break;
          }
    
        case 10: // == BIA is ON
          {
            switch (command)
            {
              case 0://bia_on
                break;
    
              case 1://bia_off
                bia_mode = 0;
                cmdOk = true;
                break;
    
              case 2://shut_open, INTERLOCK NOK
                break;
    
              case 3://shut_close while shutters are already closed NOK
                break;
    
              case 4://blue_open, INTERLOCK NOK
                break;
    
              case 5://blue_close while shutter blue is already closed NOK
                break;
    
              case 6://red_open, INTERLOCK NOK
                break;
    
              case 7://red_close while shutter red already closed NOK
                break;
    
              case 8://init : close both shutters, bia_off, OK
                bia_mode = 0;
                cmdOk = true;
                break;

              case STATECMDS_COUNT://error / command not found NOK
                break;
            }
            break;
          }
    
        case 20: // == Shutters are OPEN
          {
            switch (command)
            {
              case 0://bia_on, INTERLOCK NOK
                break;
    
              case 1://bia_off while bia is already off NOK
                break;
    
              case 2://shut_open while shutters are already open NOK
                break;
    
              case 3://shut_close : close both shutters OK
                bia_mode = 0;
                cmdOk = true;
                break;
    
              case 4://blue_open while shutter blue is already open, NOK
                break;
    
              case 5://blue_close : close blue shutter only OK
                bia_mode = 40;
                cmdOk = true;
                break;
    
              case 6://red_open while shutter red is already open, NOK
                break;
    
              case 7://red_close : close red shutter only OK
                bia_mode = 30;
                cmdOk = true;
                break;
    
              case 8://init : close both shutters, bia_off, OK
                bia_mode = 0;
                cmdOk = true;
                break;

              case STATECMDS_COUNT://error / command not found NOK
                break;
            }
            break;
    
          }
    
        case 30:// == BLUE Shutter OPEN & RED CLOSED
          {
            switch (command)
            {
              case 0://bia_on OK
                break;
    
              case 1://bia_off command while already off NOK
                break;
    
              case 2://shut_open : open both shutters OK
                bia_mode = 20;
                cmdOk = true;
                break;
    
              case 3://shut_close : close both shutters OK
                bia_mode = 0;
                cmdOk = true;
                break;
    
              case 4://blue_open while blue shutter is already open NOK
                break;
    
              case 5://blue_close : close blue shutter only OK
                bia_mode = 0;
                cmdOk = true;
                break;
    
              case 6://red_open : open red shutter only OK
                bia_mode = 20;
                cmdOk = true;
                break;
    
              case 7://red_close while red shutter already closed NOK
                break;
    
              case 8://init, close both shutters, bia_off OK
                bia_mode = 0;
                cmdOk = true;
                break;

              case STATECMDS_COUNT://error / command not found NOK
                break;
            }
            break;
          }
    
        case 40:// == RED Shutter OPEN & BLUE CLOSED
          {
            switch (command)
            {
              case 0://bia_on OK
                break;
    
              case 1://bia_off command while already off NOK
                break;
    
              case 2://shut_open : open both shutters OK
                bia_mode = 20;
                cmdOk = true;
                break;
    
              case 3://shut_close : close both shutters OK
                bia_mode = 0;
                cmdOk = true;
                break;
    
              case 4://blue_open : open blue shutter only OK
                bia_mode = 20;
                cmdOk = true;
                break;
    
              case 5://blue_close while blue shutter is already closed NOK
                break;
    
              case 6://red_open while red shutter is already open NOK
                break;
    
              case 7://red_close : close red shutter only OK
                bia_mode = 0;
                cmdOk = true;
                break;
    
              case 8://init : close both shutters, bia_off OK
                bia_mode = 0;
                cmdOk = true;
                break;

              case STATECMDS_COUNT://error / command not found NOK
                break;
            }
            break;
          }
      }
      if (!cmdOk){
        return cmdOk;
      }
    
      switch (bia_mode)
      { //Actions upon state
        case 0:  //All off.
          BIAIsOn = false;
          switchBiaLED(BIAIsOn);
    
          digitalWrite(SHR_PIN, SHUTTER_LOW); //Both shutters closed
          digitalWrite(SHB_PIN, SHUTTER_LOW);

//          ReportCompletion(STATUS_BCRC);
          if (!WaitForCompletion(STATUS_BCRC))
          {
            g_client.write("shutter switch timeout");
            cmdOk=false;
          }

          break;
    
        /////////////////////////////////////////////////////////////////// ACTIONS DU MODE 1 SHUTTER//
        case 10: //BIA is ON, shutters are closed
          BIAIsOn = true;
    
          digitalWrite(SHR_PIN, SHUTTER_LOW); //Both shutters closed
          digitalWrite(SHB_PIN, SHUTTER_LOW);
          
          if (!WaitForCompletion(STATUS_BCRC))
          {
            g_client.write("shutter switch timeout");
            cmdOk=false;
          }
          
          break;
        // //////////////////////////////////////////////////////////////////ACTIONS DU MODE BIA//
        case 20: //BIA is OFF, shutters are OPEN
          BIAIsOn = false;
          switchBiaLED(BIAIsOn);
    
          digitalWrite(SHR_PIN, SHUTTER_HIGH); //Both shutters closed
          digitalWrite(SHB_PIN, SHUTTER_HIGH);
          
          
          if (!WaitForCompletion(STATUS_BORO))
          {
            g_client.write("shutter switch timeout");
            cmdOk=false;
          }

          break;
    
        case 30:// BIA is OFF, Blue shutter is OPEN Red is CLOSED
          BIAIsOn = false;
          switchBiaLED(BIAIsOn);
    
          digitalWrite(SHB_PIN, SHUTTER_HIGH); //OPEN
          digitalWrite(SHR_PIN, SHUTTER_LOW);  //CLOSED
          
//          ReportCompletion(STATUS_BORC);
          if (!WaitForCompletion(STATUS_BORC))
          {
            g_client.write("shutter switch timeout");
            cmdOk=false;
          }

          break;
    
        case 40://BIA OFF, Red Shutter open, Blue closed
          BIAIsOn = false;
          switchBiaLED(BIAIsOn);
    
          digitalWrite(SHB_PIN, SHUTTER_LOW); //CLOSED
          digitalWrite(SHR_PIN, SHUTTER_HIGH);  //OPEN
          
//          ReportCompletion(STATUS_BCRO);
          if (!WaitForCompletion(STATUS_BCRO))
          {
            g_client.write("shutter switch timeout");
            cmdOk=false;
          }

          break;
      }
      return cmdOk;
    }
    
    
    bool CheckCommand(String command, String keyword)
    {
      int l = keyword.length();
      return (command.substring(0, l) == keyword);
    }
    
    void SetPeriod(int d)
    {
      //OCR1A = d;
    
      MsTimer2::stop();
      MsTimer2::set(d, Timer);
      MsTimer2::start();
    }
    
    int UpdateStatusWord()
    {
       ///// LECTURE EFFECTIVE DES STATUS SUR PORT IO ARDUINO //
      shb_open_status = digitalRead(SHB_OPEN_PIN);       // BS ouvert//
      shb_close_status = digitalRead(SHB_CLOSE_PIN); // BS ferme //
      shb_err_status = digitalRead(SHB_ERROR_PIN); // BS error //
      shr_open_status = digitalRead(SHR_OPEN_PIN); // RS ouvert //
      shr_close_status = digitalRead(SHR_CLOSE_PIN);  // RS fermé //
      shr_err_status = digitalRead(SHR_ERROR_PIN);  //RS error //

      StatWord = (1<<6)+
                 (!(shb_open_status)<<5)+
                 (!(shb_close_status)<<4)+
                 (!(shb_err_status)<<3)+
                 (!(shr_open_status)<<2)+
                 (!(shr_close_status)<<1)+
                 (!shr_err_status);
   
      return StatWord;
    }
    
    bool WaitForCompletion(int val)
    {
      int t, now;
      int v;
      
      t = millis();
      now = t;
      
      v = UpdateStatusWord();
      
      while ((v != val) && ((now-t)<ShutterMotionTimeout))
      {
        delay(10);
        v = UpdateStatusWord();
        now = millis();
      }
      // if an exposure has been declared then save required variable
      if (doExposure){
        if (val!=STATUS_BCRC){
          openStartedAt = t;
          fullyOpenAt = now;}
        else {
          closeStartedAt = t;
          fullyClosedAt = now;}
      }
      return (v == val);
    }
    
    void Command(EthernetClient g_client, String mycommand)
    {
    
      UpdateStatusWord();
      
      int cmd = CommandCode(mycommand);

      bool cmdOk;
      cmdOk = StatusEvolve(cmd, g_client);
          
      // Controller status, parameters parsing
      
      if (CheckCommand(mycommand, "statword"))
      {
        g_client.print(StatWord);
        cmdOk = true;
      }
    
      if (CheckCommand(mycommand, "status"))
      {
        g_client.print(bia_mode);
        cmdOk = true;
      }

      if (CheckCommand(mycommand, "get_duty"))
      {
        g_client.print(g_aduty);
        cmdOk = true;
      }
    
      if (CheckCommand(mycommand, "get_period"))
      {
        g_client.print(g_aperiod);
        cmdOk = true;
      }
    
      if (CheckCommand(mycommand, "get_power"))
      {
        g_client.print(g_apower);
        cmdOk = true;
      }

      if (CheckCommand(mycommand, "get_param"))
      {
        g_client.print(g_aduty);
        g_client.print(',');
        g_client.print(g_aperiod);
        g_client.print(',');
        g_client.print(g_apower);
        cmdOk = true;
      }

      if (CheckCommand(mycommand, "get_version"))
      {
        g_client.print(FIRMWARE_VERSION);
        cmdOk = true;
      }
       
      if (CheckCommand(mycommand, "read_phr"))
      {
        int phr0, phr1;
        phr0 = analogRead(PHR0_INPUT_PIN);
        phr1 = analogRead(PHR1_INPUT_PIN);

        g_client.print(phr0);
        g_client.print(',');
        g_client.print(phr1);

        cmdOk = true;
      }
      
      /// (DE)ACTIVATE BIA STROBING MODE ///
    
      if (CheckCommand(mycommand, "pulse_on"))
      {
        SetBiaParameters(g_sduty, g_speriod);
        cmdOk = true;
      }
    
      if (CheckCommand(mycommand, "pulse_off"))
      {
        SetBiaParameters(noStrobeDuty, noStrobePeriod);
        cmdOk = true;
      }


      /// SET NEW BIA PARAMETERS ///
      
      unsigned int v;

      if (CheckCommand(mycommand, "set_duty"))
      {
        int mylen = mycommand.length();
        String inter = mycommand.substring(8, mylen);

        v = (unsigned int)inter.toInt();

        if (SetBiaParameters(v, g_speriod)){
          g_sduty = v;
          g_client.print(g_sduty);
          cmdOk = true;
        }
      }

      if (CheckCommand(mycommand, "set_period"))
      {
        int mylen = mycommand.length();
        String inter = mycommand.substring(10, mylen);
    
        v = (unsigned int)inter.toInt();
      
        if (SetBiaParameters(g_sduty, v)){
          g_speriod = v;
          g_client.print(g_speriod);
          cmdOk = true;
        }
      }
    
      if (CheckCommand(mycommand, "set_power"))
      {
        int mylen = mycommand.length();
        String inter = mycommand.substring(9, mylen);
    
        v = (unsigned int)inter.toInt();
      
        if ((v > 0) && (v < 256)){
          g_apower = v;
          switchBiaLED(BIAIsOn);
          g_client.print(g_apower);
          cmdOk = true;
        }
      }

      /// EXPOSURE COMMANDS ///

      if (CheckCommand(mycommand, "start_exposure"))
      {
        resetExposureParam();
        doExposure = true;
        cmdOk = true;
      }

      if (CheckCommand(mycommand, "finish_exposure"))
      {
        unsigned long transientTime1 = fullyOpenAt - openStartedAt;
        unsigned long exposureTime = closeStartedAt - fullyOpenAt;
        unsigned long transientTime2 = fullyClosedAt - closeStartedAt;

        g_client.print(transientTime1);
        g_client.print(',');
        g_client.print(exposureTime);
        g_client.print(',');
        g_client.print(transientTime2);

        resetExposureParam();
        cmdOk = true;
      }

    
      //////////////////////////////////////////// AFFICHAGE COMMANDE NOK /////////////////////////////////////////////
      if (!cmdOk)
      {
        g_client.write("nok\r\n");
      }
      else
      {
        g_client.write("ok\r\n");
      }
    
        return;   
    }
    
    
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////// MAIN LOOP //////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    
    void loop() {
      switch (Ethernet.maintain()) {
        case 1: Serial.println("Error: DHCP renewed fail. ");
                break;
        case 2: Serial.println("DHCP renewed success:");
                Serial.println(Ethernet.localIP());
                break;
        case 3: Serial.println("Error: DHCP rebind fail. ");
                break;
        case 4: Serial.println("DHCP rebind success:");
                Serial.println(Ethernet.localIP());
                break;
        default://nothing happened
                break;
      }
      
      //  Serial.println("Waiting...");
      EthernetClient g_client = g_server.available();
      if (g_client)
      {
        //Serial.println("client connected...");
        commandBuffer.clear();
        commandStr[0]='\0';
        while (g_client.connected()) 
        {
          if (g_client.available())
          {
            while(g_client.available())
            {
              char c = g_client.read();
                if (c !=  - 1)
                {
                  commandBuffer.unshift(c);
                }  
            }
            bufferSize = commandBuffer.size();
            
            for (int i=0;i<bufferSize;i++)
            {
              commandStr[i] = commandBuffer[bufferSize-1-i];
            }
            commandStr[bufferSize]= '\0';

            char *ptr = strstr(commandStr, EOL);
  
            if (ptr != NULL) 
            {
              strtok(commandStr, EOL);

              Command(g_client, commandStr);
              
              commandBuffer.clear();
              commandStr[0]='\0';
            }
            
          }
          delay(10);
        }
        //Serial.println("client disconnected...");
      }

    }
