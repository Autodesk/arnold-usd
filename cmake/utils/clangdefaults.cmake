
string(APPEND CMAKE_SHARED_LINKER_FLAGS " -Wl,-undefined,error")

add_compile_options($<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wno-deprecated>)