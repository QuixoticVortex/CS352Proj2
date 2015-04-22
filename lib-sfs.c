#include "lib-sfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

#define SHARED_MEM_SIZE 4096 // TODO - Update

int segment_id;
memory_layout *memory;
char *shared_memory;

// Private methods:
/**
 * Get the shared memory segment id corresponding to the given key
 */
int get_segment_id(int sys_key) {
	int id = shmget(sys_key, SHARED_MEM_SIZE, S_IRUSR|S_IWUSR|IPC_CREAT);
	return id;
}

// Shared memory manager:
memory_layout *shared_mem_init(char* mem_base) {
	memset(mem_base, 0, sizeof(memory_layout));
	memory_layout *layout = (memory_layout *) mem_base;
	layout->next_free = mem_base + sizeof(memory_layout);
	layout->open_nodes = NULL;
	layout->processes = NULL;
	layout->resources = NULL;

	pthread_mutexattr_init(&layout->mutexattr);
	pthread_mutexattr_setpshared(&layout->mutexattr, PTHREAD_PROCESS_SHARED);
	pthread_mutex_init(&layout->mutex, &layout->mutexattr);

	pthread_condattr_init(&layout->condattr);
	pthread_condattr_setpshared(&layout->condattr, PTHREAD_PROCESS_SHARED);
	pthread_cond_init(&layout->no_cycle, &layout->condattr);

	return layout;
}

pthread_mutex_t *get_lock(memory_layout* mem) {
	return &mem->mutex;
}

void mutex_lock(memory_layout* mem) {
	pthread_mutex_lock(get_lock(mem));
}

void mutex_unlock(memory_layout* mem) {
	pthread_mutex_unlock(get_lock(mem));
}

pthread_cond_t *get_cycle_cond(memory_layout* mem) {
	return &mem->no_cycle;
}

void reset_node(node *loc) {
	memset(loc, 0, sizeof(node));

	loc->in_edges = NULL;
	loc->out_edges = NULL;
	loc->next = NULL;
	loc->pid = 0;
	loc->state = UNVISITED;
	loc->data = NULL;
	loc->fp = NULL;
}

node *create_new_node(memory_layout* mem) {
	node *loc = NULL;

	if(mem->open_nodes != NULL) {
		loc = mem->open_nodes;
		mem->open_nodes = mem->open_nodes->next;
	}
	else {
		loc = (node *) mem->next_free;
		mem->next_free += sizeof(node);
	}

	reset_node(loc);

	return loc;
}

void free_node(memory_layout *mem, node *cur) {
	reset_node(cur);
	cur->next = mem->open_nodes;
	mem->open_nodes = cur;
}


// TODO - maybe actually update
void init_process_node(node *process) {
	process->pid = getpid();
}

node *find_process_node(memory_layout* mem, pid_t pid) {
	node *cur = mem->processes;
	while(cur != NULL && cur->pid != pid) {
		cur = cur->next;
	}
	return cur;
}

node *find_file_node(memory_layout* mem, char *name) {
	node *cur = mem->resources;
	while(cur != NULL && strcmp(cur->name, name) != 0) {
		cur = cur->next;
	}
	return cur;
}

node *find_or_create_file_node(memory_layout* mem, char *name) {
	node *cur = find_file_node(mem, name);

	if(cur == NULL) {
		cur = create_new_node(mem);
		cur->fp = NULL;
		strncpy(cur->name, name, sizeof(cur->name));
		cur->next = mem->resources;
		mem->resources = cur;
	}

	return cur;
}

node *create_process_node(memory_layout *mem) {
	node *process = create_new_node(mem);
	process->next = mem->processes;
	mem->processes = process;

	return process;
}

node *create_list_node(memory_layout *mem, node *data, node **head) {
	node *cur = create_new_node(mem);
	cur->next = *head;
	*head = cur;
	cur->data = data;
	return cur;
}

int delete_out_edge(memory_layout *mem, node *start, node *end) {
	node *cur = start->out_edges;
	node *prev = NULL;

	while(cur != NULL && cur->data != end) {
		prev = cur;
		cur = cur->next;
	}

	if(cur == NULL) return 1;

	if(prev == NULL) {
		start->out_edges = cur->next;
	}
	else {
		prev->next = cur->next;
	}

	free_node(mem, cur);

	return 1;
}

int add_out_edge(memory_layout *mem, node *start, node *end) {
	if(create_list_node(mem, end, &start->out_edges) != NULL) {
		return 1;
	}
	return 0;
}

// Public API implementation:
/**
 * Called once to initialize the shared memory data structures for this library.
 */
int sfs_init(int sys_key) {
	// Initialize shared memory
	int id = get_segment_id(sys_key);
	char *shared_mem = (char *) shmat(id, NULL, 0);
	shared_mem_init(shared_mem);
	shmdt(shared_mem);

	return 1;
}

/**
 * Called by each process to declare the files it will be accessing. Also initializes process specific data.
 */
int sfs_declare(int sys_key, int file_num, char *filenames[]) {
	// Initialize shared memory pointers
	segment_id = get_segment_id(sys_key);
	shared_memory = (char *) shmat(segment_id, NULL, 0);
	memory = (memory_layout *) shared_memory;

	mutex_lock(memory);

	// Create process node for this process
	node *process = create_process_node(memory);

	// Create or get resource nodes for all files
	int i;
	for(i = 0; i < file_num; i++) {
		char *name = filenames[i];
		node *resource = find_or_create_file_node(memory, name);

		// Record that this process has claim edges to the given files
		create_list_node(memory, resource, &process->out_edges);
	}

	mutex_unlock(memory);

	return 1;
}

/**
 *
 */
FILE *sfs_fopen(char *path, char *mode) {
	mutex_lock(memory);

	// Turn claim edge to assignment edge
	node *resource = find_file_node(memory, path);
	node *process = find_process_node(memory, getpid());
	if(resource == NULL || process == NULL) return NULL;

	delete_out_edge(memory, process, resource);
	add_out_edge(memory, resource, process);

	// Check for cycles

	// While a cycle exists
		// Convert back to claim edge
		// Wait
	// Upon getting the lock, open the file

	mutex_unlock(memory);

	return NULL;
}

/**
 *
 */
int sfs_fclose(FILE *fp) {
	mutex_lock(memory);

	// Find this process and file resource
	// Convert back to claim edge

	// Broadcast conditional variable

	mutex_unlock(memory);
	return 1;
}


/**
 *
 */
// TODO - why does this argument even exist?
int sfs_leave(int sys_key) {
	mutex_lock(memory);
	// Update the shared memory stuff.
	// Loop through this process's files and delete ones with no other users (remember to close if open)
	// Remove this process's process node

	// Update the local stuff.

	// Broadcast conditional variable

	mutex_unlock(memory);
	shmdt(shared_memory);

	memory = NULL;
	shared_memory = NULL;
	segment_id = -1;
	return 1;
}

/**
 *
 */
int sfs_destroy(int sys_key) {
	shmctl(get_segment_id(sys_key), IPC_RMID, NULL); //remove the shared memory block
	return 1;
}

