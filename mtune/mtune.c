/*
 * mtune
 * Copyright (C) 2022, 2026 Kihwal Lee, K9SUL
 *
 * This is a control program that talks to an antenna tuner that speaks
 * the simple ant_tuner protocol, which was developed to replace the 
 * stand-alone N7DDC tuner firmware.
 */

#include <ncurses.h>
#include <curses.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <hamlib/rig.h>
#include <curl/curl.h>

#include "mtune.h"

#define BAUD B115200
#define TU_CONF_FILE "mtune.conf"

// definitions from "ant_tuner" project.
#define TU_CMD_SET  's'
#define TU_CMD_READ 'r'
#define TU_CMD_PWR  'p'
#define TU_CMD_MAGIC "tu0101"
#define TU_NC_HIZ 0
#define TU_NC_LOZ 1

// Use the forward and reflected power to calculate SWR.
// If not set, voltages will be used.
//#define USE_POWER

struct tu_power_swr {
  float fwd; // forward power
  float ref; // reflected power
  float swr; // swr
};

struct tu_power_swr pswr;
int fd;                   // serial device file descriptor
int lval, cval, nval;     // L-network parameters
int cfreq;                // current frequency, if known.
int rigctl_connected = 0; // conection made to a rigctl host
WINDOW *status_win;
int praw = 0;

// config variables
char *tuner_device, *rigctl_host_port, *ant_switch_host_port;
int show_net_power = 0;

/*
 * Read the config file
 */
void load_config(const char *filename) {
  char line[128];
  char key[64], value[64];
  FILE *fp = fopen(filename, "r");

  if (fp == NULL) return;

  while (fgets(line, sizeof(line), fp)) {
    // Skip comments or empty lines
    if (line[0] == '#' || line[0] == '\n')
      continue;

    // Parse key=value (splits at the '=' sign)
    if (sscanf(line, "%63[^=]=%63s", key, value) == 2) {
      // Logic to store values
      if (strcmp(key, "tuner_device") == 0) {
        tuner_device = strdup(value);
        printf("%s=%s\n", key, value);
      } else if (strcmp(key, "rigctl_host_port") == 0) {
        rigctl_host_port = strdup(value);
        printf("%s=%s\n", key, value);
      } else if (strcmp(key, "ant_switch_host_port") == 0) {
        ant_switch_host_port = strdup(value);
        printf("%s=%s\n", key, value);
      } else if (strcmp(key, "show_net_power") == 0) {
        show_net_power = atoi(value);
      }
    }
  }
  fclose(fp);
}

/*
 * Convert a raw ADC reading into watts. It also calculates the
 * SWR and saves the results to the global status structure.
 *
 * The tuner has a Teensy 2.0 with 10 bit ADCs. 1023 is mapped to
 * about 1.5kW. The tuner is good for about 1kW.  The conversion
 * table and the function is in cal.c. This was built up based on
 * actual measurements.
 */
