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

#include "Utils.h"

//CRT
#include <cassert>
#include <sstream>
#include <cstring>
#include <queue>
#include <algorithm>
#include <ctime>
#include <io.h>
#include <fcntl.h>
#include <iomanip>
#include <codecvt>
#include <cstdarg>

//Win32
#include <Shellapi.h>

//MSVC compat
#if defined(_MSC_VER) && (_MSC_VER < 1800)
#define iswblank(X) (0)
#endif

// ==========================================================================
// SYSTEM INFO
// ==========================================================================

static inline DWORD my_popcount(DWORD64 number)
{
	static const DWORD64 m1 = 0x5555555555555555;
	static const DWORD64 m2 = 0x3333333333333333;
	static const DWORD64 m4 = 0x0f0f0f0f0f0f0f0f;
	static const DWORD64 h0 = 0x0101010101010101;
	number -= (number >> 1) & m1;
	number = (number & m2) + ((number >> 2) & m2);
	number = (number + (number >> 4)) & m4;
	return DWORD((number * h0) >> 56);
}

DWORD utils::get_processor_count(void)
{
	DWORD_PTR procMask, sysMask;
	if (GetProcessAffinityMask(GetCurrentProcess(), &procMask, &sysMask))
	{
		const DWORD count = my_popcount(procMask);
		return BOUND(DWORD(1), count, DWORD(MAXWORD));
	}
	return 1;
}

// ==========================================================================
// TIME UTILS
// ==========================================================================

bool utils::get_current_time(wchar_t *const buffer, const size_t len, const bool simple)
{
	time_t timer;
	struct tm tm_info;
	time(&timer);
	if (localtime_s(&tm_info, &timer) == 0)
	{
		return (wcsftime(buffer, len, simple ? L"%Y%m%d-%H%M%S" : L"%Y:%m:%d %H:%M:%S", &tm_info) > 0);
	}
	return false;
}

// ==========================================================================
// CONSOLE OUTPUT
// ==========================================================================

static HICON g_console_backup_icon = NULL;
static wchar_t g_console_backup_title[MAX_PATH] = L"\0";

static const WORD CONSOLE_COLORS[5] =
{
	FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
	FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN,
	FOREGROUND_INTENSITY | FOREGROUND_RED,
	FOREGROUND_INTENSITY | FOREGROUND_GREEN | FOREGROUND_BLUE,
	FOREGROUND_INTENSITY | FOREGROUND_GREEN
};

static inline WORD get_console_attribs(const HANDLE console)
{
	static WORD s_attributes = 0;
	static bool s_initialized = false;
	if(!s_initialized)
	{
		CONSOLE_SCREEN_BUFFER_INFO buffer_info;
		if (GetConsoleScreenBufferInfo(console, &buffer_info))
		{
			s_attributes = buffer_info.wAttributes;
			s_initialized = true;
		}
	}
	return s_attributes;
}

static inline WORD set_console_color(const HANDLE console, const UINT index)
{
	assert(index < 5);
	const WORD original_attribs = get_console_attribs(console);
	if (SetConsoleTextAttribute(console, (original_attribs & 0xFFF0) | CONSOLE_COLORS[index]))
	{
		return original_attribs;
	}
	return WORD(0);
}

static void restore_console_icon(void)
{
	if (g_console_backup_icon)
	{
		if (const HWND hConsole = GetConsoleWindow())
		{
			if (const HICON temp = (HICON) SendMessage(hConsole, WM_SETICON, ICON_SMALL, LPARAM(g_console_backup_icon)))
			{
				DestroyIcon(temp);
			}
			g_console_backup_icon = NULL;
		}
	}
}

static void restore_console_title(void)
{
	if (g_console_backup_title[0])
	{
		SetConsoleTitleW(g_console_backup_title);
	}
}

void utils::set_console_title(const wchar_t *const fmt, ...)
{
	if (_isatty(_fileno(stderr)))
	{
		if (fmt && fmt[0])
		{
			wchar_t title_buffer[MAX_PATH];
			va_list args;
			va_start(args, fmt);
			if (_vsnwprintf_s(title_buffer, MAX_PATH, _TRUNCATE, fmt, args) > 0)
			{
				if (!g_console_backup_title[0])
				{
					if (GetConsoleTitleW(g_console_backup_title, MAX_PATH) > 0)
					{
						atexit(restore_console_title);
					}
				}
				SetConsoleTitleW(title_buffer);
			}
			va_end(args);
		}
	}
}

bool utils::set_console_icon(const wchar_t *icon_name)
{
	if(const HWND hConsole = GetConsoleWindow())
	{
		if(icon_name && icon_name[0])
		{
			if(const HICON icon = (HICON) LoadImage(GetModuleHandle(NULL), icon_name, IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR))
			{
				const HICON previous = (HICON) SendMessage(hConsole, WM_SETICON, ICON_SMALL, LPARAM(icon));
				if (previous && (previous != icon) && (!g_console_backup_icon))
				{
					g_console_backup_icon = previous;
					atexit(restore_console_icon);
				}
				return true;
			}
		}
	}
	return false;
}

