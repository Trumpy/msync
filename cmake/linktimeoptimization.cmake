include(CheckIPOSupported)

if((CMAKE_BUILD_TYPE MATCHES Release) OR (CMAKE_BUILD_TYPE MATCHES MinSizeRel))
	message("Testing for IPO support...")
	check_ipo_supported(RESULT ipo_is_supported OUTPUT error)
	if (ipo_is_supported)
		message(STATUS "IPO / LTO enabled!")
		set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
	else()
		message(STATUS "IPO / LTO not supported: <${error}>")
	endif()
endif()
