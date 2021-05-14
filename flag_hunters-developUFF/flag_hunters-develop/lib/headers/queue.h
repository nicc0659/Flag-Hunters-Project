#ifndef __QUEUE_H_
#define __QUEUE_H_
    #include "models.h"

    typedef struct Node { 
        struct Node *parent;
        Square s; // Coordinates
        int g;
        int h;
        int f;
    } Node;

    typedef struct node_t {
        Node val;
        struct node_t *next;
    } node_t;

    node_t *enqueue(node_t *, Node *);
    Node *dequeue(node_t **);
    Node *best_node(node_t *);
    node_t *remove_node(node_t *, Node *);
    int exists(node_t *, Node);
    int exists_and_f_higher(node_t *, Node);
#endif