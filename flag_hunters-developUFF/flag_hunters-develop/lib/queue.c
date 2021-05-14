#include <stdio.h>
#include <stdlib.h>

#include "headers/queue.h"

node_t *enqueue(node_t *head, Node *val) {
    node_t *new_node = malloc(sizeof(node_t));
    if (!new_node) return head;

    new_node->val = *val;
    new_node->next = head;

    return new_node;
}

Node *dequeue(node_t **head) {
    node_t *current, *prev = NULL;
    Node *retval = NULL;

    if (*head == NULL) return NULL;

    current = *head;
    while (current->next != NULL) {
        prev = current;
        current = current->next;
    }

    retval = &current->val;
    free(current);
    
    if (prev)
        prev->next = NULL;
    else
        *head = NULL;

    return retval;
}

Node *best_node(node_t *head) {
    Node *curr = &(head->val);
    head = head->next;
    while (head) {
        if (curr->f > head->val.f)
            curr = &(head->val);
        head = head->next;
    }
    return curr;
}

node_t *remove_node(node_t *head, Node *node) {
    node_t *o = head;
    node_t *prec = NULL;
    if (!head) return NULL;
    while (head) {
        if (head->val.s.x == node->s.x && head->val.s.y == node->s.y) {
            if (prec)
                prec->next = head->next;
            else
                return NULL;
        }
        prec = head;
        head = head->next;
    }
    return o;
}

int exists(node_t *head, Node node) {
    while (head) {
        if (head->val.s.x == node.s.x && head->val.s.y == node.s.y)
            return 1;
        head = head->next;
    }
    return 0;
}

int exists_and_f_higher(node_t *head, Node node) {
    while (head) {
        if (head->val.s.x == node.s.x && head->val.s.y == node.s.y && node.f > head->val.f)
            return 1;
        head = head->next;
    }
    return 0;
}