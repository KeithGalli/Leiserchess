// Copyright (c) 2015 MIT License by 6.172 Staff

#include "./eval.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "./tbassert.h"
#include "./precomp_tables.h"

// -----------------------------------------------------------------------------
// Evaluation
// -----------------------------------------------------------------------------

typedef int32_t ev_score_t;  // Static evaluator uses "hi res" values

uint8_t RANDOMIZE;

//uint8_t PCENTRAL;
int HATTACK;
int PBETWEEN;
int PCENTRAL;
int KFACE;
int KAGGRESSIVE;
int MOBILITY;
int PAWNPIN;
typedef struct heuristics_t {
  int8_t pawnpin;
  int8_t h_attackable;
  int8_t mobility;
} heuristics_t;

heuristics_t * mark_laser_path_heuristics(position_t *p, color_t c, heuristics_t * heuristics);

// Heuristics for static evaluation - described in the google doc
// mentioned in the handout.
bool inRange(const int min, const int max, const int val);

// PCENTRAL heuristic: Bonus for Pawn near center of board
ev_score_t pcentral(const fil_t f, const rnk_t r) {
  int df = BOARD_WIDTH/2 - f -1;
  if (df < 0)  df = f - BOARD_WIDTH/2;
  int dr  = BOARD_WIDTH/2 - r -1;
  if (dr < 0) dr = r - BOARD_WIDTH/2;
  const double bonus = 1 - sqrt(df * df + dr * dr)*BONUS_MULTIPLIER;
  return PCENTRAL * bonus;
}


// returns true if c lies on or between a and b, which are not ordered
bool between(const int c, const int a, const int b) {
  return ((c >= a) && (c <= b)) || ((c <= a) && (c >= b));
}

// checks if between max and min
inline bool inRange(const int min, const int max, const int val) {
  return (val >= min) && (val <= max);
}
// PBETWEEN heuristic: Bonus for Pawn at (f, r) in rectangle defined by Kings at the corners
ev_score_t pbetween(const position_t *p, const fil_t f, const rnk_t r) {
  const bool is_between =
      between(f, fil_of(p->kloc[WHITE]), fil_of(p->kloc[BLACK])) &&
      between(r, rnk_of(p->kloc[WHITE]), rnk_of(p->kloc[BLACK]));
  return is_between ? PBETWEEN : 0;
}


// KFACE heuristic: bonus (or penalty) for King facing toward the other King
ev_score_t kface(const position_t *p, const fil_t f, const rnk_t r) {
  const square_t sq = square_of(f, r);
  const piece_t x = p->board[sq];
  const color_t c = color_of(x);
  const square_t opp_sq = p->kloc[opp_color(c)];
  const int delta_fil = fil_of(opp_sq) - f;
  const int delta_rnk = rnk_of(opp_sq) - r;
  int bonus;

  switch (ori_of(x)) {
    case NN:
      bonus = delta_rnk;
      break;

    case EE:
      bonus = delta_fil;
      break;

    case SS:
      bonus = -delta_rnk;
      break;

    case WW:
      bonus = -delta_fil;
      break;

    default:
      bonus = 0;
      tbassert(false, "Illegal King orientation.\n");
  }

  return (bonus * KFACE) / (abs(delta_rnk) + abs(delta_fil));
}

// KAGGRESSIVE heuristic: bonus for King with more space to back
ev_score_t kaggressive(const position_t *p, const fil_t f, const rnk_t r) {
  const square_t sq = square_of(f, r);
  const piece_t x = p->board[sq];
  const color_t c = color_of(x);
  tbassert(ptype_of(x) == KING, "ptype_of(x) = %d\n", ptype_of(x));

  const square_t opp_sq = p->kloc[opp_color(c)];
  const fil_t of = fil_of(opp_sq);
  const rnk_t _or = (rnk_t) rnk_of(opp_sq);

  int bonus = 0;
  if(of >= f) {
    bonus = f +1;
  } else {
    bonus = BOARD_WIDTH - f;
  }
  if(_or >= r) {
    bonus *= (r + 1);
  } else {
    bonus *= (BOARD_WIDTH - r);
  }

  return (KAGGRESSIVE * bonus) / (BOARD_WIDTH * BOARD_WIDTH);
}

// Marks the path of the laser until it hits a piece or goes off the board.
//
// p : current board state
// laser_map : end result will be stored here. Every square on the
//             path of the laser is marked with mark_mask
// c : color of king shooting laser
// mark_mask: what each square is marked with
void mark_laser_path(position_t *p, char *laser_map, const color_t c,
                     const char mark_mask) {

  // Fire laser, recording in laser_map
  square_t sq = p->kloc[c];
  int8_t bdir = ori_of(p->board[sq]);

  tbassert(ptype_of(p->board[sq]) == KING,
           "ptype: %d\n", ptype_of(p->board[sq]));
  laser_map[sq] |= mark_mask;
  int8_t beam = beam_of(bdir);

  while (true) { 
    sq += beam;
    laser_map[sq] |= mark_mask;
    tbassert(sq < ARR_SIZE && sq >= 0, "sq: %d\n", sq);
    ptype_t typ = ptype_of(p->board[sq]);
    switch (typ) {
      case EMPTY:  // empty square
        break;
      case PAWN:  // Pawn
        bdir = reflect_of(bdir, ori_of(p->board[sq]));
        if (bdir < 0) {  // Hit back of Pawn
          return;
        }
        beam = beam_of(bdir);
        break;
      case KING:  // King
        return;  // sorry, game over my friend!
        break;
      case INVALID:  // Ran off edge of board
        return;
        break;
      default:  // Shouldna happen, man!
        tbassert(false, "Not cool, man.  Not cool.\n");
        break;
    }
  }
}

