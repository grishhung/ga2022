#pragma warning (disable : 4996)

#include "ecs.h"
#include "fs.h"
#include "gpu.h"
#include "heap.h"
#include "render.h"
#include "timer_object.h"
#include "transform.h"
#include "wm.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <string.h>

#include <stdio.h>
#include <stdlib.h>

#include <ctype.h>

typedef struct transform_component_t
{
	transform_t transform;
} transform_component_t;

typedef struct camera_component_t
{
	mat4f_t projection;
	mat4f_t view;
} camera_component_t;

typedef struct model_component_t
{
	gpu_mesh_info_t* mesh_info;
	gpu_shader_info_t* shader_info;
} model_component_t;

typedef struct player_component_t
{
	int index;
} player_component_t;

typedef struct name_component_t
{
	char name[32];
} name_component_t;

typedef struct custom_game_t
{
	heap_t* heap;
	fs_t* fs;
	wm_window_t* window;
	render_t* render;

	timer_object_t* timer;

	ecs_t* ecs;
	int transform_type;
	int camera_type;
	int model_type;
	int player_type;
	int name_type;
	ecs_entity_ref_t player_ent;
	ecs_entity_ref_t camera_ent;

	gpu_mesh_info_t cube_mesh[64];
	gpu_shader_info_t cube_shader;
	fs_work_t* vertex_shader_work;
	fs_work_t* fragment_shader_work;

	// For checking player + bus collision

	float player_pos_x;
	float player_pos_y;
	float player_pos_z;

	bool is_hurt;
} custom_game_t;

static void load_resources(custom_game_t* game);
static void unload_resources(custom_game_t* game);
static void spawn_player(custom_game_t* game, int type, int index, int y, int z);
static void spawn_camera(custom_game_t* game);
static void update_everything(custom_game_t* game);
static void draw_models(custom_game_t* game);

custom_game_t* custom_game_create(heap_t* heap, fs_t* fs, wm_window_t* window, render_t* render)
{
	custom_game_t* game = heap_alloc(heap, sizeof(custom_game_t), 8);
	game->heap = heap;
	game->fs = fs;
	game->window = window;
	game->render = render;

	game->timer = timer_object_create(heap, NULL);
	
	game->ecs = ecs_create(heap);
	game->transform_type = ecs_register_component_type(game->ecs, "transform", sizeof(transform_component_t), _Alignof(transform_component_t));
	game->camera_type = ecs_register_component_type(game->ecs, "camera", sizeof(camera_component_t), _Alignof(camera_component_t));
	game->model_type = ecs_register_component_type(game->ecs, "model", sizeof(model_component_t), _Alignof(model_component_t));
	game->player_type = ecs_register_component_type(game->ecs, "player", sizeof(player_component_t), _Alignof(player_component_t));
	game->name_type = ecs_register_component_type(game->ecs, "name", sizeof(name_component_t), _Alignof(name_component_t));

	load_resources(game);

	// SPAWN LEVEL (OBJECTS THAT ARE NOT SAVED)
	FILE* file;
	errno_t err;

	if ((err = fopen_s(&file, "custom_game.level", "r")) != 0) {
		printf("\n Unable to open : %s ", "custom_game.level");
		exit(1);
	}

	char line[2048];

	char* delim = ",";
	int index = 0;
	int z_val = 0;

	while (fgets(line, sizeof(line), file)) {
		int y_val = 0;
		char* type = strtok(line, delim);
		while (type != NULL)
		{
			int is_bad = 0;

			if (strlen(type) < 1) {
				is_bad = 1;
			}
			else {
				for (int i = 0; i < strlen(type); i++) {
					if (!isdigit(type[0])) {
						is_bad = 1;
					}
				}
			}

			if (!is_bad) {
				spawn_player(game, (int)atoi(type), index, y_val, z_val);
				index++;
			}

			// iterate
			type = strtok(NULL, ",");
			y_val++;
		}
		z_val++;
	}
	fclose(file);

	spawn_camera(game);

	game->is_hurt = false;

	game->player_pos_x = 0.0f;  // WILL NOT CHANGE
	game->player_pos_y = 0.0f;
	game->player_pos_z = 0.0f;

	return game;
}

void custom_game_destroy(custom_game_t* game)
{
	ecs_destroy(game->ecs);
	timer_object_destroy(game->timer);
	unload_resources(game);
	heap_free(game->heap, game);
}

