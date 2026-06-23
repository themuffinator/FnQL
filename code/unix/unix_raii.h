/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#ifndef UNIX_RAII_H
#define UNIX_RAII_H

#ifdef __cplusplus

#include <cstddef>
#include <cstdio>
#include <dirent.h>
#include <memory>
#include <unistd.h>

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

namespace fnql {
namespace posix {

inline void *LoadLibraryFallback( const char *preferred, const char *fallback )
{
	void *library = Sys_LoadLibrary( preferred );
	if ( library == nullptr && fallback != nullptr )
	{
		library = Sys_LoadLibrary( fallback );
	}

	return library;
}

class ScopedLibrary {
public:
	ScopedLibrary() = default;
	explicit ScopedLibrary( void *library ) noexcept
		: library_( library )
	{
	}

	~ScopedLibrary()
	{
		reset();
	}

	ScopedLibrary( const ScopedLibrary& ) = delete;
	ScopedLibrary& operator=( const ScopedLibrary& ) = delete;

	ScopedLibrary( ScopedLibrary&& other ) noexcept
		: library_( other.release() )
	{
	}

	ScopedLibrary& operator=( ScopedLibrary&& other ) noexcept
	{
		if ( this != &other )
		{
			reset( other.release() );
		}
		return *this;
	}

	void *get() const noexcept
	{
		return library_;
	}

	void *release() noexcept
	{
		void *library = library_;
		library_ = nullptr;
		return library;
	}

	void reset( void *library = nullptr ) noexcept
	{
		if ( library_ != nullptr && library_ != library )
		{
			Sys_UnloadLibrary( library_ );
		}
		library_ = library;
	}

	explicit operator bool() const noexcept
	{
		return library_ != nullptr;
	}

private:
	void *library_ = nullptr;
};

struct FileCloser {
	void operator()( FILE *file ) const noexcept
	{
		if ( file != nullptr )
		{
			fclose( file );
		}
	}
};

using ScopedFile = std::unique_ptr<FILE, FileCloser>;

struct DirectoryCloser {
	void operator()( DIR *directory ) const noexcept
	{
		if ( directory != nullptr )
		{
			closedir( directory );
		}
	}
};

using ScopedDirectory = std::unique_ptr<DIR, DirectoryCloser>;

class ScopedFd {
public:
	ScopedFd() = default;
	explicit ScopedFd( int fd ) noexcept
		: fd_( fd )
	{
	}

	~ScopedFd()
	{
		reset();
	}

	ScopedFd( const ScopedFd& ) = delete;
	ScopedFd& operator=( const ScopedFd& ) = delete;

	ScopedFd( ScopedFd&& other ) noexcept
		: fd_( other.release() )
	{
	}

	ScopedFd& operator=( ScopedFd&& other ) noexcept
	{
		if ( this != &other )
		{
			reset( other.release() );
		}
		return *this;
	}

	int get() const noexcept
	{
		return fd_;
	}

	int release() noexcept
	{
		int fd = fd_;
		fd_ = -1;
		return fd;
	}

	void reset( int fd = -1 ) noexcept
	{
		if ( fd_ >= 0 && fd_ != fd )
		{
			close( fd_ );
		}
		fd_ = fd;
	}

	explicit operator bool() const noexcept
	{
		return fd_ >= 0;
	}

private:
	int fd_ = -1;
};

enum class SymbolLoadFailure {
	Library,
	Support
};

template <typename Symbol, std::size_t SymbolCount>
inline qboolean LoadSymbols( void *library, Symbol ( &symbols )[ SymbolCount ], const char *context, SymbolLoadFailure failure )
{
	for ( std::size_t i = 0; i < SymbolCount; ++i )
	{
		*symbols[ i ].symbol = Sys_LoadFunction( library, symbols[ i ].name );
		if ( *symbols[ i ].symbol != nullptr )
		{
			continue;
		}

		if ( failure == SymbolLoadFailure::Support )
		{
			Com_Printf( "Couldn't find '%s' symbol, disabling %s support.\n", symbols[ i ].name, context );
		}
		else
		{
			Com_Printf( "...couldn't find '%s' in %s\n", symbols[ i ].name, context );
		}
		return qfalse;
	}

	return qtrue;
}

} // namespace posix
} // namespace fnql

#endif // __cplusplus

#endif // UNIX_RAII_H
