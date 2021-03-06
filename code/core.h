#pragma once

#include <stdarg.h>
#include <windows.h>



typedef unsigned long long uint64;
typedef unsigned int uint32;
typedef unsigned short uint16;
typedef unsigned char uint8;
typedef long long int64;
typedef int int32;
typedef short int16;
typedef char int8;
typedef int32 bool32;
typedef float float32;
typedef double float64;

typedef void (Log_Function)(const char*, va_list);



constexpr uint16 	c_port = 9999;
constexpr uint32 	c_packet_budget_per_tick = 1024;
constexpr uint32	c_max_clients = 32;
constexpr uint32	c_ticks_per_second = 60;
constexpr float32	c_seconds_per_tick = 1.0f / (float32)c_ticks_per_second;
constexpr float32 	c_turn_speed = 1.0f;	// how fast player turns
constexpr float32 	c_acceleration = 20.0f;
constexpr float32 	c_max_speed = 50.0f;
constexpr float32	c_pi = 3.14159265359f;
constexpr float32	c_deg_to_rad = c_pi / 180.0f;



constexpr uint64 kilobytes(uint32 kb)
{
	return kb * 1024;
}
constexpr uint64 megabytes(uint32 mb)
{
	return kilobytes(mb * 1024);
}
constexpr uint64 gigabytes(uint32 gb)
{
	return megabytes(gb * 1024);
}



#ifndef RELEASE
#define assert(x) if (!(x)) { int* p = 0; *p = 0; }
#else
#define assert(x)
#endif


struct Vec_3f
{
	float32 x, y, z;
};
Vec_3f vec_3f(float32 x, float32 y, float32 z);
Vec_3f vec_3f_add(Vec_3f a, Vec_3f b);
Vec_3f vec_3f_sub(Vec_3f a, Vec_3f b);
Vec_3f vec_3f_mul(Vec_3f v, float32 f);
Vec_3f vec_3f_normalised(Vec_3f v);
float vec_3f_dot(Vec_3f a, Vec_3f b);
Vec_3f vec_3f_cross(Vec_3f a, Vec_3f b);



struct Matrix_4x4
{
	// m11 m12 m13 m14
	// m21 m22 m23 m24
	// m31 m32 m33 m34
	// m41 m42 m43 m44

	// column major
	float32 m11, m21, m31, m41,
			m12, m22, m32, m42,
			m13, m23, m33, m43,
			m14, m24, m34, m44;
};
void matrix_4x4_projection(Matrix_4x4* matrix, float32 fov_y, float32 aspect_ratio, float32 near_plane, float32 far_plane);
void matrix_4x4_translation(Matrix_4x4* matrix, float32 x, float32 y, float32 z);
void matrix_4x4_translation(Matrix_4x4* matrix, Vec_3f translation);
void matrix_4x4_rotation_z(Matrix_4x4* matrix, float32 r);
void matrix_4x4_mul(Matrix_4x4* result, Matrix_4x4* a, Matrix_4x4* b);
void matrix_4x4_lookat(Matrix_4x4* matrix, Vec_3f position, Vec_3f target, Vec_3f up);


struct Circular_Index
{
	uint32 head;
	uint32 size;
	uint32 capacity;
};
Circular_Index circular_index(uint32 capacity);
bool32 circular_index_is_full(Circular_Index* index);
void circular_index_push(Circular_Index* index);
void circular_index_pop(Circular_Index* index);
uint32 circular_index_tail(Circular_Index* index);
uint32 circular_index_iterator(Circular_Index* index, uint32 offset);


struct Timer
{
	LARGE_INTEGER start;
};
Timer	timer();
void	timer_restart(Timer* timer);
float32 timer_get_s(Timer* timer);
void	timer_wait_until(Timer* timer, float32 wait_time_s);


struct Memory_Allocator
{
	uint8* memory;
	uint8* next;
	uint64 bytes_remaining;
};
Memory_Allocator memory_allocator(uint8* memory, uint64 size);
uint8* memory_allocator_alloc(Memory_Allocator* allocator, uint64 size);
uint8* alloc_permanent(uint64 size);
uint8* alloc_temp(uint64 size);


struct Globals
{
	Memory_Allocator permanent_allocator;
	Memory_Allocator temp_allocator;
	Log_Function* log_function;
	LARGE_INTEGER clock_frequency;
	bool32 sleep_granularity_was_set;
};
extern Globals* globals;
void globals_init(Log_Function* log_func);
void log(const char* format, ...);