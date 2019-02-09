/* gb_math.h - v0.07c - public domain C math library - no warranty implied; use at your own risk
   A C math library geared towards game development
   use '#define GB_MATH_IMPLEMENTATION' before including to create the implementation in _ONE_ file

Version History:
	0.07f - Fix constants
	0.07e - Fixed a warning
	0.07d - Fix mat4_inverse
	0.07c - Add gb_random01
	0.07b - Fix mat4_inverse
	0.07a - Fix Mat2
	0.07  - Better Mat4 procedures
	0.06h - Ignore silly warnings
	0.06g - Remove memzero
	0.06f - Remove warning on MSVC
	0.06e - Change brace style and fix some warnings
	0.06d - Bug fix
	0.06c - Remove extra needed define for C++ and inline all operators
	0.06b - Just formatting
	0.06a - Implement rough versions of mod, remainder, copy_sign
	0.06  - Windows GCC Support and C90-ish Support
	0.05  - Less/no dependencies or CRT
	0.04d - License Update
	0.04c - Use 64-bit murmur64 version on WIN64
	0.04b - Fix strict aliasing in gb_quake_rsqrt
	0.04a - Minor bug fixes
	0.04  - Namespace everything with gb
	0.03  - Complete Replacement
	0.01  - Initial Version

LICENSE
	This software is dual-licensed to the public domain and under the following
	license: you are granted a perpetual, irrevocable license to copy, modify,
	publish, and distribute this file as you see fit.
WARNING
	- This library is _slightly_ experimental and features may not work as expected.
	- This also means that many functions are not documented.

CONTENTS
	- Common Macros
	- Types
		- gbVec(2,3,4)
		- gbMat(2,3,4)
		- gbFloat(2,3,4)
		- gbQuat
		- gbRect(2,3)
		- gbAabb(2,3)
		- gbHalf (16-bit floating point) (storage only)
	- Operations
	- Functions
	- Type Functions
	- Random
	- Hash
*/

#ifndef GB_MATH_INCLUDE_GB_MATH_H
#define GB_MATH_INCLUDE_GB_MATH_H

#include <stddef.h>

#if !defined(GB_MATH_NO_MATH_H)
	#include <math.h>
#else
	#include <intrin.h>
#endif

#ifndef GB_MATH_DEF
	#ifdef GB_MATH_STATIC
		#define GB_MATH_DEF static
	#else
		#define GB_MATH_DEF extern
	#endif
#endif

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable:4201)
#endif

typedef union gbVec2 {
	struct { float x, y; };
	float e[2];
} gbVec2;

typedef union gbVec3 {
	struct { float x, y, z; };
	struct { float r, g, b; };

	gbVec2 xy;
	float e[3];
} gbVec3;

typedef union gbVec4 {
	struct { float x, y, z, w; };
	struct { float r, g, b, a; };
	struct { gbVec2 xy, zw; };
	gbVec3 xyz;
	gbVec3 rgb;
	float e[4];
} gbVec4;



typedef union gbMat2 {
	struct { gbVec2 x, y; };
	gbVec2 col[2];
	float e[4];
} gbMat2;

typedef union gbMat3 {
	struct { gbVec3 x, y, z; };
	gbVec3 col[3];
	float e[9];
} gbMat3;

typedef union gbMat4 {
	struct { gbVec4 x, y, z, w; };
	gbVec4 col[4];
	float e[16];
} gbMat4;


typedef union gbQuat {
	struct { float x, y, z, w; };
	gbVec4 xyzw;
	gbVec3 xyz;
	float e[4];
} gbQuat;


#if defined(_MSC_VER)
#pragma warning(pop)
#endif

typedef float gbFloat2[2];
typedef float gbFloat3[3];
typedef float gbFloat4[4];


typedef struct gbRect2 { gbVec2 pos, dim; } gbRect2;
typedef struct gbRect3 { gbVec3 pos, dim; } gbRect3;

typedef struct gbAabb2 { gbVec2 centre, half_size; } gbAabb2;
typedef struct gbAabb3 { gbVec3 centre, half_size; } gbAabb3;

#if defined(_MSC_VER)
	typedef unsigned __int32 gb_math_u32;
	typedef unsigned __int64 gb_math_u64;
#else
	#if defined(GB_USE_STDINT)
		#include <stdint.h>
		typedef uint32_t gb_math_u32;
		typedef uint64_t gb_math_u64;
	#else
		typedef unsigned int       gb_math_u32;
		typedef unsigned long long gb_math_u64;
	#endif
#endif

typedef short gbHalf;


#ifndef GB_MATH_CONSTANTS
#define GB_MATH_CONSTANTS
	#define GB_MATH_EPSILON      1.19209290e-7f
	#define GB_MATH_ZERO         0.0f
	#define GB_MATH_ONE          1.0f
	#define GB_MATH_TWO_THIRDS   0.666666666666666666666666666666666666667f

	#define GB_MATH_TAU          6.28318530717958647692528676655900576f
	#define GB_MATH_PI           3.14159265358979323846264338327950288f
	#define GB_MATH_ONE_OVER_TAU 0.159154943091895335768883763372514362f
	#define GB_MATH_ONE_OVER_PI  0.318309886183790671537767526745028724f

	#define GB_MATH_TAU_OVER_2   3.14159265358979323846264338327950288f
	#define GB_MATH_TAU_OVER_4   1.570796326794896619231321691639751442f
	#define GB_MATH_TAU_OVER_8   0.785398163397448309615660845819875721f

	#define GB_MATH_E            2.7182818284590452353602874713526625f
	#define GB_MATH_SQRT_TWO     1.41421356237309504880168872420969808f
	#define GB_MATH_SQRT_THREE   1.73205080756887729352744634150587236f
	#define GB_MATH_SQRT_FIVE    2.23606797749978969640917366873127623f

	#define GB_MATH_LOG_TWO      0.693147180559945309417232121458176568f
	#define GB_MATH_LOG_TEN      2.30258509299404568401799145468436421f
#endif


