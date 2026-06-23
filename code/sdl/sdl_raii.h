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

#ifndef SDL_RAII_H
#define SDL_RAII_H

#ifndef SDL_FUNCTION_POINTER_IS_VOID_POINTER
#	define SDL_FUNCTION_POINTER_IS_VOID_POINTER 1
#endif

#include <SDL3/SDL.h>

#ifdef __cplusplus

#include <cstdlib>
#include <memory>
#include <utility>

namespace fnql {
namespace sdl {

template <typename T>
struct SdlFreeDeleter {
	void operator()( T *memory ) const noexcept
	{
		if ( memory != nullptr )
		{
			SDL_free( memory );
		}
	}
};

template <typename T>
using ScopedSdlMemory = std::unique_ptr<T, SdlFreeDeleter<T>>;

template <typename T>
struct FreeDeleter {
	void operator()( T *memory ) const noexcept
	{
		std::free( memory );
	}
};

template <typename T>
using ScopedMemory = std::unique_ptr<T, FreeDeleter<T>>;

struct AudioStreamDeleter {
	void operator()( SDL_AudioStream *stream ) const noexcept
	{
		if ( stream != nullptr )
		{
			SDL_DestroyAudioStream( stream );
		}
	}
};

using ScopedAudioStream = std::unique_ptr<SDL_AudioStream, AudioStreamDeleter>;

struct MutexDeleter {
	void operator()( SDL_Mutex *mutex ) const noexcept
	{
		if ( mutex != nullptr )
		{
			SDL_DestroyMutex( mutex );
		}
	}
};

using ScopedMutex = std::unique_ptr<SDL_Mutex, MutexDeleter>;

struct SurfaceDeleter {
	void operator()( SDL_Surface *surface ) const noexcept
	{
		if ( surface != nullptr )
		{
			SDL_DestroySurface( surface );
		}
	}
};

using ScopedSurface = std::unique_ptr<SDL_Surface, SurfaceDeleter>;

struct WindowDeleter {
	void operator()( SDL_Window *window ) const noexcept
	{
		if ( window != nullptr )
		{
			SDL_DestroyWindow( window );
		}
	}
};

using ScopedWindow = std::unique_ptr<SDL_Window, WindowDeleter>;

class ScopedProperties {
public:
	ScopedProperties() = default;
	explicit ScopedProperties( SDL_PropertiesID props ) noexcept
		: props_( props )
	{
	}

	~ScopedProperties()
	{
		reset();
	}

	ScopedProperties( const ScopedProperties& ) = delete;
	ScopedProperties& operator=( const ScopedProperties& ) = delete;

	ScopedProperties( ScopedProperties&& other ) noexcept
		: props_( other.release() )
	{
	}

	ScopedProperties& operator=( ScopedProperties&& other ) noexcept
	{
		if ( this != &other )
		{
			reset( other.release() );
		}
		return *this;
	}

	SDL_PropertiesID get() const noexcept
	{
		return props_;
	}

	SDL_PropertiesID release() noexcept
	{
		SDL_PropertiesID props = props_;
		props_ = 0;
		return props;
	}

	void reset( SDL_PropertiesID props = 0 ) noexcept
	{
		if ( props_ != 0 && props_ != props )
		{
			SDL_DestroyProperties( props_ );
		}
		props_ = props;
	}

	explicit operator bool() const noexcept
	{
		return props_ != 0;
	}

private:
	SDL_PropertiesID props_ = 0;
};

class ScopedGLContext {
public:
	ScopedGLContext() = default;
	explicit ScopedGLContext( SDL_GLContext context ) noexcept
		: context_( context )
	{
	}

	~ScopedGLContext()
	{
		reset();
	}

	ScopedGLContext( const ScopedGLContext& ) = delete;
	ScopedGLContext& operator=( const ScopedGLContext& ) = delete;

	ScopedGLContext( ScopedGLContext&& other ) noexcept
		: context_( other.release() )
	{
	}

	ScopedGLContext& operator=( ScopedGLContext&& other ) noexcept
	{
		if ( this != &other )
		{
			reset( other.release() );
		}
		return *this;
	}

	SDL_GLContext get() const noexcept
	{
		return context_;
	}

	SDL_GLContext release() noexcept
	{
		SDL_GLContext context = context_;
		context_ = nullptr;
		return context;
	}

	void reset( SDL_GLContext context = nullptr ) noexcept
	{
		if ( context_ != nullptr && context_ != context )
		{
			SDL_GL_DestroyContext( context_ );
		}
		context_ = context;
	}

	explicit operator bool() const noexcept
	{
		return context_ != nullptr;
	}

private:
	SDL_GLContext context_ = nullptr;
};

class ScopedMutexLock {
public:
	explicit ScopedMutexLock( SDL_Mutex *mutex ) noexcept
		: mutex_( mutex )
	{
		if ( mutex_ != nullptr )
		{
			SDL_LockMutex( mutex_ );
		}
	}

	~ScopedMutexLock()
	{
		if ( mutex_ != nullptr )
		{
			SDL_UnlockMutex( mutex_ );
		}
	}

	ScopedMutexLock( const ScopedMutexLock& ) = delete;
	ScopedMutexLock& operator=( const ScopedMutexLock& ) = delete;

private:
	SDL_Mutex *mutex_ = nullptr;
};

class ScopedSubSystem {
public:
	ScopedSubSystem() = default;
	ScopedSubSystem( Uint32 flags, bool active ) noexcept
		: flags_( flags )
		, active_( active )
	{
	}

	~ScopedSubSystem()
	{
		reset();
	}

	ScopedSubSystem( const ScopedSubSystem& ) = delete;
	ScopedSubSystem& operator=( const ScopedSubSystem& ) = delete;

	ScopedSubSystem( ScopedSubSystem&& other ) noexcept
		: flags_( other.flags_ )
		, active_( other.active_ )
	{
		other.active_ = false;
	}

	ScopedSubSystem& operator=( ScopedSubSystem&& other ) noexcept
	{
		if ( this != &other )
		{
			reset();
			flags_ = other.flags_;
			active_ = other.active_;
			other.active_ = false;
		}
		return *this;
	}

	void release() noexcept
	{
		active_ = false;
	}

	void reset() noexcept
	{
		if ( active_ )
		{
			SDL_QuitSubSystem( flags_ );
			active_ = false;
		}
	}

private:
	Uint32 flags_ = 0;
	bool active_ = false;
};

} // namespace sdl
} // namespace fnql

#endif // __cplusplus

#endif // SDL_RAII_H
