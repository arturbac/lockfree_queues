cmake_minimum_required(VERSION 3.12)

project(ampi LANGUAGES CXX)

set(CMAKE_MODULE_PATH
  ${PROJECT_SOURCE_DIR} # For custom user project
  ${PROJECT_SOURCE_DIR}/cmake
)

include(CheckTypeSize)
include(CheckIncludeFile)
include(CheckIncludeFileCXX)
include(CMakeDetermineCompilerId)
include(CMakeDetermineSystem)
include(CMakeProjectUtilities)
include(CheckIncludeFiles)
include(CheckCXXSourceRuns)

option(CXX_ENABLE_SANITIZE "Enable sanitization" OFF)

find_package( Boost 1.58 COMPONENTS unit_test_framework system filesystem program_options thread )
find_package( Threads REQUIRED )
if(Boost_FOUND)
  message(" -- Found boost " ${Boost_VERSION})
  message("    : Includes " ${Boost_INCLUDE_DIRS})
  message("    : Libs     " ${Boost_LIBRARIES})

else()
  message(" -- boost not found")
endif()

if(CXX_ENABLE_SANITIZE)
  #clang
  check_cxx_accepts_flag_libs_add_res("-fsanitize=address" "asan" FSANITIZE_ADDRESS )
  check_cxx_accepts_flag_libs_add_res("-fsanitize=address -fsanitize-address-use-after-scope" "asan" FSANITIZE_ADDRESS_AFTER_SCOPE )
#   check_cxx_accepts_flag_add_res("-fsanitize=address" FSANITIZE_ADDRESS )
#   check_cxx_accepts_flag_add_res("-fsanitize=memory" FSANITIZE_memory )
#   check_cxx_accepts_flag_add_res("-fsanitize=thread" FSANITIZE_THREAD )
  check_cxx_accepts_flag_add_res("-fsanitize=undefined" FSANITIZE_UNDEFINED )
  check_cxx_accepts_flag_add_res("-fsanitize=shift" FSANITIZE_SHIFT )
  check_cxx_accepts_flag_add_res("-fsanitize=integer-divide-by-zero" FSANITIZE_DIVIDE_ZERO )
  check_cxx_accepts_flag_add_res("-fsanitize=unreachable" FSANITIZE_UNREACHABLE )
  check_cxx_accepts_flag_add_res("-fsanitize=vla-bound" FSANITIZE_VLA )
  check_cxx_accepts_flag_add_res("-fsanitize=null" FSANITIZE_NULL )
  check_cxx_accepts_flag_add_res("-fsanitize=return" FSANITIZE_RETURN )
  check_cxx_accepts_flag_add_res("-fsanitize=signed-integer-overflow" FSANITIZE_SIGNED_OVERFLOW )
  check_cxx_accepts_flag_add_res("-fsanitize-memory-track-origins" FSANITIZE_MEMORY_TRACK_ORIGINS )
  check_cxx_accepts_flag_add_res("-fsanitize=implicit-conversion" FSANITIZE_IMPLICIT_CONVERSION )
  # GCC  to permit conversions between vectors with differing element types or numbers of subparts
  check_cxx_accepts_flag_add_res("-flax-vector-conversions" FLAX_VECTOR_CONVERSIONS)
  
 set( ASAN_LIBRARIES  )
else(CXX_ENABLE_SANITIZE)
  if(ENABLE_HARDENDED_CODE)
    check_cxx_accepts_flag_add_res("-fstack-protector" FNO_STACK_PROTECTOR )
  else()
    check_cxx_accepts_flag_add_res("-fno-stack-protector" FNO_STACK_PROTECTOR )
  endif()
endif(CXX_ENABLE_SANITIZE)

set(CMAKE_C_FLAGS "-std=c11 ${CMAKE_C_FLAGS}")
check_cxx_accepts_flag_add_res("-std=c++17" CXX_17 )
if( NOT CXX_17)
  check_cxx_accepts_flag_add_res("-std=c++1z" CXX_1Z )
  if( NOT CXX_1Z )
    set(CMAKE_CXX_FLAGS "-std=c++14 ${CMAKE_CXX_FLAGS}")
  endif()
endif()

if(NOT (CMAKE_BUILD_TYPE STREQUAL "Debug") )
  check_cxx_accepts_flag_add_res("-mllvm -polly" CXX_LLVM_POLLY)
  if(CXX_LLVM_POLLY)
    check_cxx_accepts_flag_add_res("-mllvm -polly-vectorizer=stripmine" CXX_LLVM_POLLY_STRIP_MINE)
    check_cxx_accepts_flag_add_res("-mllvm -polly-parallel" CXX_LLVM_POLLY_PARALLEL)
  endif(CXX_LLVM_POLLY)