void utils::write_console(const UINT type, const bool colors, const wchar_t *const fmt, va_list &args)
{

	if ((type < 0x5) && colors && _isatty(_fileno(stderr)))
	{
		if(const HANDLE console = HANDLE(_get_osfhandle(_fileno(stderr))))
		{
			const WORD original_attribs = set_console_color(console, type);
			vfwprintf_s(stderr, fmt, args);
			fflush(stderr);
			SetConsoleTextAttribute(console, original_attribs);
		}
	}
	else
	{
		vfwprintf_s(stderr, fmt, args);
		fflush(stderr);
	}
}

// ==========================================================================
// STRING SUPPORT ROUTINES
// ==========================================================================

//Parse unsigned integer
bool utils::parse_uint32(const wchar_t *str, DWORD &value)
{
	if (swscanf_s(str, L"%lu", &value) != 1)
	{
		return false;
	}
	return true;
}

//Replace sub-strings
DWORD utils::replace_str(std::wstring& str, const std::wstring& needle, const std::wstring& replacement)
{
	DWORD count = 0;
	for (;;)
	{
		const size_t start_pos = str.find(needle);
		if (start_pos == std::string::npos)
		{
			break;
		}
		str.replace(start_pos, needle.length(), replacement);
		count++;
	}
	return count;
}

//Check for space chars
bool utils::contains_whitespace(const wchar_t *str)
{
	while (*str)
	{
		if (iswspace(*(str++)))
		{
			return true;
		}
	}
	return false;
}

//Trim trailing EOL chars
wchar_t *utils::trim_str(wchar_t *str)
{
	while ((*str) && iswspace(*str) || iswcntrl(*str) || iswblank(*str))
	{
		str++;
	}
	size_t pos = wcslen(str);
	while (pos > 0)
	{
		--pos;
		if (iswspace(str[pos]) || iswcntrl(str[pos]) || iswblank(str[pos]))
		{
			str[pos] = L'\0';
			continue;
		}
		break;
	}
	return str;
}

//Wide string to UTF-8 string
std::string utils::wstring_to_utf8(const std::wstring& str)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
	return myconv.to_bytes(str);
}

// ==========================================================================
// FILE FUNCTIONS
// ==========================================================================

//Check for FS object existence
bool utils::fs_object_exists(const wchar_t *const path)
{
	return (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES);
}

//Check for directory existence
bool utils::directory_exists(const wchar_t *const path)
{
	const DWORD attributes = GetFileAttributesW(path);
	return ((attributes != INVALID_FILE_ATTRIBUTES) && (attributes & FILE_ATTRIBUTE_DIRECTORY));
}

//Generate a unique file name
std::wstring utils::generate_unique_filename(const wchar_t *const directory, const wchar_t *const ext)
{
	wchar_t time_buffer[32];
	if (utils::get_current_time(time_buffer, 32, true))
	{
		DWORD n = 0;
		std::wstringstream file_name;
		do
		{
			if (n > 0xFFFFF)
			{
				return std::wstring(); /*deadlock prevention*/
			}
			file_name.str(std::wstring());
			file_name << directory << L'\\' << time_buffer << L'-' << std::setfill(L'0') << std::setw(5) << std::hex << (n++) << ext;
		} while (fs_object_exists(file_name.str().c_str()));
		return file_name.str();
	}
	return std::wstring();
}

//Get full path
std::wstring utils::get_full_path(const wchar_t *const rel_path)
{
	wchar_t path_buffer[MAX_PATH];
	if (const wchar_t *const full_path = _wfullpath(path_buffer, rel_path, MAX_PATH))
	{
		return std::wstring(full_path);
	}
	else
	{
		if (const wchar_t *const full_path_malloced = _wfullpath(NULL, rel_path, 0))
		{
			std::wstring result(full_path_malloced);
			free((void*)full_path_malloced);
			return result;
		}
	}
	return std::wstring();
}

//Split path into components
bool utils::split_file_name(const wchar_t *const full_path, std::wstring &drive, std::wstring &dir, std::wstring &fname, std::wstring &ext)
{
	wchar_t buff_drive[_MAX_DRIVE], buff_dir[_MAX_DIR], buff_fname[_MAX_FNAME], buff_ext[_MAX_EXT];
	const errno_t ret = _wsplitpath_s(full_path, buff_drive, buff_dir, buff_fname, buff_ext);
	if(ret == 0)
	{
		drive = buff_drive; dir = buff_dir; fname = buff_fname; ext = buff_ext;
		return true;
	}
	else if (ret == ERANGE)
	{
		const size_t len = std::max(size_t(MAX_PATH), wcslen(full_path)) + 128U;
		bool success = false;
		wchar_t* tmp_drive = new wchar_t[len], *tmp_dir = new wchar_t[len], *tmp_fname = new wchar_t[len], *tmp_ext = new wchar_t[len];
		if (_wsplitpath_s(full_path, tmp_drive, len, tmp_dir, len, tmp_fname, len, tmp_ext, len) == 0)
		{
			drive = tmp_drive; dir = tmp_dir; fname = tmp_fname; ext = tmp_ext;
			success = true;
		}
		delete[] tmp_drive; delete[] tmp_dir; delete[] tmp_fname; delete[] tmp_ext;
		return success;
	}
	else
	{
		return false; /*failed the hard way*/
	}
}
