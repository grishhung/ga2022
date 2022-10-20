#include "trace.h"

#include "mutex.h"          // STUDENT ADDED
#include "timer.h"          // STUDENT ADDED
#include "timer_object.h"   // STUDENT ADDED

#include <stddef.h>

#include <errno.h>          // STUDENT ADDED
#include <stdio.h>          // STUDENT ADDED
#include <string.h>         // STIDENT ADDED
#include <windows.h>        // STUDENT ADDED

#define MAX_NAME_LEN 64     // STUDENT ADDED
#define MAX_PATH_LEN 256    // STUDENT ADDED
#define MAX_JSON_LEN 65536  // STUDENT ADDED

#define HEADER_LEN 48       // STUDENT ADDED
#define FOOTER_LEN 6        // STUDENT ADDED

typedef struct trace_event_t {  // STUDENT ADDED
	char name[MAX_NAME_LEN];
	char ph;      // event type  // ('B' or 'E'; everything could honestly probably be reworked to make this a bool)
	int tid;      // thread id
	uint64_t ts;  // timestamp
} trace_event_t;

typedef struct trace_t {  // STUDENT ADDED
	heap_t* heap;
	int event_capacity;          // maxinum number of allowed events (100 in the homework test)

	int is_capturing;            // boolean value for whether tracing has begun or not
	trace_event_t* event_stack;  // Oops, not actually a stack; really just a list of events to put in the body
	int event_count;
	mutex_t* mutex;
	char path[MAX_PATH_LEN];
} trace_t;

trace_t* trace_create(heap_t* heap, int event_capacity)
{
	// START OF STUDENT CODE

	trace_t* trace = heap_alloc(heap, sizeof(trace_t), 8);

	trace->is_capturing = FALSE;

	trace->event_capacity = event_capacity;
	trace->heap = heap;

	trace->event_stack = heap_alloc(heap, sizeof(trace_event_t) * event_capacity, 8);
	trace->event_count = 0;

	trace->mutex = mutex_create();

	return trace;

	// END OF STUDENT CODE

	// return NULL;  // student commented out placeholder return value
}

void trace_destroy(trace_t* trace)
{
	return;
}

void populate_trace_event(trace_t* trace, const char* name, char ph)  // STUDENT ADDED
{
	if (trace->is_capturing)
	{
		// I should probably do something here if the event_capacity is exceeded...

		mutex_lock(trace->mutex);

		// TO DO: Use a <index, thread ID> hash map or something to assign thread IDs to 1, 2, etc. indices like in the example.
		//        ...Also, find a library for (or somehow implement) a hash map in C.

		int index = trace->event_count;

		strncpy_s(trace->event_stack[index].name, MAX_NAME_LEN, name, strlen(name));
		trace->event_stack[index].ph = ph;
		trace->event_stack[index].tid = GetCurrentThreadId();  // SEE TO DO
		trace->event_stack[index].ts = timer_get_ticks();

		trace->event_count++;

		mutex_unlock(trace->mutex);
	}
}

void trace_duration_push(trace_t* trace, const char* name)
{
	populate_trace_event(trace, name, 'B');  // STUDENT ADDED
}

void trace_duration_pop(trace_t* trace)
{
	// No name argument is provided, but it turns out that Chrome has all the info it needs to properly display
	// the graph, even without the name, so it's fine. (Quick solution would be to add the event at the top of the
	// stack and then pop the element, but currently my "stack" is really just a list of 'B' and 'E' events in
	// chronological order.)

	populate_trace_event(trace, "", 'E');  // STUDENT ADDED
}

void trace_capture_start(trace_t* trace, const char* path)
{
	// START OF STUDENT CODE

	trace->is_capturing = TRUE;

	strncpy_s(trace->path, MAX_PATH_LEN, path, (size_t) strlen(path));

	// END OF STUDENT CODE
}

void trace_capture_stop(trace_t* trace)
{
	// START OF STUDENT CODE

	trace->is_capturing = FALSE;

	// TO DO: Maybe make json an attr of trace_t and add the header during trace_capture_start() instead.
	//        Then form the events inside the body during push / pop and then form the footer during stop.
	//        The way this is currently done is sort of gross and the "stack" is really just a normal list.

	char json[MAX_JSON_LEN] = { '\0' };

	// Having a giant array on the stack like this is probably bad, because I'm getting a warning about it.

	strcat_s(json, MAX_JSON_LEN, "{\n\t\"displayTimeUnit\": \"ns\", \"traceEvents\": [\n");

	for (int i = 0; i < trace->event_count; ++i)
	{
		char ph_str[2] = { trace->event_stack[i].ph, '\0' };

		char tid_str[20];
		char ts_str[20];
		
		// Note to self: 10 is "radix" or base of the number.

		_itoa_s(trace->event_stack[i].tid, tid_str, 20, 10);
		_itoa_s((int) trace->event_stack[i].ts, ts_str, 20, 10);

		strcat_s(json, MAX_JSON_LEN, "\t\t{\"name\":\"");
		strcat_s(json, MAX_JSON_LEN, trace->event_stack[i].name);
		strcat_s(json, MAX_JSON_LEN, "\",\"ph\":\"");
		strcat_s(json, MAX_JSON_LEN, ph_str);
		strcat_s(json, MAX_JSON_LEN, "\",\"pid\":0,\"tid\":\"");
		strcat_s(json, MAX_JSON_LEN, tid_str);
		strcat_s(json, MAX_JSON_LEN, "\",\"ts\":\"");
		strcat_s(json, MAX_JSON_LEN, ts_str);
		strcat_s(json, MAX_JSON_LEN, "\"}");

		// Everything breaks if there's a "," after the last element of the list because JSON lists are dumb.

		if (i != trace->event_count - 1)
		{
			strcat_s(json, MAX_JSON_LEN, ",");
		}

		strcat_s(json, MAX_JSON_LEN, "\n");
	}

	strcat_s(json, MAX_JSON_LEN, "\t]\n}\n");

	// My HW2 file system writing assertions didn't all pass, so I'm going to write to file the old-fashioned way.

	FILE* fptr;

	fopen_s(&fptr, trace->path, "w");

	if (fptr == NULL)
	{
		printf("This file path does not exist.");
		exit(1);
	}

	fprintf(fptr, "%s", json);
	fclose(fptr);

	// END OF STUDENT CODE
}