#if defined(__cplusplus)
extern "C" {
#endif

#ifndef gb_clamp
#define gb_clamp(x, lower, upper) (gb_min(gb_max(x, (lower)), (upper)))
#endif
#ifndef gb_clamp01
#define gb_clamp01(x) gb_clamp(x, 0, 1)
#endif

#ifndef gb_square
#define gb_square(x) ((x)*(x))
#endif

#ifndef gb_cube
#define gb_cube(x) ((x)*(x)*(x))
#endif

#ifndef gb_abs
#define gb_abs(x) ((x) > 0 ? (x) : -(x))
#endif

#ifndef gb_sign
#define gb_sign(x) ((x) >= 0 ? 1 : -1)
#endif


GB_MATH_DEF float gb_to_radians(float degrees);
GB_MATH_DEF float gb_to_degrees(float radians);

/* NOTE(bill): Because to interpolate angles */
GB_MATH_DEF float gb_angle_diff(float radians_a, float radians_b);

#ifndef gb_min
#define gb_min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef gb_max
#define gb_max(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef gb_min3
#define gb_min3(a, b, c) gb_min(gb_min(a, b), c)
#endif

#ifndef gb_max3
#define gb_max3(a, b, c) gb_max(gb_max(a, b), c)
#endif


GB_MATH_DEF float gb_copy_sign  (float x, float y);
GB_MATH_DEF float gb_remainder  (float x, float y);
GB_MATH_DEF float gb_mod        (float x, float y);
GB_MATH_DEF float gb_sqrt       (float a);
GB_MATH_DEF float gb_rsqrt      (float a);
GB_MATH_DEF float gb_quake_rsqrt(float a); /* NOTE(bill): It's probably better to use 1.0f/gb_sqrt(a)
                                            * And for simd, there is usually isqrt functions too!
                                            */
GB_MATH_DEF float gb_sin    (float radians);
GB_MATH_DEF float gb_cos    (float radians);
GB_MATH_DEF float gb_tan    (float radians);
GB_MATH_DEF float gb_arcsin (float a);
GB_MATH_DEF float gb_arccos (float a);
GB_MATH_DEF float gb_arctan (float a);
GB_MATH_DEF float gb_arctan2(float y, float x);

GB_MATH_DEF float gb_exp      (float x);
GB_MATH_DEF float gb_exp2     (float x);
GB_MATH_DEF float gb_log      (float x);
GB_MATH_DEF float gb_log2     (float x);
GB_MATH_DEF float gb_fast_exp (float x);  /* NOTE(bill): Only valid from -1 <= x <= +1 */
GB_MATH_DEF float gb_fast_exp2(float x); /* NOTE(bill): Only valid from -1 <= x <= +1 */
GB_MATH_DEF float gb_pow      (float x, float y); /* x^y */

GB_MATH_DEF float gb_round(float x);
GB_MATH_DEF float gb_floor(float x);
GB_MATH_DEF float gb_ceil (float x);

GB_MATH_DEF float  gb_half_to_float(gbHalf value);
GB_MATH_DEF gbHalf gb_float_to_half(float value);


GB_MATH_DEF gbVec2 gb_vec2_zero(void);
GB_MATH_DEF gbVec2 gb_vec2     (float x, float y);
GB_MATH_DEF gbVec2 gb_vec2v    (float x[2]);

GB_MATH_DEF gbVec3 gb_vec3_zero(void);
GB_MATH_DEF gbVec3 gb_vec3     (float x, float y, float z);
GB_MATH_DEF gbVec3 gb_vec3v    (float x[3]);

GB_MATH_DEF gbVec4 gb_vec4_zero(void);
GB_MATH_DEF gbVec4 gb_vec4     (float x, float y, float z, float w);
GB_MATH_DEF gbVec4 gb_vec4v    (float x[4]);


GB_MATH_DEF void gb_vec2_add(gbVec2 *d, gbVec2 v0, gbVec2 v1);
GB_MATH_DEF void gb_vec2_sub(gbVec2 *d, gbVec2 v0, gbVec2 v1);
GB_MATH_DEF void gb_vec2_mul(gbVec2 *d, gbVec2 v,  float s);
GB_MATH_DEF void gb_vec2_div(gbVec2 *d, gbVec2 v,  float s);

GB_MATH_DEF void gb_vec3_add(gbVec3 *d, gbVec3 v0, gbVec3 v1);
GB_MATH_DEF void gb_vec3_sub(gbVec3 *d, gbVec3 v0, gbVec3 v1);
GB_MATH_DEF void gb_vec3_mul(gbVec3 *d, gbVec3 v,  float s);
GB_MATH_DEF void gb_vec3_div(gbVec3 *d, gbVec3 v,  float s);

GB_MATH_DEF void gb_vec4_add(gbVec4 *d, gbVec4 v0, gbVec4 v1);
GB_MATH_DEF void gb_vec4_sub(gbVec4 *d, gbVec4 v0, gbVec4 v1);
GB_MATH_DEF void gb_vec4_mul(gbVec4 *d, gbVec4 v,  float s);
GB_MATH_DEF void gb_vec4_div(gbVec4 *d, gbVec4 v,  float s);

GB_MATH_DEF void gb_vec2_addeq(gbVec2 *d, gbVec2 v);
GB_MATH_DEF void gb_vec2_subeq(gbVec2 *d, gbVec2 v);
GB_MATH_DEF void gb_vec2_muleq(gbVec2 *d, float s);
GB_MATH_DEF void gb_vec2_diveq(gbVec2 *d, float s);

GB_MATH_DEF void gb_vec3_addeq(gbVec3 *d, gbVec3 v);
GB_MATH_DEF void gb_vec3_subeq(gbVec3 *d, gbVec3 v);
GB_MATH_DEF void gb_vec3_muleq(gbVec3 *d, float s);
GB_MATH_DEF void gb_vec3_diveq(gbVec3 *d, float s);

GB_MATH_DEF void gb_vec4_addeq(gbVec4 *d, gbVec4 v);
GB_MATH_DEF void gb_vec4_subeq(gbVec4 *d, gbVec4 v);
GB_MATH_DEF void gb_vec4_muleq(gbVec4 *d, float s);
GB_MATH_DEF void gb_vec4_diveq(gbVec4 *d, float s);

GB_MATH_DEF float gb_vec2_dot(gbVec2 v0, gbVec2 v1);
GB_MATH_DEF float gb_vec3_dot(gbVec3 v0, gbVec3 v1);
GB_MATH_DEF float gb_vec4_dot(gbVec4 v0, gbVec4 v1);

GB_MATH_DEF void gb_vec2_cross(float *d, gbVec2 v0, gbVec2 v1);
GB_MATH_DEF void gb_vec3_cross(gbVec3 *d, gbVec3 v0, gbVec3 v1);

GB_MATH_DEF float gb_vec2_mag2(gbVec2 v);
GB_MATH_DEF float gb_vec3_mag2(gbVec3 v);
GB_MATH_DEF float gb_vec4_mag2(gbVec4 v);

GB_MATH_DEF float gb_vec2_mag(gbVec2 v);
GB_MATH_DEF float gb_vec3_mag(gbVec3 v);
GB_MATH_DEF float gb_vec4_mag(gbVec4 v);

GB_MATH_DEF void gb_vec2_norm(gbVec2 *d, gbVec2 v);
GB_MATH_DEF void gb_vec3_norm(gbVec3 *d, gbVec3 v);
GB_MATH_DEF void gb_vec4_norm(gbVec4 *d, gbVec4 v);

GB_MATH_DEF void gb_vec2_norm0(gbVec2 *d, gbVec2 v);
GB_MATH_DEF void gb_vec3_norm0(gbVec3 *d, gbVec3 v);
GB_MATH_DEF void gb_vec4_norm0(gbVec4 *d, gbVec4 v);

GB_MATH_DEF void gb_vec2_reflect(gbVec2 *d, gbVec2 i, gbVec2 n);
GB_MATH_DEF void gb_vec3_reflect(gbVec3 *d, gbVec3 i, gbVec3 n);
GB_MATH_DEF void gb_vec2_refract(gbVec2 *d, gbVec2 i, gbVec2 n, float eta);
GB_MATH_DEF void gb_vec3_refract(gbVec3 *d, gbVec3 i, gbVec3 n, float eta);

GB_MATH_DEF float gb_vec2_aspect_ratio(gbVec2 v);


GB_MATH_DEF void gb_mat2_identity   (gbMat2 *m);
GB_MATH_DEF void gb_float22_identity(float m[2][2]);

GB_MATH_DEF void  gb_mat2_transpose  (gbMat2 *m);
GB_MATH_DEF void  gb_mat2_mul        (gbMat2 *out, gbMat2 *m1, gbMat2 *m2);
GB_MATH_DEF void  gb_mat2_mul_vec2   (gbVec2 *out, gbMat2 *m, gbVec2 in);
GB_MATH_DEF void  gb_mat2_inverse    (gbMat2 *out, gbMat2 *in);
GB_MATH_DEF float gb_mat2_determinate(gbMat2 *m);

GB_MATH_DEF gbMat2 *gb_mat2_v(gbVec2 m[2]);
GB_MATH_DEF gbMat2 *gb_mat2_f(float m[2][2]);
GB_MATH_DEF gbFloat2 *gb_float22_m(gbMat2 *m);
GB_MATH_DEF gbFloat2 *gb_float22_v(gbVec2 m[2]);
GB_MATH_DEF gbFloat2 *gb_float22_4(float m[4]);

GB_MATH_DEF void gb_float22_transpose(float (*vec)[2]);
GB_MATH_DEF void gb_float22_mul      (float (*out)[2], float (*mat1)[2], float (*mat2)[2]);
GB_MATH_DEF void gb_float22_mul_vec2 (gbVec2 *out, float m[2][2], gbVec2 in);


GB_MATH_DEF void gb_mat3_identity   (gbMat3 *m);
GB_MATH_DEF void gb_float33_identity(float m[3][3]);

GB_MATH_DEF void  gb_mat3_transpose  (gbMat3 *m);
GB_MATH_DEF void  gb_mat3_mul        (gbMat3 *out, gbMat3 *m1, gbMat3 *m2);
GB_MATH_DEF void  gb_mat3_mul_vec3   (gbVec3 *out, gbMat3 *m, gbVec3 in);
GB_MATH_DEF void  gb_mat3_inverse    (gbMat3 *out, gbMat3 *in);
GB_MATH_DEF float gb_mat3_determinate(gbMat3 *m);


GB_MATH_DEF gbMat3 *gb_mat3_v(gbVec3 m[3]);
GB_MATH_DEF gbMat3 *gb_mat3_f(float m[3][3]);

GB_MATH_DEF gbFloat3 *gb_float33_m(gbMat3 *m);
GB_MATH_DEF gbFloat3 *gb_float33_v(gbVec3 m[3]);
GB_MATH_DEF gbFloat3 *gb_float33_9(float m[9]);

GB_MATH_DEF void gb_float33_transpose(float (*vec)[3]);
GB_MATH_DEF void gb_float33_mul      (float (*out)[3], float (*mat1)[3], float (*mat2)[3]);
GB_MATH_DEF void gb_float33_mul_vec3 (gbVec3 *out, float m[3][3], gbVec3 in);

GB_MATH_DEF void gb_mat4_identity   (gbMat4 *m);
GB_MATH_DEF void gb_float44_identity(float m[4][4]);

GB_MATH_DEF void  gb_mat4_transpose  (gbMat4 *m);
GB_MATH_DEF void  gb_mat4_mul        (gbMat4 *out, gbMat4 *m1, gbMat4 *m2);
GB_MATH_DEF void  gb_mat4_mul_vec4   (gbVec4 *out, gbMat4 *m, gbVec4 in);
GB_MATH_DEF void  gb_mat4_inverse    (gbMat4 *out, gbMat4 *in);

GB_MATH_DEF gbMat4 *gb_mat4_v(gbVec4 m[4]);
GB_MATH_DEF gbMat4 *gb_mat4_f(float m[4][4]);

GB_MATH_DEF gbFloat4 *gb_float44_m (gbMat4 *m);
GB_MATH_DEF gbFloat4 *gb_float44_v (gbVec4 m[4]);
GB_MATH_DEF gbFloat4 *gb_float44_16(float m[16]);

GB_MATH_DEF void gb_float44_transpose(float (*vec)[4]);
GB_MATH_DEF void gb_float44_mul      (float (*out)[4], float (*mat1)[4], float (*mat2)[4]);
GB_MATH_DEF void gb_float44_mul_vec4 (gbVec4 *out, float m[4][4], gbVec4 in);


GB_MATH_DEF void gb_mat4_translate           (gbMat4 *out, gbVec3 v);
GB_MATH_DEF void gb_mat4_rotate              (gbMat4 *out, gbVec3 v, float angle_radians);
GB_MATH_DEF void gb_mat4_scale               (gbMat4 *out, gbVec3 v);
GB_MATH_DEF void gb_mat4_scalef              (gbMat4 *out, float s);
GB_MATH_DEF void gb_mat4_ortho2d             (gbMat4 *out, float left, float right, float bottom, float top);
GB_MATH_DEF void gb_mat4_ortho3d             (gbMat4 *out, float left, float right, float bottom, float top, float z_near, float z_far);
GB_MATH_DEF void gb_mat4_perspective         (gbMat4 *out, float fovy, float aspect, float z_near, float z_far);
GB_MATH_DEF void gb_mat4_infinite_perspective(gbMat4 *out, float fovy, float aspect, float z_near);

GB_MATH_DEF void gb_mat4_look_at(gbMat4 *out, gbVec3 eye, gbVec3 centre, gbVec3 up);



GB_MATH_DEF gbQuat gb_quat             (float x, float y, float z, float w);
GB_MATH_DEF gbQuat gb_quatv            (float e[4]);
GB_MATH_DEF gbQuat gb_quat_axis_angle  (gbVec3 axis, float angle_radians);
GB_MATH_DEF gbQuat gb_quat_euler_angles(float pitch, float yaw, float roll);
GB_MATH_DEF gbQuat gb_quat_identity    (void);

GB_MATH_DEF void gb_quat_add(gbQuat *d, gbQuat q0, gbQuat q1);
GB_MATH_DEF void gb_quat_sub(gbQuat *d, gbQuat q0, gbQuat q1);
GB_MATH_DEF void gb_quat_mul(gbQuat *d, gbQuat q0, gbQuat q1);
GB_MATH_DEF void gb_quat_div(gbQuat *d, gbQuat q0, gbQuat q1);

GB_MATH_DEF void gb_quat_mulf(gbQuat *d, gbQuat q, float s);
GB_MATH_DEF void gb_quat_divf(gbQuat *d, gbQuat q, float s);


GB_MATH_DEF void gb_quat_addeq(gbQuat *d, gbQuat q);
GB_MATH_DEF void gb_quat_subeq(gbQuat *d, gbQuat q);
GB_MATH_DEF void gb_quat_muleq(gbQuat *d, gbQuat q);
GB_MATH_DEF void gb_quat_diveq(gbQuat *d, gbQuat q);


GB_MATH_DEF void gb_quat_muleqf(gbQuat *d, float s);
GB_MATH_DEF void gb_quat_diveqf(gbQuat *d, float s);




GB_MATH_DEF float gb_quat_dot(gbQuat q0, gbQuat q1);
GB_MATH_DEF float gb_quat_mag(gbQuat q);

GB_MATH_DEF void gb_quat_norm   (gbQuat *d, gbQuat q);
GB_MATH_DEF void gb_quat_conj   (gbQuat *d, gbQuat q);
GB_MATH_DEF void gb_quat_inverse(gbQuat *d, gbQuat q);

GB_MATH_DEF void  gb_quat_axis (gbVec3 *axis, gbQuat q);
GB_MATH_DEF float gb_quat_angle(gbQuat q);

GB_MATH_DEF float gb_quat_pitch(gbQuat q);
GB_MATH_DEF float gb_quat_yaw  (gbQuat q);
GB_MATH_DEF float gb_quat_roll (gbQuat q);

/* NOTE(bill): Rotate v by q */
GB_MATH_DEF void gb_quat_rotate_vec3(gbVec3 *d, gbQuat q, gbVec3 v);
GB_MATH_DEF void gb_mat4_from_quat  (gbMat4 *out, gbQuat q);
GB_MATH_DEF void gb_quat_from_mat4  (gbQuat *out, gbMat4 *m);



/* Interpolations */
GB_MATH_DEF float gb_lerp         (float a, float b, float t);
GB_MATH_DEF float gb_unlerp       (float t, float a, float b);
GB_MATH_DEF float gb_smooth_step  (float a, float b, float t);
GB_MATH_DEF float gb_smoother_step(float a, float b, float t);

GB_MATH_DEF void gb_vec2_lerp(gbVec2 *d, gbVec2 a, gbVec2 b, float t);
GB_MATH_DEF void gb_vec3_lerp(gbVec3 *d, gbVec3 a, gbVec3 b, float t);
GB_MATH_DEF void gb_vec4_lerp(gbVec4 *d, gbVec4 a, gbVec4 b, float t);

GB_MATH_DEF void gb_quat_lerp (gbQuat *d, gbQuat a, gbQuat b, float t);
GB_MATH_DEF void gb_quat_nlerp(gbQuat *d, gbQuat a, gbQuat b, float t);
GB_MATH_DEF void gb_quat_slerp(gbQuat *d, gbQuat a, gbQuat b, float t);
GB_MATH_DEF void gb_quat_nquad(gbQuat *d, gbQuat p, gbQuat a, gbQuat b, gbQuat q, float t);
GB_MATH_DEF void gb_quat_squad(gbQuat *d, gbQuat p, gbQuat a, gbQuat b, gbQuat q, float t);
GB_MATH_DEF void gb_quat_slerp_approx(gbQuat *d, gbQuat a, gbQuat b, float t);
GB_MATH_DEF void gb_quat_squad_approx(gbQuat *d, gbQuat p, gbQuat a, gbQuat b, gbQuat q, float t);


/* Rects */
GB_MATH_DEF gbRect2 gb_rect2(gbVec2 pos, gbVec2 dim);
GB_MATH_DEF gbRect2 gb_rect2v(float v[4]);

GB_MATH_DEF gbRect3 gb_rect3(gbVec3 pos, gbVec3 dim);
GB_MATH_DEF gbRect3 gb_rect3v(float v[6]);

GB_MATH_DEF int gb_rect2_contains           (gbRect2 a, float x, float y);
GB_MATH_DEF int gb_rect2_contains_vec2      (gbRect2 a, gbVec2 p);
GB_MATH_DEF int gb_rect2_intersects         (gbRect2 a, gbRect2 b);
GB_MATH_DEF int gb_rect2_intersection_result(gbRect2 a, gbRect2 b, gbRect2 *intersection);


#ifndef	GB_MURMUR64_DEFAULT_SEED
#define GB_MURMUR64_DEFAULT_SEED 0x9747b28c
#endif
/* Hashing */
GB_MATH_DEF gb_math_u64 gb_hash_murmur64(void const *key, size_t num_bytes, gb_math_u64 seed);

/* Random */
/* TODO(bill): Use a generator for the random numbers */
GB_MATH_DEF float gb_random_range_float(float min_inc, float max_inc);
GB_MATH_DEF int   gb_random_range_int  (int min_inc, int max_inc);
GB_MATH_DEF float gb_random01          (void);



#if defined(__cplusplus)
}
#endif

