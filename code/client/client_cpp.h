/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL.

FnQL is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.
===========================================================================
*/
// client_cpp.h -- C++ helpers for client implementation files

#pragma once

#include <cstddef>
#include <cstring>
#include <type_traits>
#include <utility>

namespace fnql {

constexpr qboolean ToQboolean( bool value ) noexcept
{
	return value ? qtrue : qfalse;
}

template <typename T>
T ReadUnaligned( const void *data ) noexcept
{
	static_assert( std::is_trivially_copyable<T>::value, "ReadUnaligned requires a trivially copyable type" );

	T value{};
	std::memcpy( &value, data, sizeof( value ) );
	return value;
}

template <typename T>
void WriteUnaligned( void *data, const T &value ) noexcept
{
	static_assert( std::is_trivially_copyable<T>::value, "WriteUnaligned requires a trivially copyable type" );

	std::memcpy( data, &value, sizeof( value ) );
}

inline int SizeToInt( std::size_t size ) noexcept
{
	return static_cast<int>( size );
}

inline int FileRead( fileHandle_t file, void *buffer, std::size_t bytes )
{
	return FS_Read( buffer, SizeToInt( bytes ), file );
}

inline int FileWrite( fileHandle_t file, const void *buffer, std::size_t bytes )
{
	return FS_Write( buffer, SizeToInt( bytes ), file );
}

template <typename T>
int FileReadObject( fileHandle_t file, T &object )
{
	return FileRead( file, &object, sizeof( object ) );
}

template <typename T>
int FileWriteObject( fileHandle_t file, const T &object )
{
	return FileWrite( file, &object, sizeof( object ) );
}

class ScopedFilePosition {
public:
	explicit ScopedFilePosition( fileHandle_t file ) noexcept
		: file_( file ),
		  position_( FS_FTell( file ) )
	{
	}

	~ScopedFilePosition()
	{
		if ( valid() ) {
			FS_Seek( file_, position_, FS_SEEK_SET );
		}
	}

	ScopedFilePosition( const ScopedFilePosition & ) = delete;
	ScopedFilePosition &operator=( const ScopedFilePosition & ) = delete;

	[[nodiscard]] bool valid() const noexcept
	{
		return position_ >= 0;
	}

private:
	fileHandle_t file_ = FS_INVALID_HANDLE;
	int position_ = -1;
};

inline void CloseFile( fileHandle_t &handle )
{
	if ( handle != FS_INVALID_HANDLE ) {
		FS_FCloseFile( handle );
		handle = FS_INVALID_HANDLE;
	}
}

class ScopedFileHandle {
public:
	ScopedFileHandle() = default;

	explicit ScopedFileHandle( fileHandle_t handle ) noexcept
		: handle_( handle )
	{
	}

	~ScopedFileHandle()
	{
		reset();
	}

	ScopedFileHandle( const ScopedFileHandle & ) = delete;
	ScopedFileHandle &operator=( const ScopedFileHandle & ) = delete;

	ScopedFileHandle( ScopedFileHandle &&other ) noexcept
		: handle_( other.release() )
	{
	}

	ScopedFileHandle &operator=( ScopedFileHandle &&other ) noexcept
	{
		if ( this != &other ) {
			reset( other.release() );
		}
		return *this;
	}

	[[nodiscard]] fileHandle_t get() const noexcept
	{
		return handle_;
	}

	[[nodiscard]] explicit operator bool() const noexcept
	{
		return handle_ != FS_INVALID_HANDLE;
	}

	fileHandle_t release() noexcept
	{
		return std::exchange( handle_, FS_INVALID_HANDLE );
	}

	void reset( fileHandle_t handle = FS_INVALID_HANDLE )
	{
		CloseFile( handle_ );
		handle_ = handle;
	}

private:
	fileHandle_t handle_ = FS_INVALID_HANDLE;
};

inline int OpenFileRead( const char *qpath, ScopedFileHandle &file, qboolean uniqueFILE = qfalse )
{
	fileHandle_t handle = FS_INVALID_HANDLE;
	const int length = FS_FOpenFileRead( qpath, &handle, uniqueFILE );
	file.reset( handle );
	return length;
}

inline int OpenHomeFileRead( const char *qpath, ScopedFileHandle &file )
{
	fileHandle_t handle = FS_INVALID_HANDLE;
	const int length = FS_Home_FOpenFileRead( qpath, &handle );
	file.reset( handle );
	return length;
}

inline int OpenSvFileRead( const char *qpath, ScopedFileHandle &file )
{
	fileHandle_t handle = FS_INVALID_HANDLE;
	const int length = FS_SV_FOpenFileRead( qpath, &handle );
	file.reset( handle );
	return length;
}

class ScopedTempMemory {
public:
	ScopedTempMemory() = default;

	explicit ScopedTempMemory( void *ptr ) noexcept
		: ptr_( ptr )
	{
	}

	~ScopedTempMemory()
	{
		reset();
	}

