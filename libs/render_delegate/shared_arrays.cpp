#include "shared_arrays.h"

ArrayHolder::BufferMapT ArrayHolder::_bufferMap;
std::mutex ArrayHolder::_bufferMapMutex;