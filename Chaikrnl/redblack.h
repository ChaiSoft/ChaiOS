#ifndef CHAIOS_REDBLACK_H
#define CHAIOS_REDBLACK_H

#include <stdheaders.h>
#include <rbtree.h>
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

#pragma pack(push, 1)

template <class off_t> struct inplace_tree_offset {
	off_t left;
	off_t right;
	off_t parent;
	uint8_t colour;
};
#pragma pack(pop)

template <class L, class R> struct pair {
	L first;
	R second;
};

template<class T1, class T2>
class pair_proxy {
	pair<T1, T2> p;
public:
	pair_proxy(const pair<T1, T2>& p) : p(p) {}
	pair<T1, T2>* operator->() { return &p; }
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
		internal_tree = create_rbtree(&compare_thunk, &get_key, sizeof(value_type));
	}
	~RedBlackTree()
	{
		destroy_rbtree(internal_tree);
	}

	size_t size() { return rbtree_size(internal_tree); }
	bool empty() { return size() == 0; }

	mapped_type& operator[](const key_type& key) {
		iterator_t it = rbtree_find(internal_tree, (key_t)key);
		if (it == rbtree_end(internal_tree))
		{
			value_type val;
			val.first = key;
			it = rbtree_insert(internal_tree, (nodedata_t*)&val);
			return ((value_type*)rbtree_iterator_value(internal_tree, it))->second;
		}
	}
	void remove(const key_type& key)
	{
		rbtree_delete(internal_tree, rbtree_find(internal_tree, (key_t)key));
	}

private:

	static int compare_thunk(key_t lhs, key_t rhs)
	{
		return Compare::compare((key_type)lhs, (key_type)rhs);
	}

	static key_t get_key(nodedata_t* data)
	{
		value_type* val = (value_type*)data;
		return (key_t)val->first;
	}

public:
	typedef class RedBlackIterator {
	public:
		RedBlackIterator& operator ++()
		{
			rbtree_iterator_increment(m_parent, &m_iterator);
			return *this;
		}
		RedBlackIterator& operator --()
		{
			rbtree_iterator_decrement(m_parent, &m_iterator);
			return *this;
		}
		value_type operator *() {
			value_type val;
			val.first = (key_type)rbtree_iterator_key(m_parent, m_iterator);
			val.second = (mapped_type)*rbtree_iterator_value(m_parent, m_iterator);
		}
		pair_proxy<K, V> operator ->() {
			if (m_iterator)
			{
				value_type ret;
				ret.first = (key_type)rbtree_iterator_key(m_parent, m_iterator);
				ret.second = (mapped_type)*rbtree_iterator_value(m_parent, m_iterator);
				return pair_proxy<K, V>(ret);
			}
		}
		bool operator ==(const RedBlackIterator& rhs) {
			return rhs.m_iterator == m_iterator;
		}
		bool operator !=(const RedBlackIterator& rhs) {
			return rhs.m_iterator != m_iterator;
		}
	private:
		RedBlackIterator(iterator_t internalit, rbtree_t parent)
		{
			m_iterator = internalit;
			m_parent = parent;
		}
		iterator_t m_iterator;
		rbtree_t m_parent;
		friend class RedBlackTree<K, V, Compare>;
	}iterator;

	iterator begin() {
		return iterator(rbtree_begin(internal_tree), internal_tree);
	}

	iterator end() {
		return iterator(rbtree_end(internal_tree), internal_tree);
	}

	iterator near(const key_type& key)
	{
		return iterator(rbtree_near(internal_tree, (key_t)key), internal_tree);
	}

	iterator find(const key_type& k)
	{
		return iterator(rbtree_find(internal_tree, (key_t)k), internal_tree);
	}

private:
	rbtree_t internal_tree;
};

