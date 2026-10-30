cmake_minimum_required(VERSION 3.24)

project(Chronox VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
include(FetchContent)

FetchContent_Declare(sqlite3
    URL https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip
    URL_HASH SHA256=0a9c8e34352dad46006aa82836d5383bff9dc6bc9ce5a60553c05aa30e6ad1f9
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(sqlite3)

add_subdirectory(src)
