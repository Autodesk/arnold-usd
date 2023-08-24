
# So we can use std::min and std::max, because windows headers are indirectly included by TF.
add_compile_definitions(NOMINMAX)

# Disable warning C4996 regarding fopen(), strcpy(), etc.
add_compile_definitions("_CRT_SECURE_NO_WARNINGS")

if (TBB_NO_EXPLICIT_LINKAGE)
    add_compile_definitions(__TBB_NO_IMPLICIT_LINKAGE=1)
endif ()

if (MSVC)
    if (MSVC_VERSION GREATER_EQUAL 1920)
        set(CMAKE_CXX_FLAGS "/Zc:inline- ${CMAKE_CXX_FLAGS}")
    endif ()
endif ()

# Warning removal (from the usd code USDREPO/cmake/defaults/msvcdefaults.cmake)

# truncation from 'double' to 'float' due to matrix and vector classes in `Gf`
add_compile_options("/wd4244" "/wd4305")

# conversion from size_t to int. While we don't want this enabled
# it's in the Python headers. So all the Python wrap code is affected.
add_compile_options("/wd4267")

# no definition for inline function
# this affects Glf only
add_compile_options("/wd4506")

# 'typedef ': ignored on left of '' when no variable is declared
# XXX:figure out why we need this
add_compile_options("/wd4091")

# c:\python27\include\pymath.h(22): warning C4273: 'round': inconsistent dll linkage 
# XXX:figure out real fix
add_compile_options("/wd4273")

# qualifier applied to function type has no meaning; ignored
# tbb/parallel_for_each.h
add_compile_options("/wd4180")

# '<<': result of 32-bit shift implicitly converted to 64 bits
# tbb/enumerable_thread_specific.h
add_compile_options("/wd4334")


# Enable multiprocessor builds.
add_compile_options("/MP")
add_compile_options("/Gm-")