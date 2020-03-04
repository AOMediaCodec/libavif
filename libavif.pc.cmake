prefix=@CMAKE_INSTALL_PREFIX@
exec_prefix=${prefix}/bin
libdir=${prefix}/lib
includedir=${prefix}/include

Name: @PROJECT_NAME@
Description: Library for encoding and decoding .avif files
Version: @PROJECT_VERSION@
Libs: -L${libdir} -lavif
Cflags: -I${includedir}
