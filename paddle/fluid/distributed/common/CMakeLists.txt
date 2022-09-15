cc_library(
  afs_wrapper
  SRCS afs_warpper.cc
  DEPS fs ps_framework_proto)

add_library(read_afs afs_api.cc)
target_link_libraries(read_afs afs_api fs)
add_dependencies(read_afs afs_api fs)
#set_property(GLOBAL PROPERTY COMMON_DEPS afs_warpper)