#if defined(__cplusplus)

/* TODO(bill): How should I apply GB_MATH_DEF to these operator overloads? */

inline bool operator==(gbVec2 a, gbVec2 b) { return (a.x == b.x) && (a.y == b.y); }
inline bool operator!=(gbVec2 a, gbVec2 b) { return !operator==(a, b); }

inline gbVec2 operator+(gbVec2 a) { return a; }
inline gbVec2 operator-(gbVec2 a) { gbVec2 r = {-a.x, -a.y}; return r; }

inline gbVec2 operator+(gbVec2 a, gbVec2 b) { gbVec2 r; gb_vec2_add(&r, a, b); return r; }
inline gbVec2 operator-(gbVec2 a, gbVec2 b) { gbVec2 r; gb_vec2_sub(&r, a, b); return r; }

inline gbVec2 operator*(gbVec2 a, float scalar) { gbVec2 r; gb_vec2_mul(&r, a, scalar); return r; }
inline gbVec2 operator*(float scalar, gbVec2 a) { return operator*(a, scalar); }

inline gbVec2 operator/(gbVec2 a, float scalar) { return operator*(a, 1.0f/scalar); }

/* Hadamard Product */
inline gbVec2 operator*(gbVec2 a, gbVec2 b) { gbVec2 r = {a.x*b.x, a.y*b.y}; return r; }
inline gbVec2 operator/(gbVec2 a, gbVec2 b) { gbVec2 r = {a.x/b.x, a.y/b.y}; return r; }

inline gbVec2 &operator+=(gbVec2 &a, gbVec2 b)       { return (a = a + b); }
inline gbVec2 &operator-=(gbVec2 &a, gbVec2 b)       { return (a = a - b); }
inline gbVec2 &operator*=(gbVec2 &a, float scalar) { return (a = a * scalar); }
inline gbVec2 &operator/=(gbVec2 &a, float scalar) { return (a = a / scalar); }


inline bool operator==(gbVec3 a, gbVec3 b) { return (a.x == b.x) && (a.y == b.y) && (a.z == b.z); }
inline bool operator!=(gbVec3 a, gbVec3 b) { return !operator==(a, b); }

inline gbVec3 operator+(gbVec3 a) { return a; }
inline gbVec3 operator-(gbVec3 a) { gbVec3 r = {-a.x, -a.y, -a.z}; return r; }

inline gbVec3 operator+(gbVec3 a, gbVec3 b) { gbVec3 r; gb_vec3_add(&r, a, b); return r; }
inline gbVec3 operator-(gbVec3 a, gbVec3 b) { gbVec3 r; gb_vec3_sub(&r, a, b); return r; }

inline gbVec3 operator*(gbVec3 a, float scalar) { gbVec3 r; gb_vec3_mul(&r, a, scalar); return r; }
inline gbVec3 operator*(float scalar, gbVec3 a) { return operator*(a, scalar); }

inline gbVec3 operator/(gbVec3 a, float scalar) { return operator*(a, 1.0f/scalar); }

/* Hadamard Product */
inline gbVec3 operator*(gbVec3 a, gbVec3 b) { gbVec3 r = {a.x*b.x, a.y*b.y, a.z*b.z}; return r; }
inline gbVec3 operator/(gbVec3 a, gbVec3 b) { gbVec3 r = {a.x/b.x, a.y/b.y, a.z/b.z}; return r; }

inline gbVec3 &operator+=(gbVec3 &a, gbVec3 b)       { return (a = a + b); }
inline gbVec3 &operator-=(gbVec3 &a, gbVec3 b)       { return (a = a - b); }
inline gbVec3 &operator*=(gbVec3 &a, float scalar) { return (a = a * scalar); }
inline gbVec3 &operator/=(gbVec3 &a, float scalar) { return (a = a / scalar); }


inline bool operator==(gbVec4 a, gbVec4 b) { return (a.x == b.x) && (a.y == b.y) && (a.z == b.z) && (a.w == b.w); }
inline bool operator!=(gbVec4 a, gbVec4 b) { return !operator==(a, b); }

inline gbVec4 operator+(gbVec4 a) { return a; }
inline gbVec4 operator-(gbVec4 a) { gbVec4 r = {-a.x, -a.y, -a.z, -a.w}; return r; }

inline gbVec4 operator+(gbVec4 a, gbVec4 b) { gbVec4 r; gb_vec4_add(&r, a, b); return r; }
inline gbVec4 operator-(gbVec4 a, gbVec4 b) { gbVec4 r; gb_vec4_sub(&r, a, b); return r; }

inline gbVec4 operator*(gbVec4 a, float scalar) { gbVec4 r; gb_vec4_mul(&r, a, scalar); return r; }
inline gbVec4 operator*(float scalar, gbVec4 a) { return operator*(a, scalar); }

inline gbVec4 operator/(gbVec4 a, float scalar) { return operator*(a, 1.0f/scalar); }

/* Hadamard Product */
inline gbVec4 operator*(gbVec4 a, gbVec4 b) { gbVec4 r = {a.x*b.x, a.y*b.y, a.z*b.z, a.w*b.w}; return r; }
inline gbVec4 operator/(gbVec4 a, gbVec4 b) { gbVec4 r = {a.x/b.x, a.y/b.y, a.z/b.z, a.w/b.w}; return r; }

inline gbVec4 &operator+=(gbVec4 &a, gbVec4 b)       { return (a = a + b); }
inline gbVec4 &operator-=(gbVec4 &a, gbVec4 b)       { return (a = a - b); }
inline gbVec4 &operator*=(gbVec4 &a, float scalar) { return (a = a * scalar); }
inline gbVec4 &operator/=(gbVec4 &a, float scalar) { return (a = a / scalar); }


inline gbMat2 operator+(gbMat2 const &a, gbMat2 const &b) {
	int i, j;
	gbMat2 r = {0};
	for (j = 0; j < 2; j++) {
		for (i = 0; i < 2; i++)
			r.e[2*j+i] = a.e[2*j+i] + b.e[2*j+i];
	}
	return r;
}

inline gbMat2 operator-(gbMat2 const &a, gbMat2 const &b) {
	int i, j;
	gbMat2 r = {0};
	for (j = 0; j < 2; j++) {
		for (i = 0; i < 2; i++)
			r.e[2*j+i] = a.e[2*j+i] - b.e[2*j+i];
	}
	return r;
}

inline gbMat2 operator*(gbMat2 const &a, gbMat2 const &b) { gbMat2 r; gb_mat2_mul(&r, (gbMat2 *)&a, (gbMat2 *)&b); return r; }
inline gbVec2 operator*(gbMat2 const &a, gbVec2 v) { gbVec2 r; gb_mat2_mul_vec2(&r, (gbMat2 *)&a, v); return r; }
inline gbMat2 operator*(gbMat2 const &a, float scalar) {
	gbMat2 r = {0};
	int i;
	for (i = 0; i < 2*2; i++) r.e[i] = a.e[i] * scalar;
	return r;
}
inline gbMat2 operator*(float scalar, gbMat2 const &a) { return operator*(a, scalar); }
inline gbMat2 operator/(gbMat2 const &a, float scalar) { return operator*(a, 1.0f/scalar); }

inline gbMat2& operator+=(gbMat2& a, gbMat2 const &b) { return (a = a + b); }
inline gbMat2& operator-=(gbMat2& a, gbMat2 const &b) { return (a = a - b); }
inline gbMat2& operator*=(gbMat2& a, gbMat2 const &b) { return (a = a * b); }



inline gbMat3 operator+(gbMat3 const &a, gbMat3 const &b) {
	int i, j;
	gbMat3 r = {0};
	for (j = 0; j < 3; j++) {
		for (i = 0; i < 3; i++)
			r.e[3*j+i] = a.e[3*j+i] + b.e[3*j+i];
	}
	return r;
}

inline gbMat3 operator-(gbMat3 const &a, gbMat3 const &b) {
	int i, j;
	gbMat3 r = {0};
	for (j = 0; j < 3; j++) {
		for (i = 0; i < 3; i++)
			r.e[3*j+i] = a.e[3*j+i] - b.e[3*j+i];
	}
	return r;
}

inline gbMat3 operator*(gbMat3 const &a, gbMat3 const &b) { gbMat3 r; gb_mat3_mul(&r, (gbMat3 *)&a, (gbMat3 *)&b); return r; }
inline gbVec3 operator*(gbMat3 const &a, gbVec3 v) { gbVec3 r; gb_mat3_mul_vec3(&r, (gbMat3 *)&a, v); return r; } inline gbMat3 operator*(gbMat3 const &a, float scalar) {
	gbMat3 r = {0};
	int i;
	for (i = 0; i < 3*3; i++) r.e[i] = a.e[i] * scalar;
	return r;
}
inline gbMat3 operator*(float scalar, gbMat3 const &a) { return operator*(a, scalar); }
inline gbMat3 operator/(gbMat3 const &a, float scalar) { return operator*(a, 1.0f/scalar); }

inline gbMat3& operator+=(gbMat3& a, gbMat3 const &b) { return (a = a + b); }
inline gbMat3& operator-=(gbMat3& a, gbMat3 const &b) { return (a = a - b); }
inline gbMat3& operator*=(gbMat3& a, gbMat3 const &b) { return (a = a * b); }



inline gbMat4 operator+(gbMat4 const &a, gbMat4 const &b) {
	int i, j;
	gbMat4 r = {0};
	for (j = 0; j < 4; j++) {
		for (i = 0; i < 4; i++)
			r.e[4*j+i] = a.e[4*j+i] + b.e[4*j+i];
	}
	return r;
}

inline gbMat4 operator-(gbMat4 const &a, gbMat4 const &b) {
	int i, j;
	gbMat4 r = {0};
	for (j = 0; j < 4; j++) {
		for (i = 0; i < 4; i++)
			r.e[4*j+i] = a.e[4*j+i] - b.e[4*j+i];
	}
	return r;
}

inline gbMat4 operator*(gbMat4 const &a, gbMat4 const &b) { gbMat4 r; gb_mat4_mul(&r, (gbMat4 *)&a, (gbMat4 *)&b); return r; }
inline gbVec4 operator*(gbMat4 const &a, gbVec4 v) { gbVec4 r; gb_mat4_mul_vec4(&r, (gbMat4 *)&a, v); return r; }
inline gbMat4 operator*(gbMat4 const &a, float scalar) {
	gbMat4 r = {0};
	int i;
	for (i = 0; i < 4*4; i++) r.e[i] = a.e[i] * scalar;
	return r;
}
inline gbMat4 operator*(float scalar, gbMat4 const &a) { return operator*(a, scalar); }
inline gbMat4 operator/(gbMat4 const &a, float scalar) { return operator*(a, 1.0f/scalar); }

inline gbMat4& operator+=(gbMat4 &a, gbMat4 const &b) { return (a = a + b); }
inline gbMat4& operator-=(gbMat4 &a, gbMat4 const &b) { return (a = a - b); }
inline gbMat4& operator*=(gbMat4 &a, gbMat4 const &b) { return (a = a * b); }



inline bool operator==(gbQuat a, gbQuat b) { return a.xyzw == b.xyzw; }
inline bool operator!=(gbQuat a, gbQuat b) { return !operator==(a, b); }

inline gbQuat operator+(gbQuat q) { return q; }
inline gbQuat operator-(gbQuat q) { return gb_quat(-q.x, -q.y, -q.z, -q.w); }

inline gbQuat operator+(gbQuat a, gbQuat b) { gbQuat r; gb_quat_add(&r, a, b); return r; }
inline gbQuat operator-(gbQuat a, gbQuat b) { gbQuat r; gb_quat_sub(&r, a, b); return r; }

inline gbQuat operator*(gbQuat a, gbQuat b)  { gbQuat r; gb_quat_mul(&r, a, b); return r; }
inline gbQuat operator*(gbQuat q, float s) { gbQuat r; gb_quat_mulf(&r, q, s); return r; }
inline gbQuat operator*(float s, gbQuat q) { return operator*(q, s); }
inline gbQuat operator/(gbQuat q, float s) { gbQuat r; gb_quat_divf(&r, q, s); return r; }

inline gbQuat &operator+=(gbQuat &a, gbQuat b) { gb_quat_addeq(&a, b); return a; }
inline gbQuat &operator-=(gbQuat &a, gbQuat b) { gb_quat_subeq(&a, b); return a; }
inline gbQuat &operator*=(gbQuat &a, gbQuat b) { gb_quat_muleq(&a, b); return a; }
inline gbQuat &operator/=(gbQuat &a, gbQuat b) { gb_quat_diveq(&a, b); return a; }

