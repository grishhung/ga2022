#include "debug.h"
#include "fs.h"
#include "heap.h"
#include "render.h"
#include "simple_game.h"
#include "timer.h"
#include "wm.h"

#include "custom_game.h"

int main(int argc, const char* argv[])
{
	debug_set_print_mask(k_print_info | k_print_warning | k_print_error);
	debug_install_exception_handler();

	timer_startup();

	heap_t* heap = heap_create(2 * 1024 * 1024);
	fs_t* fs = fs_create(heap, 8);
	wm_window_t* window = wm_create(heap);
	render_t* render = render_create(heap, window);

	char game_name[64] = "custom_game";

	custom_game_t* game = custom_game_create(heap, fs, window, render, game_name);

	while (!wm_pump(window))
	{
		custom_game_update(game);
	}

	/* XXX: Shutdown render before the game. Render uses game resources. */
	render_destroy(render);

	custom_game_destroy(game, game_name);

	wm_destroy(window);
	fs_destroy(fs);
	heap_destroy(heap);

	return 0;
}
