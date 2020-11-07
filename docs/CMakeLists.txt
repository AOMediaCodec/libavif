# Copyright 2020 Shun Sakai. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause

if(UNIX OR NOT MSVC)
    add_subdirectory(man/man1)
endif()