inline gbQuat &operator*=(gbQuat &a, float b) { gb_quat_muleqf(&a, b); return a; }
inline gbQuat &operator/=(gbQuat &a, float b) { gb_quat_diveqf(&a, b); return a; }

/* Rotate v by a */
inline gbVec3 operator*(gbQuat q, gbVec3 v) { gbVec3 r; gb_quat_rotate_vec3(&r, q, v); return r; }

#endif




#endif /* GB_MATH_INCLUDE_GB_MATH_H */

/****************************************************************
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 * Implementation
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 ****************************************************************/

#if defined(GB_MATH_IMPLEMENTATION) && !defined(GB_MATH_IMPLEMENTATION_DONE)
#define GB_MATH_IMPLEMENTATION_DONE

 #if (defined(__GCC__) || defined(__GNUC__)) && !defined(__clang__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wattributes"
 #pragma GCC diagnostic ignored "-Wmissing-braces"
 #elif __clang__
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Wattributes"
 #pragma clang diagnostic ignored "-Wmissing-braces"
 #endif


/* NOTE(bill): To remove the need for memcpy */
static void gb__memcpy_4byte(void *dest, void const *src, size_t size) {
	size_t i;
	unsigned int *d, *s;
	d = (unsigned int *)dest;
	s = (unsigned int *)src;
	for (i = 0; i < size/4; i++) {
		*d++ = *s++;
	}
}


float gb_to_radians(float degrees) { return degrees * GB_MATH_TAU / 360.0f; }
float gb_to_degrees(float radians) { return radians * 360.0f / GB_MATH_TAU; }

float gb_angle_diff(float radians_a, float radians_b) {
	float delta = gb_mod(radians_b-radians_a, GB_MATH_TAU);
	delta = gb_mod(delta + 1.5f*GB_MATH_TAU, GB_MATH_TAU);
	delta -= 0.5f*GB_MATH_TAU;
	return delta;
}

float gb_copy_sign(float x, float y) {
	int ix, iy;
	ix = *(int *)&x;
	iy = *(int *)&y;

	ix &= 0x7fffffff;
	ix |= iy & 0x80000000;
	return *(float *)&ix;
}

float gb_remainder(float x, float y) {
	return x - (gb_round(x/y)*y);
}

float gb_mod(float x, float y) {
	float result;
	y = gb_abs(y);
	result = gb_remainder(gb_abs(x), y);
	if (gb_sign(result)) result += y;
	return gb_copy_sign(result, x);
}


float gb_quake_rsqrt(float a) {
	union {
		int i;
		float f;
	} t;
	float x2;
	float const three_halfs = 1.5f;

	x2 = a * 0.5f;
	t.f = a;
	t.i = 0x5f375a86 - (t.i >> 1);                /* What the fuck? */
	t.f = t.f * (three_halfs - (x2 * t.f * t.f)); /* 1st iteration */
	t.f = t.f * (three_halfs - (x2 * t.f * t.f)); /* 2nd iteration, this can be removed */

	return t.f;
}


#if defined(GB_MATH_NO_MATH_H)
#if defined(_MSC_VER)

	float gb_rsqrt(float a) { return _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(a))); }
	float gb_sqrt(float a)  { return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(a))); };
	float
	gb_sin(float a)
	{
		static float const a0 = +1.91059300966915117e-31f;
		static float const a1 = +1.00086760103908896f;
		static float const a2 = -1.21276126894734565e-2f;
		static float const a3 = -1.38078780785773762e-1f;
		static float const a4 = -2.67353392911981221e-2f;
		static float const a5 = +2.08026600266304389e-2f;
		static float const a6 = -3.03996055049204407e-3f;
		static float const a7 = +1.38235642404333740e-4f;
		return a0 + a*(a1 + a*(a2 + a*(a3 + a*(a4 + a*(a5 + a*(a6 + a*a7))))));
	}
	float
	gb_cos(float a)
	{
		static float const a0 = +1.00238601909309722f;
		static float const a1 = -3.81919947353040024e-2f;
		static float const a2 = -3.94382342128062756e-1f;
		static float const a3 = -1.18134036025221444e-1f;
		static float const a4 = +1.07123798512170878e-1f;
		static float const a5 = -1.86637164165180873e-2f;
		static float const a6 = +9.90140908664079833e-4f;
		static float const a7 = -5.23022132118824778e-14f;
		return a0 + a*(a1 + a*(a2 + a*(a3 + a*(a4 + a*(a5 + a*(a6 + a*a7))))));
	}

	float
	gb_tan(float radians)
	{
		float rr = radians*radians;
		float a = 9.5168091e-03f;
		a *= rr;
		a += 2.900525e-03f;
		a *= rr;
		a += 2.45650893e-02f;
		a *= rr;
		a += 5.33740603e-02f;
		a *= rr;
		a += 1.333923995e-01f;
		a *= rr;
		a += 3.333314036e-01f;
		a *= rr;
		a += 1.0f;
		a *= radians;
		return a;
	}

	float gb_arcsin(float a) { return gb_arctan2(a, gb_sqrt((1.0f + a) * (1.0f - a))); }
	float gb_arccos(float a) { return gb_arctan2(gb_sqrt((1.0f + a) * (1.0 - a)), a); }

	float
	gb_arctan(float a)
	{
		float u  = a*a;
		float u2 = u*u;
		float u3 = u2*u;
		float u4 = u3*u;
		float f  = 1.0f+0.33288950512027f*u-0.08467922817644f*u2+0.03252232640125f*u3-0.00749305860992f*u4;
		return a/f;
	}

	float
	gb_arctan2(float y, float x)
	{
		if (gb_abs(x) > gb_abs(y)) {
			float a = gb_arctan(y/x);
			if (x > 0.0f)
				return a;
			else
				return y > 0.0f ? a+GB_MATH_TAU_OVER_2:a-GB_MATH_TAU_OVER_2;
		} else {
			float a = gb_arctan(x/y);
			if (x > 0.0f)
				return y > 0.0f ? GB_MATH_TAU_OVER_4-a:-GB_MATH_TAU_OVER_4-a;
			else
				return y > 0.0f ? GB_MATH_TAU_OVER_4+a:-GB_MATH_TAU_OVER_4+a;
		}
	}

	float
	gb_exp(float a)
	{
		union { float f; int i; } u, v;
		u.i = (int)(6051102 * a + 1056478197);
		v.i = (int)(1056478197 - 6051102 * a);
		return u.f / v.f;
	}

	float
	gb_log(float a)
	{
		union { float f; int i; } u = {a};
		return (u.i - 1064866805) * 8.262958405176314e-8f; /* 1 / 12102203.0; */
	}

	float
	gb_pow(float a, float b)
	{
		int flipped = 0, e;
		float f, r = 1.0f;
		if (b < 0) {
			flipped = 1;
			b = -b;
		}

		e = (int)b;
		f = gb_exp(b - e);

		while (e) {
			if (e & 1) r *= a;
			a *= a;
			e >>= 1;
		}

		r *= f;
		return flipped ? 1.0f/r : r;
	}

#else

	float gb_rsqrt(float a)            { return 1.0f/__builtin_sqrt(a); }
	float gb_sqrt(float a)             { return __builtin_sqrt(a); }
	float gb_sin(float radians)        { return __builtin_sinf(radians); }
	float gb_cos(float radians)        { return __builtin_cosf(radians); }
	float gb_tan(float radians)        { return __builtin_tanf(radians); }
	float gb_arcsin(float a)           { return __builtin_asinf(a); }
	float gb_arccos(float a)           { return __builtin_acosf(a); }
	float gb_arctan(float a)           { return __builtin_atanf(a); }
	float gb_arctan2(float y, float x) { return __builtin_atan2f(y, x); }


	float gb_exp(float x)  { return __builtin_expf(x); }
	float gb_log(float x)  { return __builtin_logf(x); }

	// TODO(bill): Should this be gb_exp(y * gb_log(x)) ???
	float gb_pow(float x, float y) { return __builtin_powf(x, y); }

#endif

#else
	float gb_rsqrt(float a)            { return 1.0f/sqrtf(a); }
	float gb_sqrt(float a)             { return sqrtf(a); };
	float gb_sin(float radians)        { return sinf(radians); };
	float gb_cos(float radians)        { return cosf(radians); };
	float gb_tan(float radians)        { return tanf(radians); };
	float gb_arcsin(float a)           { return asinf(a); };
	float gb_arccos(float a)           { return acosf(a); };
	float gb_arctan(float a)           { return atanf(a); };
	float gb_arctan2(float y, float x) { return atan2f(y, x); };

	float gb_exp(float x)          { return expf(x); }
	float gb_log(float x)          { return logf(x); }
	float gb_pow(float x, float y) { return powf(x, y); }
#endif

float gb_exp2(float x) { return gb_exp(GB_MATH_LOG_TWO * x); }
float gb_log2(float x) { return gb_log(x) / GB_MATH_LOG_TWO; }


float gb_fast_exp(float x) {
	/* NOTE(bill): Only works in the range -1 <= x <= +1 */
	float e = 1.0f + x*(1.0f + x*0.5f*(1.0f + x*0.3333333333f*(1.0f + x*0.25f*(1.0f + x*0.2f))));
	return e;
}

float gb_fast_exp2(float x) { return gb_fast_exp(GB_MATH_LOG_TWO * x); }



float gb_round(float x) { return (float)((x >= 0.0f) ? gb_floor(x + 0.5f) : gb_ceil(x - 0.5f)); }
float gb_floor(float x) { return (float)((x >= 0.0f) ? (int)x : (int)(x-0.9999999999999999f)); }
float gb_ceil(float x)  { return (float)((x < 0) ? (int)x : ((int)x)+1); }





float gb_half_to_float(gbHalf value) {
	union { unsigned int i; float f; } result;
	int s = (value >> 15) & 0x001;
	int e = (value >> 10) & 0x01f;
	int m =  value        & 0x3ff;

	if (e == 0) {
		if (m == 0) {
			/* Plus or minus zero */
			result.i = (unsigned int)(s << 31);
			return result.f;
		} else {
			/* Denormalized number */
			while (!(m & 0x00000400)) {
				m <<= 1;
				e -=  1;
			}

			e += 1;
			m &= ~0x00000400;
		}
	} else if (e == 31) {
		if (m == 0) {
			/* Positive or negative infinity */
			result.i = (unsigned int)((s << 31) | 0x7f800000);
			return result.f;
		} else {
			/* Nan */
			result.i = (unsigned int)((s << 31) | 0x7f800000 | (m << 13));
			return result.f;
		}
	}

	e = e + (127 - 15);
	m = m << 13;

	result.i = (unsigned int)((s << 31) | (e << 23) | m);
	return result.f;
}

gbHalf gb_float_to_half(float value) {
	union { unsigned int i; float f; } v;
	int i, s, e, m;

	v.f = value;
	i = (int)v.i;

	s =  (i >> 16) & 0x00008000;
	e = ((i >> 23) & 0x000000ff) - (127 - 15);
	m =   i        & 0x007fffff;


	if (e <= 0) {
		if (e < -10) return (gbHalf)s;
		m = (m | 0x00800000) >> (1 - e);

		if (m & 0x00001000)
			m += 0x00002000;

		return (gbHalf)(s | (m >> 13));
	} else if (e == 0xff - (127 - 15)) {
		if (m == 0) {
			return (gbHalf)(s | 0x7c00); /* NOTE(bill): infinity */
		} else {
			/* NOTE(bill): NAN */
			m >>= 13;
			return (gbHalf)(s | 0x7c00 | m | (m == 0));
		}
	} else {
		if (m & 0x00001000) {
			m += 0x00002000;
			if (m & 0x00800000) {
				m = 0;
				e += 1;
			}
		}

		if (e > 30) {
			float volatile f = 1e12f;
			int j;
			for (j = 0; j < 10; j++)
				f *= f; /* NOTE(bill): Cause overflow */

			return (gbHalf)(s | 0x7c00);
		}

		return (gbHalf)(s | (e << 10) | (m >> 13));
	}
}







#define GB_VEC2_2OP(a,c,post)  \
	a->x =        c.x post;    \
	a->y =        c.y post;

#define GB_VEC2_3OP(a,b,op,c,post) \
	a->x = b.x op c.x post;        \
	a->y = b.y op c.y post;

#define GB_VEC3_2OP(a,c,post) \
	a->x =        c.x post;   \
	a->y =        c.y post;   \
	a->z =        c.z post;

#define GB_VEC3_3OP(a,b,op,c,post) \
	a->x = b.x op c.x post;        \
	a->y = b.y op c.y post;        \
	a->z = b.z op c.z post;

