/*
    Antennta Switch by K9SUL
    Network controlled coax switches.
    It has a 4-port antenna switch and two 2-port coax switches for
    the transfer function.

    GP0: relay 1 - off xfer 1, on xfer 2
    GP1: relay 2 - off r3, on r4
    GP2: relay 3 - off a1, on a2
    GP3: relay 4 - off a3, on a4
    GP4: button (input)

    The current version of LiquidCrystal_I2C does not work with I2C1 of the Pico.
    GP8: i2c0 SDA - LCD address 0x27
    GP9: i2c0 SCL

    WIZnet Ethernet via SPI
    Use the board package from WIZnet:
    https://github.com/WIZnet-ArduinoEthernet/arduino-pico/releases/download/global/package_rp2040-ethernet_index.json
    Select - Board WIZnet W5100S-EVB-Pico

    http commands
    /status  : get status. e.g. 2,1 (ant 2 and transfer 1)
    /set?a=n : select antenna n (1 to 4)
    /set?t=n : select transfer port n (1 or 2)

    Browsing to / shows a web page where a port can be switched with a click.
*/
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <Ethernet.h>

// Use a custom DHCP client name. The default is "WIZnet".
#ifdef HOST_NAME
#undef HOST_NAME
#define HOST_NAME "antsw"
#endif

//       R2 R3 R4
// A1 :  0  0  0
// A2 :  0  1  0
// A3 :  1  0  0
// A4 :  1  0  1
#define ANTSW_RY1 0
#define ANTSW_RY2 1
#define ANTSW_RY3 2
#define ANTSW_RY4 3

#define ANTSW_BTN 4

uint8_t antenna_port = 1; // 1, 2, 3, 4
uint8_t xfer_port = 1; // 1 or 2
uint8_t current_port = 0; // 0 for ant, 1 for xfer
boolean use_network = true;
volatile boolean line1_buff_updated = false;
boolean net_link_down = false;
char line1_buff[20];

byte mac[] = { 0xAE, 0xC8, 0x20, 0xCC, 0xC7, 0x9B };
LiquidCrystal_I2C lcd(0x27, 16, 2);
EthernetServer server(3209);

// core 0 setup. network init.
// network status is shown in line1 of the LCD.
// core 0 populates the buffer and core 1 does the actual updateof the display.
void setup() {
  // Initialize the network
  sprintf(line1_buff, "Initializing...");
  line1_buff_updated = true;
  Ethernet.init(17); // GP17 is connected to the CSn pin

  net_link_down = (Ethernet.linkStatus() != LinkON);
  if (net_link_down) {
    // hw link is down. 
    sprintf(line1_buff, "No connection");
    line1_buff_updated = true;
    use_network = false;
  } else if (Ethernet.begin(mac)) {
    // dhcp succeeded
    use_network = true;
    update_ip_address();
    server.begin();
  } else {
    // link was up, but dhcp failed.
    sprintf(line1_buff, "DHCP timeout");
    line1_buff_updated = true;
    use_network = false;
    delay(2000);
    sprintf(line1_buff, "No connection");
    line1_buff_updated = true;
  }
}

// Core 1 setup.
void setup1() {
  // Initialize the i2c LCD
  Wire.setSDA(8); // GP8 for SDA
  Wire.setSCL(9); // GP9 for SCL
  Wire.begin();

  lcd.init();
  lcd.backlight();
  lcd.begin(16, 2);
  lcd.setCursor(0, 0);
  lcd.print("COAX SW by K9SUL");

  // relay control pins
  pinMode(ANTSW_RY1, OUTPUT_12MA);
  pinMode(ANTSW_RY2, OUTPUT_12MA);
  pinMode(ANTSW_RY3, OUTPUT_12MA);
  pinMode(ANTSW_RY4, OUTPUT_12MA);

  // button
  pinMode(ANTSW_BTN, INPUT_PULLUP);

  // delay for splash screen. Update line1 in the mean time.
  for (int i = 0; i < 10; i++) {
    update_lcd_line1();
    delay(200);
  }
  update_display();
}


void next_ant_port() {
  antenna_port++;
  if (antenna_port > 4)
    antenna_port = 1;
}

