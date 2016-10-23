// Copyright (c) 2015 MIT License by 6.172 Staff

// This file contains the scout search routine. Although this routine contains
//   some duplicated logic from the searchPV routine in search.c, it is
//   convenient to maintain it separately. This allows one to easily
//   parallelize scout search separately from searchPV.

#include "./tbassert.h"
#include "./simple_mutex.h"
#include <cilk/cilk_api.h>

#define YOUNG_BROTHERS_WAIT 5

// Checks whether a node's parent has aborted.
//   If this occurs, we should just stop and return 0 immediately.
bool parallel_parent_aborted(searchNode* node) {
  searchNode* pred = node->parent;
  while (pred != NULL) {
    if (pred->abort) {
      return true;
    }
    pred = pred->parent;
  }
  return false;
}

// Checks whether this node has aborted due to a cut-off.
//   If this occurs, we should actually return the score.
bool parallel_node_aborted(searchNode* node) {
  if (node->abort) return true;
  return false;
}

// Initialize a scout search node for a "Null Window" search.
//   https://chessprogramming.wikispaces.com/Scout
//   https://chessprogramming.wikispaces.com/Null+Window
static void initialize_scout_node(searchNode *node, const int depth) {
  node->type = SEARCH_SCOUT;
  node->beta = -(node->parent->alpha);
  node->alpha = node->beta - 1;
  node->depth = depth;
  node->ply = node->parent->ply + 1;
  node->subpv[0] = 0;
  node->legal_move_count = 0;
  node->fake_color_to_move = color_to_move_of(&(node->position));
  // point of view = 1 for white, -1 for black
  node->pov = 1 - node->fake_color_to_move * 2;
  node->best_move_index = 0;  // index of best move found
  node->abort = false;
}

