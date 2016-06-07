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

//Buffer
namespace utils
{
	static const DWORD SCRATCH_BUFFER_SIZE = 32768;
	static wchar_t g_scratch_buffer[SCRATCH_BUFFER_SIZE];
}

// ==========================================================================
// SYSTEM INFO
// ==========================================================================

namespace utils
{
	namespace sysinfo
	{
		namespace impl
		{
			//Count '1' bits (aka "popcount")
			static inline DWORD popcount(DWORD64 number)
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
		}

		//Get number of processors
		DWORD get_processor_count(void)
		{
			DWORD_PTR procMask, sysMask;
			if (GetProcessAffinityMask(GetCurrentProcess(), &procMask, &sysMask))
			{
				const DWORD count = impl::popcount(procMask);
				return BOUND(DWORD(1), count, DWORD(64));
			}
			return 1;
		}

		//System time
		bool get_current_time(wchar_t *const buffer, const size_t len, const bool simple)
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
	}
}

// ==========================================================================
// CONSOLE OUTPUT
// ==========================================================================

namespace utils
{
	namespace console
	{
		namespace impl
		{
			//Globals
			static HICON   g_console_backup_icon       = NULL;
			static BOOL    g_console_backup_menu       = -1;
			static wchar_t g_console_backup_title[512] = L"\0";

			//Color constants
			static const WORD CONSOLE_COLORS[5] =
			{
				FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
				FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN,
				FOREGROUND_INTENSITY | FOREGROUND_RED,
				FOREGROUND_INTENSITY | FOREGROUND_GREEN | FOREGROUND_BLUE,
				FOREGROUND_INTENSITY | FOREGROUND_GREEN
			};

			//Get current console attributes
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

			//Set console color
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

			//Restore original icon
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

			//Restore original "close" state
			static void restore_console_menu(void)
			{
				if (g_console_backup_menu >= 0)
				{
					if (const HWND hConsole = GetConsoleWindow())
					{
						if(const HMENU hmenu = GetSystemMenu(hConsole, FALSE))
						{
							EnableMenuItem(hmenu, SC_CLOSE, g_console_backup_menu);
							g_console_backup_menu = -1;
						}
					}
				}
			}

			//Restore original title
			static void restore_console_title(void)
			{
				if (g_console_backup_title[0])
				{
					SetConsoleTitleW(g_console_backup_title);
				}
			}
		}

		//Set the console title
		void set_console_title(const wchar_t *const fmt, ...)
		{
			if (_isatty(_fileno(stderr)))
			{
				if (fmt && fmt[0])
				{
					va_list args;
					va_start(args, fmt);
					if (_vsnwprintf_s(g_scratch_buffer, SCRATCH_BUFFER_SIZE, _TRUNCATE, fmt, args) > 0)
					{
						if (!impl::g_console_backup_title[0])
						{
							if (GetConsoleTitleW(impl::g_console_backup_title, 512) > 0)
							{
								atexit(impl::restore_console_title);
							}
						}
						SetConsoleTitleW(g_scratch_buffer);
					}
					va_end(args);
				}
			}
		}

		//Setup console icon and disable close
		bool inti_console_window(const wchar_t *icon_name)
		{
			if(const HWND hConsole = GetConsoleWindow())
			{
				bool success = true;
				if(icon_name && icon_name[0])
				{
					success = false;
					if(const HICON icon = (HICON) LoadImage(GetModuleHandle(NULL), icon_name, IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR))
					{
						const HICON prev_icon = (HICON) SendMessage(hConsole, WM_SETICON, ICON_SMALL, LPARAM(icon));
						if (prev_icon && (prev_icon != icon) && (!impl::g_console_backup_icon))
						{
							impl::g_console_backup_icon = prev_icon;
							atexit(impl::restore_console_icon);
						}
						success = true;
					}
				}
				if(success)
				{
					if(const HMENU hmenu = GetSystemMenu(hConsole, FALSE))
					{
						const BOOL prev_state = EnableMenuItem(hmenu, SC_CLOSE, MF_GRAYED);
						success = (prev_state >= 0);
						if(success && (impl::g_console_backup_menu < 0))
						{
							impl::g_console_backup_menu = prev_state;
							atexit(impl::restore_console_menu);
						}
					}
				}
				return success;
			}
			return false;
		}