float get_swr (int fwd, int ref) {
  float vf, vr;

  // save the raw number
  praw = fwd;

  // convert the sensor readings to power watts
  pswr.fwd = get_watts(fwd);
  pswr.ref = get_watts(ref);

  // No need to calculate SWR for these cases.
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

/*
 * Open the serial device for the tuner.
 */
int openTuner() {
  int fd;
  struct termios tio;

  fd = open(tuner_device, O_RDWR | O_NOCTTY);
  if (fd < 0) {
    perror(tuner_device);
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

/*
 * Read the L-network setting from the tuner
 *  capacitor combination 0..127
 *  inductor combination 0..127
 *  network config 0 or 1 (Hi-z or Lo-z)
 */
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

/*
 * Read the power readings from the tuner.
 */
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

/*
 * Update the UI of the power and the swr.
 */
void update_power_status() {
  float f;
  if (show_net_power)
    f = pswr.fwd - pswr.ref;
  else
    f = pswr.fwd;
#ifdef TU_SHOW_RAW_PWR
  mvprintw(5, 2, "FWD %4.1fW, REF %4.1fW, SWR %1.2f:1 RAW: %d   ", f, pswr.ref, pswr.swr, praw);
#else
  mvprintw(5, 2, "FWD %4.1fW, REF %4.1fW, SWR %1.2f:1   ", f, pswr.ref, pswr.swr);
#endif
  move(11, 2);
  refresh();
}

/*
 * Take multiple power readings from the tuner and calculate the average.
 */
float check_swr(int samples) {
  int fwd, ref, tmp1, tmp2;
  int i, min_f, max_f, min_r, max_r;


  // if the sample size is greater than 10, drop the min and the max.
  min_f = 1023;
  max_f = 0;
  min_r = 1023;
  max_r = 0;
  for (i = 0, fwd = 0, ref = 0; i < samples; i++) {
    read_power(fd, &tmp1, &tmp2);
    fwd += tmp1;
    ref += tmp2;
    if (samples >= 10) {
      max_f = (tmp1 > max_f) ? tmp1 :max_f;
      min_f = (tmp1 < min_f) ? tmp1 :min_f;
      max_r = (tmp2 > max_r) ? tmp2 :max_r;
      min_r = (tmp2 < min_r) ? tmp2 :min_r;
    }
  }
  if (samples >= 10) {
    fwd = (fwd - max_f -min_f)/(samples -2);
    ref = (ref - max_r -min_r)/(samples -2);
  } else {
    fwd /= samples;
    ref /= samples;
  }
  
  return get_swr(fwd, ref); 
}

/*
 * Set the tuner to the specified L-network parameter.
 */
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

/*
 * L network setting function used for the fine tuning feature.
 *  lc - 0 for inductor, 1 for capacitor
 *  diff - difference against the current lval or cval.
 */
void fine_set_lc(int diff, int lc) {
  if (lc) { // C
    set_tuner(fd, lval, cval + diff, nval);
    mvprintw(3, 2, "L= %3d, C = %3d, %s", lval, cval + diff, (nval == TU_NC_HIZ) ? "Hi-Z" : "Lo-Z");
  } else {
    set_tuner(fd, lval + diff, cval, nval);
    mvprintw(3, 2, "L= %3d, C = %3d, %s", lval + diff, cval, (nval == TU_NC_HIZ) ? "Hi-Z" : "Lo-Z");
  }
}


#define TU_SAMPLES 10

/*
 * Fine tuning one paramter (L or C).
 *  lc - 0 for L, 1 for C
 */
int fine_tune(int lc) {
  float org_swr, min_swr, tmp;
  int min, direction, start;
 
  wprintw(status_win, "fine_tune: tuning %s\n", lc?"C":"L");
  wrefresh(status_win);

  org_swr = check_swr(TU_SAMPLES);
  // find the direction to check. start with an increase.
  direction = 1;
  start = lc ? cval : lval;
  fine_set_lc(direction, lc);
  wprintw(status_win, "fine_tune: trying %c: ", lc?'C':'L');
  wprintw(status_win, "%d ", start + direction);
  wrefresh(status_win);
  tmp = check_swr(TU_SAMPLES);
  update_power_status();

  if (tmp <= org_swr) {
    // the increase resulted a better swr.
    min = direction + start;
    min_swr = tmp;
    direction++; // keep increasing
    while (start + direction < 128) {
      fine_set_lc(direction, lc);
      wprintw(status_win, "%d ", start + direction);
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
      wprintw(status_win, "%d ", start + direction);
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
  wprintw(status_win, "\nfine_tune: L=%d, C=%d N=%s.\n", lval, cval, nval?"LoZ":"HiZ");
  wrefresh(status_win);
}

/* Automatically find a good matching parameters.  This is only meant for
 * fine tuning. The tuner should already be close to the optimal matching
 * condition.
 */
int tune(int long_tune) {
  float swr, prev_swr;
  int i, pl, pc, pn, tmp;

  prev_swr = check_swr(20);
  pl = lval;
  pc = cval;
  pn = nval;

  // Is there enough power present?
  if (pswr.fwd < 1) {
    wprintw(status_win, "Not enough power for tuning.\n");
    wrefresh(status_win);
    return 1;
  }
  wprintw(status_win, "===============\n");
  wprintw(status_win, "Start tuning...\n");
  wrefresh(status_win);

  if (long_tune > 1) {
    lval = cval = nval = 0;
    set_tuner(fd, lval, cval, nval);
  }

  for (i = 0; i < long_tune; i++) { 
    // do one iteration of fine tuning of L and C.
    fine_tune(0);
    fine_tune(1);
  
    // Check the network configuration.
    wprintw(status_win, "Checking alternate network configuration.\n");
    tmp = (nval == TU_NC_HIZ) ? TU_NC_LOZ : TU_NC_HIZ;
    swr = check_swr(10);
    set_tuner(fd, lval, cval, tmp);
    if (swr > check_swr(20)) {
      // the alternate config is better.
      nval = tmp;
      fine_tune(0);
      fine_tune(1);
    } else {
      // flip back.
      set_tuner(fd, lval, cval, nval);
    }

    swr = check_swr(20); 
    if (swr < 1.20f) {
      i++;
      break;
    }
    
    if (prev_swr < swr) {
      wprintw(status_win, "Restoring previous parameters.\n");
      lval = pl;
      cval = pc;
      nval = pn;
      set_tuner(fd, lval, cval, nval);
      break;
    } else {
      prev_swr = swr;
      pl = lval;
      pc = cval;
      pn = nval;
    }
  }

  swr = check_swr(5);
  wprintw(status_win, "Done tuning in %d iterations. Final SWR=%.2f\n", i, swr);
  wrefresh(status_win);

  return 0;
}

/*
 * Handle user frequency input.
 * Use a pop-up window. [Esc] to cancel.
 */
int get_frequency_input() {
  WINDOW *popup;
  int count, ch;
  char input[6];

  // create a pop-up window
  popup = newwin(4, 30, 5, 5);
  keypad(popup, TRUE);
  box(popup, 0, 0);
  mvwprintw(popup, 1, 2, "Enter frequency(kHz):");
  wmove(popup, 2, 3);
  wrefresh(popup);
  
  while(1) {
    // We are handling one character at a time.
    // Input echoing is also done one at a time here.
    ch = wgetch(popup);
    if (ch == 27) {
      // escape
      count = 0;
      break;
    }
    if ((ch == '\n' || ch == KEY_ENTER) && count >= 4)
      break;  // newline
    if ((ch == KEY_BACKSPACE || ch == 127 || ch == 8) && count > 0) {
      // handle backspace
      count--;
      input[count] = '\0';
      mvwaddch(popup, 2, 2 + count, ' ');
      wmove(popup, 2, 2 + count);
    } else if (isdigit(ch) && count < 5) {
      input[count] = ch;
      // echo
      mvwaddch(popup, 2, 2 + count, ch);
      count++;
      input[count] = '\0';
    }
    wrefresh(popup);
  }
  if (count > 0) {
    sscanf (input, "%d", &ch);
  } else {
    ch = 0;
  }
  delwin(popup);
  touchwin(stdscr);
  refresh();
  return ch;
}

/*
 * talk to pihpsdr using hamlib and obtain the operating frequency.
 */
int get_pihpsdr_freq() {
  static RIG *my_rig;  
  freq_t freq;
  int status;
  
  rig_set_debug(RIG_DEBUG_NONE);
  
  if (!rigctl_connected) {
    // Make a connectio if not already made
    my_rig = rig_init(2040);
    if (!my_rig) return -1;

    // Set the network address/port
    strncpy(my_rig->state.rigport.pathname, rigctl_host_port, FILPATHLEN - 1);

    // Open the connection
    status = rig_open(my_rig);
    if (status != RIG_OK) {
        rig_cleanup(my_rig);
        return -1;
    }
    rigctl_connected = 1;
  }
    // Get the frequency
    status = rig_get_freq(my_rig, RIG_VFO_CURR, &freq);
    if (status == RIG_OK) {
        return freq/1000; // Returns frequency in kHz
    }

    rig_close(my_rig);
    rig_cleanup(my_rig);
    rigctl_connected = 0;
    return -1;
}

/*
 * Update the tuner and the antenna switch
 */
void update_devices(tudata *td) {
  static char ubuff[64];
  static CURL *curl = NULL;
  CURLcode res;

  if (!curl) {
    // curl init, if not done already
    curl = curl_easy_init();
    if (!curl) {
      // init failed
      wprintw(status_win, "%s\n", curl_easy_strerror(res));
      wrefresh(status_win);
      return;
    }
  }

  // update the local records
  lval = td->lval;
  cval = td->cval;
  nval = td->nc;

  // ant port
  sprintf(ubuff, "http://%s/ref?a=%d", ant_switch_host_port, td->ant);
  curl_easy_setopt(curl, CURLOPT_URL, ubuff);
  res = curl_easy_perform(curl);

  // xfer port
  sprintf(ubuff, "http://%s/ref?x=%d", ant_switch_host_port, td->xfer);
  curl_easy_setopt(curl, CURLOPT_URL, ubuff);
  res = curl_easy_perform(curl);
}

int main() {
  int ch, freq, count;
  int c, updated, follow_freq = 0;
  char buff[32];
  tudata *td, def_td;

  // load the config
  load_config(TU_CONF_FILE);

  // open the serial port
  fd = openTuner();
  if (fd < 0) {
    return 1;
  }

  // initial conditions
  pswr.fwd = 0.0f;
  pswr.ref = 0.0f;
  pswr.swr = 1.0f;
  cfreq = 0;
 
  // default settings
  def_td.lval = 0;
  def_td.cval = 0;
  def_td.nc   = 0;
  def_td.ant  = 2;
  def_td.xfer = 1;
  
  // read the current tuner setting.
  if (read_status(fd, &lval, &cval, &nval) < 0) {
    printf("Error reading initial status.");
    return 1;
  }

  // init
  initscr();
  //cbreak();
  
  status_win = newwin(20, 75, 13, 1);
  scrollok(status_win, TRUE);
  refresh();
  wrefresh(status_win);

  halfdelay(2);
  noecho();
  attron(A_BOLD);
  mvprintw(0, 2, "Remote Tuner by K9SUL");
  attroff(A_BOLD);
  
  mvprintw(3, 2, "L = %3d, C = %3d, %s, F = %5d kHz (tracking %3s)", lval, cval, (nval == TU_NC_HIZ) ? "Hi-Z" : "Lo-Z", cfreq, follow_freq?"ON":"OFF");
  mvprintw( 8, 2, "Inductance(s,d)   Capacitance(j,k)   Network(n)     Reset(r)");
  mvprintw( 9, 2, "Enter freq(f)     Track trx(g)       Fine tune(t)   Long Tune(y)");
  mvprintw(10, 2, "Select databank(b)                                  Quit(q)");
  mvprintw(12, 1, "----------------------- LOG -----------------------");
  move(11,2);

  // read in the tune data file
  read_tune_data();
  
  while (1) {
    updated = 0;
    ch = getch();
    if (ch == 'q')
      break;

    switch(ch) {
     case 's':  // decrease L
       if (lval > 0) {
         lval--;
         updated = 1;
       }
       break;
     case 'd':  // increase L
       if (lval < 127) {
         lval++;
         updated = 1;
       }
       break;
     case 'f':  // direct frequency input
       freq = get_frequency_input();
       if (freq == 0)
         break;
       td = search_tune_data(freq);
       cfreq = freq;
       updated = 1;
       if (td == NULL)
         break;
       update_devices(td);
       break;
     case 'g':  // use rigctl to follow
       if (follow_freq) {
         follow_freq = 0;
       } else {
         follow_freq = 1;
       }
       updated = 1;
       break;
     case 'j':  // decrease C
       if (cval > 0) {
         cval--;
         updated = 1;
       }
       break;
     case 'k':  // increase C
       if (cval < 127) {
         cval++;
         updated = 1;
       }
       break;
     case 'n':  // Switch the matching network config
       if (nval == 0) {
         nval= 1;
       } else {
         nval = 0;
       }
       updated = 1;
       break;
     case 'r':  // reset
       update_devices(&def_td);
       updated = 1;
       break;
     case 't':  // short tune
      tune(1);
      updated = 1;
      break;
     case 'y':  // long tune
      tune(20);
      updated = 1;
      break;

     default:
       // ignored
       break;
    }

    // get the frequency from the rig
    if (follow_freq) {
      freq = get_pihpsdr_freq();
      if (freq > 0) { 
        if (freq != cfreq) {
          // frequency changed
          td = search_tune_data(freq);
          cfreq = freq;
          if (td != NULL) {
            update_devices(td);
          } else {
            // not found in the database.
            // set tuner to default.
            update_devices(&def_td);
          }
          updated = 1;
        }
        // don't do anything if the freq didn't change.
      } else {
        // couldn't talk to the rig. stop following.
        follow_freq = 0;
        updated = 1;
      }
    }

    // the tuner setting was updated. set it accordingly.
    if (updated) {
      if (set_tuner(fd, lval, cval, nval) < 0) {
        printw("Error writing command\n");
        getch();
        endwin();
        return 1;
      }
      mvprintw(3, 2, "L = %3d, C = %3d, %s, F = %5d kHz (tracking %3s)", lval, cval, (nval == TU_NC_HIZ) ? "Hi-Z" : "Lo-Z", cfreq, follow_freq?"ON":"OFF");
    }

    //update_power_swr();
    check_swr(10);
    update_power_status();
  }
  delwin(status_win);
  endwin();
  return 0;
}
