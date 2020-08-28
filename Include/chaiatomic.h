#ifndef CHAIOS_ATOMIC_H
#define CHAIOS_ATOMIC_H

#include <arch/cpu.h>

#ifdef __cplusplus

namespace std {
	template <class T> class atomic {
	};
	class atomic_size_t : public atomic<size_t>
	{
	public:
		atomic_size_t()
		{
			internalValue = 0;
		}
		size_t operator =(size_t value)
		{

		}
	private:
		size_t internalValue;
	};
};

#endif

#endif
