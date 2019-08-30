#ifndef CHAIOS_KCSTDLIB_RBTREE_H
#define CHAIOS_KCSTDLIB_RBTREE_H

#include <kcstdlib.h>
#include <stdint.h>

typedef void* rbtree_t;
typedef void* nodedata_t;
typedef void* key_t;

typedef void* iterator_t;

//Return 0 for ==, 1 for lhs < rhs, -1 for lhs > rhs
typedef int(*key_compare_t)(key_t lhs, key_t rhs);
typedef key_t(*get_key_t)(nodedata_t* dataarea);
typedef void*(*allocator_t)(size_t size);
typedef void(*deallocator_t)(void* ptr);

EXTERN KCSTDLIB_FUNC rbtree_t create_rbtree(key_compare_t, get_key_t, size_t datasize, allocator_t allocator = 0, deallocator_t dealloc = 0, uint8_t multimap = 0);
EXTERN KCSTDLIB_FUNC iterator_t rbtree_insert(rbtree_t tree, nodedata_t* data);
EXTERN KCSTDLIB_FUNC iterator_t rbtree_find(rbtree_t tree, key_t key);
EXTERN KCSTDLIB_FUNC iterator_t rbtree_near(rbtree_t tree, key_t key);
EXTERN KCSTDLIB_FUNC void rbtree_delete(rbtree_t tree, iterator_t it);

EXTERN KCSTDLIB_FUNC iterator_t rbtree_begin(rbtree_t tree);
EXTERN KCSTDLIB_FUNC iterator_t rbtree_end(rbtree_t tree);

EXTERN KCSTDLIB_FUNC size_t rbtree_size(rbtree_t tree);

EXTERN KCSTDLIB_FUNC void rbtree_iterator_increment(rbtree_t tree, iterator_t* it);
EXTERN KCSTDLIB_FUNC void rbtree_iterator_decrement(rbtree_t tree, iterator_t* it);

EXTERN KCSTDLIB_FUNC key_t rbtree_iterator_key(rbtree_t tree, iterator_t it);
EXTERN KCSTDLIB_FUNC nodedata_t* rbtree_iterator_value(rbtree_t tree, iterator_t it);

EXTERN KCSTDLIB_FUNC void destroy_rbtree(rbtree_t tree);


#endif
