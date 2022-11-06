#include "fs.h"

#include "event.h"
#include "heap.h"
#include "queue.h"
#include "thread.h"

#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "lz4/lz4.h"  // STUDEMT ADDED


typedef struct fs_t
{
	heap_t* heap;
	queue_t* file_queue;
	thread_t* file_thread;

	// Use a separate queue!
	queue_t* compression_queue;    // STUDENT ADDED
	thread_t* compression_thread;  // STUDENT ADDED
} fs_t;

typedef enum fs_work_op_t
{
	k_fs_work_op_read,
	k_fs_work_op_write,
} fs_work_op_t;

typedef struct fs_work_t
{
	heap_t* heap;
	fs_work_op_t op;
	char path[1024];
	bool null_terminate;
	bool use_compression;
	void* buffer;
	size_t size;
	event_t* done;
	int result;
} fs_work_t;

static int file_thread_func(void* user);
static int compression_thread_func(void* user);  // STUDENT ADDED

fs_t* fs_create(heap_t* heap, int queue_capacity)
{
	fs_t* fs = heap_alloc(heap, sizeof(fs_t), 8);
	fs->heap = heap;
	fs->file_queue = queue_create(heap, queue_capacity);
	fs->file_thread = thread_create(file_thread_func, fs);
	fs->compression_queue = queue_create(heap, queue_capacity);           // STUDENT ADDED
	fs->compression_thread = thread_create(compression_thread_func, fs);  // STUDENT ADDED
	return fs;
}

void fs_destroy(fs_t* fs)
{
	queue_push(fs->file_queue, NULL);
	thread_destroy(fs->file_thread);
	queue_destroy(fs->file_queue);
	queue_push(fs->compression_queue, NULL);  // STIDENT ADDED
	thread_destroy(fs->compression_thread);   // STIDENT ADDED
	queue_destroy(fs->compression_queue);     // STIDENT ADDED
	heap_free(fs->heap, fs);
}

fs_work_t* fs_read(fs_t* fs, const char* path, heap_t* heap, bool null_terminate, bool use_compression)
{
	fs_work_t* work = heap_alloc(fs->heap, sizeof(fs_work_t), 8);
	work->heap = heap;
	work->op = k_fs_work_op_read;
	strcpy_s(work->path, sizeof(work->path), path);
	work->buffer = NULL;
	work->size = 0;
	work->done = event_create();
	work->result = 0;
	work->null_terminate = null_terminate;
	work->use_compression = use_compression;
	queue_push(fs->file_queue, work);
	return work;
}

fs_work_t* fs_write(fs_t* fs, const char* path, const void* buffer, size_t size, bool use_compression)
{
	fs_work_t* work = heap_alloc(fs->heap, sizeof(fs_work_t), 8);
	work->heap = fs->heap;
	work->op = k_fs_work_op_write;
	strcpy_s(work->path, sizeof(work->path), path);
	work->buffer = (void*)buffer;
	work->size = size;
	work->done = event_create();
	work->result = 0;
	work->null_terminate = false;
	work->use_compression = use_compression;

	if (use_compression)
	{
		// HOMEWORK 2: Queue file write work on compression queue!
		queue_push(fs->compression_queue, work);

	}
	else
	{
		queue_push(fs->file_queue, work);
	}

	return work;
}

bool fs_work_is_done(fs_work_t* work)
{
	return work ? event_is_raised(work->done) : true;
}

void fs_work_wait(fs_work_t* work)
{
	if (work)
	{
		event_wait(work->done);
	}
}

int fs_work_get_result(fs_work_t* work)
{
	fs_work_wait(work);
	return work ? work->result : -1;
}

void* fs_work_get_buffer(fs_work_t* work)
{
	fs_work_wait(work);
	return work ? work->buffer : NULL;
}

size_t fs_work_get_size(fs_work_t* work)
{
	fs_work_wait(work);
	return work ? work->size : 0;
}

void fs_work_destroy(fs_work_t* work)
{
	if (work)
	{
		event_wait(work->done);
		event_destroy(work->done);
		heap_free(work->heap, work);
	}
}

