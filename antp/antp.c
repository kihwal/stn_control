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

// All U12 commands are 8 bytes.
#define U12_COMMAND_LENGTH 8

// ant == 0 means ant1, ant == 1 means ant2
int amp = 0, trx = 0, dummy = 0, ant = 0;

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
        if(errno == LIBUSB_ERROR_TIMEOUT) {
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
  amp = (result >> 4) & 0x01;
  trx = (result >> 5) & 0x01;
  ant = (result >> 6) & 0x01;
  dummy = (result >> 7) & 0x01;
}

uint8_t create_data_for_write() {
  // if dummy is set ant won't matter.
  return (uint8_t)((amp & 0x01) |
               ((trx << 1) & 0x02) |
               ((ant << 2) & 0x04) |
               ((dummy << 3) & 0x08));
}

void update_status_display() {
  mvprintw(3, 2, "Power   : Amp %5s, TRX %5s", amp ? "[ON]" : "[OFF]", trx ? "[ON]" : "[OFF]");
  mvprintw(5, 2, "Antenna : ");
  if (dummy) {
    printw(" ant1   ant2  [DUMMY]");
  } else if (ant) {
    printw(" ant1  [ANT2]  dummy ");
  } else {
    printw("[ANT1]  ant2   dummy ");
  }
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
  mvprintw(7, 2, "Amp(a), TRX(t), Antenna(1/2/d), quit(q)");
  move(8,2);

  while(1) {
    ch = getch();
    if (ch == 'q') {
      break;
    } else if (ch == 'a') {
      amp = amp ? 0 : 1;
    } else if (ch == 't') {
      trx = trx ? 0 : 1;
    } else if (ch == 'd') {
      dummy = 1;
    } else if (ch == '2') {
      dummy = 0;
      ant = 1;
    } else if (ch == '1') {
      dummy = 0;
      ant = 0;
    } else {
      continue;
    }
    val = create_data_for_write();
    //mvprintw(8,2, "amp %d, trx %d, ant %d, dummy %d, val 0x%x", amp, trx, ant, dummy, val);
    buildDIOwrite(sendBuffer, val, 1);
    writeRead(devHandle, sendBuffer, recBuffer);
    parse_result_and_update(recBuffer[3]);
    update_status_display();
    move(8,2);
  }

  LJUSB_CloseDevice(devHandle);
  endwin();
  return 0;
}

