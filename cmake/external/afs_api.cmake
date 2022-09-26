# Copyright (c) 2018 PaddlePaddle Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

include(ExternalProject)

set(AFS_API_PROJECT "extern_afs_api")
if((NOT DEFINED AFS_API_VER) OR (NOT DEFINED AFS_API_URL))
  message(STATUS "use pre defined download url")
  set(AFS_API_VER
      "0.1.1"
      CACHE STRING "" FORCE)
  set(AFS_API_NAME
      "afs_api"
      CACHE STRING "" FORCE)
  set(AFS_API_URL
      "https://pslib.bj.bcebos.com/afs_api_featuredb.tar.gz"
      CACHE STRING "" FORCE)
endif()
message(STATUS "AFS_API_NAME: ${AFS_API_NAME}, AFS_API_URL: ${AFS_API_URL}")
set(AFS_API_SOURCE_DIR "${THIRD_PARTY_PATH}/afs_api")
set(AFS_API_DOWNLOAD_DIR "${AFS_API_SOURCE_DIR}/src/${AFS_API_PROJECT}")
set(AFS_API_DST_DIR "afs_api")
set(AFS_API_INSTALL_ROOT "${THIRD_PARTY_PATH}/install")
set(AFS_API_INSTALL_DIR ${AFS_API_INSTALL_ROOT}/${AFS_API_DST_DIR})
set(AFS_API_ROOT ${AFS_API_INSTALL_DIR})
set(AFS_API_INC_DIR ${AFS_API_ROOT}/include)
set(AFS_API_LIB_DIR ${AFS_API_ROOT}/lib)
set(AFS_API_LIB
    ${AFS_API_LIB_DIR}/libafsclient.a
    CACHE FILEPATH "afs api lib" FORCE)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_RPATH}" "${AFS_API_ROOT}/lib")

include_directories(${AFS_API_INC_DIR})

file(
  WRITE ${AFS_API_DOWNLOAD_DIR}/CMakeLists.txt
  "PROJECT(AFS_API)\n" "cmake_minimum_required(VERSION 3.0)\n"
  "install(DIRECTORY ${AFS_API_NAME}/include ${AFS_API_NAME}/lib \n"
  "        DESTINATION ${AFS_API_DST_DIR})\n")

#afs://wolong.afs.baidu.com:9902/user/mlarch/gpubox_parquet/afs_api.tar.gz
ExternalProject_Add(
  ${AFS_API_PROJECT}
  ${EXTERNAL_PROJECT_LOG_ARGS}
  PREFIX ${AFS_API_SOURCE_DIR}
  DOWNLOAD_DIR ${AFS_API_DOWNLOAD_DIR}
  #DOWNLOAD_COMMAND wget --no-check-certificate ${AFS_API_URL} -c -q -O
  #                 ${AFS_API_NAME}.tar.gz && tar zxvf ${AFS_API_NAME}.tar.gz
  DOWNLOAD_COMMAND cp /work/git/baidu/inf/afs-api/afs_api.tar.gz . &&
  DOWNLOAD_NO_PROGRESS 1
  UPDATE_COMMAND ""
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${AFS_API_INSTALL_ROOT}
             -DCMAKE_BUILD_TYPE=${THIRD_PARTY_BUILD_TYPE}
  CMAKE_CACHE_ARGS -DCMAKE_INSTALL_PREFIX:PATH=${AFS_API_INSTALL_ROOT}
                   -DCMAKE_BUILD_TYPE=${THIRD_PARTY_BUILD_TYPE}
  BUILD_BYPRODUCTS ${AFS_API_LIB})

add_library(afs_api STATIC IMPORTED GLOBAL)
set_property(TARGET afs_api PROPERTY IMPORTED_LOCATION ${AFS_API_LIB})
add_dependencies(afs_api ${AFS_API_PROJECT})
