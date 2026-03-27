#include <stdio.h>
#include <stdint.h>

#ifdef TU_CAL_USE_INTERNAL
#define TU_CAL_TBL_SIZE 100

uint16_t tu_cal_adc[TU_CAL_TBL_SIZE] = {
  0,  10,  20,  28,  34,  38,  44,  49,  52,  58,
 60,  79,  92, 106, 117, 138, 157, 174, 188, 202,
211, 224, 227, 234, 237, 245, 253, 254, 257, 264,
270, 278, 282, 291, 298, 303, 309, 313, 318, 322,
332, 354, 369, 384, 399, 412, 427, 440, 454, 468,
480, 493, 500, 515, 527, 533, 544, 550, 561, 566,
577, 582, 590, 596, 601, 609, 615, 619, 628, 632,
638, 642, 657, 666, 670, 674, 678, 683, 687, 695,
700, 704, 712, 716, 719, 726, 731, 735, 740, 744,
747, 754, 758, 760, 766, 768, 775, 780, 799, 806};

uint16_t tu_cal_watts[TU_CAL_TBL_SIZE] = {
   0,   1,   2,   3,   4,   5,   6,   7,   8,   9,
  10,  15,  20,  25,  30,  40,  50,  60,  70,  80,
  90,  98, 101, 107, 111, 117, 124, 126, 130, 136,
 143, 150, 155, 165, 172, 180, 185, 190, 194, 200,
 211, 240, 262, 283, 302, 323, 346, 367, 386, 410,
 433, 456, 469, 488, 511, 522, 544, 557, 580, 591,
 614, 625, 647, 657, 668, 690, 698, 708, 733, 739,
 751, 760, 798, 820, 831, 841, 852, 863, 874, 895,
 905, 917, 940, 950, 960, 979, 990,1000,1018,1027,
1037,1056,1065,1071,1087,1095,1112,1122,1184,1207};

#else
#define TU_CAL_TBL_SIZE 90

uint16_t tu_cal_adc[TU_CAL_TBL_SIZE] = {
   0,  10,  15,  19,  23,  26,  29,  32,  34,  36,
  39,  48,  56,  64,  73,  89, 100, 105, 111, 117,
 124, 129, 135, 138, 144, 148, 151, 155, 160, 165,
 170, 175, 180, 185, 193, 198, 202, 206, 211, 213,
 225, 232, 245, 254, 263, 270, 279, 287, 295, 301,
 306, 310, 318, 320, 329, 336, 338, 346, 352, 355,
 362, 364, 367, 374, 376, 382, 385, 387, 393, 395,
 398, 402, 405, 408, 410, 415, 417, 419, 422, 424,
 427, 431, 434, 435, 437, 440, 443, 447, 450, 453
};

uint16_t tu_cal_watts[TU_CAL_TBL_SIZE] = {
   0,   1,   2,   3,   4,   5,   6,   7,   8,   9,
  10,  15,  20,  25,  30,  45,  55,  62,  70,  77,
  86,  93, 101, 107, 115, 121, 127, 133, 141, 151,
 160, 171, 181, 190, 205, 213, 223, 232, 243, 250,
 276, 292, 323, 351, 375, 391, 418, 442, 467, 488,
 502, 515, 540, 553, 579, 604, 615, 645, 668, 679,
 705, 716, 728, 753, 763, 786, 800, 811, 832, 843,
 855, 876, 887, 898, 909, 930, 940, 951, 962, 973,
 984,1003,1017,1025,1034,1045,1059,1072,1086,1099
};
#endif


float get_watts(uint16_t raw) {
  int exact = 0, idx, min, max;
  float power, diff, rate;

  // beyond the table coverage
  if (raw > tu_cal_adc[TU_CAL_TBL_SIZE - 1]) {
    diff = (float)(raw - tu_cal_adc[TU_CAL_TBL_SIZE - 1]);
#ifdef TU_CAL_USE_INTERNAL    
    return (float)tu_cal_adc[TU_CAL_TBL_SIZE - 1] + diff*3.3f;
#else
    return (float)tu_cal_adc[TU_CAL_TBL_SIZE - 1] + diff*4.5f;
#endif
  }

  // search for the equal or the closest lower value.
  min = 0;
  max = TU_CAL_TBL_SIZE;
  while(1) {
    idx = (min + max)/2;
    if (tu_cal_adc[idx] == raw) {
      exact = 1;
      break;
    } else if (tu_cal_adc[max] == raw) {
      exact = 1;
      idx = max;
      break;
    } else if (tu_cal_adc[min] == raw) {
      exact = 1;
      idx = min;
      break;
    } else if (tu_cal_adc[idx] < raw) {
      if (tu_cal_adc[idx+1] > raw) {
        // found the target
        break;
      } else {
        min = idx;
      }
    } else if (tu_cal_adc[idx] > raw) {
      max = idx;
    }
  }

  if (exact) {
    return (float)tu_cal_watts[idx];
  }

  // interpolate
  rate = (tu_cal_watts[idx+1] - tu_cal_watts[idx])/(float)(tu_cal_adc[idx+1] - tu_cal_adc[idx]);
  power = (float)tu_cal_watts[idx] + (raw - tu_cal_adc[idx])*rate;
  return power;
}

void print_all() {
  int i;

  for (i = 0; i < 1024; i++) {
    printf("%d %4.1f\n", i, get_watts(i));
  }
}
