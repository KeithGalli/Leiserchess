#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#define BOARD_WIDTH 10
#define ARR_WIDTH 16
#define FIL_ORIGIN ((ARR_WIDTH - BOARD_WIDTH) / 2)
#define FIL_SHIFT 4
#define FIL_MASK 15
#define RNK_ORIGIN ((ARR_WIDTH - BOARD_WIDTH) / 2)
#define RNK_SHIFT 0
#define RNK_MASK 15

#define PIECE_SIZE 5  // Number of bits in (ptype, color, orientation)

// MOVE_MASK is 20 bits
#define MOVE_MASK 0xfffff

#define PTYPE_MV_SHIFT 18
#define PTYPE_MV_MASK 3
#define FROM_SHIFT 8
#define FROM_MASK 0xFF
#define TO_SHIFT 0
#define TO_MASK 0xFF
#define ROT_SHIFT 16
#define ROT_MASK 3

#define PTYPE_SHIFT 2
#define PTYPE_MASK 3

typedef int32_t ev_score_t;  // Static evaluator uses "hi res" values
int PCENTRAL;

typedef enum {
  EMPTY,
  PAWN,
  KING,
  INVALID
} ptype_t;

typedef int fil_t;
typedef int rnk_t;
typedef int square_t;

// Finds file of square
fil_t fil_of(square_t sq) {
  fil_t f = ((sq >> FIL_SHIFT) & FIL_MASK) - FIL_ORIGIN;
  return f;
}

// Finds rank of square
rnk_t rnk_of(square_t sq) {
  rnk_t r = ((sq >> RNK_SHIFT) & RNK_MASK) - RNK_ORIGIN;
  return r;
}

// Harmonic-ish distance: 1/(|dx|+1) + 1/(|dy|+1)
float h_dist(square_t a, square_t b) {
  //  printf("a = %d, FIL(a) = %d, RNK(a) = %d\n", a, FIL(a), RNK(a));
  //  printf("b = %d, FIL(b) = %d, RNK(b) = %d\n", b, FIL(b), RNK(b));
  int delta_fil = abs(fil_of(a) - fil_of(b));
  int delta_rnk = abs(rnk_of(a) - rnk_of(b));
  float x = (1.0 / (delta_fil + 1)) + (1.0 / (delta_rnk + 1));
  //  printf("max_dist = %d\n\n", x);
  return x;
}

// For no square, use 0, which is guaranteed to be off board
square_t square_of(fil_t f, rnk_t r) {
  square_t s = ARR_WIDTH * (FIL_ORIGIN + f) + RNK_ORIGIN + r;
  return s;
}

// Using our valid square_of method, generate a table that precomputes these values
void generate_square_table() {
  FILE *fp = fopen("square_of_table.c", "wb");
  int x;
  int y;
  fprintf(fp, "static const unsigned char square_of_table[13][15] = {\n");
  for (x = 0; x < 13; x++) {
    fprintf(fp, "{");
    for (y = 0; y < 15; y++) {
      if (square_of(x-3, y-3) < 0) {
        fprintf(fp, "-'\\x%1x', ", -square_of(x-3,y-3));
      } else {
        fprintf(fp, "'\\x%1x', ", square_of(x-3,y-3));
      }
    }
    fprintf(fp, "},\n");
  }
  fprintf(fp, "};");
};

// Using our valid h_dist method, generate a file that is formatted as a precomputed table for these values
void generate_h_dist_table() {
  FILE *fp = fopen("h_dist_table.c", "wb");
  int x;
  int y;
  fprintf(fp, "static const float h_dist_table[216][216] = {\n");
  for (x = 0; x < 216; x++) {
    fprintf(fp, "{");
    for (y = 0; y < 216; y++) {
      fprintf(fp, "%f, ", h_dist(x, y));
    }
    fprintf(fp, "},\n");
  }
  fprintf(fp, "};");
  fclose(fp);
};

// Using our valid fil_of method, generate a file that is formatted as a precomputed table for these values
void generate_fil_table(){
  FILE *fp = fopen("fil_table.c", "wb");
  int x;
  fprintf(fp, "static const unsigned int fil_table[216] = {\n");
  for (x = 0; x < 216; x++) {
    if (fil_of((square_t)x) < 0) {
      fprintf(fp, "-%d, ", -(int)fil_of((square_t)x));
    } else {
      fprintf(fp, "%d, ", (int)fil_of((square_t)x));
    }
    if ((x + 1) % 16 == 0) {
      fprintf(fp, "\n");
    }
  }
  fprintf(fp, "};");
  fclose(fp);
};

// Using our valid rnk_of method, generate a file that is formatted as a precomputed table for these values
void generate_rnk_table() {
  FILE *fp = fopen("rnk_table.c", "wb");
  int x;
  fprintf(fp, "static const unsigned int rnk_table[216] = {\n");
  for (x = 0; x < 216; x++) {
    fprintf(fp, "%d, ", (int)rnk_of((square_t)x));
    if ((x + 1) % 16 == 0) {
      fprintf(fp, "\n");
    }
  }
  fprintf(fp, "};");
  fclose(fp);
};


// PCENTRAL heuristic: Bonus for Pawn near center of board
double pcentral(fil_t f, rnk_t r) {
  double df = BOARD_WIDTH/2 - f - 1;
  if (df < 0)  df = f - BOARD_WIDTH/2;
  double dr = BOARD_WIDTH/2 - r - 1;
  if (dr < 0) dr = r - BOARD_WIDTH/2;
  double bonus = 1 - sqrt(df * df + dr * dr) / (BOARD_WIDTH / sqrt(2));
//  printf("%lf\n", bonus);

//  printf("Pcentral = %lf", PCENTRAL);
  return bonus;
}

// Using our valid pcentral method, generate a file that is formatted as a precomputed table for these values
void generate_pcentral(){
  FILE *fp = fopen("pcentral_table.c", "wb");
  int x;
  int y;
  fprintf(fp, "static const int pcentral_table[13][15] = {\n");
  for (x = 0; x < 16; x++) {
    fprintf(fp, "{");
    for (y = 0; y < 16; y++) {
//      if (square_of(x-3, y-3) < 0) {
//        fprintf(fp, "PRIu32, ", (uint32_t)-pcentral(x-3,y-3));
//      } else {
        printf("%lf", pcentral((fil_t)x,(rnk_t)y));
        fprintf(fp, "%lf, ", pcentral((fil_t)x,(rnk_t)y));
//      }
    }
    fprintf(fp, "},\n");
  }
  fprintf(fp, "};");
}


int main() {
//   generate_h_dist_table();
//  generate_fil_table();
//  generate_rnk_table();
//  generate_square_table();
  generate_pcentral();

  return 0;
}
