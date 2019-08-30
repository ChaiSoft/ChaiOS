#include <rbtree.h>
#include <kstdio.h>
#include <liballoc.h>
#include <string.h>

#define END_ITERATOR ((iterator_t)nullptr)

class RBTree {
public:
	RBTree(key_compare_t comparison, get_key_t keyretrieve, allocator_t allocator, deallocator_t dealloc, size_t datasize, bool multimap)
		:m_comparison(comparison),
		m_keyretrieve(keyretrieve),
		m_datasize(datasize)
	{
		m_alloc = (allocator ? allocator : &kmalloc);
		m_dealloc = (dealloc ? dealloc : &kfree);
		m_root = nullptr;
		m_multimap = multimap;
		m_size = 0;
	}

	iterator_t insert(nodedata_t* data)
	{
		key_t key = m_keyretrieve(data);
		PRBNODE loc = internal_find(key);
		int comparisonv = 0;
		if (loc)
			comparisonv = m_comparison(get_key(loc), key);
		if (comparisonv == 0)
			return (iterator_t)loc;		//TODO: multimap
		else
		{
			//Insert
			if (loc == END_ITERATOR)
			{
				//No root
				m_root = create_node(nullptr, data);
				return (iterator_t)m_root;
			}
			//OK, we have a node to work with
			PRBNODE& nodept = (comparisonv < 0 ? loc->left : loc->right);
			nodept = create_node(loc, data);
			return (iterator_t)nodept;
		}

	}

	iterator_t find(key_t key)
	{
		PRBNODE nod = internal_find(key);
		if (m_comparison(get_key(nod), key) != 0)
			return END_ITERATOR;
		else
			return (iterator_t)nod;
	}

	iterator_t near(key_t key)
	{
		return (iterator_t)internal_find(key);
	}

	size_t size()
	{
		return m_size;
	}

	iterator_t begin()
	{
		PRBNODE loc = m_root;
		while (loc && loc->left)
		{
			loc = loc->left;
		}
		return (iterator_t)loc;
	}

	iterator_t rightmost()
	{
		PRBNODE loc = m_root;
		while (loc && loc->right)
		{
			loc = loc->right;
		}
		return (iterator_t)loc;
	}

	iterator_t end()
	{
		return END_ITERATOR;
	}

	iterator_t next(iterator_t it)
	{
		if (it == END_ITERATOR)
			return it;
		PRBNODE nod = (PRBNODE)it;
		if (nod->right)
		{
			nod = nod->right;
			while (nod->left)
				nod = nod->left;
			return (iterator_t)nod;
		}
		else
		{
			while (nod->parent)
			{
				if (nod == nod->parent->left)
				{
					return (iterator_t)nod->parent;
				}
				nod = nod->parent;
			}
			return END_ITERATOR;
		}
	}

	iterator_t prev(iterator_t it)
	{
		if (it == END_ITERATOR)
			return rightmost();

		PRBNODE nod = (PRBNODE)it;
		if (nod->left)
		{
			nod = nod->left;
			while (nod->right)
				nod = nod->right;
			return (iterator_t)nod;
		}
		else
		{
			while (nod->parent)
			{
				if (nod == nod->parent->right)
				{
					return (iterator_t)nod->parent;
				}
				nod = nod->parent;
			}
			return it;		//begin
		}
	}

	key_t get_key(iterator_t it)
	{
		return get_key((PRBNODE)it);
	}

	nodedata_t* get_value(iterator_t it)
	{
		return ((PRBNODE)it)->data;
	}

private:
	enum RED_BLACK_COLOUR {
		BLACK,
		RED
	};

	typedef struct _rbnode {
		struct _rbnode* left;
		struct _rbnode* right;
		struct _rbnode* parent;
		RED_BLACK_COLOUR colour;
		nodedata_t data[0];
	}RBNODE, *PRBNODE;

private:
	PRBNODE internal_find(key_t key)
	{
		PRBNODE current = m_root;
		while (current != END_ITERATOR)
		{
			int compar = m_comparison(get_key(current), key);
			if (compar == 0)
				return current;
			else if (compar > 0)
			{
				//current->key < key
				if (current->right)
					current = current->right;
				else
					break;
			}
			else
			{
				if (current->left)
					current = current->left;
				else
					break;
			}
		}
		return current;
	}

	PRBNODE create_node(PRBNODE parent, nodedata_t nodedata)
	{
		PRBNODE val = (PRBNODE)m_alloc(sizeof(RBNODE) + m_datasize);
		val->parent = parent;
		val->colour = BLACK;
		val->left = nullptr;
		val->right = nullptr;
		memcpy(val->data, nodedata, m_datasize);
		return val;
	}

	key_t get_key(PRBNODE node)
	{
		if (!node)
			return 0;
		return m_keyretrieve(node->data);
	}

private:
	const key_compare_t m_comparison;
	const get_key_t m_keyretrieve;
	const size_t m_datasize;
	allocator_t m_alloc;
	deallocator_t m_dealloc;
	PRBNODE m_root;
	bool m_multimap;
	size_t m_size;
};


EXTERN KCSTDLIB_FUNC rbtree_t create_rbtree(key_compare_t comparison, get_key_t keyretrieve, size_t datasize, allocator_t allocator, deallocator_t dealloc, uint8_t multimap)
{
	RBTree* tree = new RBTree(comparison, keyretrieve, allocator, dealloc ,datasize, multimap != 0);

	if (multimap != 0)
	{
		kprintf(u"Fatal error: multimaps not supported! %s\n", u"" __FILE__);
		while (1);
	}
	return (rbtree_t)tree;
}

EXTERN KCSTDLIB_FUNC iterator_t rbtree_near(rbtree_t tree, key_t key)
{
	return ((RBTree*)tree)->near(key);
}
	

EXTERN KCSTDLIB_FUNC iterator_t rbtree_insert(rbtree_t tree, nodedata_t* data)
{
	return ((RBTree*)tree)->insert(data);
}

EXTERN KCSTDLIB_FUNC iterator_t rbtree_find(rbtree_t tree, key_t key)
{
	return ((RBTree*)tree)->find(key);
}


EXTERN KCSTDLIB_FUNC size_t rbtree_size(rbtree_t tree)
{
	return ((RBTree*)tree)->size();
}

EXTERN KCSTDLIB_FUNC void rbtree_delete(rbtree_t tree, iterator_t it)
{

}

EXTERN KCSTDLIB_FUNC iterator_t rbtree_begin(rbtree_t tree)
{
	return ((RBTree*)tree)->begin();
}
EXTERN KCSTDLIB_FUNC iterator_t rbtree_end(rbtree_t tree)
{
	return ((RBTree*)tree)->end();
}

EXTERN KCSTDLIB_FUNC void rbtree_iterator_increment(rbtree_t tree, iterator_t* it)
{
	*it = ((RBTree*)tree)->next(*it);
}

EXTERN KCSTDLIB_FUNC void rbtree_iterator_decrement(rbtree_t tree, iterator_t* it)
{
	*it = ((RBTree*)tree)->prev(*it);
}

EXTERN KCSTDLIB_FUNC key_t rbtree_iterator_key(rbtree_t tree, iterator_t it)
{
	return ((RBTree*)tree)->get_key(it);
}
EXTERN KCSTDLIB_FUNC nodedata_t* rbtree_iterator_value(rbtree_t tree, iterator_t it)
{
	return ((RBTree*)tree)->get_value(it);
}

EXTERN KCSTDLIB_FUNC void destroy_rbtree(rbtree_t tree)
{

}