void custom_game_update(custom_game_t* game)
{
	timer_object_update(game->timer);
	ecs_update(game->ecs);
	update_everything(game);
	draw_models(game);
	render_push_done(game->render);
}

static void load_resources(custom_game_t* game)
{
	game->vertex_shader_work = fs_read(game->fs, "shaders/triangle.vert.spv", game->heap, false, false);
	game->fragment_shader_work = fs_read(game->fs, "shaders/triangle.frag.spv", game->heap, false, false);
	game->cube_shader = (gpu_shader_info_t)
	{
		.vertex_shader_data = fs_work_get_buffer(game->vertex_shader_work),
		.vertex_shader_size = fs_work_get_size(game->vertex_shader_work),
		.fragment_shader_data = fs_work_get_buffer(game->fragment_shader_work),
		.fragment_shader_size = fs_work_get_size(game->fragment_shader_work),
		.uniform_buffer_count = 1,
	};

	static vec3f_t cube_verts[] =
	{
		{ -0.5f, -0.5f,  0.5f }, { 1.0f, 1.0f, 0.0f },
		{  0.5f, -0.5f,  0.5f }, { 1.0f, 1.0f, 0.0f },
		{  0.5f,  0.5f,  0.5f }, { 1.0f, 1.0f, 0.0f },
		{ -0.5f,  0.5f,  0.5f }, { 1.0f, 1.0f, 0.0f },
		{ -0.5f, -0.5f, -0.5f }, { 1.0f, 1.0f, 0.0f },
		{  0.5f, -0.5f, -0.5f }, { 1.0f, 1.0f, 0.0f },
		{  0.5f,  0.5f, -0.5f }, { 1.0f, 1.0f, 0.0f },
		{ -0.5f,  0.5f, -0.5f }, { 1.0f, 1.0f, 0.0f },
	};
	static uint16_t cube_indices[] =
	{
		0, 1, 2,
		2, 3, 0,
		1, 5, 6,
		6, 2, 1,
		7, 6, 5,
		5, 4, 7,
		4, 0, 3,
		3, 7, 4,
		4, 5, 1,
		1, 0, 4,
		3, 2, 6,
		6, 7, 3
	};
	game->cube_mesh[0] = (gpu_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = cube_verts,
		.vertex_data_size = sizeof(cube_verts),
		.index_data = cube_indices,
		.index_data_size = sizeof(cube_indices),
	};
}

static void unload_resources(custom_game_t* game)
{
	fs_work_destroy(game->fragment_shader_work);
	fs_work_destroy(game->vertex_shader_work);
}

static void spawn_player(custom_game_t* game, int type, int index, int y_val, int z_val)
{
	uint64_t k_player_ent_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->model_type) |
		(1ULL << game->player_type) |
		(1ULL << game->name_type);
	game->player_ent = ecs_entity_add(game->ecs, k_player_ent_mask);

	transform_component_t* transform_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->transform_type, true);
	transform_identity(&transform_comp->transform);

	transform_comp->transform.scale.x = 1.0f;
	transform_comp->transform.scale.y = 1.0f;
	transform_comp->transform.scale.z = 1.0f;
	transform_comp->transform.translation.y = (float)y_val;
	transform_comp->transform.translation.z = (float)z_val;

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "player");

	player_component_t* player_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->player_type, true);
	player_comp->index = index;

	model_component_t* model_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->model_type, true);
	model_comp->mesh_info = &game->cube_mesh[0];
	model_comp->shader_info = &game->cube_shader;
}

static void spawn_camera(custom_game_t* game)
{
	uint64_t k_camera_ent_mask =
		(1ULL << game->camera_type) |
		(1ULL << game->name_type);
	game->camera_ent = ecs_entity_add(game->ecs, k_camera_ent_mask);

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->camera_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "camera");

	camera_component_t* camera_comp = ecs_entity_get_component(game->ecs, game->camera_ent, game->camera_type, true);
	// note: rendering in the wrong order with perspective camera (things that should be in front are behind)
	// mat4f_make_perspective(&camera_comp->projection, (float)M_PI / 2.0f, 16.0f / 9.0f, 0.1f, 100.0f);
	float zoom = 30.0f;  // Someone suggested in the game architecture discord that I should add this; it's very handy!
	mat4f_make_orthographic(&camera_comp->projection, -zoom, zoom, 9.0f / 16.0f * zoom, -9.0f / 16.0f * zoom, -1000.0f, 1000.0f);

	vec3f_t eye_pos = vec3f_scale(vec3f_forward(), -5.0f);
	vec3f_t forward = vec3f_forward();
	vec3f_t up = vec3f_up();
	mat4f_make_lookat(&camera_comp->view, &eye_pos, &forward, &up);
}

