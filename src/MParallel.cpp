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

#include "MParallel.h"

//CRT
#include <cassert>
#include <string>
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
#define NOMINMAX 1
#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>
#include <Shellapi.h>

//Utils
#define MATCH(X,Y) (_wcsicmp((X), (Y)) == 0)
#define BOUND(MIN,VAL,MAX) (std::min(std::max((MIN), (VAL)), (MAX)))

//Const
static const UINT FATAL_EXIT_CODE = 666;
static const wchar_t *const DEFAULT_SEP = L":";
static const size_t MAX_TASKS = MAXIMUM_WAIT_OBJECTS - 1;
static const wchar_t *const FILE_DELIMITERS = L"/\\:";
static const wchar_t *const BLANK_STR = L"";

//Types
typedef std::queue<std::wstring> queue_t;

//Enum
typedef enum _priority_t
{
	PRIORITY_LOWEST  = 0,
	PRIORITY_LOWER   = 1,
	PRIORITY_DEFAULT = 2,
	PRIORITY_HIGHER  = 3,
	PRIORITY_HIGHEST = 4
}
priority_t;

//Options
namespace options
{
	static DWORD        max_instances;
	static DWORD        process_priority;
	static DWORD        process_timeout;
	static bool         read_stdin_lines;
	static bool         auto_quote_vars;
	static bool         disable_lineargv;
	static bool         force_use_shell;
	static bool         abort_on_failure;
	static bool         print_manpage;
	static bool         enable_tracing;
	static bool         disable_outputs;
	static bool         disable_jobctrl;
	static bool         disable_concolor;
	static bool         ignore_exitcode;
	static bool         detached_console;
	static bool         encoding_utf16;
	static std::wstring separator;
	static std::wstring command_pattern;
	static std::wstring log_file_name;
	static std::wstring input_file_name;
	static std::wstring redir_path_name;
}

//Globals
static queue_t g_queue;
static bool    g_logo_printed;
static HANDLE  g_processes[MAX_TASKS];
static bool    g_isrunning[MAX_TASKS];
static DWORD   g_process_count;
static DWORD   g_processes_completed[2];
static DWORD   g_max_exit_code;
static HANDLE  g_job_object;
static FILE*   g_log_file;
static HANDLE  g_interrupt_event;

//Text out
#define PRINT_NFO(...) print_impl(0x0, __VA_ARGS__)
#define PRINT_WRN(...) print_impl(0x1, __VA_ARGS__)
#define PRINT_ERR(...) print_impl(0x2, __VA_ARGS__)
#define PRINT_EMP(...) print_impl(0x3, __VA_ARGS__)
#define PRINT_FIN(...) print_impl(0x4, __VA_ARGS__)
#define PRINT_TRC(...) print_impl(0x5, __VA_ARGS__)

//Forward declaration
static inline void print_impl(const UINT, const wchar_t *const, ...);

//MSVC compat
#if defined(_MSC_VER) && (_MSC_VER < 1800)
#define iswblank(X) (0)
#endif


// ==========================================================================
// WIN32 SUPPORT
// ==========================================================================

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

static inline DWORD processor_count(void)
{
	DWORD_PTR procMask, sysMask;
	if (GetProcessAffinityMask(GetCurrentProcess(), &procMask, &sysMask))
	{
		const DWORD count = my_popcount(procMask);
		return BOUND(DWORD(1), count, DWORD(MAX_TASKS));
	}
	return 1;
}

// ==========================================================================
// LOGO / MANPAGE
// ==========================================================================

static void print_logo(void)
{
	PRINT_NFO(L"===============================================================================\n");
	PRINT_NFO(L"MParallel - Parallel Batch Processor, Version %u.%u.%u [%S]\n", MPARALLEL_VERSION_MAJOR, MPARALLEL_VERSION_MINOR, MPARALLEL_VERSION_PATCH, __DATE__);
	PRINT_NFO(L"Copyright (c) 2016 LoRd_MuldeR <mulder2@gmx.de>. Some rights reserved.\n\n");
	PRINT_NFO(L"This program is free software: you can redistribute it and/or modify\n");
	PRINT_NFO(L"it under the terms of the GNU General Public License <http://www.gnu.org/>.\n");
	PRINT_NFO(L"Note that this program is distributed with ABSOLUTELY NO WARRANTY.\n");
	PRINT_NFO(L"=============================================================================== \n\n");
}

