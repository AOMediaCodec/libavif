// Copyright 2026 Jeff Bindel. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause
//
// Portability macros for optional Clang -fbounds-safety.
//
// When AVIF_SUPPORT_FBOUNDS_SAFETY is defined (typically via
// -DAVIF_SUPPORT_FBOUNDS_SAFETY and a Clang toolchain that implements
// -fbounds-safety), these macros expand to Clang bounds annotations.
// Otherwise they expand to nothing so default builds are unchanged.
//
// Pattern matches libwebp / libpng style: annotations are inert unless
// explicitly enabled. Enabling a full -fbounds-safety build also needs a
// whole-TU baseline (e.g. __ptrcheck_abi_assume_unsafe_indexable()); that
// migration is intentionally out of scope for this first annotation CL.

#ifndef AVIF_AVIF_BOUNDS_SAFETY_H
#define AVIF_AVIF_BOUNDS_SAFETY_H

#ifdef AVIF_SUPPORT_FBOUNDS_SAFETY

#  include <ptrcheck.h>
/* Prefer __counted_by_or_null for pointers that may be NULL while the
 * companion size field is zero (avifRWData empty / freed pattern).
 */
#  define AVIF_COUNTED_BY(n) __counted_by(n)
#  define AVIF_COUNTED_BY_OR_NULL(n) __counted_by_or_null(n)

#else /* !AVIF_SUPPORT_FBOUNDS_SAFETY */

#  define AVIF_COUNTED_BY(n)
#  define AVIF_COUNTED_BY_OR_NULL(n)

#endif /* AVIF_SUPPORT_FBOUNDS_SAFETY */

#endif /* AVIF_AVIF_BOUNDS_SAFETY_H */