static void update_everything(custom_game_t* game)
{
	/*float dt = (float)timer_object_get_delta_ms(game->timer) * 0.001f;

	uint32_t key_mask = wm_get_key_mask(game->window);

	uint64_t k_query_mask = (1ULL << game->transform_type) | (1ULL << game->player_type);

	for (ecs_query_t query = ecs_query_create(game->ecs, k_query_mask);
		ecs_query_is_valid(game->ecs, &query);
		ecs_query_next(game->ecs, &query))
	{
		transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);
		player_component_t* player_comp = ecs_query_get_component(game->ecs, &query, game->player_type);

		// movement

		transform_t move;
		transform_identity(&move);

		int ind = player_comp->index;

		if (ind == 0) {
			// player movement - controlled
			if (key_mask & k_key_up) {
				move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_up(), -dt));
			}
			if (key_mask & k_key_down) {
				move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_up(), dt));
			}
			if (key_mask & k_key_left) {
				move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), -dt));
			}
			if (key_mask & k_key_right) {
				move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), dt));
			}
		}

		if (ind == 0) {
			game->player_pos_y = transform_comp->transform.translation.y;
			game->player_pos_z = transform_comp->transform.translation.z;
		}

		transform_multiply(&transform_comp->transform, &move);

		// Wrap the buses around to the left side when they hit the side boundaries

		if (transform_comp->transform.translation.y > 10.0f) {
			transform_comp->transform.translation.y -= 20.0f;
		}
		else if (transform_comp->transform.translation.y < -10.0f) {
			transform_comp->transform.translation.y += 20.0f;
		}

		// check player collision
		
		if (ind != 0) {
			float bus_y = transform_comp->transform.translation.y;
			float bus_z = transform_comp->transform.translation.z;
			if (((bus_y - 1.5f) <= game->player_pos_y)
					&& ((bus_y + 1.5f) >= game->player_pos_y)
					&& ((bus_z - 1.0f) <= game->player_pos_z)
					&& ((bus_z + 1.0f) >= game->player_pos_z)) {
				game->is_hurt = true;
			}
		}

		// respawn if player hits traffic or wins

		if ((ind == 0 && (game->is_hurt || transform_comp->transform.translation.z < -3.0f))) {
			transform_comp->transform.translation.y = 0.0f;
			transform_comp->transform.translation.z = 4.0f;

			game->player_pos_y = 0.0f;
			game->player_pos_z = 4.0f;

			game->is_hurt = false;
		}
	}*/
}

static void draw_models(custom_game_t* game)
{
	uint64_t k_camera_query_mask = (1ULL << game->camera_type);
	for (ecs_query_t camera_query = ecs_query_create(game->ecs, k_camera_query_mask);
		ecs_query_is_valid(game->ecs, &camera_query);
		ecs_query_next(game->ecs, &camera_query))
	{
		camera_component_t* camera_comp = ecs_query_get_component(game->ecs, &camera_query, game->camera_type);

		uint64_t k_model_query_mask = (1ULL << game->transform_type) | (1ULL << game->model_type);
		for (ecs_query_t query = ecs_query_create(game->ecs, k_model_query_mask);
			ecs_query_is_valid(game->ecs, &query);
			ecs_query_next(game->ecs, &query))
		{
			transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);
			model_component_t* model_comp = ecs_query_get_component(game->ecs, &query, game->model_type);
			ecs_entity_ref_t entity_ref = ecs_query_get_entity(game->ecs, &query);

			struct
			{
				mat4f_t projection;
				mat4f_t model;
				mat4f_t view;
			} uniform_data;
			uniform_data.projection = camera_comp->projection;
			uniform_data.view = camera_comp->view;
			transform_to_matrix(&transform_comp->transform, &uniform_data.model);
			gpu_uniform_buffer_info_t uniform_info = { .data = &uniform_data, sizeof(uniform_data) };

			render_push_model(game->render, &entity_ref, model_comp->mesh_info, model_comp->shader_info, &uniform_info);
		}
	}
}
