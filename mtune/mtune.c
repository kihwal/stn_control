#include <ncurses.h>
#include <stdio.h>
#include <sys/types.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "cal.h"

#define BAUD B115200
#define TU_DEV "/dev/ttyACM0"

#define TU_CMD_SET  's'
#define TU_CMD_READ 'r'
#define TU_CMD_PWR  'p'
#define TU_CMD_TEST 't'
#define TU_CMD_MAGIC "tu0101"
#define TU_NC_HIZ 0
#define TU_NC_LOZ 1

struct tu_power_swr {
  float fwd; // forward power
  float ref; // reflected power
  float swr; // swr
};

struct tu_power_swr pswr;
struct timespec delay;
int fd;                   // serial device file descriptor
int lval, cval, nval;     // L-network parameters

// Teensy 2.0 has 10 bit ADCs.
// The tandem directional coupler uses 20:1 transformers
// The conversion table is in cal.c.
//#define USE_POWER
float get_swr (int fwd, int ref) {
  float vf, vr;

  // convert the sensor readings to power watts
  pswr.fwd = get_watts(fwd);
  pswr.ref = get_watts(ref);

  // Noneed to calculate SWR for these cases.
  if (fwd == 0) {
    pswr.swr = 1.0f;
    return pswr.swr;
  } else if (fwd < ref) {
    pswr.swr = 9.99f;
    return pswr.swr;
  }

  // calculate the SWR
#ifdef USE_POWER
  pswr.swr = (1.0f + sqrtf(pswr.ref/pswr.fwd))/(1.0f - sqrtf(pswr.ref/pswr.fwd));
#else
  // calculate voltages
  vf = sqrtf(pswr.fwd*50.0f);
  vr = sqrtf(pswr.ref*50.0f);
  pswr.swr = (vf+vr)/(vf-vr);
#endif
  if (pswr.swr > 9.99f) {
    pswr.swr = 9.99f;
  }
  return pswr.swr; 
}

int openTuner() {
  int fd;
  struct termios tio;

  fd = open(TU_DEV, O_RDWR | O_NOCTTY);
  if (fd < 0) {
    perror(TU_DEV);
    return fd;
  }

  bzero(&tio, sizeof(tio));
  tio.c_cflag = BAUD | CS8 | CLOCAL | CLOCAL | CREAD;
  tio.c_iflag =  IGNPAR | ICRNL;
  tio.c_oflag = 0;
  tio.c_lflag = ICANON;

  tcflush(fd, TCIFLUSH);
  tcsetattr(fd,TCSANOW,&tio);

  return fd;
}

int read_status(int fd, int *ind, int *cap, int *nc) {
  int c, i;
  char buff[32];

  // read the current setting.
  sprintf(buff, "%s%c", TU_CMD_MAGIC, TU_CMD_READ);
  c = write(fd, buff, strlen(buff));
  if (c != strlen(buff)) {
    printf("Error writing to the tuner");
    return -1;
  }

  bzero(buff, 32);
  c = 0, i = 0;
  while (c < 11) {
    i = read(fd, buff + i, 31 - i);
    c += i;
  }

  while (read(fd, buff + c, 1) == 1) {
    if (buff[c] == '\n')
      break;
  }
  buff[11] = 0;
  if (buff[0] != 'o' || buff[1] != 'k') {
    return -1;
  }

  // parse the result from back.
  *nc = (buff[10] == '0') ? 0 : 1;
  buff[10] = 0;
  sscanf(buff + 6, "%d", cap);
  buff[6] = 0;
  sscanf(buff + 2, "%d", ind);
  return 0;
}

int read_power(int fd, int *fwd, int *ref) {
  int c, i;
  char buff[32];

  // read the current setting.
  sprintf(buff, "%s%c", TU_CMD_MAGIC, TU_CMD_PWR);
  c = write(fd, buff, strlen(buff));
  if (c != strlen(buff)) {
    printf("Error writing to the tuner");
    return -1;
  }

  bzero(buff, 32);
  c = 0, i = 0;
  while (c < 11) {
    i = read(fd, buff + i, 31 - i);
    c += i;
  }

  while (read(fd, buff + c, 1) == 1) {
    if (buff[c] == '\n')
      break;
  }
  buff[11] = 0;
  if (buff[0] != 'o' || buff[1] != 'k') {
    return -1;
  }

  // parse the result from back.
  buff[10] = 0;
  sscanf(buff + 6, "%d", fwd);
  buff[6] = 0;
  sscanf(buff + 2, "%d", ref);
  return 0;
}

void update_power_status() {
  mvprintw(5, 2, "FWD %4.1fW, REF %4.1fW, SWR %1.2f:1   ", (pswr.fwd - pswr.ref), pswr.ref, pswr.swr);
  move(8, 2);
  refresh();
}

// return the current swr without updating the global status.
float check_swr(int samples) {
  int fwd, ref, tmp1, tmp2;
  int i;

  for (i = 0, fwd = 0, ref = 0; i < samples; i++) {
    read_power(fd, &tmp1, &tmp2);
    fwd += tmp1;
    ref += tmp2;
  }
  fwd /= samples;
  ref /= samples;
  
  return get_swr(fwd, ref); 
}