static void print_manpage(void)
{
	PRINT_NFO(L"Synopsis:\n");
	PRINT_NFO(L"  MParallel.exe [options] <command_1> : <command_2> : ... : <command_n>\n");
	PRINT_NFO(L"  MParallel.exe [options] --input=commands.txt\n");
	PRINT_NFO(L"  GenerateCommands.exe [parameters] | MParallel.exe [options] --stdin\n\n");
	PRINT_NFO(L"Options:\n");
	PRINT_NFO(L"  --count=<N>          Run at most N instances in parallel (Default is %u)\n", processor_count());
	PRINT_NFO(L"  --pattern=<PATTERN>  Generate commands from the specified PATTERN\n");
	PRINT_NFO(L"  --separator=<SEP>    Set the command separator to SEP (Default is '%s')\n", DEFAULT_SEP);
	PRINT_NFO(L"  --input=<FILE>       Read additional commands from specified FILE\n");
	PRINT_NFO(L"  --stdin              Read additional commands from STDIN stream\n");
	PRINT_NFO(L"  --logfile=<FILE>     Save logfile to FILE, appends if the file exists\n");
	PRINT_NFO(L"  --out-path=<PATH>    Redirect the stdout/stderr of sub-processes to PATH\n");
	PRINT_NFO(L"  --auto-wrap          Automatically wrap tokens in quotation marks\n");
	PRINT_NFO(L"  --no-split-lines     Ignore whitespaces when reading commands from file\n");
	PRINT_NFO(L"  --shell              Start each command inside a new sub-shell (cmd.exe)\n");
	PRINT_NFO(L"  --timeout=<TIMEOUT>  Kill processes after TIMEOUT milliseconds\n");
	PRINT_NFO(L"  --priority=<VALUE>   Run commands with the specified process priority\n");
	PRINT_NFO(L"  --ignore-exitcode    Do NOT check the exit code of sub-processes\n");
	PRINT_NFO(L"  --utf16              Read the input file as UTF-16 (Default is UTF-8)\n");
	PRINT_NFO(L"  --detached           Run each sub-process in a separate console window\n");
	PRINT_NFO(L"  --abort              Abort batch, if any command failed to execute\n");
	PRINT_NFO(L"  --no-jobctrl         Do NOT add new sub-processes to job object\n");
	PRINT_NFO(L"  --silent             Disable all textual messages, aka \"silent mode\"\n");
	PRINT_NFO(L"  --no-colors          Do NOT applay colors to textual console output\n");
	PRINT_NFO(L"  --trace              Enable more diagnostic outputs (for debugging only)\n");
	PRINT_NFO(L"  --help               Print this help screen\n");
}

// ==========================================================================
// TIME UTILS
// ==========================================================================

static bool get_current_time(wchar_t *const buffer, const size_t len, const bool simple)
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
// TEXT OUTPUT
// ==========================================================================

#define LOG(...) do \
{ \
	if(g_log_file) \
	{ \
		logging_impl(__VA_ARGS__); \
	} \
} \
while(0)

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
	CONSOLE_SCREEN_BUFFER_INFO buffer_info;
	if (GetConsoleScreenBufferInfo(console, &buffer_info))
	{
		return buffer_info.wAttributes;
	}
	return WORD(0);
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

static inline void write_console(const UINT type, const wchar_t *const fmt, va_list &args)
{

	if ((type < 0x5) && (!options::disable_concolor) && _isatty(_fileno(stderr)))
	{
		const HANDLE console = HANDLE(_get_osfhandle(_fileno(stderr)));
		const WORD original_attribs = set_console_color(console, type);
		vfwprintf_s(stderr, fmt, args);
		fflush(stderr);
		SetConsoleTextAttribute(console, original_attribs);
	}
	else
	{
		vfwprintf_s(stderr, fmt, args);
		fflush(stderr);
	}
}

static inline void logging_impl(const wchar_t *const fmt, ...)
{
	assert(g_log_file != NULL);
	wchar_t time_buffer[32];
	if (get_current_time(time_buffer, 32, false))
	{
		va_list args;
		va_start(args, fmt);
		fwprintf_s(g_log_file, L"[%s] ", time_buffer);
		vfwprintf_s(g_log_file, fmt, args);
		va_end(args);
	}
}

static inline void print_impl(const UINT type, const wchar_t *const fmt, ...)
{
	if ((type < 0x4) && (!g_logo_printed))
	{
		g_logo_printed = true;
		print_logo();
	}
	if ((type < 0x5) || options::enable_tracing)
	{
		va_list args;
		va_start(args, fmt);
		write_console(type, fmt, args);
		va_end(args);
	}
}

// ==========================================================================
// FILE FUNCTIONS
// ==========================================================================

//Check for FS object existence
static bool fs_object_exists(const wchar_t *const path)
{
	return (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES);
}

//Check for directory existence
static bool directory_exists(const wchar_t *const path)
{
	const DWORD attributes = GetFileAttributesW(path);
	return ((attributes != INVALID_FILE_ATTRIBUTES) && (attributes & FILE_ATTRIBUTE_DIRECTORY));
}

