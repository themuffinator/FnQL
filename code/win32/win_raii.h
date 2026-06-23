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

#ifndef WIN_RAII_H
#define WIN_RAII_H

#ifndef CINTERFACE
#define CINTERFACE
#endif
#include <windows.h>
#include <objbase.h>

namespace fnql {
namespace win {

class ScopedCryptProvider {
public:
	ScopedCryptProvider() = default;
	explicit ScopedCryptProvider( HCRYPTPROV provider ) noexcept
		: provider_( provider )
	{
	}

	~ScopedCryptProvider()
	{
		reset();
	}

	ScopedCryptProvider( const ScopedCryptProvider& ) = delete;
	ScopedCryptProvider& operator=( const ScopedCryptProvider& ) = delete;

	ScopedCryptProvider( ScopedCryptProvider&& other ) noexcept
		: provider_( other.release() )
	{
	}

	ScopedCryptProvider& operator=( ScopedCryptProvider&& other ) noexcept
	{
		if ( this != &other )
		{
			reset( other.release() );
		}
		return *this;
	}

	HCRYPTPROV get() const noexcept
	{
		return provider_;
	}

	HCRYPTPROV* receive() noexcept
	{
		reset();
		return &provider_;
	}

	HCRYPTPROV release() noexcept
	{
		HCRYPTPROV provider = provider_;
		provider_ = 0;
		return provider;
	}

	void reset( HCRYPTPROV provider = 0 ) noexcept
	{
		if ( provider_ != 0 && provider_ != provider )
		{
			CryptReleaseContext( provider_, 0 );
		}
		provider_ = provider;
	}

	explicit operator bool() const noexcept
	{
		return provider_ != 0;
	}

private:
	HCRYPTPROV provider_ = 0;
};

class ScopedLibrary {
public:
	ScopedLibrary() = default;
	explicit ScopedLibrary( HMODULE module ) noexcept
		: module_( module )
	{
	}

	~ScopedLibrary()
	{
		reset();
	}

	ScopedLibrary( const ScopedLibrary& ) = delete;
	ScopedLibrary& operator=( const ScopedLibrary& ) = delete;

	ScopedLibrary( ScopedLibrary&& other ) noexcept
		: module_( other.release() )
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

	HMODULE get() const noexcept
	{
		return module_;
	}

	HMODULE release() noexcept
	{
		HMODULE module = module_;
		module_ = nullptr;
		return module;
	}

	void reset( HMODULE module = nullptr ) noexcept
	{
		if ( module_ != nullptr && module_ != module )
		{
			FreeLibrary( module_ );
		}
		module_ = module;
	}

	explicit operator bool() const noexcept
	{
		return module_ != nullptr;
	}

private:
	HMODULE module_ = nullptr;
};

class ScopedRegistryKey {
public:
	ScopedRegistryKey() = default;
	explicit ScopedRegistryKey( HKEY key ) noexcept
		: key_( key )
	{
	}

	~ScopedRegistryKey()
	{
		reset();
	}

	ScopedRegistryKey( const ScopedRegistryKey& ) = delete;
	ScopedRegistryKey& operator=( const ScopedRegistryKey& ) = delete;

	ScopedRegistryKey( ScopedRegistryKey&& other ) noexcept
		: key_( other.release() )
	{
	}

	ScopedRegistryKey& operator=( ScopedRegistryKey&& other ) noexcept
	{
		if ( this != &other )
		{
			reset( other.release() );
		}
		return *this;
	}

	HKEY get() const noexcept
	{
		return key_;
	}

	HKEY* receive() noexcept
	{
		reset();
		return &key_;
	}

	HKEY release() noexcept
	{
		HKEY key = key_;
		key_ = nullptr;
		return key;
	}

	void reset( HKEY key = nullptr ) noexcept
	{
		if ( key_ != nullptr && key_ != key )
		{
			RegCloseKey( key_ );
		}
		key_ = key;
	}

	explicit operator bool() const noexcept
	{
		return key_ != nullptr;
	}

private:
	HKEY key_ = nullptr;
};

class ScopedHandle {
public:
	ScopedHandle() = default;
	explicit ScopedHandle( HANDLE handle ) noexcept
		: handle_( handle )
	{
	}

	~ScopedHandle()
	{
		reset();
	}

	ScopedHandle( const ScopedHandle& ) = delete;
	ScopedHandle& operator=( const ScopedHandle& ) = delete;

	ScopedHandle( ScopedHandle&& other ) noexcept
		: handle_( other.release() )
	{
	}

	ScopedHandle& operator=( ScopedHandle&& other ) noexcept
	{
		if ( this != &other )
		{
			reset( other.release() );
		}
		return *this;
	}