void switch_xfer_port() {
  xfer_port = (xfer_port == 1) ? 2 : 1;
}

// show port status on the 1st line
void update_display() {
  char buff[20];
  lcd.setCursor(0, 0);

  if (current_port == 0) {
    sprintf(buff, "[ANT] %d   XFR  %d", antenna_port, xfer_port);
  } else {
    sprintf(buff, " ANT  %d  [XFR] %d", antenna_port, xfer_port);
  }
  lcd.print(buff);
}

void update_lcd_line1() {
  if (!line1_buff_updated)
    return;
  lcd.setCursor(0, 1);
  lcd.print("                ");
  lcd.setCursor(0, 1);
  lcd.print(line1_buff);
  line1_buff_updated = false;
}

void update_ip_address() {
 sprintf(line1_buff, "%s", Ethernet.localIP().toString().c_str());
 line1_buff_updated = true;
}

void actuate_ant_port(uint8_t ant) {
  switch (ant) {
    case 1:
      digitalWrite(ANTSW_RY2, LOW);
      digitalWrite(ANTSW_RY3, LOW);
      digitalWrite(ANTSW_RY4, LOW);
      break;
    case 2:
      digitalWrite(ANTSW_RY2, LOW);
      digitalWrite(ANTSW_RY3, HIGH);
      digitalWrite(ANTSW_RY4, LOW);
      break;
    case 3:
      digitalWrite(ANTSW_RY2, HIGH);
      digitalWrite(ANTSW_RY3, LOW);
      digitalWrite(ANTSW_RY4, LOW);
      break;
    case 4:
      digitalWrite(ANTSW_RY2, HIGH);
      digitalWrite(ANTSW_RY3, LOW);
      digitalWrite(ANTSW_RY4, HIGH);
      break;
    default:
      break;
  }
}

void actuate_xfer_port(uint8_t xfer) {
  if (xfer == 1) {
    digitalWrite(ANTSW_RY1, LOW);
  } else {
    digitalWrite(ANTSW_RY1, HIGH);
  }
}

// handle short and long presses.
// the delay of 200ms also works as debounce
void handle_btn_press() {
  if (!digitalRead(ANTSW_BTN)) {
    int i; // delay counter

    // wait up to 1.5 seconds.
    for (i = 1; i <= 15; i++) {
      delay(100);
      if (digitalRead(ANTSW_BTN))
        break;
    }

    // a short press.
    if (i < 15) {
      if (current_port == 1) {
        switch_xfer_port();
        actuate_xfer_port(xfer_port);
      } else {
        next_ant_port();
        actuate_ant_port(antenna_port);
      }
      update_display();
    } else {
      // a long press. Switch the current port.
      current_port = (current_port == 1) ? 0 : 1;
      update_display();
      for (i = 1; i <= 15; i++) {
        delay(100);
        if (digitalRead(ANTSW_BTN))
          break;
      }
    }
  }
}

// Core 1 will handle the button presses and LCD updates
void loop1() {
  handle_btn_press();
  update_lcd_line1();
}

// GET /status HTTP/1.1
// GET /set?a=2 HTTP/1.1
// GET /set?x=1 HTTP/1.1
// GET /refresh?a=1 HTTP/1.1
//
// returns < 0 for errors
//           0 for the index page request
int parse_request(String request) {
  boolean refresh = false;
  boolean ant_req = false;
  boolean xfr_req = false;

  if (!request.startsWith("GET ")) {
    // not a valid http request
    return -1;
  }

  // requests are simple. no need to write a state machine
  if (request.startsWith("GET / ")) {
    // full html page request
    return 1;
  }

  if (request.startsWith("GET /status ")) {
    // simple status request
    return 0;
  } 

  // "ref" request is used by the web page. The port switching
  // part is identical to "set", but the response should show
  // the updated web page.
  refresh = (request.indexOf("GET /ref") >= 0);
  if (!refresh && (request.indexOf("GET /set") < 0)) {
    return -3;
  }
  ant_req = (request.indexOf("a=") > 7);
  xfr_req = (request.indexOf("x=") > 7);


  // handle the port change request
  if (ant_req || xfr_req) {
    // a single digit number for the port
    int port = request.substring(11, 12).toInt();
    if (ant_req) {
      if (port < 1 || port > 4)
        return -2;
      antenna_port = port;
      actuate_ant_port(antenna_port);
    } else {
      if (port < 1 || port > 2)
        return -2;
      xfer_port = port;
      actuate_xfer_port(xfer_port);
    }
    update_display();
    if (refresh) {
      return 2; // redirect to /
    }
    return 0; // text status
  }

  // unknown request
  return -3;
}

