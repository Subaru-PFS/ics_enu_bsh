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
    const char FIRMWARE_VERSION[] = "0.1.4";

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
    
    String state_commands[] =
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

    CircularBuffer<char, 100> command_buffer;
    int buffer_size;
    char command_str[100];
    const char EOL[] = "\r\n";
        
    // BIA Variables
    int bia_mode;
    bool bia_is_on;
    bool desired_led_state;
    bool current_led_state;

    const unsigned int SCALING = 100;
    const unsigned int NO_STROBE_DUTY = 100;
    const unsigned int NO_STROBE_PERIOD = 1000;
    unsigned int div_factor;
    unsigned int max_on_iter;
    unsigned int cycle_iter;

    unsigned int g_aduty; // duty
    unsigned int g_aperiod; // period
    unsigned int g_apower; // power

    unsigned int g_sduty; // saved duty
    unsigned int g_speriod; // saved period

    // Shutters variables
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
    
    const int SHUTTER_MOTION_TIMEOUT = 2000;
    int status_word;

    // Exposure variables
    bool do_exposure;
    unsigned long open_started_at;
    unsigned long fully_open_at;
    unsigned long close_started_at;
    unsigned long fully_closed_at;
    
    // setup
    void setup(){
      // Serial just when usb is connected
      Serial.begin(9600);      
      Serial.print("ics_enu_bsh_version=");
      Serial.println(FIRMWARE_VERSION);
    
      if (Ethernet.begin(mac) == 0){
        Serial.println("Failed to configure Ethernet using DHCP.");

        if (Ethernet.hardwareStatus() == EthernetNoHardware) {
          Serial.println("Ethernet shield was not found.");
        }
           
        else if (Ethernet.linkStatus() == LinkOFF) {
          Serial.println("Ethernet cable is not connected.");
        }
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
      cycle_iter = 0;
      div_factor = 1;
      max_on_iter = 1;
      g_apower = 0; // 0% power
      g_aduty = NO_STROBE_DUTY;
      g_aperiod = NO_STROBE_PERIOD;

      g_sduty = g_aduty;
      g_speriod = g_aperiod;

      bia_is_on = false;
      desired_led_state = false;
      current_led_state = false;

      //////////////////////////////////// PARAM EXPOSURE //////////////////////////////////////
      do_exposure = false;
      open_started_at = 0;
      fully_open_at = 0;
      close_started_at = 0;
      fully_closed_at = 0;

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
    
      MsTimer2::set(g_aperiod, biaTimerCallback); // 1000ms period
      MsTimer2::start();
    }

    void setTimerPeriod(unsigned int timer_period){
      // set timer period, note that MsTimer2.set() take nominally unsigned long.
      MsTimer2::stop();
      MsTimer2::set(timer_period, biaTimerCallback);
      MsTimer2::start();
    }

    //ISR(TIMER1_COMPA_vect)
    void biaTimerCallback(){
      // bia timer callback.
      if (bia_is_on){
        desired_led_state = cycle_iter < max_on_iter;
        if (desired_led_state != current_led_state){
          switchBiaLED(desired_led_state);
        }
        cycle_iter++;
        if (cycle_iter >= div_factor){
          cycle_iter = 0;
        }
      }
    }

    int calcGCD(unsigned int a, unsigned int b) {
      // calculate greatest common divisor.
      if (b == 0){
        return a;
      }
      else {
        return calcGCD(b, a % b);
      }
    }

    bool setBiaParameters(unsigned int duty, unsigned int period){
      // set new bia parameters.
      unsigned int gcd = calcGCD(SCALING, duty);
      if (period<(SCALING/gcd)){
        return false;
      }
  
      div_factor = SCALING / gcd;
      max_on_iter = duty / gcd;
      g_aduty = duty;
      g_aperiod = period;

      setTimerPeriod(period/div_factor);
      return true;
    }

    void switchBiaLED(bool requested_state){
      // change LED state.
      if (requested_state) {
        analogWrite(LEDS_PIN, g_apower);}
      else {
        digitalWrite(LEDS_PIN, LOW);}

      current_led_state = requested_state;
    }

    void resetExposureParam(){
      // reset exposure parameters.
      do_exposure = false;
      open_started_at = 0;
      fully_open_at = 0;
      close_started_at = 0;
      fully_closed_at = 0;
    }

    int getCommandCode(String comm){
      // return state command indice.
      int i;
      bool found;
   
      i = 0;
      found = false;
      while ((i < STATECMDS_COUNT) && (!checkCommand(comm, state_commands[i])))
        i++;
    
      return i;
    }
    
    int statusEvolve(int command){
      // state machine command, will be rejected if forbidden or irrelevant.
      int error_code = -3; // set transition error by default.
      switch (bia_mode)
      {
        ////////////// BIA OFF, BLUE shutter CLOSED, RED shutter CLOSED ///////////////
        case 0:
          {
            switch (command)
            {
              case 0://bia_on, OK
                bia_mode = 10;
                error_code = 0; // cmdOk
                break;
    
              case 1://bia_off while already off NOK
                break;
    
              case 2://shut_open : open both shutters OK
                bia_mode = 20;
                error_code = 0; // cmdOk
                break;
    
              case 3://shut_close while they are already closed NOK
                break;
    
              case 4://blue_open : open blue shutter only OK
                bia_mode = 30;
                error_code = 0; // cmdOk
                break;
    
              case 5://blue_close while blue shutter is already closed NOK
                break;
    
              case 6://red_open : open red shutter only OK
                bia_mode = 40;
                error_code = 0; // cmdOk
                break;
    
              case 7://red_close : close red shutter while already closed NOK
                break;

              case 8://init : close both shutters, bia_off OK
                bia_mode = 0;
                error_code = 0; // cmdOk
                break;

              case STATECMDS_COUNT://error / command not found NOK
                error_code = -1; // not recognized command
                break;

            }
            break;
          }
        ////////////// BIA ON, BLUE shutter CLOSED, RED shutter CLOSED ///////////////
        case 10: // == BIA is ON
          {
            switch (command)
            {
              case 0://bia_on
                break;
    
              case 1://bia_off
                bia_mode = 0;
                error_code = 0; // cmdOk
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
                error_code = 0; // cmdOk
                break;

              case STATECMDS_COUNT://error / command not found NOK
                error_code = -1; // not recognized command
                break;
            }
            break;
          }
        ////////////// BIA OFF, BLUE shutter OPEN, RED shutter OPEN ///////////////
        case 20:
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
                error_code = 0; // cmdOk
                break;
    
              case 4://blue_open while shutter blue is already open, NOK
                break;
    
              case 5://blue_close : close blue shutter only OK
                bia_mode = 40;
                error_code = 0; // cmdOk
                break;
    
              case 6://red_open while shutter red is already open, NOK
                break;
    
              case 7://red_close : close red shutter only OK
                bia_mode = 30;
                error_code = 0; // cmdOk
                break;
    
              case 8://init : close both shutters, bia_off, OK
                bia_mode = 0;
                error_code = 0; // cmdOk
                break;

              case STATECMDS_COUNT://error / command not found NOK
                error_code = -1; // not recognized command
                break;
            }
            break;
          }
        ////////////// BIA OFF, BLUE shutter OPEN, RED shutter CLOSED ////////////////
        case 30:
          {
            switch (command)
            {
              case 0://bia_on OK
                break;
    
              case 1://bia_off command while already off NOK
                break;
    
              case 2://shut_open : open both shutters OK
                bia_mode = 20;
                error_code = 0; // cmdOk
                break;
    
              case 3://shut_close : close both shutters OK
                bia_mode = 0;
                error_code = 0; // cmdOk
                break;
    
              case 4://blue_open while blue shutter is already open NOK
                break;
    
              case 5://blue_close : close blue shutter only OK
                bia_mode = 0;
                error_code = 0; // cmdOk
                break;
    
              case 6://red_open : open red shutter only OK
                bia_mode = 20;
                error_code = 0; // cmdOk
                break;
    
              case 7://red_close while red shutter already closed NOK
                break;
    
              case 8://init, close both shutters, bia_off OK
                bia_mode = 0;
                error_code = 0; // cmdOk
                break;

              case STATECMDS_COUNT://error / command not found NOK
                error_code = -1; // not recognized command
                break;
            }
            break;
          }
        ////////////// BIA OFF, BLUE shutter CLOSED, RED shutter OPEN ///////////////
        case 40:
          {
            switch (command)
            {
              case 0://bia_on OK
                break;
    
              case 1://bia_off command while already off NOK
                break;
    
              case 2://shut_open : open both shutters OK
                bia_mode = 20;
                error_code = 0; // cmdOk
                break;
    
              case 3://shut_close : close both shutters OK
                bia_mode = 0;
                error_code = 0; // cmdOk
                break;
    
              case 4://blue_open : open blue shutter only OK
                bia_mode = 20;
                error_code = 0; // cmdOk
                break;
    
              case 5://blue_close while blue shutter is already closed NOK
                break;
    
              case 6://red_open while red shutter is already open NOK
                break;
    
              case 7://red_close : close red shutter only OK
                bia_mode = 0;
                error_code = 0; // cmdOk
                break;
    
              case 8://init : close both shutters, bia_off OK
                bia_mode = 0;
                error_code = 0; // cmdOk
                break;

              case STATECMDS_COUNT://error / command not found NOK
                error_code = -1; // not recognized command
                break;
            }
            break;
          }
      }

      if (error_code != 0){ //cmd not OK, not need to go further.
        return error_code;
      }

      //Actions upon state
      switch (bia_mode)
      {
        ////////////// BIA OFF, BLUE shutter CLOSED, RED shutter CLOSED ///////////////
        case 0:
          bia_is_on = false;
          switchBiaLED(bia_is_on);
    
          digitalWrite(SHR_PIN, SHUTTER_LOW); //CLOSED
          digitalWrite(SHB_PIN, SHUTTER_LOW); //CLOSED

          if (!waitForCompletion(STATUS_BCRC)){
            error_code = -2; // shutter timeout error.
          }
          break;
    
        ////////////// BIA ON, BLUE shutter CLOSED, RED shutter CLOSED ///////////////
        case 10:
          bia_is_on = true;
    
          digitalWrite(SHR_PIN, SHUTTER_LOW); //CLOSED
          digitalWrite(SHB_PIN, SHUTTER_LOW); //CLOSED
          
          if (!waitForCompletion(STATUS_BCRC)){
            error_code = -2; // shutter timeout error.
          }
          break;

        ////////////// BIA OFF, BLUE shutter OPEN, RED shutter OPEN ///////////////
        case 20:
          bia_is_on = false;
          switchBiaLED(bia_is_on);
    
          digitalWrite(SHR_PIN, SHUTTER_HIGH); //OPEN
          digitalWrite(SHB_PIN, SHUTTER_HIGH); //OPEN

          if (!waitForCompletion(STATUS_BORO)){
            error_code = -2; // shutter timeout error.
          }
          break;

        ////////////// BIA OFF, BLUE shutter OPEN, RED shutter CLOSED ////////////////
        case 30:
          bia_is_on = false;
          switchBiaLED(bia_is_on);
    
          digitalWrite(SHB_PIN, SHUTTER_HIGH); //OPEN
          digitalWrite(SHR_PIN, SHUTTER_LOW);  //CLOSED

          if (!waitForCompletion(STATUS_BORC)){
            error_code = -2; // shutter timeout error.
          }
          break;

        ////////////// BIA OFF, BLUE shutter CLOSED, RED shutter OPEN ///////////////
        case 40:
          bia_is_on = false;
          switchBiaLED(bia_is_on);
    
          digitalWrite(SHB_PIN, SHUTTER_LOW);  //CLOSED
          digitalWrite(SHR_PIN, SHUTTER_HIGH); //OPEN

          if (!waitForCompletion(STATUS_BCRO)){
            error_code = -2; // shutter timeout error.
          }
          break;
      }
      return error_code;
    }

    bool checkCommand(String input_command, String known_command){
      // check input command against known command.
      int l = known_command.length();
      return (input_command.substring(0, l) == known_command);
    }
    
    int updateStatusWord(){
      // reading actual inputs from shutters.
      
      shb_open_status = digitalRead(SHB_OPEN_PIN);    // BS open limit switch state//
      shb_close_status = digitalRead(SHB_CLOSE_PIN); // BS close limit switch state //
      shb_err_status = digitalRead(SHB_ERROR_PIN); // BS error //
      shr_open_status = digitalRead(SHR_OPEN_PIN); // RS open limit switch state //
      shr_close_status = digitalRead(SHR_CLOSE_PIN);  // RS close limit switch state //
      shr_err_status = digitalRead(SHR_ERROR_PIN);  //RS error //

      status_word = (1<<6) +
                    (!(shb_open_status)<<5) +
                    (!(shb_close_status)<<4) +
                    (!(shb_err_status)<<3) +
                    (!(shr_open_status)<<2) +
                    (!(shr_close_status)<<1) +
                    (!shr_err_status);
   
      return status_word;
    }
    
    bool waitForCompletion(int dest){
      // wait for shutter status word to match dest word.
      unsigned long motion_start, now_ms;
      int wd;
      
      motion_start = millis();
      now_ms = motion_start;
      wd = updateStatusWord();
      
      while ((wd != dest) && ((now_ms - motion_start) < SHUTTER_MOTION_TIMEOUT)){
        delay(10);
        wd = updateStatusWord();
        now_ms = millis();
      }
      // if an exposure has been declared then save required variable.
      if (do_exposure){
        if (dest != STATUS_BCRC){
          open_started_at = motion_start;
          fully_open_at = now_ms;
        }
        else {
          close_started_at = motion_start;
          fully_closed_at = now_ms;
        }
      }
      return (wd == dest);
    }
    
    long retrieveInputValue(String input_command, String command_head){
      // retrieve input value from input_command
      String inter = input_command.substring(command_head.length(), input_command.length());
      long new_value = inter.toInt();
      return new_value;
    }

    void parseCommand(EthernetClient g_client, String input_command){
      // parse input command.
      updateStatusWord();

      // check state machine command.
      int cmd = getCommandCode(input_command);
      int error_code = statusEvolve(cmd);
          
      /// STATUS, PARAMETERS COMMAND. ///
      
      if (checkCommand(input_command, "statword")){
        g_client.print(status_word);
        error_code = 0; // cmdOk
      }
    
      if (checkCommand(input_command, "status")){
        g_client.print(bia_mode);
        error_code = 0; // cmdOk
      }

      if (checkCommand(input_command, "get_duty")){
        g_client.print(g_aduty);
        error_code = 0; // cmdOk
      }
    
      if (checkCommand(input_command, "get_period")){
        g_client.print(g_aperiod);
        error_code = 0; // cmdOk
      }
    
      if (checkCommand(input_command, "get_power")){
        g_client.print(g_apower);
        error_code = 0; // cmdOk
      }

      if (checkCommand(input_command, "get_param")){
        g_client.print(g_aduty);
        g_client.print(',');
        g_client.print(g_aperiod);
        g_client.print(',');
        g_client.print(g_apower);
        error_code = 0; // cmdOk
      }

      if (checkCommand(input_command, "get_version")){
        g_client.print(FIRMWARE_VERSION);
        error_code = 0; // cmdOk
      }
       
      if (checkCommand(input_command, "read_phr")){
        int phr0, phr1;
        phr0 = analogRead(PHR0_INPUT_PIN);
        phr1 = analogRead(PHR1_INPUT_PIN);

        g_client.print(phr0);
        g_client.print(',');
        g_client.print(phr1);

        error_code = 0; // cmdOk
      }
      
      /// (DE)ACTIVATE BIA STROBING MODE ///
    
      if (checkCommand(input_command, "pulse_on")){
        setBiaParameters(g_sduty, g_speriod);
        error_code = 0; // cmdOk
      }
    
      if (checkCommand(input_command, "pulse_off")){
        setBiaParameters(NO_STROBE_DUTY, NO_STROBE_PERIOD);
        error_code = 0; // cmdOk
      }

      /// SET NEW BIA PARAMETERS ///
      
      long new_value;

      if (checkCommand(input_command, "set_duty")){
        new_value = retrieveInputValue(input_command, "set_duty");
        // check duty range(1-100) and compatibility with saved period.
        if (((new_value > 0) && (new_value <= 100)) && (setBiaParameters(new_value, g_speriod))){
          g_sduty = new_value;
          g_client.print(g_sduty);
          error_code = 0; // cmdOk
        }
        else {
          error_code = -4; // out of range
        }
      }

      if (checkCommand(input_command, "set_period")) {
        new_value = retrieveInputValue(input_command, "set_period");
        // check period range(1-65535, per unsigned int) and compatibility with saved duty cycle.
        if (((new_value > 0) && (new_value <= 65535)) && (setBiaParameters(g_sduty, new_value))){
          g_speriod = new_value;
          g_client.print(g_speriod);
          error_code = 0; // cmdOk
        }
        else {
          error_code = -4; // out of range
        }
      }
    
      if (checkCommand(input_command, "set_power")){
        new_value = retrieveInputValue(input_command, "set_power");
        if ((new_value > 0) && (new_value <= 255)){
          g_apower = new_value;
          switchBiaLED(bia_is_on);
          g_client.print(g_apower);
          error_code = 0; // cmdOk
        }
        else {
          error_code = -4; // out of range
        }
      }

      /// EXPOSURE COMMANDS ///

      if (checkCommand(input_command, "start_exp")){
        if (!do_exposure){
          resetExposureParam();
          do_exposure = true;
          error_code = 0; // cmdOk
        }
        else{
          error_code = -5; // exposure already declared.
        }
      }

      if (checkCommand(input_command, "finish_exp")){
        // check that close cmd has logically followed open cmd
        if (do_exposure){
          if (open_started_at==0){
            error_code = -6; // exposure not completed yet.
          }
          else if (close_started_at==0){
            error_code = -6; // exposure not completed yet.
          }
          else{
            unsigned long transient_time_opening = fully_open_at - open_started_at;
            unsigned long exposure_time = close_started_at - fully_open_at;
            unsigned long transient_time_closing = fully_closed_at - close_started_at;

            g_client.print(transient_time_opening);
            g_client.print(',');
            g_client.print(exposure_time);
            g_client.print(',');
            g_client.print(transient_time_closing);

            resetExposureParam();
            error_code = 0; // cmdOk
          }
        }
        else{
          error_code = -7; // exposure not declared.
        }
      }

      if (checkCommand(input_command, "cancel_exp")){
        if (do_exposure){
          resetExposureParam();
          error_code = 0; // cmdOk
        }
        else{
          error_code = 60; // exposure not declared.
        }
      }

      //////////////////////////////////////////// AFFICHAGE COMMANDE NOK /////////////////////////////////////////////

      if (error_code != 0){ //cmd not OK
        switch (error_code){
          case -1:
            g_client.write("unrecognized command:");
            g_client.write(command_str);
            break;
          case -2:
            g_client.write("shutter switch timeout.");
            break;
          case -3:
            g_client.write("transition not allowed.");
            break;
          case -4:
            g_client.write("value out of range.");
            break;
          case -5:
            g_client.write("exposure already declared.");
            break;
          case -6:
            g_client.write("exposure not yet completed.");
            break;
          case -7:
            g_client.write("no exposure declared.");
            break;
          }
        g_client.write("nok\r\n");
      }
      else{
        g_client.write("ok\r\n");
      }
      return;
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////// MAIN LOOP //////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    
    void loop() {
      switch (Ethernet.maintain()) {
        case 1: Serial.println("DHCP renewed fail!");
                break;
        case 2: Serial.println("DHCP renewed success:");
                Serial.println(Ethernet.localIP());
                break;
        case 3: Serial.println("DHCP rebind fail!");
                break;
        case 4: Serial.println("DHCP rebind success:");
                Serial.println(Ethernet.localIP());
                break;
        default://nothing happened
                break;
      }
      
      //  Serial.println("Waiting...");
      EthernetClient g_client = g_server.available();
      if (g_client){
        // new client connected.
        command_buffer.clear();
        command_str[0]='\0';
        while (g_client.connected()){
          // while socket is open.
          if (g_client.available()){
            // client sent some bytes, put every character in the command_buffer.
            while(g_client.available()){
              char c = g_client.read();
              if (c != -1){
                command_buffer.unshift(c);
              }
            }
            // building the input command_str
            buffer_size = command_buffer.size();
            for (int i=0;i<buffer_size;i++){
              command_str[i] = command_buffer[buffer_size-1-i];
            }
            command_str[buffer_size]= '\0';

            //check that the EOL is indeed in the command_str.
            char *ptr = strstr(command_str, EOL);
            if (ptr != NULL){
              // split EOL from command_str and parse command.
              strtok(command_str, EOL);
              parseCommand(g_client, command_str);
              // clear command_buffer.
              command_buffer.clear();
              command_str[0]='\0';
            }
          }
          delay(10);
          // client is still connected, waiting for next command.
        }
        // client disconnected.
      }

    }