	HANDLE get() const noexcept
	{
		return handle_;
	}

	HANDLE release() noexcept
	{
		HANDLE handle = handle_;
		handle_ = nullptr;
		return handle;
	}

	void reset( HANDLE handle = nullptr ) noexcept
	{
		if ( handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE && handle_ != handle )
		{
			CloseHandle( handle_ );
		}
		handle_ = handle;
	}

	explicit operator bool() const noexcept
	{
		return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
	}

private:
	HANDLE handle_ = nullptr;
};

class ScopedClipboard {
public:
	explicit ScopedClipboard( HWND owner ) noexcept
		: open_( OpenClipboard( owner ) != FALSE )
	{
	}

	~ScopedClipboard()
	{
		if ( open_ )
		{
			CloseClipboard();
		}
	}

	ScopedClipboard( const ScopedClipboard& ) = delete;
	ScopedClipboard& operator=( const ScopedClipboard& ) = delete;

	explicit operator bool() const noexcept
	{
		return open_;
	}

private:
	bool open_ = false;
};

class ScopedGlobalMemory {
public:
	ScopedGlobalMemory() = default;
	explicit ScopedGlobalMemory( HGLOBAL memory ) noexcept
		: memory_( memory )
	{
	}

	~ScopedGlobalMemory()
	{
		reset();
	}

	ScopedGlobalMemory( const ScopedGlobalMemory& ) = delete;
	ScopedGlobalMemory& operator=( const ScopedGlobalMemory& ) = delete;

	ScopedGlobalMemory( ScopedGlobalMemory&& other ) noexcept
		: memory_( other.release() )
	{
	}

	ScopedGlobalMemory& operator=( ScopedGlobalMemory&& other ) noexcept
	{
		if ( this != &other )
		{
			reset( other.release() );
		}
		return *this;
	}

	HGLOBAL get() const noexcept
	{
		return memory_;
	}

	HGLOBAL release() noexcept
	{
		HGLOBAL memory = memory_;
		memory_ = nullptr;
		return memory;
	}

	void reset( HGLOBAL memory = nullptr ) noexcept
	{
		if ( memory_ != nullptr && memory_ != memory )
		{
			GlobalFree( memory_ );
		}
		memory_ = memory;
	}

	explicit operator bool() const noexcept
	{
		return memory_ != nullptr;
	}

private:
	HGLOBAL memory_ = nullptr;
};

template <typename T>
class ScopedCoTaskMem {
public:
	ScopedCoTaskMem() = default;
	explicit ScopedCoTaskMem( T *memory ) noexcept
		: memory_( memory )
	{
	}

	~ScopedCoTaskMem()
	{
		reset();
	}

	ScopedCoTaskMem( const ScopedCoTaskMem& ) = delete;
	ScopedCoTaskMem& operator=( const ScopedCoTaskMem& ) = delete;

	ScopedCoTaskMem( ScopedCoTaskMem&& other ) noexcept
		: memory_( other.release() )
	{
	}

	ScopedCoTaskMem& operator=( ScopedCoTaskMem&& other ) noexcept
	{
		if ( this != &other )
		{
			reset( other.release() );
		}
		return *this;
	}

	T *get() const noexcept
	{
		return memory_;
	}

	T **receive() noexcept
	{
		reset();
		return &memory_;
	}

	T *release() noexcept
	{
		T *memory = memory_;
		memory_ = nullptr;
		return memory;
	}

	void reset( T *memory = nullptr ) noexcept
	{
		if ( memory_ != nullptr && memory_ != memory )
		{
			CoTaskMemFree( memory_ );
		}
		memory_ = memory;
	}

	explicit operator bool() const noexcept
	{
		return memory_ != nullptr;
	}

private:
	T *memory_ = nullptr;
};

class ScopedComInitialization {
public:
	ScopedComInitialization() = default;

	~ScopedComInitialization()
	{
		reset();
	}

	ScopedComInitialization( const ScopedComInitialization& ) = delete;
	ScopedComInitialization& operator=( const ScopedComInitialization& ) = delete;

	HRESULT initialize( LPVOID reserved = nullptr ) noexcept
	{
		reset();
		result_ = CoInitialize( reserved );
		initialized_ = result_ == S_OK;
		return result_;
	}

	void reset() noexcept
	{
		if ( initialized_ )
		{
			CoUninitialize();
			initialized_ = false;
		}
		result_ = E_UNEXPECTED;
	}

	HRESULT result() const noexcept
	{
		return result_;
	}