// HTML-based index page and response
// "/ref" is used.
void print_html_page(EthernetClient client) {
  // response and header
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Cache-Control: no-cache, must-revalidate");
  client.println("Connection: close");
  client.println();
  client.println("<html><body>");

  // body
  client.print("Antenna : ");
  for (int i = 1; i <= 4; i++) {
    if (antenna_port == i) {
      client.print("[");
      client.print(i);
      client.print("]&nbsp;&nbsp;&nbsp;");
    } else {
      client.print("<a href=/ref?a=");
      client.print(i);
      client.print(">");
      client.print(i);
      client.print("</a>&nbsp;&nbsp;&nbsp;");
    }
  }
  client.println("<br><br>");

  client.print("Transfer: ");
  for (int i = 1; i <= 2; i++) {
    if (xfer_port == i) {
      client.print("[");
      client.print(i);
      client.print("]&nbsp;&nbsp;&nbsp;");
    } else {
      client.print("<a href=/ref?x=");
      client.print(i);
      client.print(">");
      client.print(i);
      client.print("</a>&nbsp;&nbsp;&nbsp;");
    }
  }
  client.println("<br><br>");
  client.println("</body></html>");
}



// Core 0 handles the network
void loop() {
  // use_network is false when DHCP failed. The link can be up or down.
  // If the link was previously down, retry DHCP.
  // If the link was up, stay down. no retries.
  // Once DHCP succeeds, check the server state and call begin if necessary.
  if (!use_network) {
    if (Ethernet.linkStatus() == LinkON) {
      if (net_link_down && Ethernet.begin(mac)) {
        // the link was previously down, but it's up now.
        // DHCP succeeded.
        if (!server) {
          server.begin();
        }
        // recovery was succesful
        update_ip_address();
        use_network = true;
      }
      net_link_down = false;
    } else {
      // no recovery is attempted if the link is down
      net_link_down = true;
      // no reason to proceed any further
      return;
    }
  } else {
    // network was fine, but it is cut off now
    if (Ethernet.linkStatus() != LinkON) {
      sprintf(line1_buff, "No connection");
      line1_buff_updated = true;
      use_network = false;
      net_link_down = true;
      return;
    }
  }

  // listen for incoming clients
  EthernetClient client = server.available();
  if (client) {
    boolean currentLineIsBlank = true;
    String rcv_string = "";

    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        rcv_string += c;

        // The supported requests are all very short, but there can be
        // lengthy headers. Limit the input size.
        if (rcv_string.length() > 1024) {
          client.println("HTTP/1.1 400 Bad Request");
          client.println();
          break;
        }

        if (c == '\n' && currentLineIsBlank) {
          int res = parse_request(rcv_string);
          if (res < 0) {
            // error
            client.println("HTTP/1.1 404 Not Found");
            client.println();
            client.print("Error-code: ");
            client.println(res);
            client.println("====");
            client.println(rcv_string);
            client.println("====");
          } else if (res == 0) {
            // status in text
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: text/plane");
            client.println("Connection: close");
            client.println();
            client.print(antenna_port);
            client.print(',');
            client.println(xfer_port);
          } else if (res == 1) {
            // display the html page
            print_html_page(client);
          } else {
            // redirect to the root.
            client.println("HTTP/1.1 307 Temporary Redirect");
            client.println("Location: /");
            client.println("Connection: close");
          }
          break;
        }
        if (c == '\n') {
          currentLineIsBlank = true;
        } else if (c != '\r') {
          currentLineIsBlank = false;
        }
      }
    }
    client.flush(); // flush the buffer
    client.stop();  // close the connection
  }

  // renew the dhcp lease if necessary
  if (use_network && Ethernet.maintain() == 2) {
    // a renew happened.
    update_ip_address();
  }
}