#define GB_VEC4_2OP(a,c,post) \
	a->x =        c.x post;   \
	a->y =        c.y post;   \
	a->z =        c.z post;   \
	a->w =        c.w post;

#define GB_VEC4_3OP(a,b,op,c,post) \
	a->x = b.x op c.x post;        \
	a->y = b.y op c.y post;        \
	a->z = b.z op c.z post;        \
	a->w = b.w op c.w post;


gbVec2 gb_vec2_zero(void)        { gbVec2 v = {0, 0};                return v; }
gbVec2 gb_vec2(float x, float y) { gbVec2 v; v.x = x;    v.y = y;    return v; }
gbVec2 gb_vec2v(float x[2])      { gbVec2 v; v.x = x[0]; v.y = x[1]; return v; }

gbVec3 gb_vec3_zero(void)                 { gbVec3 v = {0, 0, 0};                         return v; }
gbVec3 gb_vec3(float x, float y, float z) { gbVec3 v; v.x = x; v.y = y; v.z = z;          return v; }
gbVec3 gb_vec3v(float x[3])               { gbVec3 v; v.x = x[0]; v.y = x[1]; v.z = x[2]; return v; }

gbVec4 gb_vec4_zero(void)                          { gbVec4 v = {0, 0, 0, 0};                                  return v; }
gbVec4 gb_vec4(float x, float y, float z, float w) { gbVec4 v; v.x = x; v.y = y; v.z = z; v.w = w;             return v; }
gbVec4 gb_vec4v(float x[4])                        { gbVec4 v; v.x = x[0]; v.y = x[1]; v.z = x[2]; v.w = x[3]; return v; }


void gb_vec2_add(gbVec2 *d, gbVec2 v0, gbVec2 v1) { GB_VEC2_3OP(d,v0,+,v1,+0); }
void gb_vec2_sub(gbVec2 *d, gbVec2 v0, gbVec2 v1) { GB_VEC2_3OP(d,v0,-,v1,+0); }
void gb_vec2_mul(gbVec2 *d, gbVec2 v,  float s)   { GB_VEC2_2OP(d,v,* s);      }
void gb_vec2_div(gbVec2 *d, gbVec2 v,  float s)   { GB_VEC2_2OP(d,v,/ s);      }

void gb_vec3_add(gbVec3 *d, gbVec3 v0, gbVec3 v1) { GB_VEC3_3OP(d,v0,+,v1,+0); }
void gb_vec3_sub(gbVec3 *d, gbVec3 v0, gbVec3 v1) { GB_VEC3_3OP(d,v0,-,v1,+0); }
void gb_vec3_mul(gbVec3 *d, gbVec3 v,  float s)   { GB_VEC3_2OP(d,v,* s);      }
void gb_vec3_div(gbVec3 *d, gbVec3 v,  float s)   { GB_VEC3_2OP(d,v,/ s);      }

void gb_vec4_add(gbVec4 *d, gbVec4 v0, gbVec4 v1) { GB_VEC4_3OP(d,v0,+,v1,+0); }
void gb_vec4_sub(gbVec4 *d, gbVec4 v0, gbVec4 v1) { GB_VEC4_3OP(d,v0,-,v1,+0); }
void gb_vec4_mul(gbVec4 *d, gbVec4 v,  float s)   { GB_VEC4_2OP(d,v,* s);      }
void gb_vec4_div(gbVec4 *d, gbVec4 v,  float s)   { GB_VEC4_2OP(d,v,/ s);      }


void gb_vec2_addeq(gbVec2 *d, gbVec2 v) { GB_VEC2_3OP(d,(*d),+,v,+0); }
void gb_vec2_subeq(gbVec2 *d, gbVec2 v) { GB_VEC2_3OP(d,(*d),-,v,+0); }
void gb_vec2_muleq(gbVec2 *d, float s)  { GB_VEC2_2OP(d,(*d),* s);    }
void gb_vec2_diveq(gbVec2 *d, float s)  { GB_VEC2_2OP(d,(*d),/ s);    }

void gb_vec3_addeq(gbVec3 *d, gbVec3 v) { GB_VEC3_3OP(d,(*d),+,v,+0); }
void gb_vec3_subeq(gbVec3 *d, gbVec3 v) { GB_VEC3_3OP(d,(*d),-,v,+0); }
void gb_vec3_muleq(gbVec3 *d, float s)  { GB_VEC3_2OP(d,(*d),* s);    }
void gb_vec3_diveq(gbVec3 *d, float s)  { GB_VEC3_2OP(d,(*d),/ s);    }

void gb_vec4_addeq(gbVec4 *d, gbVec4 v) { GB_VEC4_3OP(d,(*d),+,v,+0); }
void gb_vec4_subeq(gbVec4 *d, gbVec4 v) { GB_VEC4_3OP(d,(*d),-,v,+0); }
void gb_vec4_muleq(gbVec4 *d, float s)  { GB_VEC4_2OP(d,(*d),* s);    }
void gb_vec4_diveq(gbVec4 *d, float s)  { GB_VEC4_2OP(d,(*d),/ s);    }


#undef GB_VEC2_2OP
#undef GB_VEC2_3OP
#undef GB_VEC3_3OP
#undef GB_VEC3_2OP
#undef GB_VEC4_2OP
#undef GB_VEC4_3OP




float gb_vec2_dot(gbVec2 v0, gbVec2 v1) { return v0.x*v1.x + v0.y*v1.y; }
float gb_vec3_dot(gbVec3 v0, gbVec3 v1) { return v0.x*v1.x + v0.y*v1.y + v0.z*v1.z; }
float gb_vec4_dot(gbVec4 v0, gbVec4 v1) { return v0.x*v1.x + v0.y*v1.y + v0.z*v1.z + v0.w*v1.w; }

void gb_vec2_cross(float *d, gbVec2 v0, gbVec2 v1) { *d = v0.x*v1.y - v1.x*v0.y; }
void gb_vec3_cross(gbVec3 *d, gbVec3 v0, gbVec3 v1)  { d->x = v0.y*v1.z - v0.z*v1.y;
	                                          d->y = v0.z*v1.x - v0.x*v1.z;
	                                          d->z = v0.x*v1.y - v0.y*v1.x; }

float gb_vec2_mag2(gbVec2 v) { return gb_vec2_dot(v, v); }
float gb_vec3_mag2(gbVec3 v) { return gb_vec3_dot(v, v); }
float gb_vec4_mag2(gbVec4 v) { return gb_vec4_dot(v, v); }

/* TODO(bill): Create custom sqrt function */
float gb_vec2_mag(gbVec2 v) { return gb_sqrt(gb_vec2_dot(v, v)); }
float gb_vec3_mag(gbVec3 v) { return gb_sqrt(gb_vec3_dot(v, v)); }
float gb_vec4_mag(gbVec4 v) { return gb_sqrt(gb_vec4_dot(v, v)); }

void gb_vec2_norm(gbVec2 *d, gbVec2 v) {
	float inv_mag = gb_rsqrt(gb_vec2_dot(v, v));
	gb_vec2_mul(d, v, inv_mag);
}
void gb_vec3_norm(gbVec3 *d, gbVec3 v) {
	float mag = gb_vec3_mag(v);
	gb_vec3_div(d, v, mag);
}
void gb_vec4_norm(gbVec4 *d, gbVec4 v) {
	float mag = gb_vec4_mag(v);
	gb_vec4_div(d, v, mag);
}

void gb_vec2_norm0(gbVec2 *d, gbVec2 v) {
	float mag = gb_vec2_mag(v);
	if (mag > 0)
		gb_vec2_div(d, v, mag);
	else
		*d = gb_vec2_zero();
}
void gb_vec3_norm0(gbVec3 *d, gbVec3 v) {
	float mag = gb_vec3_mag(v);
	if (mag > 0)
		gb_vec3_div(d, v, mag);
	else
		*d = gb_vec3_zero();
}
void gb_vec4_norm0(gbVec4 *d, gbVec4 v) {
	float mag = gb_vec4_mag(v);
	if (mag > 0)
		gb_vec4_div(d, v, mag);
	else
		*d = gb_vec4_zero();
}


void gb_vec2_reflect(gbVec2 *d, gbVec2 i, gbVec2 n) {
	gbVec2 b = n;
	gb_vec2_muleq(&b, 2.0f*gb_vec2_dot(n, i));
	gb_vec2_sub(d, i, b);
}

void gb_vec3_reflect(gbVec3 *d, gbVec3 i, gbVec3 n) {
	gbVec3 b = n;
	gb_vec3_muleq(&b, 2.0f*gb_vec3_dot(n, i));
	gb_vec3_sub(d, i, b);
}

void gb_vec2_refract(gbVec2 *d, gbVec2 i, gbVec2 n, float eta) {
	gbVec2 a, b;
	float dv, k;

	dv = gb_vec2_dot(n, i);
	k = 1.0f - eta*eta * (1.0f - dv*dv);
	gb_vec2_mul(&a, i, eta);
	gb_vec2_mul(&b, n, eta*dv*gb_sqrt(k));
	gb_vec2_sub(d, a, b);
	gb_vec2_muleq(d, (float)(k >= 0.0f));
}

void gb_vec3_refract(gbVec3 *d, gbVec3 i, gbVec3 n, float eta) {
	gbVec3 a, b;
	float dv, k;

	dv = gb_vec3_dot(n, i);
	k = 1.0f - eta*eta * (1.0f - dv*dv);
	gb_vec3_mul(&a, i, eta);
	gb_vec3_mul(&b, n, eta*dv*gb_sqrt(k));
	gb_vec3_sub(d, a, b);
	gb_vec3_muleq(d, (float)(k >= 0.0f));
}





float gb_vec2_aspect_ratio(gbVec2 v) { return (v.y < 0.0001f) ? 0.0f : v.x/v.y; }





void gb_mat2_transpose(gbMat2 *m) { gb_float22_transpose(gb_float22_m(m)); }
void gb_mat2_identity(gbMat2 *m)  { gb_float22_identity(gb_float22_m(m));  }
void gb_mat2_mul(gbMat2 *out, gbMat2 *m1, gbMat2 *m2) { gb_float22_mul(gb_float22_m(out), gb_float22_m(m1), gb_float22_m(m2)); }

void gb_float22_identity(float m[2][2]) {
	m[0][0] = 1; m[0][1] = 0;
	m[1][0] = 0; m[1][1] = 1;
}

void gb_mat2_mul_vec2(gbVec2 *out, gbMat2 *m, gbVec2 in) { gb_float22_mul_vec2(out, gb_float22_m(m), in); }

gbMat2 *gb_mat2_v(gbVec2 m[2])   { return (gbMat2 *)m; }
gbMat2 *gb_mat2_f(float m[2][2]) { return (gbMat2 *)m; }

gbFloat2 *gb_float22_m(gbMat2 *m)   { return (gbFloat2 *)m; }
gbFloat2 *gb_float22_v(gbVec2 m[2]) { return (gbFloat2 *)m; }
gbFloat2 *gb_float22_4(float m[4])  { return (gbFloat2 *)m; }

void gb_float22_transpose(float (*vec)[2]) {
	int i, j;
	for (j = 0; j < 2; j++) {
		for (i = j + 1; i < 2; i++) {
			float t = vec[i][j];
			vec[i][j] = vec[j][i];
			vec[j][i] = t;
		}
	}
}



void gb_float22_mul(float (*out)[2], float (*mat1)[2], float (*mat2)[2]) {
	int i, j;
	float temp1[2][2], temp2[2][2];
	if (mat1 == out) { gb__memcpy_4byte(temp1, mat1, sizeof(temp1)); mat1 = temp1; }
	if (mat2 == out) { gb__memcpy_4byte(temp2, mat2, sizeof(temp2)); mat2 = temp2; }
	for (j = 0; j < 2; j++) {
		for (i = 0; i < 2; i++) {
			out[j][i] = mat1[0][i]*mat2[j][0]
			          + mat1[1][i]*mat2[j][1];
		}
	}
}

void gb_float22_mul_vec2(gbVec2 *out, float m[2][2], gbVec2 v) {
	out->x = m[0][0]*v.x + m[0][1]*v.y;
	out->y = m[1][0]*v.x + m[1][1]*v.y;
}

float gb_mat2_determinate(gbMat2 *m) {
	gbFloat2 *e = gb_float22_m(m);
	return e[0][0]*e[1][1] - e[1][0]*e[0][1];
}

void gb_mat2_inverse(gbMat2 *out, gbMat2 *in) {
	gbFloat2 *o = gb_float22_m(out);
	gbFloat2 *i = gb_float22_m(in);

	float ood = 1.0f / gb_mat2_determinate(in);

	o[0][0] = +i[1][1] * ood;
	o[0][1] = -i[0][1] * ood;
	o[1][0] = -i[1][0] * ood;
	o[1][1] = +i[0][0] * ood;
}






