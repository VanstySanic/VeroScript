# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

# Deploy the SDK's app-local dependencies alongside the consumer, including
# resources and redistribution notices that cannot be inferred from PE imports.
foreach(_directory IN ITEMS VEROSCRIPT_SDK_RUNTIME_DIR VEROSCRIPT_CONSUMER_DIR)
	if(NOT DEFINED ${_directory} OR NOT IS_DIRECTORY "${${_directory}}")
		message(FATAL_ERROR "Missing runtime deployment directory: ${_directory}")
	endif()
endforeach()
file(REAL_PATH "${VEROSCRIPT_SDK_RUNTIME_DIR}" _runtime_dir)
file(REAL_PATH "${VEROSCRIPT_CONSUMER_DIR}" _consumer_dir)
string(TOLOWER "${_runtime_dir}" _runtime_key)
string(TOLOWER "${_consumer_dir}" _consumer_key)
if(_runtime_key STREQUAL _consumer_key)
	return()
endif()

file(GLOB _runtime_dlls LIST_DIRECTORIES false "${_runtime_dir}/*.dll")
if(NOT _runtime_dlls)
	message(FATAL_ERROR "No SDK runtime DLLs found in ${_runtime_dir}")
endif()
file(COPY ${_runtime_dlls} DESTINATION "${_consumer_dir}")
foreach(_resource IN ITEMS localization licenses)
	if(IS_DIRECTORY "${_runtime_dir}/${_resource}")
		file(COPY "${_runtime_dir}/${_resource}" DESTINATION "${_consumer_dir}")
	endif()
endforeach()

# Project and Application vendor notices are installed at the SDK root. Include
# them when deploying the DLLs from bin into an independent consumer directory.
get_filename_component(_sdk_dir "${_runtime_dir}" DIRECTORY)
if(EXISTS "${_sdk_dir}/LICENSE")
	# Keep VeroScript's notice separate from the consuming application's license.
	file(MAKE_DIRECTORY "${_consumer_dir}/licenses/VeroScript")
	file(COPY "${_sdk_dir}/LICENSE" DESTINATION "${_consumer_dir}/licenses/VeroScript")
endif()
if(IS_DIRECTORY "${_sdk_dir}/licenses/application")
	file(MAKE_DIRECTORY "${_consumer_dir}/licenses")
	file(COPY "${_sdk_dir}/licenses/application" DESTINATION "${_consumer_dir}/licenses")
endif()
