
#include "heap.h"

#include "debug.h"
#include "tlsf/tlsf.h"

#include <stddef.h>
#include <stdio.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <dbghelp.h>  // STUDENT ADDED

// Unreasonably large numbers, but just in case.
#define MAX_STACK_SIZE 64
#define MAX_CALLSTACK_COUNT 1024
#define MAX_NAME_LEN 256

typedef struct callstack_t {  // STUDENT ADDED
	char** frames;
	unsigned short frame_count;
	struct callstack_t* next;
} callstack_t;

typedef struct arena_t
{
	pool_t pool;
	struct arena_t* next;

	// START OF STUDENT CODE

	// What if there are multiple memory leaks in one pool?

	// THIS WON'T WORK BECAUSE WE NEED TO SUPPORT MORE THAN ONE CALLSTACK PER POOL!

	// HANDLE process;
	// void* frames[MAX_STACK_SIZE];
	// unsigned short frame_count;
	
	// Note: The "giant fixed size callstack array" idea doesn't seem
	//       to work if the array gets too big, so I'm going to try
	//       replacing it with a linked list of callstack_t's instead.

	callstack_t* callstack_list_head;
	callstack_t* callstack_list_tail;

	// END OF STUDENT CODE
} arena_t;

typedef struct heap_t
{
	tlsf_t tlsf;
	size_t grow_increment;
	arena_t* arena;
} heap_t;

heap_t* heap_create(size_t grow_increment)
{
	heap_t* heap = VirtualAlloc(NULL, sizeof(heap_t) + tlsf_size(),
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!heap)
	{
		debug_print(
			k_print_error,
			"OUT OF MEMORY!\n");
		return NULL;
	}

	heap->grow_increment = grow_increment;
	heap->tlsf = tlsf_create(heap + 1);
	heap->arena = NULL;

	return heap;
}

void* heap_alloc(heap_t* heap, size_t size, size_t alignment)
{
	void* address = tlsf_memalign(heap->tlsf, alignment, size);
	if (!address)
	{
		size_t arena_size =
			__max(heap->grow_increment, size * 2) +
			sizeof(arena_t);
		arena_t* arena = VirtualAlloc(NULL,
			arena_size + tlsf_pool_overhead(),
			MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!arena)
		{
			debug_print(
				k_print_error,
				"OUT OF MEMORY!\n");
			return NULL;
		}

		arena->pool = tlsf_add_pool(heap->tlsf, arena + 1, arena_size);

		arena->next = heap->arena;
		heap->arena = arena;

		address = tlsf_memalign(heap->tlsf, alignment, size);

		// START OF STUDENT CODE

		// This didn't end up working because it only accounted for one callstack.
		
		// arena->process = GetCurrentProcess();
		// SymInitialize(arena->process, NULL, TRUE);
		// arena->frame_count = CaptureStackBackTrace(0, MAX_STACK_SIZE, arena->frames, NULL);
		
		// END OF STUDENT CODE
	}

	// START OF STIDENT CODE

	// Construct the callstack linked list for heap->arena.

	void* callstack[MAX_STACK_SIZE];

	// This is an array of call name strings (e.g., "main", "homework1_test", etc.).
	char** callstack_names = VirtualAlloc(NULL,
		MAX_STACK_SIZE * sizeof(char*),
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

	HANDLE process = GetCurrentProcess();
	SymInitialize(process, NULL, TRUE);

	callstack_t* curr_callstack_node = VirtualAlloc(NULL,
		sizeof(callstack_t),
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

	WORD callstack_size = CaptureStackBackTrace(0, MAX_STACK_SIZE, callstack, NULL);
	curr_callstack_node->frame_count = (unsigned short)callstack_size;

	// // Sanity check: Should be 10 for each homework1_test, including calls deeper than main
	// printf("%d\n", curr_callstack_node->frame_count);

	char symbol_info[sizeof(SYMBOL_INFO) + (MAX_NAME_LEN + 1) * sizeof(TCHAR)];
	SYMBOL_INFO* symbol = (SYMBOL_INFO*)symbol_info;
	symbol->MaxNameLen = MAX_NAME_LEN;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

	for (unsigned short i = 0; i < callstack_size; i++) {
		callstack_names[i] = VirtualAlloc(NULL,
			MAX_NAME_LEN * sizeof(char),
			MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

		DWORD64 address = (DWORD64)callstack[i];

		SymFromAddr(process, address, 0, symbol);
		sprintf_s(callstack_names[i], MAX_NAME_LEN, "%s", symbol->Name);
	}

	curr_callstack_node->frames = callstack_names;

	SymCleanup(process);

	if (heap->arena->callstack_list_head == NULL) {
		// If this is the first callstack in the pool, initialize the linked list.
		// (Set both the head and tail to the current callstack.)
		heap->arena->callstack_list_head = curr_callstack_node;
		heap->arena->callstack_list_tail = curr_callstack_node;
	}
	else {
		// Otherwise, append the list.
		heap->arena->callstack_list_tail->next = curr_callstack_node;
		heap->arena->callstack_list_tail = curr_callstack_node;
	}

	// END OF STUDENT CODE

	return address;
}

void heap_free(heap_t* heap, void* address)
{
	tlsf_free(heap->tlsf, address);
}

// Based on tlsf.default_walker()
void leak_detection_walker(void* ptr, size_t size, int used, arena_t* curr_arena) {
	/*if (used) {

		// This ended up not working because the original implementation only accounted for one callstack.

		printf("Memory leak of size %d with call stack:\n", (unsigned int)size);

		SYMBOL_INFO* symbol = (SYMBOL_INFO*)calloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char), 1);
		symbol->MaxNameLen = MAX_NAME_LEN;
		symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		
		for (int i = 0; i < curr_arena->frame_count; i++) {
			SymFromAddr(curr_arena->process, (DWORD64)(curr_arena->frames[i]), 0, symbol);
			printf("[%d] %s\n", i, symbol->Name);
		}
		
		free(symbol);
	}*/
	callstack_t* curr_callstack = curr_arena->callstack_list_head;

	if (curr_callstack == NULL) {  // End of the list. We're done!
		return;
	}
	else if (used) {
		printf("Memory leak of size %d bytes with callstack:\n", (unsigned int)size);

		int main_reached = 0;  // Boolean value for avoiding printing anything after main.

		// Set i = 1 to avoid printing "[0] heap_alloc"
		// Mote: It would be better to find a way to do this without hardcoding i = 1 in the future.
		for (unsigned short i = 1; i < curr_callstack->frame_count; i++) {
			unsigned short index = i - 1;

			if (!main_reached) {
				printf("[%d] %s\n", index, curr_callstack->frames[i]);
			}

			if (strcmp(curr_callstack->frames[i], "main") == 0) {
				main_reached = 1;  // Don't print anything further!
			}

			VirtualFree(curr_callstack->frames[i], 0, MEM_RELEASE);
		}

		VirtualFree(curr_callstack->frames, 0, MEM_RELEASE);
	}

	curr_arena->callstack_list_head = curr_callstack->next; // Keep going down the list...

	VirtualFree(curr_callstack, 0, MEM_RELEASE);
}

void heap_destroy(heap_t* heap)
{
	tlsf_destroy(heap->tlsf);

	arena_t* arena = heap->arena;
	while (arena)
	{
		tlsf_walk_pool(arena->pool, leak_detection_walker, arena);  // STUDENT ADDED

		arena_t* next = arena->next;
		VirtualFree(arena, 0, MEM_RELEASE);
		arena = next;
	}

	VirtualFree(heap, 0, MEM_RELEASE);
}