
// freq ant_port xfer_port l-net-z L C

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tdata.h"

#define MAX_LINE_LENGTH 32
#define MAX_LINES 256

tudata tune_data[MAX_LINES];
int number_of_lines;

int read_tune_data() {
    FILE *file;
    char line[MAX_LINE_LENGTH];
    
    file = fopen("tune_data", "r");

    if (file == NULL) {
        perror("Error opening file");
        return 1;
    }
  number_of_lines = 0;
    while (fgets(line, sizeof(line), file)) {
        // 1. Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }

        // 2. Parse 6 integers from the line
        int count = sscanf(line, "%d %d %d %d %d %d", 
                           &tune_data[number_of_lines].freq, 
                           &tune_data[number_of_lines].ant, 
                           &tune_data[number_of_lines].xfer, 
                           &tune_data[number_of_lines].nc, 
                           &tune_data[number_of_lines].lval, 
                           &tune_data[number_of_lines].cval);

        // 3. Only increment if we successfully read all 6 integers
        if (count == 6) {
            number_of_lines++;
        }

        // Safety check to prevent buffer overflow
        if (number_of_lines >= MAX_LINES) {
            printf("Reached maximum capacity.\n");
            break;
        }
    }

    fclose(file);
    return 0;
}

/*
 * Returns the one with the closest frequency.
 * data is stored sorted.
 */
tudata* search_tune_data(int freq_khz) {
  int i, diff, cdiff, closest;

  cdiff = 999;
  closest = 0;
  for (i = 0; i < number_of_lines; i++) {
    diff = tune_data[i].freq - freq_khz;
    if (diff > 1000 || diff < -1000) {
      // a different band
      continue;
    } else if (diff >= 0) {
      // no need to search further.
      if (diff < 0) diff *= -1;
      if (cdiff > diff) {
        return &(tune_data[i]);
      } else {
        return &(tune_data[closest]);
      }
    } else {
      if (diff < 0) diff *= -1;
      if (cdiff > diff) {
        cdiff = diff;
        closest = i;
      }
      continue;
    }
  }
  // not found
  return NULL;
}

/*
int main() {
  int f;
  tudata *r;
  
  read_tune_data();
  
  scanf("%d", &f);
  printf("Searching %d\n", f);
  r = search_tune_data(f);
  if (r != NULL)
    printf("\n%d\n", r->freq); 
}
*/
