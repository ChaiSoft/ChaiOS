#ifndef CHAIOS_REDBLACK_H
#define CHAIOS_REDBLACK_H
#include "kterm.h"
/*
template <class T> class iterator {
public:
	operator --() = 0;
	operator ++() = 0;
	operator *() = 0;
	operator ->() = 0;
	operator !=() = 0;
};
*/

template <class L, class R> struct pair {
	L first;
	R second;
};

template <class T> class less {
public:
	static int compare(const T& lhs, const T& rhs)
	{
		if (lhs == rhs)
			return 0;
		else if (lhs < rhs)
			return 1;
		else
			return -1;
	}
};

template <class K, class V, class Compare = less<K>> class RedBlackTree {
public:
	typedef void* node_t;
	typedef K key_type;
	typedef pair<K, V> value_type;
	typedef V mapped_type;
	RedBlackTree()
	{
		m_size = 0;
		root = nullptr;
	}
	~RedBlackTree()
	{

	}

	size_t size() { return m_size; }
	bool empty() { return m_size == 0; }

	mapped_type& operator[](const key_type& key) {
		node** node_ptr = &root;
		node* noded = *node_ptr;
		node* parent = nullptr;
		while (noded)
		{
			int compar = Compare::compare(noded->key, key);
			if (compar == 0)
			{
				return noded->value;
			}
			else if (compar == 1)
			{
				node_ptr = &noded->right;
				parent = noded;
				noded = noded->right;
			}
			else
			{
				node_ptr = &noded->left;
				parent = noded;
				noded = noded->left;
			}
		}
		//Not in the tree, create a new node
		node* newnode = new node;
		newnode->key = key;
		newnode->parent = parent;
		newnode->left = nullptr;
		newnode->right = nullptr;
		newnode->colour = 0;
		*node_ptr = newnode;
		++m_size;
		return newnode->value;
	}

private:
	 typedef struct _node {
		K key;
		V value;
		struct _node* parent;
		struct _node* left;
		struct _node* right;
		unsigned int colour;
	}node;

public:
	typedef class RedBlackIterator {
	public:
		RedBlackIterator& operator ++()
		{
			if (!nod)
				return *this;
			if (nod->right)
			{
				nod = nod->right;
				while (nod->left) { nod = nod->left; }
			}
			else
			{
				node* parent = nod->parent;
				node* cur = nod;
				while (parent && cur == parent->right) { cur = parent; parent = parent->parent; }
				nod = parent;
			}
			return *this;
		}
		RedBlackIterator& operator --()
		{
			if (!nod)
			{
				nod = root;
				node* cur = nod;
				while (nod)
				{
					cur = nod;
					nod = cur->right;
				}
				nod = cur;
				return *this;
			}
			if (nod->left)
			{
				nod = nod->left;
				while (nod->right) { nod = nod->right; }
			}
			else
			{
				node* parent = nod->parent;
				node* cur = nod;
				while (parent && cur == parent->left) { cur = parent; parent = parent->parent; }
				if(parent)
					nod = parent;	
			}
			return *this;
		}
		value_type operator *() {
			if (nod)
			{
				value_type ret;
				ret.first = nod->key;
				ret.second = nod->value;
				return ret;
			}
		}
		bool operator ==(const RedBlackIterator& rhs) {
			return rhs.nod == nod;
		}
		bool operator !=(const RedBlackIterator& rhs) {
			return rhs.nod != nod;
		}
	private:
		RedBlackIterator(node* tnod)
		{
			nod = tnod;
		}
		node* nod;
		friend class RedBlackTree<K,V>;
	}iterator;

	iterator begin() {
		node* curnode = nullptr;
		node* next = root;
		while (next)
		{
			curnode = next;
			next = curnode->left;
		}
		return iterator(curnode);
	}

	iterator end() {
		return iterator(nullptr);
	}

private:
	node* root = nullptr;
	size_t m_size = 0;
};

#endif