//Generate a unique file name
static std::wstring generate_unique_filename(const wchar_t *const directory, const wchar_t *const ext)
{
	wchar_t time_buffer[32];
	if (get_current_time(time_buffer, 32, true))
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
static std::wstring get_full_path(const wchar_t *const rel_path)
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
static bool split_file_name(const wchar_t *const full_path, std::wstring &drive, std::wstring &dir, std::wstring &fname, std::wstring &ext)
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

// ==========================================================================
// LOGGING
// ==========================================================================

static void open_log_file(const wchar_t *file_name)
{
	if (!g_log_file)
	{
		if (g_log_file = _wfsopen(file_name, L"a,ccs=UTF-8", _SH_DENYWR))
		{
			_fseeki64(g_log_file, 0, SEEK_END);
			if (_ftelli64(g_log_file) > 0)
			{
				fwprintf(g_log_file, L"---------------------\n");
			}
		}
		else
		{
			g_log_file = NULL;
			PRINT_ERR(L"ERROR: Failed to open log file \"%s\" for writing!\n\n", options::log_file_name.c_str());
		}
	}
}

// ==========================================================================
// ERROR HANDLING
// ==========================================================================

static void fatal_exit(const wchar_t *const error_message)
{
	const HANDLE hStdErr = GetStdHandle(STD_ERROR_HANDLE);
	if (hStdErr != INVALID_HANDLE_VALUE)
	{
		DWORD written;
		WriteFile(hStdErr, error_message, lstrlenW(error_message), &written, NULL);
		FlushFileBuffers(hStdErr);
	}
	TerminateProcess(GetCurrentProcess(), FATAL_EXIT_CODE);
}

static void my_invalid_parameter_handler(wchar_t const*, wchar_t const*, wchar_t const*, unsigned int, uintptr_t)
{
	fatal_exit(L"\n\nFATAL: Invalid parameter handler invoked!\n\n");
}

static BOOL __stdcall console_ctrl_handler(DWORD ctrl_type)
{
	switch (ctrl_type)
	{
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
		if (g_interrupt_event)
		{
			SetEvent(g_interrupt_event);
			return TRUE;
		}
	}
	return FALSE;
}

// ==========================================================================
// STRING SUPPORT ROUTINES
// ==========================================================================

//Parse unsigned integer
static bool parse_uint32(const wchar_t *str, DWORD &value)
{
	if (swscanf_s(str, L"%lu", &value) != 1)
	{
		options::disable_outputs = false;
		PRINT_ERR(L"ERROR: Argument \"%s\" doesn't look like a valid integer!\n\n", str);
		return false;
	}
	return true;
}

//Replace sub-strings
static DWORD replace_str(std::wstring& str, const std::wstring& needle, const std::wstring& replacement)
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
static bool contains_whitespace(const wchar_t *str)
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
static wchar_t *trim_str(wchar_t *str)
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
static std::string wstring_to_utf8(const std::wstring& str)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
	return myconv.to_bytes(str);
}

// ==========================================================================
// COMMAND-LINE HANDLING
// ==========================================================================

static DWORD expand_placeholder(std::wstring &str, const DWORD n, const wchar_t postfix, const wchar_t *const value)
{
	std::wstringstream placeholder;
	if (postfix)
	{
		placeholder << L"{{" << n << L':' << postfix << L"}}";
	}
	else
	{
		placeholder << L"{{" << n << L"}}";
	}

	if (options::auto_quote_vars && ((!value) || (!value[0]) || contains_whitespace(value)))
	{
		std::wstringstream replacement;
		replacement << L'"' << value << L'"';
		return replace_str(str, placeholder.str(), replacement.str());
	}
	else
	{
		return replace_str(str, placeholder.str(), value);
	}
}

//Parse commands (simple)
static void parse_commands_simple(const int argc, const wchar_t *const argv[], const int offset, const wchar_t *const separator)
{
	int i = offset;
	std::wstringstream command_buffer;
	while (i < argc)
	{
		const wchar_t *const current = argv[i++];
		PRINT_TRC(L"Process token: %s\n", current);
		if ((!separator) || wcscmp(current, separator))
		{
			if (command_buffer.tellp())
			{
				command_buffer << L' ';
			}
			if ((!current[0]) || contains_whitespace(current))
			{
				command_buffer << L'"' << current << L'"';
			}
			else
			{
				command_buffer << current;
			}
		}
		else
		{
			if (command_buffer.tellp())
			{
				g_queue.push(std::move(command_buffer.str()));
			}
			command_buffer.str(std::wstring());
			command_buffer.clear();
		}
	}
	if (command_buffer.tellp())
	{
		g_queue.push(command_buffer.str());
	}
}

//Parse commands with pattern
static void parse_commands_pattern(const std::wstring &pattern, int argc, const wchar_t *const argv[], const int offset, const wchar_t *const separator)
{
	int i = offset, var_idx = 0;
	std::wstring command_buffer = pattern;
	while (i < argc)
	{
		const wchar_t *const current = argv[i++];
		PRINT_TRC(L"Process token: %s\n", current);
		if ((!separator) || wcscmp(current, separator))
		{
			static const wchar_t *const TYPES = L"FDPNX";
			DWORD expanded = 0;
			expanded += expand_placeholder(command_buffer, var_idx, 0x00, current);
			const std::wstring file_full = get_full_path(current);
			if (!file_full.empty())
			{
				expanded += expand_placeholder(command_buffer, var_idx, TYPES[0], file_full.c_str());
				std::wstring file_drive, file_dir, file_fname, file_ext;
				if (split_file_name(file_full.c_str(), file_drive, file_dir, file_fname, file_ext))
				{
					expanded += expand_placeholder(command_buffer, var_idx, TYPES[1], file_drive.c_str());
					expanded += expand_placeholder(command_buffer, var_idx, TYPES[2], file_dir.c_str());
					expanded += expand_placeholder(command_buffer, var_idx, TYPES[3], file_fname.c_str());
					expanded += expand_placeholder(command_buffer, var_idx, TYPES[4], file_ext.c_str());
				}
			}
			for (DWORD i = 0; TYPES[i]; i++)
			{
				expanded += expand_placeholder(command_buffer, var_idx, TYPES[i], BLANK_STR);
			}
			if(expanded < 1)
			{
				PRINT_WRN(L"WARNING: Discarding token \"%s\", due to missing {{%u}} placeholder!\n\n", current, var_idx);
			}
			var_idx++;
		}
		else
		{
			if (!command_buffer.empty())
			{
				g_queue.push(command_buffer);
				var_idx = 0;
				command_buffer = pattern;
			}
		}
	}
	if ((!command_buffer.empty()) && (var_idx > 0))
	{
		g_queue.push(command_buffer);
	}
}

