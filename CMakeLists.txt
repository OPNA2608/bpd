cmake_minimum_required (VERSION 3.10)
project (discord-bot-thingy VERSION 0.0.1 LANGUAGES C)

include (GNUInstallDirs)

# TODO detect concord
find_package(json-c CONFIG REQUIRED)

add_subdirectory (src)
