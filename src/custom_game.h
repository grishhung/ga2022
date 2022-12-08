#pragma once

// custom Test Game
// Brings together major engine systems to make a custom game.

typedef struct custom_game_t custom_game_t;

typedef struct fs_t fs_t;
typedef struct heap_t heap_t;
typedef struct render_t render_t;
typedef struct wm_window_t wm_window_t;

// Create an instance of custom test game.
custom_game_t* custom_game_create(heap_t* heap, fs_t* fs, wm_window_t* window, render_t* render, char* game_name);

// Destroy an instance of custom test game.
void custom_game_destroy(custom_game_t* game, char* game_name);

// Per-frame update for our custom test game.
void custom_game_update(custom_game_t* game);
