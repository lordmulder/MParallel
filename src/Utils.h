//////////////////////////////////////////////////////////////////////////////////
// MParallel - Parallel Batch Processor
// Copyright (c) 2016 LoRd_MuldeR <mulder2@gmx.de>. Some rights reserved.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//
// http://www.gnu.org/licenses/gpl-2.0.txt
//////////////////////////////////////////////////////////////////////////////////

#pragma once

//Crt
#include <stdlib.h>
#include <string>

//Win32
#define NOMINMAX 1
#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>

//Utils
#define MATCH(X,Y) (_wcsicmp((X), (Y)) == 0)
#define BOUND(MIN,VAL,MAX) (std::min(std::max((MIN), (VAL)), (MAX)))

namespace utils
{
	//System info
	DWORD get_processor_count(void);
	bool get_current_time(wchar_t *const buffer, const size_t len, const bool simple);

	//Console
	void set_console_title(const wchar_t *const fmt, ...);
	bool set_console_icon(const wchar_t *icon_name);
	void write_console(const UINT type, const bool colors, const wchar_t *const fmt, va_list &args);

	//String support
	bool parse_uint32(const wchar_t *str, DWORD &value);
	DWORD replace_str(std::wstring& str, const std::wstring& needle, const std::wstring& replacement);
	bool contains_whitespace(const wchar_t *str);
	wchar_t *trim_str(wchar_t *str);
	std::string wstring_to_utf8(const std::wstring& str);

	//File utils
	bool fs_object_exists(const wchar_t *const path);
	bool directory_exists(const wchar_t *const path);
	std::wstring generate_unique_filename(const wchar_t *const directory, const wchar_t *const ext);
	std::wstring get_full_path(const wchar_t *const rel_path);
	bool split_file_name(const wchar_t *const full_path, std::wstring &drive, std::wstring &dir, std::wstring &fname, std::wstring &ext);
}

//Close file
#define CLOSE_FILE(X) do \
{ \
	if((X)) \
	{ \
		fclose((X)); \
		(X) = NULL; \
	} \
} \
while(0)

//Close handle
#define CLOSE_HANDLE(X) do \
{ \
	if((X)) \
	{ \
		CloseHandle((X)); \
		(X) = NULL; \
	} \
} \
while(0)
