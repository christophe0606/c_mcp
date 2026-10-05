/* Internal intrusive AVL index: one startup allocation per registered entry,
 * no character-level nodes and no request-time allocations. */
#ifndef MCP_INDEX_H
#define MCP_INDEX_H
#include <string.h>

struct mcp_index {
    const char *key;
    void *value;
    struct mcp_index *left, *right;
    int height;
};

static int index_height(const struct mcp_index *node) { return node ? node->height : 0; }
static void index_update(struct mcp_index *node)
{
    int left = index_height(node->left), right = index_height(node->right);
    node->height = 1 + (left > right ? left : right);
}
static struct mcp_index *index_rotate_left(struct mcp_index *node)
{
    struct mcp_index *top = node->right;
    node->right = top->left; top->left = node;
    index_update(node); index_update(top); return top;
}
static struct mcp_index *index_rotate_right(struct mcp_index *node)
{
    struct mcp_index *top = node->left;
    node->left = top->right; top->right = node;
    index_update(node); index_update(top); return top;
}
static struct mcp_index *index_insert(struct mcp_index *root, struct mcp_index *entry)
{
    int balance;
    if (!root) return entry;
    if (strcmp(entry->key, root->key) < 0) root->left = index_insert(root->left, entry);
    else root->right = index_insert(root->right, entry);
    index_update(root);
    balance = index_height(root->left) - index_height(root->right);
    if (balance > 1) {
        if (strcmp(entry->key, root->left->key) > 0) root->left = index_rotate_left(root->left);
        return index_rotate_right(root);
    }
    if (balance < -1) {
        if (strcmp(entry->key, root->right->key) < 0) root->right = index_rotate_right(root->right);
        return index_rotate_left(root);
    }
    return root;
}
static void *index_find(const struct mcp_index *root, const char *key, size_t *steps)
{
    if (steps) *steps = 0;
    if (!key) return NULL;
    while (root) {
        int comparison = strcmp(key, root->key);
        if (steps) ++*steps;
        if (!comparison) return root->value;
        root = comparison < 0 ? root->left : root->right;
    }
    return NULL;
}
#endif
