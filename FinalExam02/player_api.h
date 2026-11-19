#ifndef PLAYER_API_H
#define PLAYER_API_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "diagnostics.h"

#define MAP_MAX 32
#define NPC_COUNT 6
#define PLAYER_COUNT NPC_COUNT
#define MAX_HP 2
#define KEY_FLOOR_RESPAWN_TURNS 3
/* Each group of four uses the order up, down, left, right. */
typedef enum Command {
    COMMAND_UP, COMMAND_DOWN, COMMAND_LEFT, COMMAND_RIGHT,
    COMMAND_ATTACK_UP, COMMAND_ATTACK_DOWN, COMMAND_ATTACK_LEFT, COMMAND_ATTACK_RIGHT,
    COMMAND_PICK_UP, COMMAND_PICK_DOWN, COMMAND_PICK_LEFT, COMMAND_PICK_RIGHT,
    COMMAND_DROP_UP, COMMAND_DROP_DOWN, COMMAND_DROP_LEFT, COMMAND_DROP_RIGHT,
    COMMAND_COUNT
} Command;

/* x is the cell column, y is the row; one x occupies TWO console characters. */
typedef struct Position { int x, y; } Position;
typedef struct PlayerState {
    Position pos, spawn;
    int hp;
    unsigned int deaths;
    uint64_t rest;
    int rank; /* 0: still racing (including respawn); 1..5: escaped; 6: last. */
} PlayerState;

/* Public, file-only information; no pointer to the live core is exposed to AI. */
typedef struct Snapshot {
    int size, turn, self, key_owner, key_floor_turns;
    Position key, key_start, exit;
    PlayerState players[PLAYER_COUNT];
    char terrain[MAP_MAX][MAP_MAX];
} Snapshot;

/* A key that survives this turn on the floor returns to key_start.  A player
   already beside it still gets the final pickup attempt before that reset. */
static int snapshot_key_resets_after_turn(const Snapshot *state)
{
    return state->key_owner < 0 &&
        state->key_floor_turns + 1 >= KEY_FLOOR_RESPAWN_TURNS;
}

/* Route planners should stop walking toward a key that will disappear before
   they can pick it up.  Immediate adjacent pickups keep the real floor cell. */
static inline void snapshot_forecast_expiring_key(Snapshot *state)
{
    Position me;
    if (!snapshot_key_resets_after_turn(state)) return;
    me = state->players[state->self].pos;
    if (abs(me.x - state->key.x) + abs(me.y - state->key.y) > 1) {
        state->key = state->key_start;
        state->key_floor_turns = 0;
    }
}

/* Parse an already-open state file. The caller retains ownership and must fclose. */
int snapshot_read(FILE *file, Snapshot *state);
#endif