//Parse commands
static void parse_commands(int argc, const wchar_t *const argv[], const int offset, const wchar_t *const separator)
{
	if (!options::command_pattern.empty())
	{
		parse_commands_pattern(options::command_pattern, argc, argv, offset, separator);
	}
	else
	{
		parse_commands_simple(argc, argv, offset, separator);
	}
}

// ==========================================================================
// OPTION HANDLING
// ==========================================================================

#define REQUIRE_VALUE() do \
{ \
	if ((!value) || (!value[0])) \
	{ \
		options::disable_outputs = false; \
		PRINT_ERR(L"ERROR: Argumet for option \"--%s\" is missing!\n\n", option); \
		return false; \
	} \
} \
while(0)

#define REQUIRE_NO_VALUE() do \
{ \
	if (value && value[0]) \
	{ \
		options::disable_outputs = false; \
		PRINT_ERR(L"ERROR: Excess argumet for option \"--%s\" encountred!\n\n", option); \
		return false; \
	} \
} \
while(0)

//Load defaults
static void reset_all_options(void)
{
	options::force_use_shell = false;
	options::read_stdin_lines = false;
	options::auto_quote_vars = false;
	options::disable_lineargv = false;
	options::abort_on_failure = false;
	options::enable_tracing = false;
	options::disable_outputs = false;
	options::disable_jobctrl = false;
	options::disable_concolor = false;
	options::ignore_exitcode = false;
	options::detached_console = false;
	options::encoding_utf16 = false;
	options::separator = DEFAULT_SEP;
	options::max_instances = processor_count();
	options::process_timeout = 0;
	options::process_priority = PRIORITY_DEFAULT;
}

//Parse option
static bool parse_option_string(const wchar_t *const option, const wchar_t *const value)
{
	DWORD temp;

	if (MATCH(option, L"pattern"))
	{
		REQUIRE_VALUE();
		options::command_pattern = value;
		return true;
	}
	else if (MATCH(option, L"count"))
	{
		REQUIRE_VALUE();
		if (parse_uint32(value, temp))
		{
			options::max_instances = BOUND(DWORD(1), temp, DWORD(MAX_TASKS));
			return true;
		}
		return false;
	}
	else if (MATCH(option, L"separator"))
	{
		REQUIRE_VALUE();
		options::separator = value;
		return true;
	}
	else if (MATCH(option, L"stdin"))
	{
		REQUIRE_NO_VALUE();
		options::read_stdin_lines = true;
		return true;
	}
	else if (MATCH(option, L"input"))
	{
		REQUIRE_VALUE();
		options::input_file_name = value;
		return true;
	}
	else if (MATCH(option, L"logfile"))
	{
		REQUIRE_VALUE();
		options::log_file_name = value;
		return true;
	}
	else if (MATCH(option, L"out-path"))
	{
		REQUIRE_VALUE();
		options::redir_path_name = value;
		return true;
	}
	else if (MATCH(option, L"auto-wrap"))
	{
		REQUIRE_NO_VALUE();
		options::auto_quote_vars = true;
		return true;
	}
	else if (MATCH(option, L"no-split-lines"))
	{
		REQUIRE_NO_VALUE();
		options::disable_lineargv = true;
		return true;
	}
	else if (MATCH(option, L"shell"))
	{
		REQUIRE_NO_VALUE();
		options::force_use_shell = true;
		return true;
	}
	else if (MATCH(option, L"timeout"))
	{
		REQUIRE_VALUE();
		if (parse_uint32(value, temp))
		{
			options::process_timeout = temp;
			return true;
		}
		return false;
	}
	else if (MATCH(option, L"priority"))
	{
		REQUIRE_VALUE();
		if (parse_uint32(value, temp))
		{
			options::process_priority = BOUND(DWORD(PRIORITY_LOWEST), temp, DWORD(PRIORITY_HIGHEST));
			return true;
		}
		return false;
	}
	else if (MATCH(option, L"detached"))
	{
		REQUIRE_NO_VALUE();
		options::detached_console = true;
		return true;
	}
	else if (MATCH(option, L"abort"))
	{
		REQUIRE_NO_VALUE();
		options::abort_on_failure = true;
		return true;
	}
	else if (MATCH(option, L"no-jobctrl"))
	{
		REQUIRE_NO_VALUE();
		options::disable_jobctrl = true;
		return true;
	}
	else if (MATCH(option, L"ignore-exitcode"))
	{
		REQUIRE_NO_VALUE();
		options::ignore_exitcode = true;
		return true;
	}
	else if (MATCH(option, L"utf16"))
	{
		REQUIRE_NO_VALUE();
		options::encoding_utf16 = true;
		return true;
	}
	else if (MATCH(option, L"trace"))
	{
		REQUIRE_NO_VALUE();
		options::enable_tracing = true;
		return true;
	}
	else if (MATCH(option, L"silent"))
	{
		REQUIRE_NO_VALUE();
		options::disable_outputs = true;
		return true;
	}
	else if (MATCH(option, L"no-colors"))
	{
		REQUIRE_NO_VALUE();
		options::disable_concolor = true;
		return true;
	}
	else if (MATCH(option, L"help"))
	{
		REQUIRE_NO_VALUE();
		options::print_manpage = true;
		return true;
	}

	options::disable_outputs = false;
	PRINT_ERR(L"ERROR: Unknown option \"--%s\" encountred!\n\n", option);
	return false;
}

