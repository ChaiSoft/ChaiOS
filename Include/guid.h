#ifndef CHAIOS_GUID_H
#define CHAIOS_GUID_H

#include <stdint.h>
#include <endian.h>

#pragma pack(push, 1)
typedef struct _GUID {
	uint32_t  Data1;
	uint16_t  Data2;
	uint16_t  Data3;
	union {
		struct {
			uint16_be  Data4;
			uint8_t   Data5[6];
		};
		uint64_t comparison;
	};
} GUID;
#pragma pack(pop)

#define MKGUID(a, b, c, d, e0, e1, e2, e3, e4, e5) \
	{a, b, c, {{BE_PREPROCESSOR16(d), {e0, e1, e2, e3, e4, e5}}}}

#define NULL_GUID MKGUID(0, 0, 0, 0, 0, 0, 0, 0, 0, 0)

inline bool compare_guid(GUID* guid1, GUID* guid2)
{
	uint64_t* lhs = (uint64_t*)guid1;
	uint64_t* rhs = (uint64_t*)guid2;
	return (lhs[0] == rhs[0]) && (lhs[1] == rhs[1]);
}

#ifdef __cplusplus
inline bool operator == (GUID& lhs, GUID& rhs)
{
	return (lhs.Data1 == rhs.Data1) && (lhs.Data2 == rhs.Data2) && (lhs.Data3 == rhs.Data3) && (lhs.comparison == rhs.comparison);
}
inline bool operator != (GUID& lhs, GUID& rhs)
{
	return !(lhs == rhs);
}
#endif

#endif