#include <ncurses.h>
#include <stdio.h>
#include <sys/types.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define BAUD B9600
#define TU_DEV "/dev/ttyACM0"

#define TU_CMD_SET  's'
#define TU_CMD_READ 'r'
#define TU_CMD_PWR  'p'
#define TU_CMD_TEST 't'
#define TU_CMD_MAGIC "tu0101"
#define TU_NC_HIZ 0
#define TU_NC_LOZ 1

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


int main() {
  int ch, lval, cval, nval;
  int fd, c, updated;
  char buff[32];
  int fwd, ref;

  // open the serial port
  fd = openTuner();
  if (fd < 0) {
    return 1;
  }

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
  mvprintw(6, 2, "Inductance(s,d), Capacitance(j,k), Network(n), Reset(r), Quit(q):");
  move(7,2);
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

    read_power(fd, &fwd, &ref);
    mvprintw(7, 4, "FWD %4d, REF %4d", fwd, ref);
    move(7, 2);
    refresh();
  }
  endwin();
  return 0;
}
