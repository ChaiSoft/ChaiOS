#include "kterm.h"

static void(*puts_s)(const char16_t*) = nullptr;
void setKtermOutputProc(void(*putsp)(const char16_t*))
{
	puts_s = putsp;
}

void puts(const char16_t* s)
{
	puts_s(s);
}