//Parse option
static bool parse_option_string(const wchar_t *const option_str)
{
	wchar_t opt_buffer[32];
	const wchar_t *const pos = wcschr(option_str, L'=');
	if (pos && (pos != option_str))
	{
		wcsncpy_s(opt_buffer, 32, option_str, (pos - option_str));
		return parse_option_string(opt_buffer, (*(pos + 1)) ? (pos + 1) : NULL);
	}
	else
	{
		return parse_option_string(option_str, NULL);
	}
}

//Validate options
static bool validate_options(void)
{
	if (options::enable_tracing && options::disable_outputs)
	{
		options::disable_outputs = false;
		PRINT_ERR(L"ERROR: Options \"--trace\" and \"--silent\" are mutually exclusive!\n\n");
		return false;
	}
	if (!options::redir_path_name.empty())
	{
		if (!directory_exists(options::redir_path_name.c_str()))
		{
			CreateDirectoryW(options::redir_path_name.c_str(), NULL);
			if (!directory_exists(options::redir_path_name.c_str()))
			{
				options::disable_outputs = false;
				PRINT_ERR(L"ERROR: Specified output directory \"%s\" does NOT exist!\n\n", options::redir_path_name.c_str());
				return false;
			}
		}
	}
	return true;
}

//Parse arguments
static bool parse_arguments(const int argc, const wchar_t *const argv[])
{
	int i = 1;
	while(i < argc)
	{
		const wchar_t *const current = argv[i++];
		if ((current[0] == L'-') && (current[1] == L'-'))
		{
			PRINT_TRC(L"Process token: %s\n", current);
			if (current[2])
			{
				if (!parse_option_string(&current[2]))
				{
					return false;
				}
				if (options::print_manpage)
				{
					options::disable_outputs = false;
					break;
				}
			}
			else
			{
				parse_commands(argc, argv, i, options::separator.c_str());
				break;
			}
		}
		else
		{
			parse_commands(argc, argv, --i, options::separator.c_str());
			break;
		}
	}
	return validate_options();
}

//Read from file stream
static void parse_commands_file(FILE *const input)
{
	wchar_t line_buffer[32768];
	while (wchar_t *const current_line = fgetws(line_buffer, 32768, input))
	{
		int argc;
		const wchar_t *const trimmed = trim_str(current_line);
		if (trimmed && trimmed[0])
		{
			PRINT_TRC(L"Read line: %s\n", trimmed);
			if (!options::disable_lineargv)
			{
				wchar_t *const *const argv = CommandLineToArgvW(trimmed, &argc);
				if (!argv)
				{
					fatal_exit(L"Exit: CommandLineToArgvW() has failed!\n\n");
				}
				parse_commands(argc, argv, 0, NULL);
				LocalFree((HLOCAL)argv);
			}
			else
			{
				const wchar_t *const argv[1] = { trimmed };
				parse_commands(1, argv, 0, NULL);
			}
		}
	}
}

//Read from file
static bool parse_commands_file(const wchar_t *const file_name)
{
	FILE *file = NULL;
	if (_wfopen_s(&file, file_name, options::encoding_utf16 ? L"r,ccs=UTF-16LE" : L"r,ccs=UTF-8") == 0)
	{
		parse_commands_file(file);
		fclose(file);
		return true;
	}
	PRINT_ERR(L"ERROR: Unbale to open file \"%s\" for reading!\n\n", file_name);
	return false;
}

// ==========================================================================
// PROCESS FUNCTIONS
// ==========================================================================

//Dequeue next task
static inline std::wstring dequeue_next_command(void)
{
	assert(g_queue.size() > 0);
	const std::wstring next_item = g_queue.front();
	g_queue.pop();
	return next_item;
}