static void file_read(fs_t* fs, fs_work_t* work)
{
	wchar_t wide_path[1024];
	if (MultiByteToWideChar(CP_UTF8, 0, work->path, -1, wide_path, sizeof(wide_path)) <= 0)
	{
		work->result = -1;
		event_signal(work->done);
		return;
	}

	HANDLE handle = CreateFile(wide_path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE)
	{
		work->result = GetLastError();
		event_signal(work->done);
		return;
	}

	if (!GetFileSizeEx(handle, (PLARGE_INTEGER)&work->size))
	{
		work->result = GetLastError();
		CloseHandle(handle);
		event_signal(work->done);
		return;
	}

	work->buffer = heap_alloc(work->heap, work->null_terminate ? work->size + 1 : work->size, 8);

	DWORD bytes_read = 0;
	if (!ReadFile(handle, work->buffer, (DWORD)work->size, &bytes_read, NULL))
	{
		work->result = GetLastError();
		CloseHandle(handle);
		event_signal(work->done);
		return;
	}

	work->size = bytes_read;
	if (work->null_terminate)
	{
		((char*)work->buffer)[bytes_read] = 0;
	}

	CloseHandle(handle);

	if (work->use_compression)
	{
		// HOMEWORK 2: Queue file read work on decompression queue!
		queue_push(fs->compression_queue, work);
	}
	else
	{
		event_signal(work->done);
	}
}

static void file_write(fs_work_t* work)
{
	wchar_t wide_path[1024];
	if (MultiByteToWideChar(CP_UTF8, 0, work->path, -1, wide_path, sizeof(wide_path)) <= 0)
	{
		work->result = -1;
		event_signal(work->done);
		return;
	}

	HANDLE handle = CreateFile(wide_path, GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE)
	{
		work->result = GetLastError();
		event_signal(work->done);
		return;
	}

	DWORD bytes_written = 0;
	if (!WriteFile(handle, work->buffer, (DWORD)work->size, &bytes_written, NULL))
	{
		work->result = GetLastError();
		CloseHandle(handle);
		event_signal(work->done);
		return;
	}

	work->size = bytes_written;

	CloseHandle(handle);

	event_signal(work->done);
}

static int file_thread_func(void* user)
{
	fs_t* fs = user;
	while (true)
	{
		fs_work_t* work = queue_pop(fs->file_queue);
		if (work == NULL)
		{
			break;
		}
		
		switch (work->op)
		{
		case k_fs_work_op_read:
			file_read(fs, work);
			break;
		case k_fs_work_op_write:
			file_write(work);
			break;
		}
	}
	return 0;
}

// START OF STUDENT CODE

// This code is largely based on the outline demonstrated in office hours on 2022-09-26.

static void decompress(fs_work_t* work) {
	// Putting a random giant number here that will not work if our buffer size needs to be really big. FIX THIS LATER!
	int dst_buffer_size = 1000000;
	void* dst_buffer = heap_alloc(work->heap, dst_buffer_size, 8);
	int decompressed_size = LZ4_decompress_safe(work->buffer, dst_buffer, (int)work->size, dst_buffer_size);

	work->buffer = dst_buffer;
	work->size = decompressed_size;

	event_signal(work->done);
}

static void compress(fs_t* fs, fs_work_t* work) {
	int dst_buffer_size = LZ4_compressBound((int)work->size);
	void* dst_buffer = heap_alloc(work->heap, dst_buffer_size, 8);
	int compressed_size = LZ4_compress_default(work->buffer, dst_buffer, (int)work->size, dst_buffer_size);

	work->buffer = dst_buffer;
	work->size = compressed_size;

	queue_push(fs->file_queue, work);
}

static int compression_thread_func(void* user)
{
	fs_t* fs = user;
	while (true)
	{
		fs_work_t* work = queue_pop(fs->compression_queue);
		if (work == NULL)
		{
			break;
		}
		switch (work->op)
		{
		case k_fs_work_op_read:
			decompress(work);
			break;
		case k_fs_work_op_write:
			compress(fs, work);
			break;
		}
	}
	return 0;
}

// END OF STUDENT CODE