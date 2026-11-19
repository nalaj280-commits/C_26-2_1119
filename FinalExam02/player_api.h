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

/* End a very long stalemate without replacing each player's normal strategy.
   From turn 160, move toward a cell that can interact with the current
   objective: pick up a floor key, attack its carrier, or reach the exit. */
static inline int snapshot_endgame_progress(const Snapshot *state, Command *command)
{
    static const int dx[4] = {0, 0, -1, 1}, dy[4] = {-1, 1, 0, 0};
    int distance[MAP_MAX][MAP_MAX], queue_x[MAP_MAX * MAP_MAX], queue_y[MAP_MAX * MAP_MAX];
    int occupied[MAP_MAX][MAP_MAX] = {{0}}, head = 0, tail = 0;
    int i, x, y, d, k, best = 100000, best_dir = -1, carrying;
    Position me, objective;
    if (!state || !command || state->turn < 160) return 0;
    me = state->players[state->self].pos;
    carrying = state->key_owner == state->self;
    objective = carrying ? state->exit :
        state->key_owner < 0 ? state->key : state->players[state->key_owner].pos;
    for (i = 0; i < PLAYER_COUNT; ++i)
        if (i != state->self && state->players[i].hp > 0 && !state->players[i].rank)
            occupied[state->players[i].pos.y][state->players[i].pos.x] = 1;
    if (!carrying && state->key_owner < 0) {
        for (d = 0; d < 4; ++d)
            if (me.x + dx[d] == objective.x && me.y + dy[d] == objective.y) {
                *command = (Command)(COMMAND_PICK_UP + d); return 1;
            }
    } else if (!carrying) {
        for (d = 0; d < 4; ++d) {
            Position hit = {me.x + dx[d], me.y + dy[d]};
            for (k = -1; k <= 1; ++k) {
                Position cell = hit;
                if (d < 2) cell.x += k; else cell.y += k;
                if (cell.x == objective.x && cell.y == objective.y) {
                    *command = (Command)(COMMAND_ATTACK_UP + d); return 1;
                }
            }
        }
    }
    for (y = 0; y < state->size; ++y)
        for (x = 0; x < state->size; ++x) distance[y][x] = -1;
    if (carrying) {
        distance[objective.y][objective.x] = 0;
        queue_x[tail] = objective.x; queue_y[tail++] = objective.y;
    } else if (state->key_owner < 0) {
        for (d = 0; d < 4; ++d) {
            x = objective.x - dx[d]; y = objective.y - dy[d];
            if (x < 0 || y < 0 || x >= state->size || y >= state->size ||
                state->terrain[y][x] == '#' || state->terrain[y][x] == 'E' || distance[y][x] >= 0) continue;
            distance[y][x] = 0; queue_x[tail] = x; queue_y[tail++] = y;
        }
    } else {
        /* Cells from which one fan attack can hit the current carrier. */
        for (d = 0; d < 4; ++d) for (k = -1; k <= 1; ++k) {
            x = objective.x - dx[d] - (d < 2 ? k : 0);
            y = objective.y - dy[d] - (d >= 2 ? k : 0);
            if (x < 0 || y < 0 || x >= state->size || y >= state->size ||
                state->terrain[y][x] == '#' || state->terrain[y][x] == 'E' || distance[y][x] >= 0) continue;
            distance[y][x] = 0; queue_x[tail] = x; queue_y[tail++] = y;
        }
    }
    while (head < tail) {
        x = queue_x[head]; y = queue_y[head++];
        for (d = 0; d < 4; ++d) {
            int nx = x + dx[d], ny = y + dy[d];
            if (nx < 0 || ny < 0 || nx >= state->size || ny >= state->size || distance[ny][nx] >= 0 ||
                state->terrain[ny][nx] == '#' || (!carrying && state->terrain[ny][nx] == 'E')) continue;
            distance[ny][nx] = distance[y][x] + 1;
            queue_x[tail] = nx; queue_y[tail++] = ny;
        }
    }
    for (i = 0; i < 4; ++i) {
        d = (i + state->self + state->turn) % 4;
        x = me.x + dx[d]; y = me.y + dy[d];
        if (x < 0 || y < 0 || x >= state->size || y >= state->size || occupied[y][x] || distance[y][x] < 0) continue;
        if (distance[y][x] < best) { best = distance[y][x]; best_dir = d; }
    }
    if (best_dir < 0) return 0;
    *command = (Command)(COMMAND_UP + best_dir);
    return 1;
}

/* Parse an already-open state file. The caller retains ownership and must fclose. */
int snapshot_read(FILE *file, Snapshot *state);
#endif
