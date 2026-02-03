#include <ncurses.h>
#include <stdio.h>
#include <sys/types.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "labjackusb.h"
#include <libusb-1.0/libusb.h>

#define PORT0_LABEL "Port 0"
#define PORT1_LABEL "Anan-10"
#define PORT2_LABEL "Port 2"
#define PORT3_LABEL "Port 3"


// All U12 commands are 8 bytes.
#define U12_COMMAND_LENGTH 8

int port0 = 0, port1 = 0, port2 = 0, port3 = 0;

/* ------------- LabJack Related Helper Functions Definitions ------------- */

int writeRead(HANDLE devHandle, BYTE * sendBuffer, BYTE * recBuffer ) {
    int r = 0;

    // Write the command to the device.
    // LJUSB_Write( handle, sendBuffer, length of sendBuffer )
    r = LJUSB_Write( devHandle, sendBuffer, U12_COMMAND_LENGTH );

    if( r != U12_COMMAND_LENGTH ) {
        printf("An error occurred when trying to write the buffer. The error was: %d\n", errno);
        // *Always* close the device when you error out.
        LJUSB_CloseDevice(devHandle);
        exit(-1);
    }

    // Read the result from the device.
    // LJUSB_Read( handle, recBuffer, number of bytes to read)
    r = LJUSB_Read( devHandle, recBuffer, U12_COMMAND_LENGTH );

    if( r != U12_COMMAND_LENGTH ) {
        if(errno == LIBUSB_ERROR_TIMEOUT || errno == ETIMEDOUT) {
            return -1;
        }

        printf("An error occurred when trying to read from the U12. The error was: %d\n", errno);
        LJUSB_CloseDevice(devHandle);
        exit(-1);
    }

    return 0;
}


// Write or read val io0 to io3
// write = 0 for read, 1 for write.
void buildDIOwrite(BYTE * sendBuffer, uint8_t val, int write) {
  sendBuffer[0] = 0; // D15 - D8 set to output
  sendBuffer[1] = 0; // D7 - D0 set to output
  sendBuffer[2] = 0; // D15 - D8 state
  sendBuffer[3] = 0; // D7 - D0 state
  sendBuffer[4] = val; // 0x01 will set IO0 to IO3 for output and IO0 val to 1.
  sendBuffer[5] = 0x57; // 0b01x10111 for DIO
  sendBuffer[6] =  write; // 1 for update, 0 for read only
  sendBuffer[7] = 0; // na
}

void parse_result_and_update(uint8_t result) {
  port0 = (result >> 4) & 0x01;
  port1 = (result >> 5) & 0x01;
  port2 = (result >> 6) & 0x01;
  port3 = (result >> 7) & 0x01;
}

uint8_t create_data_for_write() {
  return (uint8_t)((port0 & 0x01) |
               ((port1 << 1) & 0x02) |
               ((port2 << 2) & 0x04) |
               ((port3 << 3) & 0x08));
}

void update_status_display() {
  mvprintw(3, 2, "0  %10s: %s", PORT0_LABEL, port0 ? "[ON] " : "[OFF]");
  mvprintw(5, 2, "1  %10s: %s", PORT1_LABEL, port1 ? "[ON] " : "[OFF]");
  mvprintw(7, 2, "2  %10s: %s", PORT2_LABEL, port2 ? "[ON] " : "[OFF]");
  mvprintw(9, 2, "3  %10s: %s", PORT3_LABEL, port3 ? "[ON] " : "[OFF]");
}

int main() {
  int ch;
  uint8_t val;

  // Setup the variables we will need.
  int r = 0; // For checking return values
  HANDLE devHandle = 0;
  BYTE sendBuffer[U12_COMMAND_LENGTH], recBuffer[U12_COMMAND_LENGTH];

  // Open the U12
  devHandle = LJUSB_OpenDevice(1, 0, U12_PRODUCT_ID);
  if (devHandle == NULL) {
    printf("Couldn't open U12. Please connect one and try again.\n");
    exit(-1);
  }

  // read the current state
  buildDIOwrite(sendBuffer, 0, 0);
  r = writeRead(devHandle, sendBuffer, recBuffer );
  if (r == -1){
    r = writeRead(devHandle, sendBuffer, recBuffer );

    if (r != 0){
      // If you still have errors after the first try, then you have
      // bigger problems.
      printf("Command timed out twice. Exiting...");
      LJUSB_CloseDevice(devHandle);
      exit(-1);
    }
  }

  parse_result_and_update(recBuffer[3]);

  // init
  initscr();
  cbreak();
  noecho();
  attron(A_BOLD);
  mvprintw(0, 2, "Shack Control by K9SUL");
  attroff(A_BOLD);

  update_status_display();
  mvprintw(11, 2, "0/1/2/3 to switch or quit(q)");
  move(12,2);

  while(1) {
    ch = getch();
    if (ch == 'q') {
      break;
    } else if (ch == '0') {
      port0 = port0 ? 0 : 1;
    } else if (ch == '1') {
      port1 = port1 ? 0 : 1;
    } else if (ch == '2') {
      port2 = port2 ? 0 : 1;
    } else if (ch == '3') {
      port3 = port3 ? 0 : 1;
    } else {
      continue;
    }
    val = create_data_for_write();
    buildDIOwrite(sendBuffer, val, 1);
    writeRead(devHandle, sendBuffer, recBuffer);
    parse_result_and_update(recBuffer[3]);
    update_status_display();
    move(12,2);
  }

  LJUSB_CloseDevice(devHandle);
  endwin();
  return 0;
}