void gb_mat3_transpose(gbMat3 *m) { gb_float33_transpose(gb_float33_m(m)); }
void gb_mat3_identity(gbMat3 *m)  { gb_float33_identity(gb_float33_m(m));  }
void gb_mat3_mul(gbMat3 *out, gbMat3 *m1, gbMat3 *m2) { gb_float33_mul(gb_float33_m(out), gb_float33_m(m1), gb_float33_m(m2)); }

void gb_float33_identity(float m[3][3]) {
	m[0][0] = 1; m[0][1] = 0; m[0][2] = 0;
	m[1][0] = 0; m[1][1] = 1; m[1][2] = 0;
	m[2][0] = 0; m[2][1] = 0; m[2][2] = 1;
}

void gb_mat3_mul_vec3(gbVec3 *out, gbMat3 *m, gbVec3 in) { gb_float33_mul_vec3(out, gb_float33_m(m), in); }

gbMat3 *gb_mat3_v(gbVec3 m[3])   { return (gbMat3 *)m; }
gbMat3 *gb_mat3_f(float m[3][3]) { return (gbMat3 *)m; }

gbFloat3 *gb_float33_m(gbMat3 *m)   { return (gbFloat3 *)m; }
gbFloat3 *gb_float33_v(gbVec3 m[3]) { return (gbFloat3 *)m; }
gbFloat3 *gb_float33_16(float m[9]) { return (gbFloat3 *)m; }

void gb_float33_transpose(float (*vec)[3]) {
	int i, j;
	for (j = 0; j < 3; j++) {
		for (i = j + 1; i < 3; i++) {
			float t = vec[i][j];
			vec[i][j] = vec[j][i];
			vec[j][i] = t;
		}
	}
}

void gb_float33_mul(float (*out)[3], float (*mat1)[3], float (*mat2)[3]) {
	int i, j;
	float temp1[3][3], temp2[3][3];
	if (mat1 == out) { gb__memcpy_4byte(temp1, mat1, sizeof(temp1)); mat1 = temp1; }
	if (mat2 == out) { gb__memcpy_4byte(temp2, mat2, sizeof(temp2)); mat2 = temp2; }
	for (j = 0; j < 3; j++) {
		for (i = 0; i < 3; i++) {
			out[j][i] = mat1[0][i]*mat2[j][0]
			          + mat1[1][i]*mat2[j][1]
			          + mat1[2][i]*mat2[j][2];
		}
	}
}

void gb_float33_mul_vec3(gbVec3 *out, float m[3][3], gbVec3 v) {
	out->x = m[0][0]*v.x + m[0][1]*v.y + m[0][2]*v.z;
	out->y = m[1][0]*v.x + m[1][1]*v.y + m[1][2]*v.z;
	out->z = m[2][0]*v.x + m[2][1]*v.y + m[2][2]*v.z;
}



float gb_mat3_determinate(gbMat3 *m) {
	gbFloat3 *e = gb_float33_m(m);
	float d = +e[0][0] * (e[1][1] * e[2][2] - e[1][2] * e[2][1])
	          -e[0][1] * (e[1][0] * e[2][2] - e[1][2] * e[2][0])
	          +e[0][2] * (e[1][0] * e[2][1] - e[1][1] * e[2][0]);
	return d;
}

void gb_mat3_inverse(gbMat3 *out, gbMat3 *in) {
	gbFloat3 *o = gb_float33_m(out);
	gbFloat3 *i = gb_float33_m(in);

	float ood = 1.0f / gb_mat3_determinate(in);

	o[0][0] = +(i[1][1] * i[2][2] - i[2][1] * i[1][2]) * ood;
	o[0][1] = -(i[1][0] * i[2][2] - i[2][0] * i[1][2]) * ood;
	o[0][2] = +(i[1][0] * i[2][1] - i[2][0] * i[1][1]) * ood;
	o[1][0] = -(i[0][1] * i[2][2] - i[2][1] * i[0][2]) * ood;
	o[1][1] = +(i[0][0] * i[2][2] - i[2][0] * i[0][2]) * ood;
	o[1][2] = -(i[0][0] * i[2][1] - i[2][0] * i[0][1]) * ood;
	o[2][0] = +(i[0][1] * i[1][2] - i[1][1] * i[0][2]) * ood;
	o[2][1] = -(i[0][0] * i[1][2] - i[1][0] * i[0][2]) * ood;
	o[2][2] = +(i[0][0] * i[1][1] - i[1][0] * i[0][1]) * ood;
}











void gb_mat4_transpose(gbMat4 *m) { gb_float44_transpose(gb_float44_m(m)); }
void gb_mat4_identity(gbMat4 *m)  { gb_float44_identity(gb_float44_m(m));  }
void gb_mat4_mul(gbMat4 *out, gbMat4 *m1, gbMat4 *m2) { gb_float44_mul(gb_float44_m(out), gb_float44_m(m1), gb_float44_m(m2)); }

void gb_float44_identity(float m[4][4]) {
	m[0][0] = 1; m[0][1] = 0; m[0][2] = 0; m[0][3] = 0;
	m[1][0] = 0; m[1][1] = 1; m[1][2] = 0; m[1][3] = 0;
	m[2][0] = 0; m[2][1] = 0; m[2][2] = 1; m[2][3] = 0;
	m[3][0] = 0; m[3][1] = 0; m[3][2] = 0; m[3][3] = 1;
}

void gb_mat4_mul_vec4(gbVec4 *out, gbMat4 *m, gbVec4 in) {
	gb_float44_mul_vec4(out, gb_float44_m(m), in);
}

gbMat4 *gb_mat4_v(gbVec4 m[4])   { return (gbMat4 *)m; }
gbMat4 *gb_mat4_f(float m[4][4]) { return (gbMat4 *)m; }

gbFloat4 *gb_float44_m(gbMat4 *m)    { return (gbFloat4 *)m; }
gbFloat4 *gb_float44_v(gbVec4 m[4])  { return (gbFloat4 *)m; }
gbFloat4 *gb_float44_16(float m[16]) { return (gbFloat4 *)m; }

void gb_float44_transpose(float (*vec)[4]) {
	float tmp;
	tmp = vec[1][0]; vec[1][0] = vec[0][1]; vec[0][1] = tmp;
	tmp = vec[2][0]; vec[2][0] = vec[0][2]; vec[0][2] = tmp;
	tmp = vec[3][0]; vec[3][0] = vec[0][3]; vec[0][3] = tmp;
	tmp = vec[2][1]; vec[2][1] = vec[1][2]; vec[1][2] = tmp;
	tmp = vec[3][1]; vec[3][1] = vec[1][3]; vec[1][3] = tmp;
	tmp = vec[3][2]; vec[3][2] = vec[2][3]; vec[2][3] = tmp;
}

void gb_float44_mul(float (*out)[4], float (*mat1)[4], float (*mat2)[4]) {
	int i, j;
	float temp1[4][4], temp2[4][4];
	if (mat1 == out) { gb__memcpy_4byte(temp1, mat1, sizeof(temp1)); mat1 = temp1; }
	if (mat2 == out) { gb__memcpy_4byte(temp2, mat2, sizeof(temp2)); mat2 = temp2; }
	for (j = 0; j < 4; j++) {
		for (i = 0; i < 4; i++) {
			out[j][i] = mat1[0][i]*mat2[j][0]
			          + mat1[1][i]*mat2[j][1]
			          + mat1[2][i]*mat2[j][2]
			          + mat1[3][i]*mat2[j][3];
		}
	}
}

void gb_float44_mul_vec4(gbVec4 *out, float m[4][4], gbVec4 v) {
	out->x = m[0][0]*v.x + m[1][0]*v.y + m[2][0]*v.z + m[3][0]*v.w;
	out->y = m[0][1]*v.x + m[1][1]*v.y + m[2][1]*v.z + m[3][1]*v.w;
	out->z = m[0][2]*v.x + m[1][2]*v.y + m[2][2]*v.z + m[3][2]*v.w;
	out->w = m[0][3]*v.x + m[1][3]*v.y + m[2][3]*v.z + m[3][3]*v.w;
}

void gb_mat4_inverse(gbMat4 *out, gbMat4 *in) {
	gbFloat4 *o = gb_float44_m(out);
	gbFloat4 *m = gb_float44_m(in);

	float ood;

	float sf00 = m[2][2] * m[3][3] - m[3][2] * m[2][3];
	float sf01 = m[2][1] * m[3][3] - m[3][1] * m[2][3];
	float sf02 = m[2][1] * m[3][2] - m[3][1] * m[2][2];
	float sf03 = m[2][0] * m[3][3] - m[3][0] * m[2][3];
	float sf04 = m[2][0] * m[3][2] - m[3][0] * m[2][2];
	float sf05 = m[2][0] * m[3][1] - m[3][0] * m[2][1];
	float sf06 = m[1][2] * m[3][3] - m[3][2] * m[1][3];
	float sf07 = m[1][1] * m[3][3] - m[3][1] * m[1][3];
	float sf08 = m[1][1] * m[3][2] - m[3][1] * m[1][2];
	float sf09 = m[1][0] * m[3][3] - m[3][0] * m[1][3];
	float sf10 = m[1][0] * m[3][2] - m[3][0] * m[1][2];
	float sf11 = m[1][1] * m[3][3] - m[3][1] * m[1][3];
	float sf12 = m[1][0] * m[3][1] - m[3][0] * m[1][1];
	float sf13 = m[1][2] * m[2][3] - m[2][2] * m[1][3];
	float sf14 = m[1][1] * m[2][3] - m[2][1] * m[1][3];
	float sf15 = m[1][1] * m[2][2] - m[2][1] * m[1][2];
	float sf16 = m[1][0] * m[2][3] - m[2][0] * m[1][3];
	float sf17 = m[1][0] * m[2][2] - m[2][0] * m[1][2];
	float sf18 = m[1][0] * m[2][1] - m[2][0] * m[1][1];

	o[0][0] = +(m[1][1] * sf00 - m[1][2] * sf01 + m[1][3] * sf02);
	o[1][0] = -(m[1][0] * sf00 - m[1][2] * sf03 + m[1][3] * sf04);
	o[2][0] = +(m[1][0] * sf01 - m[1][1] * sf03 + m[1][3] * sf05);
	o[3][0] = -(m[1][0] * sf02 - m[1][1] * sf04 + m[1][2] * sf05);

	o[0][1] = -(m[0][1] * sf00 - m[0][2] * sf01 + m[0][3] * sf02);
	o[1][1] = +(m[0][0] * sf00 - m[0][2] * sf03 + m[0][3] * sf04);
	o[2][1] = -(m[0][0] * sf01 - m[0][1] * sf03 + m[0][3] * sf05);
	o[3][1] = +(m[0][0] * sf02 - m[0][1] * sf04 + m[0][2] * sf05);

	o[0][2] = +(m[0][1] * sf06 - m[0][2] * sf07 + m[0][3] * sf08);
	o[1][2] = -(m[0][0] * sf06 - m[0][2] * sf09 + m[0][3] * sf10);
	o[2][2] = +(m[0][0] * sf11 - m[0][1] * sf09 + m[0][3] * sf12);
	o[3][2] = -(m[0][0] * sf08 - m[0][1] * sf10 + m[0][2] * sf12);

	o[0][3] = -(m[0][1] * sf13 - m[0][2] * sf14 + m[0][3] * sf15);
	o[1][3] = +(m[0][0] * sf13 - m[0][2] * sf16 + m[0][3] * sf17);
	o[2][3] = -(m[0][0] * sf14 - m[0][1] * sf16 + m[0][3] * sf18);
	o[3][3] = +(m[0][0] * sf15 - m[0][1] * sf17 + m[0][2] * sf18);

	ood = 1.0f / (m[0][0] * o[0][0] +
				  m[0][1] * o[1][0] +
				  m[0][2] * o[2][0] +
				  m[0][3] * o[3][0]);

	o[0][0] *= ood;
	o[0][1] *= ood;
	o[0][2] *= ood;
	o[0][3] *= ood;
	o[1][0] *= ood;
	o[1][1] *= ood;
	o[1][2] *= ood;
	o[1][3] *= ood;
	o[2][0] *= ood;
	o[2][1] *= ood;
	o[2][2] *= ood;
	o[2][3] *= ood;
	o[3][0] *= ood;
	o[3][1] *= ood;
	o[3][2] *= ood;
	o[3][3] *= ood;
}







void gb_mat4_translate(gbMat4 *out, gbVec3 v) {
	gb_mat4_identity(out);
	out->col[3].xyz = v;
	out->col[3].w  = 1;
}

