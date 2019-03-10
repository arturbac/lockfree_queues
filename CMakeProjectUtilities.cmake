#
# A big shout out to the cmake gurus @ compiz
#
message ("---- UTILITIES ------" )
include(CheckCXXSourceCompiles)
include(CheckCXXCompilerFlag)

#------------------------------------------------------------------------------------------

 string (ASCII 27 _escape)
function (colormsg)

  set(WHITE "29")
  set(GRAY "30")
  set(RED "31")
  set(GREEN "32")
  set(YELLOW "33")
  set(BLUE "34")
  set(MAG "35")
  set(CYAN "36")

  foreach (color WHITE GRAY RED GREEN YELLOW BLUE MAG CYAN)
    set(HI${color} "1\;${${color}}")
    set(LO${color} "2\;${${color}}")
    set(_${color}_ "4\;${${color}}")
    set(_HI${color}_ "1\;4\;${${color}}")
    set(_LO${color}_ "2\;4\;${${color}}")
  endforeach()

  set(str "")
  set(coloron FALSE)
  foreach(arg ${ARGV})
    if (NOT ${${arg}} STREQUAL "")
      if (CMAKE_COLOR_MAKEFILE)
        set(str "${str}${_escape}[${${arg}}m")
        set(coloron TRUE)
      endif()
    else()
      set(str "${str}${arg}")
      if (coloron)
        set(str "${str}${_escape}[0m")
        set(coloron FALSE)
      endif()
      set(str "${str} ")
    endif()
  endforeach()
  message(STATUS ${str})
endfunction()

function( ConfigureRc DESC PROD FV V RC_FILE )
   set( DESCRIPTION ${DESC} )
   set( PRODUCT_NAME ${PROD} )
   set( FILE_VERSION ${FV} )
   set( LIBRARY_FILE_NAME ${PRODUCT_NAME}.${FILE_VERSION} )
   if(WIN32)
      configure_file( ${CMAKE_SOURCE_DIR}/cmake/version.rc.cmake ${RC_FILE} )
   endif()
endfunction()

#------------------------------------------------------------------------------------------

# Helper macro to control the debugging output globally. There are
# two versions for controlling how verbose your output should be.
MACRO(_DBG_MSG _MSG)
  MESSAGE(STATUS
    "${CMAKE_CURRENT_LIST_FILE}(${CMAKE_CURRENT_LIST_LINE}): ${_MSG}")
ENDMACRO(_DBG_MSG)
#------------------------------------------------------------------------------------------

MACRO(_DBG_MSG_V _MSG)
 MESSAGE(STATUS
    "${CMAKE_CURRENT_LIST_FILE}(${CMAKE_CURRENT_LIST_LINE}): ${_MSG}")
ENDMACRO(_DBG_MSG_V)

#------------------------------------------------------------------------------------------

#
# Helper macro to append space separeated value to string variable
# similar to LIST(APPEND ...) but uses strings+space instead of semicolon+list
# AUTHOR Jan Woetzel
MACRO(STRING_APPEND  _VAR _VALUE )
  IF (${_VAR})
    # not empty, add space and value
    SET(${_VAR} "${${_VAR}} ${_VALUE}")
  ELSE(${_VAR})
    # empty, no space required.
    SET(${_VAR} "${_VALUE}")
  ENDIF (${_VAR})
ENDMACRO(STRING_APPEND)

#------------------------------------------------------------------------------------------

macro(print_option_info _VALUE _MESSAGE )

	if(${_VALUE})
		message( STATUS "Enabling ${_escape}[0;32;40m${_MESSAGE} ${_escape}[0m")
	else(${_VALUE})
		message( STATUS "Disabling ${_escape}[1;33;40m ${_MESSAGE} ${_escape}[0m")
	endif(${_VALUE})

endmacro(print_option_info)

#------------------------------------------------------------------------------------------

macro(print_option_info_neg _VALUE _MESSAGE )

	if(NOT ${_VALUE})
		message( STATUS  "Enabling ${_escape}[0;32;40m ${_MESSAGE} ${_escape}[0m")
	else()
		message( STATUS "Disabling ${_escape}[1;33;40m ${_MESSAGE} ${_escape}[0m")
	endif()

endmacro(print_option_info_neg)

#------------------------------------------------------------------------------------------

macro(add_definitions_if _VALUE _DEFINITION )

	if(${_VALUE})
		message( STATUS "Adding deffinition [${_escape}[0;32;40m${_DEFINITION}${_escape}[0m]")
		add_definitions( ${_DEFINITION} )
	else()
		message( STATUS "Skipping deffinition [${_escape}[1;31;40m${_DEFINITION}${_escape}[0m]")
	endif(${_VALUE})
endmacro(add_definitions_if)

#------------------------------------------------------------------------------------------

macro(add_definitions_ifv _VALUE _DEFINITION )
	if(${_VALUE})
		message( STATUS "Adding deffinition [${_escape}[1;33;40m${_DEFINITION}=1${_escape}[0m]")
		add_definitions( ${_DEFINITION}=1 )
	else()
		add_definitions( ${_DEFINITION}=0 )
		message( STATUS "Adding deffinition [${_escape}[1;33;40m${_DEFINITION}=0${_escape}[0m]")
	endif(${_VALUE})
endmacro(add_definitions_ifv)

#------------------------------------------------------------------------------------------