endif(NOT (CMAKE_BUILD_TYPE STREQUAL "Debug") )
check_cxx_accepts_flag_add_res("-fstack-protector-strong" CXX_STACK_PROTECTOR_STRONG )
#------------------------------------------------------------------------------------------------------

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=lld" )
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=lld" )
set(CMAKE_C_OUTPUT_EXTENSION ".bc")
set(CMAKE_STATIC_LIBRARY_SUFFIX ".bca")
if(ENABLE_THIN_LTO OR ((NOT (CMAKE_BUILD_TYPE STREQUAL "Debug") ) AND NOT ENABLE_SANITIZATION AND NOT ( CMAKE_SYSTEM_PROCESSOR STREQUAL "armv7-a" ) ) )
  check_cxx_accepts_flag_add_res("-flto" CXX_LTO )
  if(CXX_LTO)
    set(ENABLE_THIN_LTO TRUE )
    set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -flto ")
  else(CXX_LTO)
    check_cxx_accepts_flag_add_res("-flto=thin" CXX_LTO_THIN )
    if(CXX_LTO_THIN)
        set(ENABLE_THIN_LTO TRUE )
        check_cxx_accepts_flag_add_res("-Wl,--lto-O3" CXX__LINKER__LTO_O3 ) #Optimization level for LTO
    endif()
  endif(CXX_LTO)
  if( CXX_LTO OR CXX_LTO_THIN )
    check_cxx_accepts_flag_add_res("-Wl,--lto-O3" CXX__LINKER__LTO_O3 ) #Optimization level for LTO
  endif( CXX_LTO OR CXX_LTO_THIN )
endif()
#------------------------------------------------------------------------------------------------------

check_cxx_accepts_flag_add_res("-fuse-ld=lld" HOST_SUPPORTS_LLVM_LINKER )
if(HOST_SUPPORTS_LLVM_LINKER)
  set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -fuse-ld=lld ")
  
  if(NOT (CMAKE_BUILD_TYPE STREQUAL "Debug") )
    check_cxx_accepts_flag_add_res("-flto" CXX_LTO )
    if(CXX_LTO)
        set(ENABLE_THIN_LTO TRUE )
        set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -flto")
    endif(CXX_LTO)
  endif()
  set(CMAKE_REQUIRED_FLAGS)
  #-Wl,-cache_path_lto,${PROJECT_BINARY_DIR}/lto_cache
endif(HOST_SUPPORTS_LLVM_LINKER)

check_cxx_accepts_flag_add_res("-ggdb" CXX_GDB )

check_cxx_accepts_flag_add_res("-fcolor-diagnostics" CXX_COLOR_DIAGNOSTICS )
check_cxx_accepts_flag_add_res("-fdiagnostics-show-category=name" CXX_DIAGNOSTICS_SHOW_CATEGORY_NAME )
check_cxx_accepts_flag_add_res("-fdiagnostics-show-template-tree" CXX_DIAGNOSTICS_SHOW_TEMPLATE_TREE )
check_cxx_accepts_flag_add_res("-Wall" CXX_WALL)
check_cxx_accepts_flag_add_res("-Weverything" CXX_WEVERYTHING)
check_cxx_accepts_flag_add_res("-Werror=return-type" CXX_ERROR_RETURN_TYPE )
if( CXX_WEVERYTHING )
  check_cxx_accepts_flag_add_res("-Wno-c++98-compat-pedantic" CXX_WNOC98COMPATPEDANTIC)
  check_cxx_accepts_flag_add_res("-Wno-reserved-id-macro" CXX_WRESERVEDIDMACRO)
  check_cxx_accepts_flag_add_res("-Wno-c++98-compat" CXX_WC98COMPAT)
  check_cxx_accepts_flag_add_res("-Wno-documentation" CXX_WDOCUMENTATION )
  check_cxx_accepts_flag_add_res("-Wno-documentation-unknown-command" CXX_WDOCUMENTATIONUNKNOWNCOMMAND )
  check_cxx_accepts_flag_add_res("-Wno-old-style-cast" CXX_NOOLDSTYLECAST)
  check_cxx_accepts_flag_add_res("-Wno-undefined-func-template" CXX_WNO_UNDEFINEDFUNCTEMPLATE )
  check_cxx_accepts_flag_add_res("-Wno-padded" CXX_PADDED )
  check_cxx_accepts_flag_add_res("-Wno-sign-conversion" CXX_WNOSIGNCONVERSION )
  check_cxx_accepts_flag_add_res("-Wno-shorten-64-to-32" CXX_WNOSHORTEN64TO32)
  check_cxx_accepts_flag_add_res("-Wno-implicit-int-conversion" CXX_WNOIMPLICITINTCONVERSION)
  check_cxx_accepts_flag_add_res("-Wno-switch-enum" CXX_WNOSWITCHENUM )
  check_cxx_accepts_flag_add_res("-Wno-date-time" CXX_NODATETIME)
  check_cxx_accepts_flag_add_res("-Wno-float-equal" CXX_WNOFLOATEQUAL )
  check_cxx_accepts_flag_add_res("-Wno-unused-command-line-argument" CXXUNUSEDCOMMANDLINEARGUMENT)
endif()
#--------------------------------------------------------------------------------------
# unit tests 
add_executable(lockfree_ut)
target_sources(lockfree_ut PRIVATE lockfree.cc )

target_link_libraries( lockfree_ut
PRIVATE
  Boost::unit_test_framework
  ${CMAKE_THREAD_LIBS_INIT}
  )

target_compile_options( lockfree_ut
PRIVATE
  -Wno-disabled-macro-expansion;
  -Wno-global-constructors 
  )
#--------------------------------------------------------------------------------------

add_executable( lockfree_fifo_wild )
target_sources( lockfree_fifo_wild PRIVATE lockfree_fifo_wild.cc )
target_link_libraries( lockfree_fifo_wild
PRIVATE
  Boost::unit_test_framework
  ${CMAKE_THREAD_LIBS_INIT}
  )

target_compile_options( lockfree_fifo_wild
PRIVATE
  -Wno-disabled-macro-expansion;
  -Wno-global-constructors 
  )

# install(TARGETS lockfree_ut RUNTIME DESTINATION bin)
