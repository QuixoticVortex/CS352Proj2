#include "lib-sfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

#define SHARED_MEM_SIZE 32768

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

node *find_file_node_fp(memory_layout* mem, FILE *fp) {
	node *cur = mem->resources;
	while(cur != NULL && cur->fp != fp) {
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
	init_process_node(process);
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

int resource_has_incoming_edges(memory_layout *mem, node *given_resource) {
	node *cur_process = mem->processes;
	while(cur_process != NULL) {
		node *cur_ptr = cur_process->out_edges;
		while(cur_ptr != NULL) {
			node *cur_resource = cur_ptr->data;
			if(cur_resource == given_resource) {
				return 1;
			}
			cur_ptr = cur_ptr->next;
		}

		cur_process = cur_process->next;
	}

	return 0;
}

void delete_resource_node(memory_layout *mem, node *given_resource) {
	node* cur_resource = mem->resources;
	node* prev_resource = NULL;
	while(cur_resource != NULL && cur_resource != given_resource) {
		prev_resource = cur_resource;
		cur_resource = cur_resource->next;
	}
	if(prev_resource != NULL) {
		prev_resource->next = cur_resource->next;
	}
	else {
		mem->resources = cur_resource->next;
	}

	free_node(mem, given_resource);
}

int cycle_recursive(node *cur) {
	// For outgoing edges
	cur->state = VISITED;
	node *cur_list_node = cur->out_edges;
	while(cur_list_node != NULL) {
		node *cur_node = cur_list_node->data;
		if(cur_node->state == UNVISITED) {
			// Visit that guy
			if(cycle_recursive(cur_node)) return 1;
		}
		else if(cur_node->state == VISITED) {
			// CYCLE
			return 1;
		}
		else if(cur_node->state == PROCESSED) {
			// No need to check
		}

		cur_list_node = cur_list_node->next;
	}
	cur->state = PROCESSED;
	return 0;
}

int cycle_exists(memory_layout *mem) {
	// Initialize nodes' status
	node *cur = mem->processes;
	while(cur != NULL) {
		cur->state = UNVISITED;
		cur = cur->next;
	}
	cur = mem->resources;
	while(cur != NULL) {
		cur->state = UNVISITED;
		cur = cur->next;
	}

	// Start recursion in a loop or two
	cur = mem->processes;
	while(cur != NULL) {
		if(cur->state == UNVISITED) {
			if(cycle_recursive(cur)) return 1;
		}

		cur = cur->next;
	}
	cur = mem->resources;
	while(cur != NULL) {
		if(cur->state == UNVISITED) {
			if(cycle_recursive(cur)) return 1;
		}

		cur = cur->next;
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
	if(resource == NULL || process == NULL) {
		mutex_unlock(memory);
		return NULL;
	}

	delete_out_edge(memory, process, resource);
	add_out_edge(memory, resource, process);

	// While a cycle exists
	while(cycle_exists(memory)) {
		// Convert back to claim edge
		delete_out_edge(memory, resource, process);
		add_out_edge(memory, process, resource);

		// Wait
		pthread_cond_wait(get_cycle_cond(memory), get_lock(memory));

		// Add edge back
		delete_out_edge(memory, process, resource);
		add_out_edge(memory, resource, process);
	}

	// Upon getting the lock and assuring no cycle, open the file
	FILE *res = fopen(path, mode);
	resource->fp = res;
	mutex_unlock(memory);

	return res;
}

/**
 *
 */
int sfs_fclose(FILE *fp) {
	mutex_lock(memory);
	// Find this process and file resource
	node *resource = find_file_node_fp(memory, fp);
	node *process = find_process_node(memory, getpid());
	if(resource == NULL || process == NULL) {
		mutex_unlock(memory);
		return 0;
	}
	//resource->fp = NULL;

	// Convert back to claim edge
	delete_out_edge(memory, resource, process);
	add_out_edge(memory, process, resource);
	// Close file
	int result = fclose(fp);
	// Broadcast conditional variable
	pthread_cond_broadcast(get_cycle_cond(memory));

	mutex_unlock(memory);
	return !result;
}


/**
 *
 */
int sfs_leave(int sys_key) {
	mutex_lock (memory);

	// Loop through our open files and close them
	// Remove this process from overall list
	node* cur_process = memory->processes;
	node* prev_process = NULL;
	while(cur_process != NULL && cur_process->pid != getpid()) {
		prev_process = cur_process;
		cur_process = cur_process->next;
	}

	if(prev_process != NULL) prev_process->next = cur_process->next;
	else memory->processes = cur_process->next;

	// For each resource
	node *cur_resource = memory->resources;
	while(cur_resource != NULL) {
		// For each outgoing edge
		// If we are the target
		if(cur_resource->out_edges != NULL && cur_resource->out_edges->data == cur_process) {
			// Close file and flip edge
			delete_out_edge(memory, cur_resource, cur_process);
			add_out_edge(memory, cur_process, cur_resource);

			// Close file
			fclose(cur_resource->fp);
		}
		cur_resource = cur_resource->next;
	}



	// Loop through this process's files and delete ones with no other users
	// For each outgoing edge
	node *cur_resource_list = cur_process->out_edges;
	while(cur_resource_list != NULL) {
		cur_resource = cur_resource_list->data;

		// If has an outgoing edge to someone else, go on
		if(cur_resource->out_edges == NULL) {
			// Otherwise, loop through all processes and search for outgoing edges to resource
			// If none are found, remove this resource
			if(!resource_has_incoming_edges(memory, cur_resource)) {
				// delete this node (remove from resource list and free it)
				delete_resource_node(memory, cur_resource);
			}
		}

		cur_resource_list = cur_resource_list->next;
	}

	// Remove this process's process node
	free_node(memory, cur_process);

	// Broadcast conditional variable
	pthread_cond_broadcast(get_cycle_cond(memory));

	mutex_unlock(memory);

	shmdt(shared_memory);

	// Update local stuff
	memory = NULL;
	shared_memory = NULL;
	segment_id = -1;
	return 1;
}

/**
 *
 */
int sfs_destroy(int sys_key) {
	// Close leftover files
	// For each resource, if it has outgoing edges, close that file
	segment_id = get_segment_id(sys_key);
	shared_memory = (char *) shmat(segment_id, NULL, 0);
	memory = (memory_layout *) shared_memory;

	node *cur_resource = memory->resources;
	while(cur_resource != NULL) {
		// For each outgoing edge
		if(cur_resource->out_edges != NULL) {
			// Close file
			fclose(cur_resource->fp);
		}
	}

	shmdt(shared_memory);
	shmctl(segment_id, IPC_RMID, NULL); // Remove the shared memory block forever
	return 1;
}