static score_t scout_search(searchNode *node, const int depth,
                            uint64_t *node_count_serial) {
  __cilkrts_set_param("nworkers","1");
  // Initialize the search node.
  initialize_scout_node(node, depth);

  // check whether we should abort
  if (should_abort_check() || parallel_parent_aborted(node)) {
    return 0;
  }

  // Pre-evaluate this position.
  leafEvalResult pre_evaluation_result = evaluate_as_leaf(node, SEARCH_SCOUT);

  // If we decide to stop searching, return the pre-evaluation score.
  if (pre_evaluation_result.type == MOVE_EVALUATED) {
    return pre_evaluation_result.score;
  }

  // Populate some of the fields of this search node, using some
  //  of the information provided by the pre-evaluation.
  const int hash_table_move = pre_evaluation_result.hash_table_move;
  node->best_score = pre_evaluation_result.score;
  node->quiescence = pre_evaluation_result.should_enter_quiescence;

  // Grab the killer-moves for later use.
  const move_t killer_a = killer[KMT(node->ply, 0)];
  const move_t killer_b = killer[KMT(node->ply, 1)];

  // Store the sorted move list on the stack.
  //   MAX_NUM_MOVES is all that we need.
  sortable_move_t move_list[MAX_NUM_MOVES];

  // Obtain the sorted move list.
  const int num_of_moves = get_sortable_move_list(node, move_list, hash_table_move);
  
  // For our parallel code we'll want to iterate over the first few values serially, then go parallel
  // This variable sets how many nodes we'll search serially
  // const int first_iteration_value = num_of_moves >= 5 ? 5 : num_of_moves;

  int number_of_moves_evaluated = 0;

  // A simple mutex. See simple_mutex.h for implementation details.
  simple_mutex_t node_mutex;
  init_simple_mutex(&node_mutex);

  // Sort the move list.
  //  sort_incremental_new(move_list, num_of_moves, number_of_moves_evaluated);
  
  // This is the original code here, think it might be in place for parallelizing, so keeping it here
  // but commented out for now

  moveEvaluationResult result;
  result.next_node.subpv[0] = 0;
  result.next_node.parent = node;

  for (int mv_index = 0; mv_index < num_of_moves; mv_index++) {
    // We have searched as many serial nodes as we need to. Break and start searching parallely
    if (node->legal_move_count > YOUNG_BROTHERS_WAIT) {
      break;
    }

    // Sort the move list.
    sort_incremental_new(move_list, num_of_moves, number_of_moves_evaluated);
    // Get the next move from the move list.
    int local_index = number_of_moves_evaluated++;
    // Added this line to use our new incremental_sort implementation, wasn't originally here
    move_t mv = get_move(move_list[local_index]);

    if (TRACE_MOVES) {
      print_move_info(mv, node->ply);
    }

    // increase node count
    __sync_fetch_and_add(node_count_serial, 1);

    evaluateMove(node, mv, killer_a, killer_b,
                 SEARCH_SCOUT,
                 node_count_serial,
                 &result);

    if (result.type == MOVE_ILLEGAL || result.type == MOVE_IGNORE
        || abortf || parallel_parent_aborted(node)) {
      continue;
    }

    // A legal move is a move that's not KO, but when we are in quiescence
    // we only want to count moves that has a capture.
    if (result.type == MOVE_EVALUATED) {
      //node->legal_move_count++;
      __sync_fetch_and_add(&node->legal_move_count, 1); 
    }

    // process the score. Note that this mutates fields in node.
    bool cutoff = search_process_score(node, mv, local_index, &result, SEARCH_SCOUT);

    if (cutoff) {
      node->abort = true;
      break;
    }
  }

  if (parallel_parent_aborted(node)) {
    return 0;
  }
  
  // We have not found a cutoff, continue to search parallely
  if (!(node->abort)) {

  int start_value = number_of_moves_evaluated;

  sort_incremental(move_list, num_of_moves, number_of_moves_evaluated);
  
  cilk_for (int mv_index = start_value; mv_index < num_of_moves; mv_index++) {
    do {
      if (node->abort) continue;
      // Get the next move from the move list.
      int local_index = __sync_fetch_and_add(&number_of_moves_evaluated, 1);
      // Added this line to use our new incremental_sort implementation, wasn't originally here
      // sort_incremental_new(move_list, num_of_moves, local_index);
      move_t mv = get_move(move_list[local_index]);

      if (TRACE_MOVES) {
        print_move_info(mv, node->ply);
      }

      // increase node count
      __sync_fetch_and_add(node_count_serial, 1);

      moveEvaluationResult result;
      result.next_node.subpv[0] = 0;
      result.next_node.parent = node;

      evaluateMove(node, mv, killer_a, killer_b,
                            SEARCH_SCOUT,
                            node_count_serial,
                            &result);

      if (result.type == MOVE_ILLEGAL || result.type == MOVE_IGNORE
          || abortf || parallel_parent_aborted(node)) {
        continue;
      }

      // A legal move is a move that's not KO, but when we are in quiescence
      // we only want to count moves that has a capture.
      if (result.type == MOVE_EVALUATED) {
        // node->legal_move_count++;
        __sync_fetch_and_add(&node->legal_move_count, 1); 
      }

      // process the score. Note that this mutates fields in node.
      simple_acquire(&node_mutex);
      bool cutoff = search_process_score(node, mv, local_index, &result, SEARCH_SCOUT);
      simple_release(&node_mutex);
      if (cutoff) {
        node->abort = true;
        continue;
      }
    } while (false);
  }

  }

  if (parallel_parent_aborted(node)) {
    return 0;
  }

  if (node->quiescence == false) {
    update_best_move_history(&(node->position), node->best_move_index,
                             move_list, number_of_moves_evaluated);
  }

  tbassert(abs(node->best_score) != -INF, "best_score = %d\n",
           node->best_score);

  // Reads node->position.key, node->depth, node->best_score, and node->ply
  update_transposition_table(node);

  return node->best_score;
}


