prefix=@CMAKE_INSTALL_PREFIX@
exec_prefix=${prefix}/bin
libdir=${prefix}/@CMAKE_INSTALL_LIBDIR@
includedir=${prefix}/include

Name: @PROJECT_NAME@
Description: Library for encoding and decoding .avif files
Version: @PROJECT_VERSION@
Libs: -L${libdir} -lavif
Cflags: -I${includedir}@AVIF_PKG_CONFIG_EXTRA_CFLAGS@