#if 0
template <class K, class V, class off_t, class Compare = less<K>> class InplaceRedBlackTree {
public:
	typedef void* node_t;
	typedef K key_type;
	typedef V mapped_type;
	typedef inplace_tree_offset<off_t> node;
	typedef K(*get_key_func)(V);
	typedef inplace_tree_offset<off_t>& (*get_node_func)(V);
	InplaceRedBlackTree(get_key_func get_key, get_node_func get_node)
	{
		m_size = 0;
		root = nullptr;
		m_keyf = get_key;
		m_nodef = get_node;
	}
	~InplaceRedBlackTree()
	{
	}

	size_t size() { return m_size; }
	bool empty() { return m_size == 0; }

	mapped_type operator[](const key_type& key) {
		mapped_type noder = root;
		while (noder)
		{
			int compar = Compare::compare(m_keyf(noder), key);
			if (compar == 0)
			{
				return noder;
			}
			else if (compar == 1)
			{
				noder = get_right(noder);
			}
			else
			{
				noder = get_left(noder);
			}
		}
		return nullptr;
	}

	void insert(mapped_type value) {
		const key_type key = m_keyf(value);
		if (!root)
		{
			root = value;
			node& nod = m_nodef(root);
			nod.left = nod.right = nod.parent = 0;
			nod.colour = COLOUR_BLACK;
		}
		else
		{
			mapped_type noded = root;
			bool left_entry = false;
			mapped_type prev = nullptr;
			while (noded)
			{
				prev = noded;
				node& noder = m_nodef(noded);
				int compar = Compare::compare(m_keyf(noded), key);
				if (compar == 0)
				{
					//Multimap insert, we just preserve the property <=
					left_entry = true;
					noded = get_left(noded);
				}
				else if (compar == 1)
				{
					left_entry = false;
					noded = get_right(noded);
				}
				else
				{
					left_entry = true;
					noded = get_left(noded);
				}
			}
			mapped_type temp = left_entry ? get_left(prev) : get_right(prev);
			left_entry ? set_left(prev, value) : set_right(prev, value);
			set_parent(value, prev);
			set_left(value, temp);
			if (temp != nullptr)
				set_parent(temp, value);
			set_right(value, nullptr);
			set_colour(value, COLOUR_RED);
			//Repair the tree
			insert_repair_tree(value);
		}
		++m_size;
	}

private:
	get_key_func m_keyf;
	get_node_func m_nodef;

	enum TREE_COLOURS {
		COLOUR_BLACK,
		COLOUR_RED
	};

	mapped_type get_left(mapped_type nodep)
	{
		return m_nodef(nodep).left == 0 ? nullptr : raw_offset<mapped_type>(nodep, m_nodef(nodep).left);
	}
	mapped_type get_right(mapped_type nodep)
	{
		return m_nodef(nodep).right == 0 ? nullptr : raw_offset<mapped_type>(nodep, m_nodef(nodep).right);
	}
	mapped_type get_parent(mapped_type nodep)
	{
		return m_nodef(nodep).parent == 0 ? nullptr : raw_offset<mapped_type>(nodep, m_nodef(nodep).parent);
	}
	TREE_COLOURS get_colour(mapped_type nodep)
	{
		if (!nodep)
			return COLOUR_BLACK;
		return (TREE_COLOURS)m_nodef(nodep).colour;
	}
	void set_colour(mapped_type nodep, TREE_COLOURS colour)
	{
		m_nodef(nodep).colour = colour;
	}
	void set_parent(mapped_type nodep, mapped_type parent)
	{
		parent == nullptr ? m_nodef(nodep).parent = 0 : m_nodef(nodep).parent = raw_diff(parent, nodep);
	}
	void set_left(mapped_type nodep, mapped_type left)
	{
		left == nullptr ? m_nodef(nodep).left = 0 : m_nodef(nodep).left = raw_diff(left, nodep);
	}
	void set_right(mapped_type nodep, mapped_type right)
	{
		right == nullptr ? m_nodef(nodep).right = 0 : m_nodef(nodep).right = raw_diff(right, nodep);
	}

	void insert_repair_tree(mapped_type tnode)
	{
		if (!tnode)
			return;
		mapped_type prev = get_parent(tnode);
		if (prev == nullptr)
		{
			set_colour(tnode, COLOUR_BLACK);
		}
		else if (get_colour(prev) == COLOUR_BLACK)
		{
			//Do nothing
		}
		else /*(get_colour(prev) == COLOUR_RED)*/
		{
			mapped_type parent_left = get_left(prev);
			mapped_type parent_right = get_right(prev);
			mapped_type grandparent = get_parent(prev);		//Must exist, as parent is red
			mapped_type gp_left = get_left(grandparent);
			mapped_type gp_right = get_right(grandparent);
			mapped_type uncle = nullptr;
			if (gp_left == prev)
				uncle = gp_right;
			else
				uncle = gp_left;
			if (get_colour(uncle) == COLOUR_RED)
			{
				//Uncle is not NULL
				set_colour(prev, COLOUR_BLACK);
				set_colour(uncle, COLOUR_BLACK);
				set_colour(grandparent, COLOUR_RED);
				insert_repair_tree(grandparent);
			}
			else
			{
				//Black uncle, red parent. Tree rotation
				if (tnode == parent_left && prev == gp_right)
				{
					rotate_right(prev);
					tnode = get_right(tnode);
				}
				else if (tnode == parent_right && prev == gp_left)
				{
					rotate_left(prev);
					tnode = get_left(tnode);
				}
				//Now do the main phase (unconditional)
				if (tnode == parent_left)
					rotate_right(grandparent);
				else
					rotate_left(grandparent);
				set_colour(prev, COLOUR_BLACK);
				set_colour(grandparent, COLOUR_RED);
			}
		}
	}
	void rotate_left(mapped_type nod)
	{
		mapped_type temp = get_right(nod);
		mapped_type par = get_parent(nod);
		set_right(nod, get_left(temp));
		set_left(temp, nod);
		set_parent(nod, temp);
		if (get_right(nod) != nullptr)
			set_parent(get_right(nod), nod);
		if (par != nullptr)
		{
			if (nod == get_left(par))
				set_left(par, temp);
			else
				set_right(par, temp);
		}
		set_parent(temp, par);
	}
	void rotate_right(mapped_type nod)
	{
		mapped_type temp = get_left(nod);
		mapped_type par = get_parent(nod);
		set_right(nod, get_right(temp));
		set_right(temp, nod);
		set_parent(nod, temp);
		if (get_left(nod) != nullptr)
			set_parent(get_left(nod), nod);
		if (par != nullptr)
		{
			if (nod == get_right(par))
				set_right(par, temp);
			else
				set_left(par, temp);
		}
		set_parent(temp, par);

	}
private:
	V root = nullptr;
	size_t m_size = 0;
};
#endif

#endif