void gb_mat4_rotate(gbMat4 *out, gbVec3 v, float angle_radians) {
	float c, s;
	gbVec3 axis, t;
	gbFloat4 *rot;

	c = gb_cos(angle_radians);
	s = gb_sin(angle_radians);

	gb_vec3_norm(&axis, v);
	gb_vec3_mul(&t, axis, 1.0f-c);

	gb_mat4_identity(out);
	rot = gb_float44_m(out);

	rot[0][0] = c + t.x*axis.x;
	rot[0][1] = 0 + t.x*axis.y + s*axis.z;
	rot[0][2] = 0 + t.x*axis.z - s*axis.y;
	rot[0][3] = 0;

	rot[1][0] = 0 + t.y*axis.x - s*axis.z;
	rot[1][1] = c + t.y*axis.y;
	rot[1][2] = 0 + t.y*axis.z + s*axis.x;
	rot[1][3] = 0;

	rot[2][0] = 0 + t.z*axis.x + s*axis.y;
	rot[2][1] = 0 + t.z*axis.y - s*axis.x;
	rot[2][2] = c + t.z*axis.z;
	rot[2][3] = 0;
}

void gb_mat4_scale(gbMat4 *out, gbVec3 v) {
	gb_mat4_identity(out);
	out->e[0]  = v.x;
	out->e[5]  = v.y;
	out->e[10] = v.z;
}

void gb_mat4_scalef(gbMat4 *out, float s) {
	gb_mat4_identity(out);
	out->e[0]  = s;
	out->e[5]  = s;
	out->e[10] = s;
}


void gb_mat4_ortho2d(gbMat4 *out, float left, float right, float bottom, float top) {
	gbFloat4 *m;
	gb_mat4_identity(out);
	m = gb_float44_m(out);

	m[0][0] = 2.0f / (right - left);
	m[1][1] = 2.0f / (top - bottom);
	m[2][2] = -1.0f;
	m[3][0] = -(right + left) / (right - left);
	m[3][1] = -(top + bottom) / (top - bottom);
}

void gb_mat4_ortho3d(gbMat4 *out, float left, float right, float bottom, float top, float z_near, float z_far) {
	gbFloat4 *m;
	gb_mat4_identity(out);
	m = gb_float44_m(out);

	m[0][0] = +2.0f / (right - left);
	m[1][1] = +2.0f / (top - bottom);
	m[2][2] = -2.0f / (z_far - z_near);
	m[3][0] = -(right + left)   / (right - left);
	m[3][1] = -(top   + bottom) / (top   - bottom);
	m[3][2] = -(z_far + z_near) / (z_far - z_near);
}


void gb_mat4_perspective(gbMat4 *out, float fovy, float aspect, float z_near, float z_far) {
	float tan_half_fovy = gb_tan(0.5f * fovy);
	gbMat4 zero_mat = {0};
	gbFloat4 *m = gb_float44_m(out);
	*out = zero_mat;

	m[0][0] = 1.0f / (aspect*tan_half_fovy);
	m[1][1] = 1.0f / (tan_half_fovy);
	m[2][2] = -(z_far + z_near) / (z_far - z_near);
	m[2][3] = -1.0f;
	m[3][2] = -2.0f*z_far*z_near / (z_far - z_near);
}

void gb_mat4_infinite_perspective(gbMat4 *out, float fovy, float aspect, float z_near) {
	float range  = gb_tan(0.5f * fovy) * z_near;
	float left   = -range * aspect;
	float right  =  range * aspect;
	float bottom = -range;
	float top    =  range;
	gbMat4 zero_mat = {0};
	gbFloat4 *m = gb_float44_m(out);
	*out = zero_mat;

	m[0][0] = (2.0f*z_near) / (right - left);
	m[1][1] = (2.0f*z_near) / (top   - bottom);
	m[2][2] = -1.0f;
	m[2][3] = -1.0f;
	m[3][2] = -2.0f*z_near;
}

void gb_mat4_look_at(gbMat4 *out, gbVec3 eye, gbVec3 centre, gbVec3 up) {
	gbVec3 f, s, u;
	gbFloat4 *m;

	gb_vec3_sub(&f, centre, eye);
	gb_vec3_norm(&f, f);

	gb_vec3_cross(&s, f, up);
	gb_vec3_norm(&s, s);

	gb_vec3_cross(&u, s, f);

	gb_mat4_identity(out);
	m = gb_float44_m(out);

	m[0][0] = +s.x;
	m[1][0] = +s.y;
	m[2][0] = +s.z;

	m[0][1] = +u.x;
	m[1][1] = +u.y;
	m[2][1] = +u.z;

	m[0][2] = -f.x;
	m[1][2] = -f.y;
	m[2][2] = -f.z;

	m[3][0] = gb_vec3_dot(s, eye);
	m[3][1] = gb_vec3_dot(u, eye);
	m[3][2] = gb_vec3_dot(f, eye);
}












gbQuat gb_quat(float x, float y, float z, float w) { gbQuat q; q.x = x; q.y = y; q.z = z; q.w = w; return q; }
gbQuat gb_quatv(float e[4]) { gbQuat q; q.x = e[0]; q.y = e[1]; q.z = e[2]; q.w = e[3]; return q; }

gbQuat gb_quat_axis_angle(gbVec3 axis, float angle_radians) {
	gbQuat q;
	gb_vec3_norm(&q.xyz, axis);
	gb_vec3_muleq(&q.xyz, gb_sin(0.5f*angle_radians));
	q.w = gb_cos(0.5f*angle_radians);
	return q;
}

gbQuat gb_quat_euler_angles(float pitch, float yaw, float roll) {
	/* TODO(bill): Do without multiplication, i.e. make it faster */
	gbQuat q, p, y, r;
	p = gb_quat_axis_angle(gb_vec3(1, 0, 0), pitch);
	y = gb_quat_axis_angle(gb_vec3(0, 1, 0), yaw);
	r = gb_quat_axis_angle(gb_vec3(0, 0, 1), roll);

	gb_quat_mul(&q, y, p);
	gb_quat_muleq(&q, r);

	return q;
}

gbQuat gb_quat_identity(void) { gbQuat q = {0, 0, 0, 1}; return q; }


void gb_quat_add(gbQuat *d, gbQuat q0, gbQuat q1) { gb_vec4_add(&d->xyzw, q0.xyzw, q1.xyzw); }
void gb_quat_sub(gbQuat *d, gbQuat q0, gbQuat q1) { gb_vec4_sub(&d->xyzw, q0.xyzw, q1.xyzw); }

void gb_quat_mul(gbQuat *d, gbQuat q0, gbQuat q1) {
	d->x = q0.w * q1.x + q0.x * q1.w + q0.y * q1.z - q0.z * q1.y;
	d->y = q0.w * q1.y - q0.x * q1.z + q0.y * q1.w + q0.z * q1.x;
	d->z = q0.w * q1.z + q0.x * q1.y - q0.y * q1.x + q0.z * q1.w;
	d->w = q0.w * q1.w - q0.x * q1.x - q0.y * q1.y - q0.z * q1.z;
}

void gb_quat_div(gbQuat *d, gbQuat q0, gbQuat q1){ gbQuat iq1; gb_quat_inverse(&iq1, q1); gb_quat_mul(d, q0, iq1); }

void gb_quat_mulf(gbQuat *d, gbQuat q0, float s) { gb_vec4_mul(&d->xyzw, q0.xyzw, s); }
void gb_quat_divf(gbQuat *d, gbQuat q0, float s) { gb_vec4_div(&d->xyzw, q0.xyzw, s); }


void gb_quat_addeq(gbQuat *d, gbQuat q) { gb_vec4_addeq(&d->xyzw, q.xyzw); }
void gb_quat_subeq(gbQuat *d, gbQuat q) { gb_vec4_subeq(&d->xyzw, q.xyzw); }
void gb_quat_muleq(gbQuat *d, gbQuat q) { gb_quat_mul(d, *d, q); }
void gb_quat_diveq(gbQuat *d, gbQuat q) { gb_quat_div(d, *d, q); }


void gb_quat_muleqf(gbQuat *d, float s) { gb_vec4_muleq(&d->xyzw, s); }
void gb_quat_diveqf(gbQuat *d, float s) { gb_vec4_diveq(&d->xyzw, s); }

float gb_quat_dot(gbQuat q0, gbQuat q1) { float r = gb_vec3_dot(q0.xyz, q1.xyz) + q0.w*q1.w; return r; }
float gb_quat_mag(gbQuat q)           { float r = gb_sqrt(gb_quat_dot(q, q)); return r; }

void gb_quat_norm(gbQuat *d, gbQuat q)    { gb_quat_divf(d, q, gb_quat_mag(q)); }
void gb_quat_conj(gbQuat *d, gbQuat q)    { d->xyz = gb_vec3(-q.x, -q.y, -q.z); d->w = q.w; }
void gb_quat_inverse(gbQuat *d, gbQuat q) { gb_quat_conj(d, q); gb_quat_diveqf(d, gb_quat_dot(q, q)); }


void gb_quat_axis(gbVec3 *axis, gbQuat q) {
	gbQuat n; gb_quat_norm(&n, q);
	gb_vec3_div(axis, n.xyz, gb_sin(gb_arccos(q.w)));
}

float gb_quat_angle(gbQuat q) {
	float mag = gb_quat_mag(q);
	float c = q.w * (1.0f/mag);
	float angle = 2.0f*gb_arccos(c);
	return angle;
}


float gb_quat_roll(gbQuat q)  { return gb_arctan2(2.0f*q.x*q.y + q.z*q.w, q.x*q.x + q.w*q.w - q.y*q.y - q.z*q.z); }
float gb_quat_pitch(gbQuat q) { return gb_arctan2(2.0f*q.y*q.z + q.w*q.x, q.w*q.w - q.x*q.x - q.y*q.y + q.z*q.z); }
float gb_quat_yaw(gbQuat q)   { return gb_arcsin(-2.0f*(q.x*q.z - q.w*q.y)); }

void gb_quat_rotate_vec3(gbVec3 *d, gbQuat q, gbVec3 v) {
	/* gbVec3 t = 2.0f * cross(q.xyz, v);
	 * *d = q.w*t + v + cross(q.xyz, t);
	 */
	gbVec3 t, p;
	gb_vec3_cross(&t, q.xyz, v);
	gb_vec3_muleq(&t, 2.0f);

	gb_vec3_cross(&p, q.xyz, t);

	gb_vec3_mul(d, t, q.w);
	gb_vec3_addeq(d, v);
	gb_vec3_addeq(d, p);
}


void gb_mat4_from_quat(gbMat4 *out, gbQuat q) {
	gbFloat4 *m;
	gbQuat a;
	float xx, yy, zz,
	      xy, xz, yz,
	      wx, wy, wz;

	gb_quat_norm(&a, q);
	xx = a.x*a.x; yy = a.y*a.y; zz = a.z*a.z;
	xy = a.x*a.y; xz = a.x*a.z; yz = a.y*a.z;
	wx = a.w*a.x; wy = a.w*a.y; wz = a.w*a.z;

	gb_mat4_identity(out);
	m = gb_float44_m(out);

	m[0][0] = 1.0f - 2.0f*(yy + zz);
	m[0][1] =        2.0f*(xy + wz);
	m[0][2] =        2.0f*(xz - wy);

	m[1][0] =        2.0f*(xy - wz);
	m[1][1] = 1.0f - 2.0f*(xx + zz);
	m[1][2] =        2.0f*(yz + wx);

	m[2][0] =        2.0f*(xz + wy);
	m[2][1] =        2.0f*(yz - wx);
	m[2][2] = 1.0f - 2.0f*(xx + yy);
}