// Harmonic-ish distance: 1/(|dx|+1) + 1/(|dy|+1)
// Because we don't want a divide by 0 error, we add one to the dx/dy values
float h_dist(square_t a, square_t b) {
    int delta_fil = fil_of(a) - fil_of(b);
    delta_fil = delta_fil < 0 ? -(delta_fil) : delta_fil;
    int delta_rnk = rnk_of(a) - rnk_of(b);
    delta_rnk = delta_rnk < 0 ? -(delta_rnk) : delta_rnk; 
    delta_fil++;
    delta_rnk++;
    return ((float)(delta_rnk + delta_fil))/((float)(delta_rnk*delta_fil));
}

// Marks the path of the laser until it hits a piece or goes off the board.
//
// p : current board state
// laser_map : end result will be stored here. Every square on the
//             path of the laser is marked with mark_mask
// c : color of king shooting laser
//
// In the heuristics version of the mark_laser_path method, we are also calculating the three heuristic values
// pawnpin, mobility, and h_squares_attackable.
//
// PAWNPIN Heuristic: count number of pawns that are pinned by the
//   opposing king's laser --- and are thus immobile.
// 
// MOBILITY heuristic: safe squares around king of color opp_color(c).
//
// H_ATTACKABLE heuristic: add value the closer the laser comes to the king
// h_attackable adds the harmonic distance from a marked laser square to the enemy square
// closer the laser is to enemy king, higher the value is
heuristics_t * mark_laser_path_heuristics(position_t *p, const color_t c, heuristics_t * heuristics) {
  square_t king_sq = p->kloc[opp_color(c)];
  
  // Initialize the h_squares_attackable value
  float h_attackable = 0;

  // Fire laser, recording in laser_map
  square_t sq = p->kloc[c];
  int8_t bdir = ori_of(p->board[sq]);

  // Create a bounding box around king's square
  const int8_t right = fil_of(king_sq)+1;
  const int8_t left = fil_of(king_sq)-1;
  const int8_t top = rnk_of(king_sq)+1;
  const int8_t bottom = rnk_of(king_sq)-1;
  
  // Column & Row of laser Square
  rnk_t sq_rank = rnk_of(sq);
  fil_t sq_file = fil_of(sq);

  // Check to see if the first block the laser fired in is directly surrounding the king, if so decrease mobility
  if ((sq_file <= right && sq_file >= left) && (sq_rank >= bottom && sq_rank <= top)) {
      heuristics->mobility--;
  }

   
  // Mark any invalid squares surrounding the king as not mobile
  for (uint8_t d = 0; d < 8; ++d) {
    square_t new_sq = king_sq + dir_of(d);
    if (ptype_of(p->board[new_sq]) == INVALID) {
      heuristics->mobility--;
    }
  }

  tbassert(ptype_of(p->board[sq]) == KING,
           "ptype: %d\n", ptype_of(p->board[sq]));
  int8_t beam = beam_of(bdir);
  h_attackable += h_dist(sq, king_sq);

  while (true) { 
    sq += beam;
    sq_file = fil_of(sq);
    sq_rank = rnk_of(sq);
    tbassert(sq < ARR_SIZE && sq >= 0, "sq: %d\n", sq);

    if ((sq_file <= right && sq_file >= left) && (sq_rank >= bottom && sq_rank <= top) && ptype_of(p->board[sq]) != INVALID) {
      heuristics->mobility--;
    } 

    switch (ptype_of(p->board[sq])) {
      case EMPTY:  // empty square
        h_attackable += h_dist(sq, king_sq);
        break;
      case PAWN:  // Pawn
        h_attackable += h_dist(sq, king_sq);
        // We have hit a pawn and pinned it, increment appropriately
        if (color_of(p->board[sq]) != c) {
          heuristics->pawnpin++;
        }
        bdir = reflect_of(bdir, ori_of(p->board[sq]));
        if (bdir < 0) {  // Hit back of Pawn
          heuristics->h_attackable = h_attackable;
          return heuristics;
        }
        beam = beam_of(bdir);
        break;
      case KING:  // King
        h_attackable += h_dist(sq, king_sq);
        heuristics->h_attackable = h_attackable;
        return heuristics;  // sorry, game over my friend!
        break;
      case INVALID:  // Ran off edge of board
        heuristics->h_attackable = h_attackable;
        return heuristics;
        break;
      default:  // Shouldna happen, man!
        tbassert(false, "Not cool, man.  Not cool.\n");
        break;
    }
  }
}