	explicit operator bool() const noexcept
	{
		return initialized_;
	}

private:
	HRESULT result_ = E_UNEXPECTED;
	bool initialized_ = false;
};

template <typename T>
class ScopedComPtr {
public:
	ScopedComPtr() = default;
	explicit ScopedComPtr( T *pointer ) noexcept
		: pointer_( pointer )
	{
	}

	~ScopedComPtr()
	{
		reset();
	}

	ScopedComPtr( const ScopedComPtr& ) = delete;
	ScopedComPtr& operator=( const ScopedComPtr& ) = delete;

	ScopedComPtr( ScopedComPtr&& other ) noexcept
		: pointer_( other.release() )
	{
	}

	ScopedComPtr& operator=( ScopedComPtr&& other ) noexcept
	{
		if ( this != &other )
		{
			reset( other.release() );
		}
		return *this;
	}

	T *get() const noexcept
	{
		return pointer_;
	}

	T **receive() noexcept
	{
		reset();
		return &pointer_;
	}

	T *release() noexcept
	{
		T *pointer = pointer_;
		pointer_ = nullptr;
		return pointer;
	}

	void reset( T *pointer = nullptr ) noexcept
	{
		if ( pointer_ != nullptr && pointer_ != pointer )
		{
			pointer_->lpVtbl->Release( pointer_ );
		}
		pointer_ = pointer;
	}

	T *operator->() const noexcept
	{
		return pointer_;
	}

	explicit operator bool() const noexcept
	{
		return pointer_ != nullptr;
	}

private:
	T *pointer_ = nullptr;
};

template <typename T>
class ScopedGlobalLock {
public:
	explicit ScopedGlobalLock( HGLOBAL memory ) noexcept
		: memory_( memory )
		, data_( static_cast<T *>( GlobalLock( memory ) ) )
	{
	}

	~ScopedGlobalLock()
	{
		if ( data_ != nullptr )
		{
			GlobalUnlock( memory_ );
		}
	}

	ScopedGlobalLock( const ScopedGlobalLock& ) = delete;
	ScopedGlobalLock& operator=( const ScopedGlobalLock& ) = delete;

	T *get() const noexcept
	{
		return data_;
	}

	explicit operator bool() const noexcept
	{
		return data_ != nullptr;
	}

private:
	HGLOBAL memory_ = nullptr;
	T *data_ = nullptr;
};

class ScopedDisplayDC {
public:
	static ScopedDisplayDC ForDisplay( LPCTSTR displayName )
	{
		return ScopedDisplayDC( CreateDC( TEXT( "DISPLAY" ), displayName, nullptr, nullptr ), nullptr, true );
	}

	static ScopedDisplayDC ForDesktop()
	{
		HWND desktop = GetDesktopWindow();
		return ScopedDisplayDC( GetDC( desktop ), desktop, false );
	}

	ScopedDisplayDC() = default;
	~ScopedDisplayDC()
	{
		reset();
	}

	ScopedDisplayDC( const ScopedDisplayDC& ) = delete;
	ScopedDisplayDC& operator=( const ScopedDisplayDC& ) = delete;

	ScopedDisplayDC( ScopedDisplayDC&& other ) noexcept
		: dc_( other.dc_ )
		, window_( other.window_ )
		, deleteDc_( other.deleteDc_ )
	{
		other.dc_ = nullptr;
		other.window_ = nullptr;
		other.deleteDc_ = false;
	}

	ScopedDisplayDC& operator=( ScopedDisplayDC&& other ) noexcept
	{
		if ( this != &other )
		{
			reset();
			dc_ = other.dc_;
			window_ = other.window_;
			deleteDc_ = other.deleteDc_;
			other.dc_ = nullptr;
			other.window_ = nullptr;
			other.deleteDc_ = false;
		}
		return *this;
	}

	HDC get() const noexcept
	{
		return dc_;
	}

	void reset() noexcept
	{
		if ( dc_ == nullptr )
		{
			return;
		}

		if ( deleteDc_ )
		{
			DeleteDC( dc_ );
		}
		else
		{
			ReleaseDC( window_, dc_ );
		}

		dc_ = nullptr;
		window_ = nullptr;
		deleteDc_ = false;
	}

	explicit operator bool() const noexcept
	{
		return dc_ != nullptr;
	}

private:
	ScopedDisplayDC( HDC dc, HWND window, bool deleteDc ) noexcept
		: dc_( dc )
		, window_( window )
		, deleteDc_( deleteDc )
	{
	}

	HDC dc_ = nullptr;
	HWND window_ = nullptr;
	bool deleteDc_ = false;
};

} // namespace win
} // namespace fnql

#endif // WIN_RAII_H
