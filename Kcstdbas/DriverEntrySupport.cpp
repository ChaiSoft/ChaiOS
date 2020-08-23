
extern "C" int CppDriverEntry(void* param);
extern bool CallConstructors();

//Weak LIB definition
extern "C" int DriverEntry(void* param)
{
	CallConstructors();
	return CppDriverEntry(param);
}