// Static evaluation.  Returns score
score_t eval(position_t *p, const bool verbose) {
  // seed rand_r with a value of 1, as per
  // http://linux.die.net/man/3/rand_r
  static __thread unsigned int seed = 1;
  // verbose = true: print out components of score
  ev_score_t score[2] = { 0, 0 };
  //  int corner[2][2] = { {INF, INF}, {INF, INF} };
  ev_score_t bonus;
  //char buf[MAX_CHARS_IN_MOVE]; used for debugging/verbose purposes
  uint8_t number_pawns[2] = {0,0};
  rnk_t king_max_rnk = 0;
  rnk_t king_min_rnk = 16;
  fil_t king_max_fil = 0;
  fil_t king_min_fil = 16;
  for(uint8_t c = 0; c < 2; c ++) {
    
    // Adds score for color's king
    const square_t sq = p->kloc[c];
    const fil_t f = fil_of(sq);
    const rnk_t r = rnk_of(sq);
    king_max_rnk = r > king_max_rnk ? r : king_max_rnk;
    king_min_rnk = r < king_min_rnk ? r : king_min_rnk;
    king_max_fil = f > king_max_fil ? f : king_max_fil;
    king_min_fil = f < king_min_fil ? f : king_min_fil;
    bonus = kface(p, f, r);

    score[c] += bonus;

    // KAGGRESSIVE heuristic
    bonus = kaggressive(p, f, r);
    score[c] += bonus;
  }
  for(uint8_t c = 0; c < 2; c++) {
    // Adds score for color's pawns
    for(uint8_t i = 0; i < NUMBER_PAWNS; i++) {
      const square_t sq = p->plocs[c][i];
      if(sq == 0) continue;
      const fil_t f = fil_of(sq);
      const rnk_t r = rnk_of(sq);
      number_pawns[c]++;
      // MATERIAL heuristic: Bonus for each Pawn
      bonus = PAWN_EV_VALUE;
      /*if (verbose) {
         printf("MATERIAL bonus %d for %s Pawn on %s\n", bonus, color_to_str(c), buf);
         }*/
      score[c] += bonus;

      // PBETWEEN heuristic
      //bonus = pbetween(p, f, r);
      bonus = inRange(king_min_rnk, king_max_rnk, r) && inRange(king_min_fil, king_max_fil, f) ? PBETWEEN : 0;
      /*if (verbose) {
        printf("PBETWEEN bonus %d for %s Pawn on %s\n", bonus, color_to_str(c), buf);
        }*/
      score[c] += bonus;

      // PCENTRAL heuristic
      bonus = pcentral(f, r);
      /*if (verbose) {
         printf("PCENTRAL bonus %d for %s Pawn on %s\n", bonus, color_to_str(c), buf);
         }*/
      score[c] += bonus;
    }
  }

  heuristics_t white_heuristics = { .pawnpin = 0, .h_attackable = 0, .mobility = 9};
  heuristics_t * w_heuristics = &white_heuristics;

  heuristics_t black_heuristics = { .pawnpin = 0, .h_attackable = 0, .mobility = 9};
  heuristics_t * b_heuristics = &black_heuristics;
  
  // Calculate the heurisitics for the white and black color
  mark_laser_path_heuristics(p, BLACK, w_heuristics);
  mark_laser_path_heuristics(p, WHITE, b_heuristics);

  const ev_score_t w_hattackable = HATTACK * b_heuristics->h_attackable;
  score[WHITE] += w_hattackable;
  /*if (verbose) {
    printf("HATTACK bonus %d for White\n", w_hattackable);
    }*/
  const ev_score_t b_hattackable = HATTACK * w_heuristics->h_attackable;
  score[BLACK] += b_hattackable;
  /*if (verbose) {
    printf("HATTACK bonus %d for Black\n", b_hattackable);
    }*/

  const int w_mobility = MOBILITY * w_heuristics->mobility;
  score[WHITE] += w_mobility;
  /*if (verbose) {
    printf("MOBILITY bonus %d for White\n", w_mobility);
    }*/
  const int b_mobility = MOBILITY * b_heuristics->mobility;
  score[BLACK] += b_mobility;
  /*if (verbose) {
    printf("MOBILITY bonus %d for Black\n", b_mobility);
    }*/

  // PAWNPIN Heuristic --- is a pawn immobilized by the enemy laser.
  const int w_pawnpin = PAWNPIN * (number_pawns[WHITE] - w_heuristics->pawnpin);
  score[WHITE] += w_pawnpin;
  const int b_pawnpin = PAWNPIN * (number_pawns[BLACK] - b_heuristics->pawnpin);
  score[BLACK] += b_pawnpin;

  // score from WHITE point of view
  ev_score_t tot = score[WHITE] - score[BLACK];

  if (RANDOMIZE) {
    const ev_score_t  z = rand_r(&seed) % (RANDOMIZE*2+1);
    tot = tot + z - RANDOMIZE;
  }

  if (color_to_move_of(p) == BLACK) {
    tot = -tot;
  }

  return tot / EV_SCORE_RATIO;
}
