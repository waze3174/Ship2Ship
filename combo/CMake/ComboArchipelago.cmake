# Ship2Ship + Archipelago: the client libraries (apclientpp over wswrap, websocketpp and asio).
#
# All header-only; they are downloaded at configure time and pinned to the commits junkx420's clients use
# (Updated-2S2H-Archipelago mm/CMake/Archipelago.cmake). Only combo/net/ComboArchipelago.cpp includes them, as the OBJECT
# library ship2ship_apclient that the launcher and the test harness link.
#
# SHIP2SHIP_AP_TLS (OFF by default) adds wss:// through OpenSSL; without it the client speaks plain ws:// only.
#
# Licenses: apclientpp MIT, wswrap MIT, websocketpp BSD-3-Clause, asio BSL-1.0, valijson BSD-2-Clause, OpenSSL Apache-2.0.
# They are listed in CREDITS.md.
include_guard(GLOBAL)
include(FetchContent)

# SHIP2SHIP_AP_TLS is declared in the top-level CMakeLists.txt, where the Windows build installs OpenSSL with vcpkg.

# The headers are used as they are: skip each project's own CMakeLists (old, or building tests).
set(_ap_no_build SOURCE_SUBDIR ship2ship-no-cmake)

FetchContent_Declare(ap_valijson GIT_REPOSITORY https://github.com/tristanpenman/valijson.git GIT_TAG v1.0.6 ${_ap_no_build})
FetchContent_Declare(ap_asio GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git GIT_TAG asio-1-30-2 ${_ap_no_build})
FetchContent_Declare(ap_websocketpp GIT_REPOSITORY https://github.com/zaphoyd/websocketpp.git
    GIT_TAG bce258674b2d544f981da4bf149244a76e8126c9 ${_ap_no_build})
FetchContent_Declare(ap_wswrap GIT_REPOSITORY https://github.com/black-sliver/wswrap.git
    GIT_TAG d0505e0ec53a26743f11051949a0dc66bcf44951 ${_ap_no_build})
FetchContent_Declare(ap_apclientpp GIT_REPOSITORY https://github.com/black-sliver/apclientpp.git
    GIT_TAG 7f33a3849887983378258c2fe8fc3887f687c430 ${_ap_no_build})
FetchContent_MakeAvailable(ap_valijson ap_asio ap_websocketpp ap_wswrap ap_apclientpp)

find_package(nlohmann_json REQUIRED)
find_package(ZLIB REQUIRED) # permessage-deflate, which Archipelago servers use

# The client's own translation unit, built once and linked into every consumer. All of its build settings are PRIVATE: they
# must not reach the launcher's other sources, which include windows.h in their own way.
add_library(ship2ship_apclient OBJECT ${CMAKE_CURRENT_LIST_DIR}/../net/ComboArchipelago.cpp)
target_include_directories(ship2ship_apclient SYSTEM PRIVATE
    ${ap_valijson_SOURCE_DIR}/include
    ${ap_asio_SOURCE_DIR}/asio/include
    ${ap_websocketpp_SOURCE_DIR}
    ${ap_wswrap_SOURCE_DIR}/include
    ${ap_apclientpp_SOURCE_DIR})
target_include_directories(ship2ship_apclient PRIVATE ${CMAKE_CURRENT_LIST_DIR}/.. ${CMAKE_CURRENT_LIST_DIR}/../net)
# asio standalone, and websocketpp on the C++11 standard library instead of Boost.
target_compile_definitions(ship2ship_apclient PRIVATE ASIO_STANDALONE _WEBSOCKETPP_CPP11_STL_)
target_link_libraries(ship2ship_apclient PUBLIC nlohmann_json::nlohmann_json ZLIB::ZLIB)

if(SHIP2SHIP_AP_TLS)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(ship2ship_apclient PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(ship2ship_apclient PRIVATE SHIP2SHIP_AP_TLS)
else()
    target_compile_definitions(ship2ship_apclient PRIVATE WSWRAP_NO_SSL)
endif()

if(WIN32)
    # asio wants the target Windows version; ws2_32 and mswsock are its sockets, crypt32 the certificate store wswrap reads.
    target_compile_definitions(ship2ship_apclient PRIVATE _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(ship2ship_apclient PUBLIC ws2_32 mswsock crypt32)
endif()
if(MSVC)
    # The whole template stack lives in one translation unit and overflows the default COFF section limit (C1128).
    target_compile_options(ship2ship_apclient PRIVATE /bigobj)
endif()
