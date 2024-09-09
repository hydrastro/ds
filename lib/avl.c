#include "avl.h"
#include <stdlib.h>
#include <stdio.h>

static int avl_get_height(avl_node_t *node) {
    return node ? node->height : 0;
}

static int avl_get_balance(avl_node_t *node) {
    return node ? avl_get_height(node->left) - avl_get_height(node->right) : 0;
}

static void avl_update_height(avl_node_t *node) {
    int left_height, right_height;
    if (node) {
        left_height = avl_get_height(node->left);
        right_height = avl_get_height(node->right);
        if(left_height >= right_height) {
            node->height = 1 + left_height;
        } else {
            node->height = 1 + right_height;
        }
    }
}

static avl_node_t *avl_left_rotate(avl_node_t *node) {
    avl_node_t *right = node->right;
    node->right = right->left;
    if (right->left) {
        right->left->parent = node;
    }
    right->parent = node->parent;
    right->left = node;
    node->parent = right;
    avl_update_height(node);
    avl_update_height(right);
    return right;
}

static avl_node_t *avl_right_rotate(avl_node_t *node) {
    avl_node_t *left = node->left;
    node->left = left->right;
    if (left->right) {
        left->right->parent = node;
    }
    left->parent = node->parent;
    left->right = node;
    node->parent = left;
    avl_update_height(node);
    avl_update_height(left);
    return left;
}

static avl_node_t *avl_balance(avl_node_t *node) {
    avl_update_height(node);
    int balance = avl_get_balance(node);

    if (balance > 1) {
        if (avl_get_balance(node->left) < 0) {
            node->left = avl_left_rotate(node->left);
        }
        return avl_right_rotate(node);
    } else if (balance < -1) {
        if (avl_get_balance(node->right) > 0) {
            node->right = avl_right_rotate(node->right);
        }
        return avl_left_rotate(node);
    }
    return node;
}

avl_t *avl_create() {
    avl_t *tree = (avl_t *)malloc(sizeof(avl_t));
    tree->root = NULL;
#ifdef AVL_THREAD_SAFE
    LOCK_INIT(tree);
#endif
    return tree;
}

static avl_node_t *avl_new_node(void *data) {
    avl_node_t *node = (avl_node_t *)malloc(sizeof(avl_node_t));
    node->data = data;
    node->left = node->right = node->parent = NULL;
    node->height = 1;
    return node;
}

avl_node_t *avl_search(avl_t *tree, void *data, int (*compare)(void *, void *)) {
#ifdef AVL_THREAD_SAFE
    LOCK(tree)
#endif
    avl_node_t *node = tree->root;
    while (node) {
        int cmp = compare(data, node->data);
        if (cmp == 0) {
#ifdef AVL_THREAD_SAFE
            UNLOCK(tree)
#endif
            return node;
        } else if (cmp < 0) {
            node = node->left;
        } else {
            node = node->right;
        }
    }
#ifdef AVL_THREAD_SAFE
    UNLOCK(tree)
#endif
    return NULL;
}

static avl_node_t *avl_insert_node(avl_node_t *node, void *data, int (*compare)(void *, void *)) {
    if (!node) {
        return avl_new_node(data);
    }
    int cmp = compare(data, node->data);
    if (cmp < 0) {
        node->left = avl_insert_node(node->left, data, compare);
        node->left->parent = node;
    } else if (cmp > 0) {
        node->right = avl_insert_node(node->right, data, compare);
        node->right->parent = node;
    } else {
        return node;
    }
    return avl_balance(node);
}

void avl_insert(avl_t *tree, void *data, int (*compare)(void *, void *)) {
#ifdef AVL_THREAD_SAFE
    LOCK(tree)
#endif
    tree->root = avl_insert_node(tree->root, data, compare);
#ifdef AVL_THREAD_SAFE
    UNLOCK(tree)
#endif
}

static avl_node_t *avl_min_node(avl_node_t *node) {
    while (node->left) {
        node = node->left;
    }
    return node;
}

static avl_node_t *avl_delete_node(avl_node_t *root, void *data, int (*compare)(void *, void *)) {
    if (!root) {
        return NULL;
    }

    int cmp = compare(data, root->data);
    if (cmp < 0) {
        root->left = avl_delete_node(root->left, data, compare);
    } else if (cmp > 0) {
        root->right = avl_delete_node(root->right, data, compare);
    } else {
        if (!root->left || !root->right) {
            avl_node_t *temp = root->left ? root->left : root->right;
            if (!temp) {
                temp = root;
                root = NULL;
            } else {
                *root = *temp;
            }
            free(temp);
        } else {
            avl_node_t *temp = avl_min_node(root->right);
            root->data = temp->data;
            root->right = avl_delete_node(root->right, temp->data, compare);
        }
    }

    if (!root) return root;
    return avl_balance(root);
}

void avl_delete(avl_t *tree, void *data, int (*compare)(void *, void *)) {
#ifdef AVL_THREAD_SAFE
    LOCK(tree)
#endif
    tree->root = avl_delete_node(tree->root, data, compare);
#ifdef AVL_THREAD_SAFE
    UNLOCK(tree)
#endif
}

static void avl_destroy_node(avl_node_t *node, void (*destroy)(void *)) {
    if (node) {
        avl_destroy_node(node->left, destroy);
        avl_destroy_node(node->right, destroy);
        if (destroy) {
            destroy(node->data);
        }
        free(node);
    }
}

void avl_destroy_tree(avl_t *tree, void (*destroy)(void *)) {
#ifdef AVL_THREAD_SAFE
    LOCK(tree)
#endif
    avl_destroy_node(tree->root, destroy);
    tree->root = NULL;
#ifdef AVL_THREAD_SAFE
    if (tree->is_thread_safe) {
        pthread_mutex_unlock(&tree->lock);
        pthread_mutex_destroy(&tree->lock);
    }
#endif
    free(tree);
}

void avl_inorder_walk(avl_t *tree, avl_node_t *node, void (*callback)(void *)) {
    if (node) {
        avl_inorder_walk(tree, node->left, callback);
        callback(node->data);
        avl_inorder_walk(tree, node->right, callback);
    }
}

void avl_inorder_walk_tree(avl_t *tree, void (*callback)(void *)) {
#ifdef AVL_THREAD_SAFE
    LOCK(tree)
#endif
    avl_inorder_walk(tree, tree->root, callback);
#ifdef AVL_THREAD_SAFE
    UNLOCK(tree)
#endif
}
