/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL.

FnQL is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.
===========================================================================
*/
// awesomium_backend_win32.hpp -- installed retail Awesomium adapter boundary

#ifndef FNQL_CLIENT_AWESOMIUM_BACKEND_WIN32_HPP
#define FNQL_CLIENT_AWESOMIUM_BACKEND_WIN32_HPP

namespace fnql::webui {

class BackendHost;

// Installs the platform adapter when the process ABI can host retail QL's
// 32-bit Awesomium runtime. Other platforms retain the null backend.
void InstallRetailAwesomiumBackend( BackendHost &host ) noexcept;

} // namespace fnql::webui

#endif // FNQL_CLIENT_AWESOMIUM_BACKEND_WIN32_HPP
