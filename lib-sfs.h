#ifndef LIBSFS_H
#define LIBSFS_H
#include <stdlib.h>
#include <stdio.h>

#define MAX_NAME_LENGTH 150

int sfs_init(int sys_key);
int sfs_declare(int sys_key, int file_num, char *filenames[]);
FILE *sfs_fopen(char *path, char *mode);
int sfs_fclose(FILE *fp);
int sfs_leave(int sys_key);
int sfs_destroy(int sys_key);
struct node_t;

typedef enum {UNVISITED, VISITED, PROCESSED} state_t;

// Only one node type - used for all data structures
typedef struct node_t {
	// The state of this node in a running cycle detection algorithm
	state_t state;

	// A pointer to the next node of the same type as this node
	struct node_t *next;

	// A pointer to a linked list of outgoing edges
	struct node_t *out_edges;

	// A pointer to a linked list of incoming edges
	struct node_t *in_edges;

	// If this is a resource and it's currently open, a file pointer
	FILE *fp;

	// If this is a resource, the name of the file
	char name[MAX_NAME_LENGTH];

	// If this is a process, the pid of that process
	pid_t pid;

	// If this is a node in a linked list, the data of this element
	struct node_t *data;

} node;

typedef struct memory_manager_t {
	char *next_free;
	node *open_nodes;
	node *processes;
	node *resources;
	pthread_mutexattr_t mutexattr;
	pthread_mutex_t mutex;
	pthread_condattr_t condattr;
	pthread_cond_t no_cycle;
} memory_layout;

#endif