	ScopedTempMemory( const ScopedTempMemory & ) = delete;
	ScopedTempMemory &operator=( const ScopedTempMemory & ) = delete;

	ScopedTempMemory( ScopedTempMemory &&other ) noexcept
		: ptr_( other.release() )
	{
	}

	ScopedTempMemory &operator=( ScopedTempMemory &&other ) noexcept
	{
		if ( this != &other ) {
			reset( other.release() );
		}
		return *this;
	}

	[[nodiscard]] void *get() const noexcept
	{
		return ptr_;
	}

	template <typename T>
	[[nodiscard]] T *as() const noexcept
	{
		return static_cast<T *>( ptr_ );
	}

	[[nodiscard]] explicit operator bool() const noexcept
	{
		return ptr_ != nullptr;
	}

	void *release() noexcept
	{
		return std::exchange( ptr_, nullptr );
	}

	void reset( void *ptr = nullptr )
	{
		if ( ptr_ ) {
			Hunk_FreeTempMemory( ptr_ );
		}
		ptr_ = ptr;
	}

	static ScopedTempMemory Allocate( std::size_t bytes )
	{
		return ScopedTempMemory( Hunk_AllocateTempMemory( SizeToInt( bytes ) ) );
	}

private:
	void *ptr_ = nullptr;
};

class ScopedZoneMemory {
public:
	ScopedZoneMemory() = default;

	explicit ScopedZoneMemory( void *ptr ) noexcept
		: ptr_( ptr )
	{
	}

	~ScopedZoneMemory()
	{
		reset();
	}

	ScopedZoneMemory( const ScopedZoneMemory & ) = delete;
	ScopedZoneMemory &operator=( const ScopedZoneMemory & ) = delete;

	ScopedZoneMemory( ScopedZoneMemory &&other ) noexcept
		: ptr_( other.release() )
	{
	}

	ScopedZoneMemory &operator=( ScopedZoneMemory &&other ) noexcept
	{
		if ( this != &other ) {
			reset( other.release() );
		}
		return *this;
	}

	[[nodiscard]] void *get() const noexcept
	{
		return ptr_;
	}

	template <typename T>
	[[nodiscard]] T *as() const noexcept
	{
		return static_cast<T *>( ptr_ );
	}

	[[nodiscard]] explicit operator bool() const noexcept
	{
		return ptr_ != nullptr;
	}

	void *release() noexcept
	{
		return std::exchange( ptr_, nullptr );
	}

	void reset( void *ptr = nullptr )
	{
		if ( ptr_ ) {
			Z_Free( ptr_ );
		}
		ptr_ = ptr;
	}

private:
	void *ptr_ = nullptr;
};

inline ScopedZoneMemory AllocateZoneMemory( std::size_t bytes, const char *label, const char *file, int line )
{
#ifdef ZONE_DEBUG
	return ScopedZoneMemory( Z_MallocDebug( SizeToInt( bytes ), const_cast<char *>( label ),
		const_cast<char *>( file ), line ) );
#else
	static_cast<void>( label );
	static_cast<void>( file );
	static_cast<void>( line );
	return ScopedZoneMemory( Z_Malloc( SizeToInt( bytes ) ) );
#endif
}

class ScopedReadFile {
public:
	ScopedReadFile() = default;

	~ScopedReadFile()
	{
		reset();
	}

	ScopedReadFile( const ScopedReadFile & ) = delete;
	ScopedReadFile &operator=( const ScopedReadFile & ) = delete;

	ScopedReadFile( ScopedReadFile &&other ) noexcept
	{
		ptr_ = other.ptr_;
		length_ = other.length_;
		other.ptr_ = nullptr;
		other.length_ = 0;
	}

	ScopedReadFile &operator=( ScopedReadFile &&other ) noexcept
	{
		if ( this != &other ) {
			reset();
			ptr_ = other.ptr_;
			length_ = other.length_;
			other.ptr_ = nullptr;
			other.length_ = 0;
		}
		return *this;
	}

	[[nodiscard]] void *get() const noexcept
	{
		return ptr_;
	}

	template <typename T>
	[[nodiscard]] T *as() const noexcept
	{
		return static_cast<T *>( ptr_ );
	}

	[[nodiscard]] int length() const noexcept
	{
		return length_;
	}

	[[nodiscard]] explicit operator bool() const noexcept
	{
		return ptr_ != nullptr && length_ > 0;
	}

	void *release() noexcept
	{
		length_ = 0;
		return std::exchange( ptr_, nullptr );
	}

	void reset( void *ptr = nullptr, int length = 0 )
	{
		if ( ptr_ ) {
			FS_FreeFile( ptr_ );
		}
		ptr_ = ptr;
		length_ = length;
	}

	static ScopedReadFile Read( const char *qpath )
	{
		void *buffer = nullptr;
		const int length = FS_ReadFile( qpath, &buffer );
		ScopedReadFile file;
		file.reset( buffer, length );
		return file;
	}

private:
	void *ptr_ = nullptr;
	int length_ = 0;
};

} // namespace fnql
