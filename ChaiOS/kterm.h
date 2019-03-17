#ifndef CHAIOS_KTERM_H
#define CHAIOS_KTERM_H

void setKtermOutputProc(void(*puts)(const char16_t*));
void puts(const char16_t* s);
void printf(const char16_t* format, ...);
void puts(const char* s);


#endif