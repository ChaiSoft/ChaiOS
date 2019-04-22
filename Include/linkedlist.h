#ifndef CHAIOS_LINKED_LIST_H
#define CHAIOS_LINKED_LIST_H

#include <stdheaders.h>

#pragma pack(push, 1)
template <class T> struct linked_list_node {
	T prev;
	T next;
};
template <class off_t> struct linked_list_node_off {
	off_t prev;
	off_t next;
};
#pragma pack(pop)

template <class T> class LinkedList {
public:
	typedef linked_list_node<T>& (*node_converter)(T);
	LinkedList(node_converter converter = nullptr)
	{
		m_start = m_end = nullptr;
		m_conv = converter;
		m_length = 0;
	}
	void init(node_converter conv)
	{
		if (!m_conv)
			m_conv = conv;
	}
	void insert(T val)
	{
		set_next(val, nullptr);
		set_prev(val, m_end);
		set_next(m_end, val);
		m_end = val;
		if (!m_start)
			m_start = val;
		++m_length;
	}
	void join_existing_list(T start, T end, size_t length)
	{
		if (m_end != nullptr)
			set_next(m_end, start);
		else
			m_start = start;
		set_prev(start, m_end);
		m_end = end;
		set_next(end, nullptr);
		m_length += length;
	}
	T pop()
	{
		if (!m_start)
			return nullptr;
		T temp = m_start;
		m_start = get_next(m_start);
		if (!m_start)
			m_end = m_start;
		else
			set_prev(m_start, nullptr);
		set_next(temp, nullptr);
		--m_length;
		return temp;
	}
	void remove(T node)
	{
		if (!node)
			return;
		T prev = get_prev(node);
		T next = get_next(node);
		if (!prev)
			m_start = next;
		else
			set_next(prev, next);
		if (!next)
			m_end = prev;
		else
			set_prev(next, prev);
	}
	size_t length()
	{
		return m_length;
	}
	typedef class LLIterator {
	public:
		LLIterator& operator ++()
		{
			if (!nod)
				return *this;
			nod = parent->get_next(nod);
			return *this;
		}
		LLIterator& operator --()
		{
			if (!nod)
				nod = parent->m_end;
			else if (!parent->get_prev(nod))
				return *this;
			else
				nod = parent->get_prev(nod);
			return *this;
		}
		T operator *() {
			return nod;
		}
		T operator ->() {
			return nod;
		}
		bool operator ==(const LLIterator& rhs) {
			return rhs.nod == nod;
		}
		bool operator !=(const LLIterator& rhs) {
			return rhs.nod != nod;
		}
	private:
		LLIterator(T tnod, LinkedList<T>* par)
		{
			nod = tnod;
			parent = par;
		}
		T nod;
		LinkedList<T>* parent;
		friend class LinkedList<T>;
	} iterator;

	iterator begin()
	{
		LLIterator ret(m_start, this);
		return ret;
	}
	iterator end()
	{
		LLIterator ret(nullptr, this);
		return ret;
	}
private:
	T m_start;
	T m_end;
	size_t m_length;
	node_converter m_conv;
	T get_next(T present)
	{
		return m_conv(present).next;
	}
	T get_prev(T present)
	{
		return m_conv(present).prev;
	}
	void set_next(T present, T next)
	{
		m_conv(present).next = next;
	}
	void set_prev(T present, T prev)
	{
		m_conv(present).prev = prev;
	}
};

template <class T, class off_t> class LinkedListOff {
public:
	typedef linked_list_node_off<off_t>& (*node_converter)(T);
	LinkedListOff(node_converter converter = nullptr)
	{
		m_start = m_end = nullptr;
		m_conv = converter;
		m_length = 0;
	}
	void init(node_converter conv)
	{
		if (!m_conv)
			m_conv = conv;
	}
	void insert(T val)
	{
		set_next(val, nullptr);
		set_prev(val, m_end);
		set_next(m_end, val);
		m_end = val;
		if (!m_start)
			m_start = val;
		++m_length;
	}
	void join_existing_list(T start, T end, size_t length)
	{
		if (m_end != nullptr)
			set_next(m_end, start);
		else
			m_start = start;
		set_prev(start, m_end);
		m_end = end;
		set_next(end, nullptr);
		m_length += length;
	}
	T pop()
	{
		if (!m_start)
			return nullptr;
		T temp = m_start;
		m_start = get_next(m_start);
		if (!m_start)
			m_end = m_start;
		else
			set_prev(m_start, nullptr);
		set_next(temp, nullptr);
		--m_length;
		return temp;
	}
	void remove(T node)
	{
		if (!node)
			return;
		T prev = get_prev(node);
		T next = get_next(node);
		if (!prev)
			m_start = next;
		else
			set_next(prev, next);
		if (!next)
			m_end = prev;
		else
			set_prev(next, prev);
		--m_length;
	}
	size_t length()
	{
		return m_length;
	}
	typedef class LLIterator {
	public:
		LLIterator& operator ++()
		{
			if (!nod)
				return *this;
			nod = parent->get_next(nod);
			return *this;
		}
		LLIterator& operator --()
		{
			if (!nod)
				nod = parent->m_end;
			else if (!parent->get_prev(nod))
				return *this;
			else
				nod = parent->get_prev(nod);
			return *this;
		}
		T operator *() {
			return nod;
		}
		T operator ->() {
			return nod;
		}
		bool operator ==(const LLIterator& rhs) {
			return rhs.nod == nod;
		}
		bool operator !=(const LLIterator& rhs) {
			return rhs.nod != nod;
		}
	private:
		LLIterator(T tnod, LinkedList<T>* par)
		{
			nod = tnod;
			parent = par;
		}
		T nod;
		LinkedList<T>* parent;
		friend class LinkedList<T>;
	} iterator;

	iterator begin()
	{
		LLIterator ret(m_start, this);
		return ret;
	}
	iterator end()
	{
		LLIterator ret(nullptr, this);
		return ret;
	}
private:
	T m_start;
	T m_end;
	size_t m_length;
	node_converter m_conv;
	T get_next(T present)
	{
		off_t nextoff = m_conv(present).next;
		return nextoff == 0 ? nullptr : raw_offset<T>(present, nextoff);
	}
	T get_prev(T present)
	{
		off_t prevoff = m_conv(present).prev;
		return prevoff == 0 ? nullptr : raw_offset<T>(present, prevoff);
	}
	void set_next(T present, T next)
	{
		off_t nextoff = next == nullptr ? 0 : raw_diff(next, present);
		m_conv(present).next = nextoff;
	}
	void set_prev(T present, T prev)
	{
		off_t prevoff = prev == nullptr ? 0 : raw_diff(prev, present);
		m_conv(present).prev = prevoff;
	}
};

#endif