//Print Win32 error message
static void print_win32_error(const wchar_t *const format, const DWORD error)
{
	wchar_t buffer[1024];
	if (FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buffer, 1024, NULL) > 0)
	{
		PRINT_WRN(format, trim_str(buffer));
	}
}

static bool release_process(const DWORD index, const bool cancelled)
{
	assert(g_isrunning[index]);
	DWORD exit_code = 1;
	bool succeeded = false;

	if (!cancelled)
	{
		if (GetExitCodeProcess(g_processes[index], &exit_code))
		{
			PRINT_TRC(L"Process 0x%X terminated with exit code 0x%X.\n", GetProcessId(g_processes[index]), exit_code);
			LOG(L"Process terminated: 0x%X (Exit code: 0x%X).\n", GetProcessId(g_processes[index]), exit_code);
			if (!(succeeded = (exit_code == 0) || options::ignore_exitcode))
			{
				PRINT_ERR(L"\nERROR: The command has failed! (ExitCode: %u)\n\n", exit_code);
			}
		}
		else
		{
			exit_code = 1; /*just to be sure*/
			PRINT_WRN(L"WARNING: Exit code for process 0x%X could not be determined.\n", GetProcessId(g_processes[index]));
			LOG(L"Process terminated: 0x%X (Exit code N/A).\n", GetProcessId(g_processes[index]));
		}
	}

	CLOSE_HANDLE(g_processes[index]);
	g_processes[index] = NULL;
	g_isrunning[index] = false;

	g_max_exit_code = std::max(g_max_exit_code, exit_code);
	g_process_count--;
	g_processes_completed[succeeded ? 0 : 1]++;

	return succeeded;
}

//Terminate all running processes
static void terminate_processes(void)
{
	for (DWORD i = 0; i < options::max_instances; i++)
	{
		if (g_isrunning[i])
		{
			TerminateProcess(g_processes[i], 666);
			release_process(i, true);
		}
	}
}

//Create job object
static HANDLE create_job_object(void)
{
	const HANDLE job_object = CreateJobObjectW(NULL, NULL);
	if (job_object)
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
	}
	return job_object;
}

//Create redirection file
static HANDLE create_redirection_file(const wchar_t *const directory, const wchar_t *const command)
{
	const std::wstring file_name = generate_unique_filename(directory, L".log");
	if (!file_name.empty())
	{
		SECURITY_ATTRIBUTES sec_attrib;
		memset(&sec_attrib, 0, sizeof(SECURITY_ATTRIBUTES));
		sec_attrib.bInheritHandle = TRUE;
		sec_attrib.nLength = sizeof(SECURITY_ATTRIBUTES);
		const HANDLE handle = CreateFileW(file_name.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sec_attrib, CREATE_ALWAYS, 0, NULL);
		if (handle != INVALID_HANDLE_VALUE)
		{
			static const char *const BOM = "\xef\xbb\xbf", *const EOL = "\r\n\r\n";
			const std::string command_utf8 = wstring_to_utf8(command);
			DWORD written;
			WriteFile(handle, BOM, (DWORD)strlen(BOM), &written, NULL);
			WriteFile(handle, command_utf8.c_str(), (DWORD)command_utf8.size(), &written, NULL);
			WriteFile(handle, EOL, (DWORD)strlen(EOL), &written, NULL);
			return handle;
		}
	}
	return NULL;
}

//Translate to Win32 priority class
static DWORD get_priority_class(const DWORD priority)
{
	switch (options::process_priority)
	{
		case PRIORITY_LOWEST:  return IDLE_PRIORITY_CLASS;         break;
		case PRIORITY_LOWER:   return BELOW_NORMAL_PRIORITY_CLASS; break;
		case PRIORITY_DEFAULT: return NORMAL_PRIORITY_CLASS;       break;
		case PRIORITY_HIGHER:  return ABOVE_NORMAL_PRIORITY_CLASS; break;
		case PRIORITY_HIGHEST: return HIGH_PRIORITY_CLASS;         break;
	}
	PRINT_WRN(L"WARNING: Unknown priority value %u specified!", priority);
	return 0;
}

