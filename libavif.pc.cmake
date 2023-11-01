prefix=@CMAKE_INSTALL_PREFIX@
exec_prefix=${prefix}
libdir=@PC_LIBDIR@
includedir=@PC_INCLUDEDIR@

Name: @PROJECT_NAME@
Description: Library for encoding and decoding .avif files
Version: @PROJECT_VERSION@
Requires:@AVIF_PKG_CONFIG_REQUIRES@
Libs: -L${libdir} -lavif@AVIF_PKG_CONFIG_LIBS@
Libs.private:@AVIF_PKG_CONFIG_LIBS_PRIVATE@
Cflags: -I${includedir}@AVIF_PKG_CONFIG_EXTRA_CFLAGS@
