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

#pragma once

/*****************************************************************************
 * name:		l_struct.h
 *
 * desc:		structure reading/writing
 *
 * $Archive: /source/code/botlib/l_struct.h $
 *
 *****************************************************************************/

#include <stddef.h>

#ifndef BOTLIB_MAX_STRINGFIELD_DEFINED
#define BOTLIB_MAX_STRINGFIELD_DEFINED
static constexpr int MAX_STRINGFIELD = 80;
#endif
//field types
static constexpr int FT_CHAR = 1;			// char
static constexpr int FT_INT = 2;			// int
static constexpr int FT_FLOAT = 3;			// float
static constexpr int FT_STRING = 4;			// char [MAX_STRINGFIELD]
static constexpr int FT_STRUCT = 6;			// struct (sub structure)
//type only mask
static constexpr int FT_TYPE = 0x00FF;		// only type, clear subtype
//sub types
static constexpr int FT_ARRAY = 0x0100;		// array of type
static constexpr int FT_BOUNDED = 0x0200;	// bounded value
static constexpr int FT_UNSIGNED = 0x0400;

struct structdef_s;

//structure field definition
struct fielddef_s
{
	const char *name;										//name of the field
	size_t offset;									//offset in the structure
	int type;										//type of the field
	//type specific fields
	int maxarray = 0;								//maximum array size
	float floatmin = 0.0f;
	float floatmax = 0.0f;						//float min and max
	structdef_s *substruct = nullptr;			//sub structure
};
using fielddef_t = fielddef_s;

//structure definition
struct structdef_s
{
	int size;
	const fielddef_t *fields;
};
using structdef_t = structdef_s;

//read a structure from a script
int ReadStructure(source_t *source, const structdef_t *def, void *structure);
//write a structure to a file
int WriteStructure(FILE *fp, const structdef_t *def, const void *structure);
//writes indents
int WriteIndent(FILE *fp, int indent);
//writes a float without trailing zeros
int WriteFloat(FILE *fp, float value);