//Start the next process
static bool start_next_process(std::wstring command)
{
	bool success = false;
	if (options::force_use_shell)
	{
		std::wstringstream builder;
		builder << L"cmd.exe /c \"" << command << L"\"";
		command = builder.str();
	}

	PRINT_EMP(L"%s\n\n", command.c_str());
	LOG(L"Starting process: %s\n", command.c_str());

	STARTUPINFOW startup_info;
	memset(&startup_info, 0, sizeof(STARTUPINFOW));

	PROCESS_INFORMATION process_info;
	memset(&process_info, 0, sizeof(PROCESS_INFORMATION));

	HANDLE redir_file = NULL;
	if (!options::redir_path_name.empty())
	{
		if (redir_file = create_redirection_file(options::redir_path_name.c_str(), command.c_str()))
		{
			startup_info.dwFlags = startup_info.dwFlags | STARTF_USESTDHANDLES;
			startup_info.hStdOutput = startup_info.hStdError = redir_file;
		}
	}

	DWORD flags = CREATE_BREAKAWAY_FROM_JOB | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | get_priority_class(options::process_priority);
	if (options::detached_console)
	{
		flags = flags | CREATE_NEW_CONSOLE;
	}
	
	if (CreateProcessW(NULL, (LPWSTR)command.c_str(), NULL, NULL, (redir_file ? TRUE : FALSE), flags, NULL, NULL, &startup_info, &process_info))
	{
		if (g_job_object)
		{
			if (!AssignProcessToJobObject(g_job_object, process_info.hProcess))
			{
				PRINT_WRN(L"WARNING: Failed to assign process to job object!\n\n");
			}
		}
		if (ResumeThread(process_info.hThread))
		{
			PRINT_TRC(L"Process 0x%X has been started.\n\n", process_info.dwProcessId);
			LOG(L"Process started: 0x%X\n", process_info.dwProcessId);
			static DWORD slot = 0;
			do
			{
				slot = (slot + 1) % options::max_instances;
			}
			while(g_isrunning[slot]);
			g_process_count++;
			g_isrunning[slot] = true;
			g_processes[slot] = process_info.hProcess;
			success = true;
		}
		else
		{
			TerminateProcess(process_info.hProcess, 666);
			CLOSE_HANDLE(process_info.hProcess);
			PRINT_ERR(L"ERROR: Failed to resume the process -> terminating!\n\n");
		}
		CLOSE_HANDLE(process_info.hThread);
	}
	else
	{
		const DWORD error = GetLastError();
		PRINT_TRC(L"CreateProcessW() failed with Win32 error code: 0x%X.\n\n", error);
		print_win32_error(L"\nProcess creation has failed: %s\n\n", error);
		PRINT_ERR(L"ERROR: Process ``%s´´could not be created!\n\n", command.c_str());
		LOG(L"Process creation failed! (Error  0x%X)\n", error);
	}

	CLOSE_HANDLE(redir_file);
	return success;
}

//Wait for *any* running process to terminate
static DWORD wait_for_process(bool &timeout, bool &interrupted)
{
	DWORD index[MAX_TASKS];
	HANDLE handles[MAX_TASKS+1];
	
	DWORD count = 0;
	for (DWORD i = 0; i < options::max_instances; i++)
	{
		if (g_isrunning[i])
		{
			index[count] = i;
			handles[count++] = g_processes[i];
		}
	}

	if (g_interrupt_event)
	{
		handles[count] = g_interrupt_event;
	}

	if (count < 1)
	{
		PRINT_ERR(L"INTERNAL ERROR: No runnings processes to be awaited!\n\n");
		abort();
	}

	const DWORD num_handels = g_interrupt_event ? (count + 1) : count;
	const DWORD ret = WaitForMultipleObjects(num_handels, &handles[0], FALSE, (options::process_timeout > 0) ? options::process_timeout : INFINITE);
	if ((ret >= WAIT_OBJECT_0) && (ret < WAIT_OBJECT_0 + count))
	{
		return index[ret - WAIT_OBJECT_0];
	}

	interrupted = (ret == WAIT_OBJECT_0 + count);
	timeout = (ret == WAIT_TIMEOUT) && (options::process_timeout > 0);
	if(interrupted || timeout)
	{
		PRINT_TRC(L"WaitForMultipleObjects() failed with Win32 error code: 0x%X.\\nn", GetLastError());
	}

	return MAXDWORD;
}

//Run processes
static void run_processes(void)
{
	DWORD slot = 0;
	bool aborted = false, interrupted = false;

	//MAIN PROCESSING LOOP
	while (!((g_queue.empty() && (g_process_count < 1)) || aborted || interrupted))
	{
		//Launch the next process(es)
		while ((!g_queue.empty()) && (g_process_count < options::max_instances))
		{
			if (g_interrupt_event)
			{
				if (WaitForSingleObject(g_interrupt_event, 0) == WAIT_OBJECT_0)
				{
					g_max_exit_code = std::max(g_max_exit_code, DWORD(1));
					interrupted = aborted = true;
					break;
				}
			}
			if (!start_next_process(dequeue_next_command()))
			{
				g_max_exit_code = std::max(g_max_exit_code, DWORD(1));
				if (options::abort_on_failure)
				{
					aborted = true;
					break;
				}
			}
		}

		//Wait for one process to terminate
		if ((!aborted) && (g_process_count > 0) && ((g_process_count >= options::max_instances) || g_queue.empty()))
		{
			bool timeout = false;
			const DWORD index = wait_for_process(timeout, interrupted);
			if (index != MAXDWORD)
			{
				if (!release_process(index, false))
				{
					if (options::abort_on_failure)
					{
						aborted = true;
						break;
					}
				}
			}
			else
			{
				g_max_exit_code = std::max(g_max_exit_code, DWORD(1));
				if (timeout)
				{
					PRINT_ERR(L"\nERROR: Timeout encountered, terminating running process!\n\n");
					if (options::abort_on_failure)
					{
						aborted = true;
						break;
					}
					terminate_processes();
				}
				else
				{
					if (!interrupted)
					{
						PRINT_ERR(L"\nFATAL ERROR: Failed to wait for running process!\n\n");
					}
					aborted = true;
					break;
				}
			}
		}
	}

	//Was the process interrupted?
	if (interrupted)
	{
		PRINT_ERR(L"\nSIGINT: Interrupted by user, exiting!\n\n");
	}

	//Terminate all processes still running at this point
	terminate_processes();
	assert(g_process_count < 1);
}

