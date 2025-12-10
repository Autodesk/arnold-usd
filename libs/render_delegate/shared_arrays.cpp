#include "shared_arrays.h"

#if SHARED_ARRAYS_USE_GLOBAL_MAP
ArrayHolder::BufferMapT ArrayHolder::_bufferMap;
std::mutex ArrayHolder::_bufferMapMutex;
#endif