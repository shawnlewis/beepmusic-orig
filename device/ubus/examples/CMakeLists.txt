cmake_minimum_required(VERSION 2.6)

ADD_DEFINITIONS(-I..)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/..)

IF(ASAN)
    ADD_DEFINITIONS(-fsanitize=address)
ENDIF()

IF (BUILD_EXAMPLES)
	ADD_EXECUTABLE(server server.c)
	TARGET_LINK_LIBRARIES(server ubus ubox blobmsg_json)
    IF(ASAN)
      SET_TARGET_PROPERTIES(server PROPERTIES LINK_FLAGS -fsanitize=address)
    ENDIF()

	ADD_EXECUTABLE(client client.c)
	TARGET_LINK_LIBRARIES(client ubus ubox)
    IF(ASAN)
      SET_TARGET_PROPERTIES(server PROPERTIES LINK_FLAGS -fsanitize=address)
    ENDIF()
ENDIF()