void gb_quat_from_mat4(gbQuat *out, gbMat4 *mat) {
	gbFloat4 *m;
	float four_x_squared_minus_1, four_y_squared_minus_1,
	      four_z_squared_minus_1, four_w_squared_minus_1,
	      four_biggest_squared_minus_1;
	int biggest_index = 0;
	float biggest_value, mult;

	m = gb_float44_m(mat);

	four_x_squared_minus_1 = m[0][0] - m[1][1] - m[2][2];
	four_y_squared_minus_1 = m[1][1] - m[0][0] - m[2][2];
	four_z_squared_minus_1 = m[2][2] - m[0][0] - m[1][1];
	four_w_squared_minus_1 = m[0][0] + m[1][1] + m[2][2];

	four_biggest_squared_minus_1 = four_w_squared_minus_1;
	if (four_x_squared_minus_1 > four_biggest_squared_minus_1) {
		four_biggest_squared_minus_1 = four_x_squared_minus_1;
		biggest_index = 1;
	}
	if (four_y_squared_minus_1 > four_biggest_squared_minus_1) {
		four_biggest_squared_minus_1 = four_y_squared_minus_1;
		biggest_index = 2;
	}
	if (four_z_squared_minus_1 > four_biggest_squared_minus_1) {
		four_biggest_squared_minus_1 = four_z_squared_minus_1;
		biggest_index = 3;
	}

	biggest_value = gb_sqrt(four_biggest_squared_minus_1 + 1.0f) * 0.5f;
	mult = 0.25f / biggest_value;

	switch (biggest_index) {
	case 0:
		out->w = biggest_value;
		out->x = (m[1][2] - m[2][1]) * mult;
		out->y = (m[2][0] - m[0][2]) * mult;
		out->z = (m[0][1] - m[1][0]) * mult;
		break;
	case 1:
		out->w = (m[1][2] - m[2][1]) * mult;
		out->x = biggest_value;
		out->y = (m[0][1] + m[1][0]) * mult;
		out->z = (m[2][0] + m[0][2]) * mult;
		break;
	case 2:
		out->w = (m[2][0] - m[0][2]) * mult;
		out->x = (m[0][1] + m[1][0]) * mult;
		out->y = biggest_value;
		out->z = (m[1][2] + m[2][1]) * mult;
		break;
	case 3:
		out->w = (m[0][1] - m[1][0]) * mult;
		out->x = (m[2][0] + m[0][2]) * mult;
		out->y = (m[1][2] + m[2][1]) * mult;
		out->z = biggest_value;
		break;
	default:
		/* NOTE(bill): This shouldn't fucking happen!!! */
		break;
	}

}






float gb_lerp         (float a, float b, float t) { return a*(1.0f-t) + b*t; }
float gb_unlerp       (float t, float a, float b) { return (t-a)/(b-a); }
float gb_smooth_step  (float a, float b, float t) { float x = (t - a)/(b - a); return x*x*(3.0f - 2.0f*x); }
float gb_smoother_step(float a, float b, float t) { float x = (t - a)/(b - a); return x*x*x*(x*(6.0f*x - 15.0f) + 10.0f); }


#define GB_VEC_LERPN(N, d, a, b, t) \
	gbVec##N db; \
	gb_vec##N##_sub(&db, b, a); \
	gb_vec##N##_muleq(&db, t); \
	gb_vec##N##_add(d, a, db)
void gb_vec2_lerp(gbVec2 *d, gbVec2 a, gbVec2 b, float t) { GB_VEC_LERPN(2, d, a, b, t); }
void gb_vec3_lerp(gbVec3 *d, gbVec3 a, gbVec3 b, float t) { GB_VEC_LERPN(3, d, a, b, t); }
void gb_vec4_lerp(gbVec4 *d, gbVec4 a, gbVec4 b, float t) { GB_VEC_LERPN(4, d, a, b, t); }

#undef GB_VEC_LERPN

void gb_quat_lerp(gbQuat *d, gbQuat a, gbQuat b, float t)  { gb_vec4_lerp(&d->xyzw, a.xyzw, b.xyzw, t); }
void gb_quat_nlerp(gbQuat *d, gbQuat a, gbQuat b, float t) { gb_quat_lerp(d, a, b, t); gb_quat_norm(d, *d); }

void gb_quat_slerp(gbQuat *d, gbQuat a, gbQuat b, float t) {
	gbQuat x, y, z;
	float cos_theta, angle;
	float s1, s0, is;

	z = b;
	cos_theta = gb_quat_dot(a, b);

	if (cos_theta < 0.0f) {
		z = gb_quat(-b.x, -b.y, -b.z, -b.w);
		cos_theta = -cos_theta;
	}

	if (cos_theta > 1.0f) {
		/* NOTE(bill): Use lerp not nlerp as it's not a real angle or they are not normalized */
		gb_quat_lerp(d, a, b, t);
	}

	angle = gb_arccos(cos_theta);

	s1 = gb_sin((1.0f - t)*angle);
	s0 = gb_sin(t*angle);
	is = 1.0f/gb_sin(angle);
	gb_quat_mulf(&x, a, s1);
	gb_quat_mulf(&y, z, s0);
	gb_quat_add(d, x, y);
	gb_quat_muleqf(d, is);
}

void gb_quat_slerp_approx(gbQuat *d, gbQuat a, gbQuat b, float t) {
	/* NOTE(bill): Derived by taylor expanding the geometric interpolation equation
	 *             Even works okay for nearly anti-parallel versors!!!
	 */
	/* NOTE(bill): Extra interations cannot be used as they require angle^4 which is not worth it to approximate */
	float tp = t + (1.0f - gb_quat_dot(a, b))/3.0f * t*(-2.0f*t*t + 3.0f*t - 1.0f);
	gb_quat_nlerp(d, a, b, tp);
}

void gb_quat_nquad(gbQuat *d, gbQuat p, gbQuat a, gbQuat b, gbQuat q, float t) {
	gbQuat x, y;
	gb_quat_nlerp(&x, p, q, t);
	gb_quat_nlerp(&y, a, b, t);
	gb_quat_nlerp(d, x, y, 2.0f*t*(1.0f-t));
}

void gb_quat_squad(gbQuat *d, gbQuat p, gbQuat a, gbQuat b, gbQuat q, float t) {
	gbQuat x, y;
	gb_quat_slerp(&x, p, q, t);
	gb_quat_slerp(&y, a, b, t);
	gb_quat_slerp(d, x, y, 2.0f*t*(1.0f-t));
}

void gb_quat_squad_approx(gbQuat *d, gbQuat p, gbQuat a, gbQuat b, gbQuat q, float t) {
	gbQuat x, y;
	gb_quat_slerp_approx(&x, p, q, t);
	gb_quat_slerp_approx(&y, a, b, t);
	gb_quat_slerp_approx(d, x, y, 2.0f*t*(1.0f-t));
}






gbRect2 gb_rect2(gbVec2 pos, gbVec2 dim) {
	gbRect2 r;
	r.pos = pos;
	r.dim = dim;
	return r;
}

gbRect2 gb_rect2v(float v[4]) {
	gbRect2 r;
	r.pos = gb_vec2v(&v[0]);
	r.dim = gb_vec2v(&v[2]);
	return r;
}

gbRect3 gb_rect3(gbVec3 pos, gbVec3 dim) {
	gbRect3 r;
	r.pos = pos;
	r.dim = dim;
	return r;
}

gbRect3 gb_rect3v(float v[6]) {
	gbRect3 r;
	r.pos = gb_vec3v(&v[0]);
	r.dim = gb_vec3v(&v[3]);
	return r;
}


int gb_rect2_contains(gbRect2 a, float x, float y) {
	float min_x = gb_min(a.pos.x, a.pos.x+a.dim.x);
	float max_x = gb_max(a.pos.x, a.pos.x+a.dim.x);
	float min_y = gb_min(a.pos.y, a.pos.y+a.dim.y);
	float max_y = gb_max(a.pos.y, a.pos.y+a.dim.y);
	int result = (x >= min_x) & (x < max_x) & (y >= min_y) & (y < max_y);
	return result;
}

int gb_rect2_contains_vec2(gbRect2 a, gbVec2 p) { return gb_rect2_contains(a, p.x, p.y); }

int gb_rect2_intersects(gbRect2 a, gbRect2 b) {
	gbRect2 r = {0};
	return gb_rect2_intersection_result(a, b, &r);
}

int gb_rect2_intersection_result(gbRect2 a, gbRect2 b, gbRect2 *intersection) {
	float a_min_x = gb_min(a.pos.x, a.pos.x+a.dim.x);
	float a_max_x = gb_max(a.pos.x, a.pos.x+a.dim.x);
	float a_min_y = gb_min(a.pos.y, a.pos.y+a.dim.y);
	float a_max_y = gb_max(a.pos.y, a.pos.y+a.dim.y);

	float b_min_x = gb_min(b.pos.x, b.pos.x+b.dim.x);
	float b_max_x = gb_max(b.pos.x, b.pos.x+b.dim.x);
	float b_min_y = gb_min(b.pos.y, b.pos.y+b.dim.y);
	float b_max_y = gb_max(b.pos.y, b.pos.y+b.dim.y);

	float x0 = gb_max(a_min_x, b_min_x);
	float y0 = gb_max(a_min_y, b_min_y);
	float x1 = gb_min(a_max_x, b_max_x);
	float y1 = gb_min(a_max_y, b_max_y);

	if ((x0 < x1) && (y0 < y1)) {
		gbRect2 r = gb_rect2(gb_vec2(x0, y0), gb_vec2(x1-x0, y1-y0));
		*intersection = r;
		return 1;
	} else {
		gbRect2 r = {0};
		*intersection = r;
		return 0;
	}
}


#if defined(_WIN64) || defined(__x86_64__) || defined(__ppc64__)
	gb_math_u64 gb_hash_murmur64(void const *key, size_t num_bytes, gb_math_u64 seed) {
		gb_math_u64 const m = 0xc6a4a7935bd1e995ULL;
		gb_math_u64 const r = 47;

		gb_math_u64 h = seed ^ (num_bytes * m);

		gb_math_u64 *data = (gb_math_u64 *)(key);
		gb_math_u64 *end = data + (num_bytes / 8);
		unsigned char *data2;

		while (data != end) {
			gb_math_u64 k = *data++;
			k *= m;
			k ^= k >> r;
			k *= m;
			h ^= k;
			h *= m;
		}

		data2 = (unsigned char *)data;

		switch (num_bytes & 7) {
		case 7: h ^= (gb_math_u64)data2[6] << 48;
		case 6: h ^= (gb_math_u64)data2[5] << 40;
		case 5: h ^= (gb_math_u64)data2[4] << 32;
		case 4: h ^= (gb_math_u64)data2[3] << 24;
		case 3: h ^= (gb_math_u64)data2[2] << 16;
		case 2: h ^= (gb_math_u64)data2[1] << 8;
		case 1: h ^= (gb_math_u64)data2[0];
			h *= m;
		};

		h ^= h >> r;
		h *= m;
		h ^= h >> r;

		return h;
	}
#else
	gb_math_u64 gb_hash_murmur64(void const *key, size_t num_bytes, gb_math_u64 seed) {
		gb_math_u32 const m = 0x5bd1e995;
		gb_math_u32 const r = 24;

		gb_math_u64 h  = 0;
		gb_math_u32 h1 = (gb_math_u32)seed ^ (gb_math_u32)num_bytes;
		gb_math_u32 h2 = (gb_math_u32)((gb_math_u64)seed >> 32);

		gb_math_u32 *data = (gb_math_u32 *)key;


		while (num_bytes >= 8) {
			gb_math_u32 k1, k2;
			k1 = *data++;
			k1 *= m;
			k1 ^= k1 >> r;
			k1 *= m;
			h1 *= m;
			h1 ^= k1;
			num_bytes -= 4;

			k2 = *data++;
			k2 *= m;
			k2 ^= k2 >> r;
			k2 *= m;
			h2 *= m;
			h2 ^= k2;
			num_bytes -= 4;
		}

		if (num_bytes >= 4) {
			gb_math_u32 k1 = *data++;
			k1 *= m;
			k1 ^= k1 >> r;
			k1 *= m;
			h1 *= m;
			h1 ^= k1;
			num_bytes -= 4;
		}

		switch (num_bytes) {
		gb_math_u32 a, b, c;
		case 3: c = data[2]; h2 ^= c << 16;
		case 2: b = data[1]; h2 ^= b <<  8;
		case 1: a = data[0]; h2 ^= a <<  0;
			h2 *= m;
		};

		h1 ^= h2 >> 18;
		h1 *= m;
		h2 ^= h1 >> 22;
		h2 *= m;
		h1 ^= h2 >> 17;
		h1 *= m;
		h2 ^= h1 >> 19;
		h2 *= m;

		h = (gb_math_u64)(h << 32) | (gb_math_u64)h2;

		return h;
	}
#endif


/* TODO(bill): Make better random number generators */
float gb_random_range_float(float min_inc, float max_inc) {
	int int_result = gb_random_range_int(0, 2147483646); /* Prevent integer overflow */
	float result = int_result/(float)2147483646;
	result *= max_inc - min_inc;
	result += min_inc;
	return result;
}

int gb_random_range_int(int min_inc, int max_inc) {
	static unsigned int random_value = 0xdeadbeef; /* Random Value */
	unsigned int diff, result;
	random_value = random_value * 2147001325 + 715136305; /* BCPL generator */
	diff = max_inc - min_inc + 1;
	result = random_value % diff;
	result += min_inc;

	return result;
}

float gb_random01(void) {
	return gb_random_range_float(0.0f, 1.0f);
}

#if defined(__GCC__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif



#endif /* GB_MATH_IMPLEMENTATION */