// ==========================================================================
// MAIN FUNCTION
// ==========================================================================

static int mparallel_main(const int argc, const wchar_t *const argv[])
{
	//Initialize globals
	g_logo_printed = false;
	g_interrupt_event = NULL;
	g_log_file = NULL;
	g_job_object = NULL;
	g_processes_completed[0] = 0;
	g_processes_completed[1] = 0;
	g_process_count = 0;
	g_max_exit_code = 0;

	//Clear
	memset(g_processes, 0, sizeof(HANDLE) * MAX_TASKS);
	memset(g_isrunning, 0, sizeof(bool)   * MAX_TASKS);
	
	//Init options
	reset_all_options();

	//Create event
	if (g_interrupt_event = CreateEventW(NULL, TRUE, FALSE, NULL))
	{
		SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
	}

	//Parse CLI arguments
	if (!parse_arguments(argc, argv))
	{
		options::disable_outputs = false;
		PRINT_WRN(L"Failed to parse command-line arguments. Run with option \"--help\" for guidance!\n\n");
		return FATAL_EXIT_CODE;
	}

	//Print manpage?
	if (options::print_manpage)
	{
		print_manpage();
		return EXIT_SUCCESS;
	}

	//Open log file
	if (!options::log_file_name.empty())
	{
		open_log_file(options::log_file_name.c_str());
	}

	//Parse jobs from file
	if (!options::input_file_name.empty())
	{
		if (!parse_commands_file(options::input_file_name.c_str()))
		{
			PRINT_WRN(L"Failed to read commands from specified input file!\n\n");
			return FATAL_EXIT_CODE;
		}
	}

	//Parse jobs from STDIN
	if (options::read_stdin_lines)
	{
		_setmode(_fileno(stdin), options::encoding_utf16 ? _O_U16TEXT : _O_U8TEXT);
		parse_commands_file(stdin);
	}

	//Valid queue?
	if (g_queue.size() < 1)
	{
		PRINT_WRN(L"Nothing to do. Run with option \"--help\" for guidance!\n\n");
		return FATAL_EXIT_CODE;
	}

	//No more logo after this point
	g_logo_printed = true;

	//Logging
	LOG(L"Enqueued tasks: %u (Parallel instances: %u)\n", g_queue.size(), options::max_instances);
	PRINT_TRC(L"Tasks in queue: %zu\n", g_queue.size());
	PRINT_TRC(L"Maximum parallel instances: %u\n", options::max_instances);

	//Create job object
	if (!options::disable_jobctrl)
	{
		g_job_object = create_job_object();
		if (!g_job_object)
		{
			PRINT_WRN(L"WARNING: Failed to create the job object!\n\n");
		}
	}

	//Run processes
	const clock_t timestamp_enter = clock();
	run_processes();
	const clock_t timestamp_leave = clock();

	//Close the job object
	if (g_job_object)
	{
		TerminateJobObject(g_job_object, FATAL_EXIT_CODE);
		CLOSE_HANDLE(g_job_object);
	}

	//Compute total time
	const double total_time = double(timestamp_leave - timestamp_enter) / double(CLOCKS_PER_SEC);
	PRINT_NFO(L"\n--------\n\n");
	if (g_processes_completed[1] < 1)
	{
		PRINT_FIN(L"Executed %u task(s) in %.2f seconds. All tasks completed successfully.\n\n", g_processes_completed[0], total_time);
	}
	else
	{
		PRINT_WRN(L"Completed %u task(s) in %.2f seconds, %u task(s) failed!\n\n", g_processes_completed[0], total_time, g_processes_completed[1]);
	}

	//Logging
	LOG(L"Total execution time: %.2f (Completed tasks: %u, Failed tasks: %u)\n", total_time, g_processes_completed);

	//Close the log file
	if (g_log_file)
	{
		fclose(g_log_file);
		g_log_file = NULL;
	}

	return g_max_exit_code;
}

int wmain(const int argc, const wchar_t *const argv[])
{
#ifndef _DEBUG
	__try
	{
		SetErrorMode(SetErrorMode(0x3) | 0x3);
		_set_error_mode(_OUT_TO_STDERR);
		_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
		_set_invalid_parameter_handler(my_invalid_parameter_handler);
		setvbuf(stderr, NULL, _IONBF, 0);
		_setmode(_fileno(stderr), _O_U8TEXT);
		return mparallel_main(argc, argv);
	}
	__except (1)
	{
		fatal_exit(L"\n\nFATAL: Unhandeled exception error!\n\n");
		return FATAL_EXIT_CODE;
	}
#else
	setvbuf(stderr, NULL, _IONBF, 0);
	_setmode(_fileno(stderr), _O_U8TEXT);
	return mparallel_main(argc, argv);
#endif //_DEBUG
}

