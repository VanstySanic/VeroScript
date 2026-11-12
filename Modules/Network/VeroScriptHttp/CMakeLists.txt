# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

add_library(VeroScriptHttp STATIC Private/HttpBackend.cpp Private/VeroScriptHttp.cpp
	Private/VeroScriptHttpSse.cpp Private/HttpSseClient.cpp Private/HttpClientHistory.cpp Private/HttpRouter.cpp Private/HttpMultipart.cpp Private/HttpDownload.cpp)
add_library(VeroScript::Http ALIAS VeroScriptHttp)
veroscript_network_target(VeroScriptHttp)
target_include_directories(VeroScriptHttp PRIVATE Internal)
set_target_properties(VeroScriptHttp PROPERTIES EXPORT_NAME Http)
install(TARGETS VeroScriptHttp EXPORT VeroScriptHttpTargets ARCHIVE DESTINATION lib COMPONENT VeroScriptHttp)
install(FILES Public/VeroScriptHttp.h Public/VeroScriptHttpBackend.h Public/VeroScriptHttpMultipart.h Public/VeroScriptHttpRouter.h Public/VeroScriptHttpClientOptions.h Public/VeroScriptHttpSse.h Public/VeroScriptHttpSseClient.h Public/VeroScriptHttpJson.h Public/VeroScriptHttpFiles.h
	DESTINATION include/VeroScriptHttp COMPONENT VeroScriptHttp)
install(DIRECTORY Resources/Examples DESTINATION share/VeroScriptHttp/Resources COMPONENT VeroScriptHttpExamples)
install(DIRECTORY Resources/Localization DESTINATION share/VeroScriptHttp/Resources COMPONENT VeroScriptHttp)
veroscript_network_binding(VeroScriptHttp)
target_link_libraries(VeroScriptHttp PUBLIC VeroScriptNetwork)

install(DIRECTORY Resources/Docs DESTINATION share/VeroScriptHttp/Resources COMPONENT VeroScriptHttp)

install(FILES Internal/HttpClientHistory.h DESTINATION include/VeroScriptHttp/Backend COMPONENT VeroScriptHttp)