macro(check_cxx_accepts_flag_colour _FLAG _RESULT)
  set(CMAKE_REQUIRED_QUIET TRUE )
  check_cxx_compiler_flag(${_FLAG} ${_RESULT} )
  set(CMAKE_REQUIRED_QUIET)
  if(${_RESULT})
    message( STATUS "CXX compiler accepts flag ${_escape}[0;29;40m ${_FLAG} -${_escape}[0;32;40m yes${_escape}[0m")
    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
      "Determining if the CXX compiler accepts the flag ${_FLAG} passed with "
      "the following output:\n${OUTPUT}\n\n")
  else(${_RESULT})
    message(STATUS "CXX compiler accepts flag ${_escape}[0;29;40m ${_FLAG} - ${_escape}[5;31;40m - no${_escape}[0m")
    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
      "Determining if the CXX compiler accepts the flag ${_FLAG} failed with "
      "the following output:\n${OUTPUT}\n\n")
  endif(${_RESULT})
endmacro()

#------------------------------------------------------------------------------------------

macro (check_cxx_source_compiles_colour _CODE _MSG _RESULT)
  set(CMAKE_REQUIRED_QUIET TRUE )
  check_cxx_source_compiles("${_CODE}" ${_RESULT} 
    FAIL_REGEX "unrecognized .*option"                     # GNU
    FAIL_REGEX "unknown .*option"                          # Clang 
    FAIL_REGEX "ignoring unknown option"                   # MSVC
    FAIL_REGEX "warning D9002"                             # MSVC, any lang
  )
  set(CMAKE_REQUIRED_QUIET)
  
  if(${_RESULT})
    message(STATUS "CXX compiler compiles ${_MSG} - ${_escape}[0;32;40m yes${_escape}[0m")
    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
      "Determining if the CXX compiler compiles ${_MSG} passed with "
      "the following output:\n${OUTPUT}\n\n")
  else()
    message(STATUS "CXX compiler compiles ${_MSG} - ${_escape}[5;31;40m - no${_escape}[0m")
    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
      "Determining if the CXX compiler compiles ${_MSG} failed with "
      "the following output:\n${OUTPUT}\n\n")
  endif()
endmacro()
#------------------------------------------------------------------------------------------

macro (check_cxx_source_runs_colour _CODE _MSG _RESULT)
  set(CMAKE_REQUIRED_QUIET TRUE )
  check_cxx_source_runs("${_CODE}" ${_RESULT} )
  set(CMAKE_REQUIRED_QUIET)
  if(${_RESULT})
    message(STATUS "CXX runs ${_MSG} - ${_escape}[0;32;40m yes${_escape}[0m")
    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
      "Determining if runs CXX code ${_MSG} passed with "
      "the following output:\n${OUTPUT}\n\n")
  else()
    message(STATUS "runs CXX code ${_MSG} - ${_escape}[5;31;40m - no${_escape}[0m")
    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
      "Determining if runs CXX code ${_MSG} failed with "
      "the following output:\n${OUTPUT}\n\n")
  endif()
endmacro()
#------------------------------------------------------------------------------------------
   
macro( check_cxx_accepts_flag_add _FLAG )
	set( ACCEPTS_FLAG_ADD FALSE )
	check_cxx_accepts_flag_colour( "${_FLAG}" ${ACCEPTS_FLAG_ADD})
	if( ${ACCEPTS_FLAG_ADD} )
		set( CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} ${_FLAG} ")
	endif( ${ACCEPTS_FLAG_ADD} )
	unset( ACCEPTS_FLAG_ADD CACHE)
	unset( ACCEPTS_FLAG_ADD )
endmacro( check_cxx_accepts_flag_add )

#------------------------------------------------------------------------------------------
macro( check_cxx_accepts_flag_add_res _FLAG VARIABLE )

	CHECK_CXX_ACCEPTS_FLAG_COLOUR( "${_FLAG}" ${VARIABLE})
	if(${VARIABLE})
		set( CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} ${_FLAG} ")
	endif(${VARIABLE})
endmacro( )

#------------------------------------------------------------------------------------------
macro( check_cxx_accepts_flag_add_if _VALUE _FLAG )
  if(${_VALUE})
    check_cxx_accepts_flag_add( "${_FLAG}" )
  endif(${_VALUE})
endmacro()

#------------------------------------------------------------------------------------------

macro( check_cxx_accepts_flag_add_if_res _VALUE _FLAG VARIABLE )
  if(${_VALUE})
    check_cxx_accepts_flag_add_res( ${_FLAG} ${VARIABLE} )
  endif(${_VALUE})
endmacro()

#------------------------------------------------------------------------------------------

macro ( add_target_qt_definitions TARGET_ )
  target_compile_definitions( ${TARGET_} PUBLIC -DBUILD_QT_BACKEND=1 )
#   add_target_definitions( ${TARGET_} "-DBUILD_QT_BACKEND=1" )
endmacro()

#------------------------------------------------------------------------------------------

macro ( add_target_wx_definitions TARGET_ )
  target_compile_definitions( ${TARGET_} PUBLIC -DBUILD_WX_BACKEND=1 )
endmacro()

#------------------------------------------------------------------------------------------

macro ( add_target_boost_definitions TARGET_ )
  target_compile_definitions( ${TARGET_} PUBLIC -DBUILD_BOOST_BACKEND=1 )
endmacro()

#------------------------------------------------------------------------------------------

macro( check_cxx_accepts_flag_libs_add_res _FLAG _LIBS VARIABLE )
# 	check_cxx_accepts_flag_colour_libs( "${_FLAG}" "${_LIBS}" ${VARIABLE})
	set(CMAKE_REQUIRED_LIBRARIES " ${_LIBS} " )
	check_cxx_accepts_flag_colour( "${_FLAG}" ${VARIABLE})
	set(CMAKE_REQUIRED_LIBRARIES)
	
	if(${VARIABLE})
		set( CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} ${_FLAG} ")
	endif(${VARIABLE})
endmacro( )