int set_tuner(int fd, int ind, int cap, int nc) {
  char buff[32];
  int c, i;

  sprintf(buff, "%s%c%03d%03d%d", TU_CMD_MAGIC, TU_CMD_SET, ind, cap, nc);
  c = write(fd, buff, strlen(buff));
  if (c != strlen(buff)) {
    printw("Error writing to the tuner");
    return -1;
  }

  bzero(buff, 32);
  c = 0, i= 0;
  while (c < 11) {
    i = read(fd, buff + i, 31 - i);
    c += i;
  }

  while (read(fd, buff + c, 1) == 1) {
    if (buff[c] == '\n')
      break;
  }

  if (buff[0] != 'o' || buff[1] != 'k') {
    printw("bad response: %s", buff);
    return -1;
  }

  return 0;
}

void fine_set_lc(int diff, int lc) {
  if (lc) { // C
    set_tuner(fd, lval, cval + diff, nval);
    mvprintw(3, 2, "L= %3d, C = %3d, %s", lval, cval + diff, (nval == TU_NC_HIZ) ? "Hi-Z" : "Lo-Z");
  } else {
    set_tuner(fd, lval + diff, cval, nval);
    mvprintw(3, 2, "L= %3d, C = %3d, %s", lval + diff, cval, (nval == TU_NC_HIZ) ? "Hi-Z" : "Lo-Z");
  }
}

// assume the LC is already farely close.
// lc=0 for L, lc=1 for C

#define TU_SAMPLES 30

int fine_tune(int lc) {
  float org_swr, min_swr, tmp;
  int min, direction, start;
  
  org_swr = check_swr(TU_SAMPLES);
  // find the direction to check. start with an increase.
  direction = 1;
  fine_set_lc(direction, lc);
  tmp = check_swr(TU_SAMPLES);
  update_power_status();
  start = lc ? cval : lval;
  
  if (tmp <= org_swr) {
    // the increase resulted a better swr.
    min = direction + lc ? cval : lval;
    min_swr = tmp;
    direction++; // keep increasing
    while (start + direction < 128) {
      fine_set_lc(direction, lc);
      tmp = check_swr(TU_SAMPLES);
      update_power_status();
      if (tmp > min_swr)
        break;
      min_swr = tmp;
      min = start + direction;
      direction++;
    }
  } else if (start == 0) {
    // an increase resulted in a worse swr and it was on the minimum
    // L or C position. there is nothing to do. we are at the minimum.
    min = start;
    min_swr = org_swr;
  } else {
    // the increasing L/C made swr worse. we will go the other direction.
    min = lc ? cval : lval; // go back to the org condition. 
    min_swr = org_swr;
    direction = -1;
    while (start + direction >= 0) {
      fine_set_lc(direction, lc);
      tmp = check_swr(TU_SAMPLES);
      update_power_status();
      if (tmp > min_swr)
        break;
      min_swr = tmp;
      min = start + direction;
      direction--;
    }
  }
  if (lc)
    cval = min;
  else
    lval = min;
}

int tune() {
  int swr, fwd, ref;
  int i, tmp, min_swr, min_c, res;
  
  check_swr(10);
  if (pswr.fwd < 5)
    return 1;
  fine_tune(0);
  fine_tune(1);

  return 0;
}

int main() {
  int ch;
  int c, updated;
  char buff[32];
  WINDOW *status_win;

  // open the serial port
  fd = openTuner();
  if (fd < 0) {
    return 1;
  }

  pswr.fwd = 0.0f;
  pswr.ref = 0.0f;
  pswr.swr = 100;
  
  // read the current setting.
  if (read_status(fd, &lval, &cval, &nval) < 0) {
    printf("Error reading initial status.");
    return 1;
  }

  // init
  initscr();
  //cbreak();
  halfdelay(2);
  noecho();
  attron(A_BOLD);
  mvprintw(0, 2, "Remote Tuner by K9SUL");
  attroff(A_BOLD);
  
  mvprintw(3, 2, "L= %3d, C = %3d, %s", lval, cval, (nval == TU_NC_HIZ) ? "Hi-Z" : "Lo-Z");
  mvprintw(8, 2, "Inductance(s,d), Capacitance(j,k), Network(n), Tune(t), Reset(r), Quit(q):");
  move(8,2);
  while (1) {
    updated = 0;
    ch = getch();
    if (ch == 'q')
      break;

    switch(ch) {
     case 's':
       if (lval > 0) {
         lval--;
         updated = 1;
       }
       break;
     case 'd':
       if (lval < 127) {
         lval++;
         updated = 1;
       }
       break;
     case 'j':
       if (cval > 0) {
         cval--;
         updated = 1;
       }
       break;
     case 'k':
       if (cval < 127) {
         cval++;
         updated = 1;
       }
       break;
     case 'n':
       if (nval == 0) {
         nval= 1;
       } else {
         nval = 0;
       }
       updated = 1;
       break;
     case 'r':
       lval = 0; cval = 0; nval = 0;
       updated = 1;
       break;
     case 't':
      tune();
      updated = 1;
      break;
     default:
       // ignored
       break;
    }

    if (updated) {
      if (set_tuner(fd, lval, cval, nval) < 0) {
        printw("Error writing command\n");
        getch();
        endwin();
        return 1;
      }
      mvprintw(3, 2, "L= %3d, C = %3d, %s", lval, cval, (nval == TU_NC_HIZ) ? "Hi-Z" : "Lo-Z");
    }

    //update_power_swr();
    check_swr(10);
    update_power_status();
  }
  endwin();
  return 0;
}