		//Write text to console
		void write_console(const UINT type, const bool colors, const wchar_t *const fmt, va_list &args)
		{

			if ((type < 0x5) && colors && _isatty(_fileno(stderr)))
			{
				if(const HANDLE console = HANDLE(_get_osfhandle(_fileno(stderr))))
				{
					const WORD original_attribs = impl::set_console_color(console, type);
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
	}
}


// ==========================================================================
// STRING SUPPORT ROUTINES
// ==========================================================================

namespace utils
{
	namespace string
	{
		//Parse unsigned integer
		bool parse_uint32(const wchar_t *const str, DWORD &value)
		{
			if (swscanf_s(str, L"%lu", &value) != 1)
			{
				return false;  /*invalid*/
			}
			return true;
		}

		//Parse boolean
		bool parse_bool(const wchar_t *const str, bool &value)
		{
			if(str && str[0])
			{
				if((_wcsicmp(str, L"0") == 0) || (_wcsicmp(str, L"NO") == 0))
				{
					value = false;
					return true;
				}
				if((_wcsicmp(str, L"1") == 0) || (_wcsicmp(str, L"YES") == 0))
				{
					value = true;
					return true;
				}
			}
			return false; /*invalid*/
		}

		//Replace sub-strings
		DWORD replace_str(std::wstring& str, const std::wstring& needle, const std::wstring& replacement)
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
		bool contains_whitespace(const wchar_t *str)
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
		wchar_t *trim_str(wchar_t *str)
		{
			while ((*str) && (iswspace(*str) || iswcntrl(*str) || iswblank(*str)))
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
		std::string wstring_to_utf8(const std::wstring& str)
		{
			std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
			return myconv.to_bytes(str);
		}
	}
}

// ==========================================================================
// JOB CONTROL
// ==========================================================================

namespace utils
{
	namespace jobs
	{
		namespace impl
		{
			static HANDLE g_job_object = NULL;

			//Release job object
			static void release_job_object(void)
			{
				if (g_job_object)
				{
					TerminateJobObject(g_job_object, FATAL_EXIT_CODE);
					CLOSE_HANDLE(g_job_object);
				}
			}

			//Create job object
			static HANDLE create_job_object(void)
			{
				if(g_job_object)
				{
					return g_job_object;
				}
				if (const HANDLE job_object = CreateJobObjectW(NULL, NULL))
				{
					JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobExtendedLimitInfo;
					memset(&jobExtendedLimitInfo, 0, sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION));
					memset(&jobExtendedLimitInfo.BasicLimitInformation, 0, sizeof(JOBOBJECT_BASIC_LIMIT_INFORMATION));
					jobExtendedLimitInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
					if (!SetInformationJobObject(job_object, JobObjectExtendedLimitInformation, &jobExtendedLimitInfo, sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION)))
					{
						CloseHandle(job_object);
						return NULL;
					}
					g_job_object = job_object;
					atexit(release_job_object);
					return job_object;
				}
				return NULL;
			}
		}

		//Add process to job object
		bool assign_process_to_job(const HANDLE process)
		{
			if(const HANDLE job_object = impl::create_job_object())
			{
				if(AssignProcessToJobObject(job_object, process) != FALSE)
				{
					return true;
				}
			}
			return false;
		}
	}
}

// ==========================================================================
// FILE FUNCTIONS
// ==========================================================================

namespace utils
{
	namespace files
	{
		//Check for FS object existence
		bool object_exists(const wchar_t *const path)
		{
			return (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES);
		}

		//Check for file existence
		bool file_exists(const wchar_t *const path)
		{
			const DWORD attributes = GetFileAttributesW(path);
			return ((attributes != INVALID_FILE_ATTRIBUTES) && ((~attributes) & FILE_ATTRIBUTE_DIRECTORY));
		}

		//Check for directory existence
		bool directory_exists(const wchar_t *const path)
		{
			const DWORD attributes = GetFileAttributesW(path);
			return ((attributes != INVALID_FILE_ATTRIBUTES) && (attributes & FILE_ATTRIBUTE_DIRECTORY));
		}

		//Generate a unique file name
		std::wstring generate_unique_filename(const wchar_t *const directory, const wchar_t *const ext)
		{
			wchar_t time_buffer[32];
			if (utils::sysinfo::get_current_time(time_buffer, 32, true))
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
				}
				while (object_exists(file_name.str().c_str()));
				return file_name.str();
			}
			return std::wstring();
		}

		//Get full path
		std::wstring get_full_path(const wchar_t *const rel_path)
		{
			if (const wchar_t *const full_path = _wfullpath(g_scratch_buffer, rel_path, SCRATCH_BUFFER_SIZE))
			{
				return std::wstring(full_path);
			}
			return std::wstring();
		}

		//Split path into components
		bool split_file_name(const wchar_t *const full_path, std::wstring &drive, std::wstring &dir, std::wstring &fname, std::wstring &ext)
		{
			static const DWORD BUFF_SIZE = SCRATCH_BUFFER_SIZE / 4;
			if(_wsplitpath_s(full_path, &g_scratch_buffer[0], BUFF_SIZE, &g_scratch_buffer[BUFF_SIZE], BUFF_SIZE, &g_scratch_buffer[2*BUFF_SIZE], BUFF_SIZE, &g_scratch_buffer[3*BUFF_SIZE], BUFF_SIZE) == 0)
			{
				drive = &g_scratch_buffer[0*BUFF_SIZE];
				dir   = &g_scratch_buffer[1*BUFF_SIZE];
				fname = &g_scratch_buffer[2*BUFF_SIZE];
				ext   = &g_scratch_buffer[3*BUFF_SIZE];
				return true;
			}
			return false; /*failed the hard way*/
		}

		//EXE file name
		std::wstring get_running_executable(void)
		{
			
			const DWORD len = GetModuleFileNameW(NULL, g_scratch_buffer, SCRATCH_BUFFER_SIZE);
			if((len > 0) && (len < SCRATCH_BUFFER_SIZE))
			{
				return std::wstring(g_scratch_buffer);
			}
			return std::wstring();
		}
	}
}
