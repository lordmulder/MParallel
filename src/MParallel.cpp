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

//Win32
#define NOMINMAX 1
#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>
#include <Shellapi.h>

//Utils
#define PRINT(...) fwprintf(stderr, __VA_ARGS__);
#define BOUND(MIN,VAL,MAX) std::min(std::max((MIN), (VAL)), (MAX));
#define MATCH(X,Y) (_wcsicmp((X), (Y)) == 0)

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
	static bool         ignore_exitcode;
	static bool         detached_console;
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
static DWORD   g_processes_completed;
static HANDLE  g_job_object;
static FILE*   g_log_file;
static HANDLE  g_interrupt_event;

// ==========================================================================
// WIN32 SUPPORT
// ==========================================================================

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

static DWORD my_popcount(DWORD64 number)
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

static DWORD processor_count(void)
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

#define my_print(...) do \
{ \
	if(!options::disable_outputs) \
	{ \
		if(!g_logo_printed) \
		{ \
			print_logo(); \
			g_logo_printed = true; \
		} \
		PRINT(__VA_ARGS__); \
	} \
} \
while(0)

#define puts_log(FMT, ...) do \
{ \
	if(options::enable_tracing) \
	{ \
		PRINT(L"[TRACE] " FMT, __VA_ARGS__); \
	} \
	if (g_log_file) \
	{ \
		wchar_t time_buffer[32]; \
		if (get_current_time(time_buffer, 32, false)) \
		{ \
			fwprintf(g_log_file, L"[%s] " FMT, time_buffer, __VA_ARGS__); \
		} \
	} \
} \
while(0)

static void print_logo(void)
{
	PRINT(L"===============================================================================\n");
	PRINT(L"MParallel - Parallel Batch Processor, Version %u.%u.%u [%S]\n", MPARALLEL_VERSION_MAJOR, MPARALLEL_VERSION_MINOR, MPARALLEL_VERSION_PATCH, __DATE__);
	PRINT(L"Copyright (c) 2016 LoRd_MuldeR <mulder2@gmx.de>. Some rights reserved.\n\n");
	PRINT(L"This program is free software: you can redistribute it and/or modify\n");
	PRINT(L"it under the terms of the GNU General Public License <http://www.gnu.org/>.\n");
	PRINT(L"Note that this program is distributed with ABSOLUTELY NO WARRANTY.\n");
	PRINT(L"=============================================================================== \n\n");
}

static void print_manpage(void)
{
	my_print(L"Synopsis:\n");
	my_print(L"  MParallel.exe [options] <command_1> : <command_2> : ... : <command_n>\n");
	my_print(L"  MParallel.exe [options] --input=commands.txt\n");
	my_print(L"  GenerateCommands.exe [parameters] | MParallel.exe [options] --stdin\n\n");
	my_print(L"Options:\n");
	my_print(L"  --count=<N>          Run at most N instances in parallel (Default is %u)\n", processor_count());
	my_print(L"  --pattern=<PATTERN>  Generate commands from the specified PATTERN\n");
	my_print(L"  --separator=<SEP>    Set the command separator to SEP (Default is '%s')\n", DEFAULT_SEP);
	my_print(L"  --input=<FILE>       Read additional commands from specified FILE\n");
	my_print(L"  --stdin              Read additional commands from STDIN stream\n");
	my_print(L"  --logfile=<FILE>     Save logfile to FILE, appends if the file exists\n");
	my_print(L"  --out-path=<PATH>    Redirect the stdout/stderr of sub-processes to PATH\n");
	my_print(L"  --auto-wrap          Automatically wrap tokens in quotation marks\n");
	my_print(L"  --no-split-lines     Ignore whitespaces when reading commands from file\n");
	my_print(L"  --shell              Start each command inside a new sub-shell (cmd.exe)\n");
	my_print(L"  --timeout=<TIMEOUT>  Kill processes after TIMEOUT milliseconds\n");
	my_print(L"  --priority=<VALUE>   Run commands with the specified process priority\n");
	my_print(L"  --ignore-exitcode    Do NOT check the exit code of sub-processes\n");
	my_print(L"  --detached           Run each sub-process in a separate console window\n");
	my_print(L"  --abort              Abort batch, if any command failed to execute\n");
	my_print(L"  --no-jobctrl         Do NOT add new sub-processes to job object\n");
	my_print(L"  --silent             Disable all textual messages, aka \"silent mode\"\n");
	my_print(L"  --trace              Enable more diagnostic outputs (for debugging only)\n");
	my_print(L"  --help               Print this help screen\n");
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
		if (_wfopen_s(&g_log_file, file_name, L"a") == 0)
		{
			_setmode(_fileno(g_log_file), _O_U8TEXT);
			_fseeki64(g_log_file, 0, SEEK_END);
			if (_ftelli64(g_log_file) > 0)
			{
				fwprintf(g_log_file, L"---------------------\n");
			}
		}
		else
		{
			g_log_file = NULL;
			my_print(L"ERROR: Failed to open log file \"%s\" for writing!\n\n", options::log_file_name.c_str());
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
		my_print(L"ERROR: Argument \"%s\" doesn't look like a valid integer!\n\n", str);
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
		puts_log("Process token: %s\n", current);
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
		puts_log("Process token: %s\n", current);
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
				my_print(L"WARNING: Discarding token \"%s\", due to missing {{%u}} placeholder!\n\n", current, var_idx);
			}
			var_idx++;
		}
		else
		{
			if (!command_buffer.empty())
			{
				var_idx = 0;
				g_queue.push(command_buffer);
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
		my_print(L"ERROR: Argumet for option \"--%s\" is missing!\n\n", option); \
		return false; \
	} \
} \
while(0)

#define REQUIRE_NO_VALUE() do \
{ \
	if (value && value[0]) \
	{ \
		options::disable_outputs = false; \
		my_print(L"ERROR: Excess argumet for option \"--%s\" encountred!\n\n", option); \
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
	options::ignore_exitcode = false;
	options::detached_console = false;
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
	else if (MATCH(option, L"help"))
	{
		REQUIRE_NO_VALUE();
		options::print_manpage = true;
		return true;
	}

	options::disable_outputs = false;
	my_print(L"ERROR: Unknown option \"--%s\" encountred!\n\n", option);
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
		my_print(L"Error: Options \"--trace\" and \"--silent\" are mutually exclusive!\n\n");
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
				my_print(L"Error: Specified output directory \"%s\" does NOT exist!\n\n", options::redir_path_name.c_str());
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
			puts_log("Process token: %s\n", current);
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
	while (fgetws(line_buffer, 32768, input))
	{
		int argc;
		const wchar_t *const trimmed = trim_str(line_buffer);
		if (trimmed && trimmed[0])
		{
			puts_log("Read line: %s\n", trimmed);
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
	if (_wfopen_s(&file, file_name, L"r") == 0)
	{
		_setmode(_fileno(file), _O_U8TEXT);
		parse_commands_file(file);
		fclose(file);
		return true;
	}
	my_print(L"ERROR: Unbale to open file \"%s\" for reading!\n\n", file_name);
	return false;
}

// ==========================================================================
// PROCESS FUNCTIONS
// ==========================================================================

//Print Win32 error message
static void print_win32_error(const wchar_t *const format, const DWORD error)
{
	wchar_t buffer[1024];
	if (FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buffer, 1024, NULL) > 0)
	{
		my_print(format, trim_str(buffer));
	}
}

//Terminate all running processes
static void terminate_processes(void)
{
	for (DWORD i = 0; i < options::max_instances; i++)
	{
		if (g_isrunning[i])
		{
			g_process_count--;
			TerminateProcess(g_processes[i], 666);
			CLOSE_HANDLE(g_processes[i]);
			g_isrunning[i] = false;
			g_processes[i] = NULL;
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
	my_print(L"WARNING: Unknown priority value %u specified!", priority);
	return 0;
}

//Start the next process
static HANDLE start_next_process(std::wstring command)
{
	static const DWORD defaultFlags = CREATE_BREAKAWAY_FROM_JOB | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT;

	if (options::force_use_shell)
	{
		std::wstringstream builder;
		builder << L"cmd.exe /c \"" << command << L"\"";
		command = builder.str();
	}

	puts_log(L"Starting process: %s\n", command.c_str());

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

	DWORD flags = defaultFlags | get_priority_class(options::process_priority);
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
				my_print(L"WARNING: Failed to assign process to job object!\n\n");
			}
		}
		ResumeThread(process_info.hThread);
		CLOSE_HANDLE(redir_file);
		CLOSE_HANDLE(process_info.hThread);
		g_processes_completed++;
		puts_log(L"Process 0x%X has been started.\n", process_info.dwProcessId);
		return process_info.hProcess;
	}

	const DWORD error = GetLastError();
	puts_log(L"CreateProcessW() failed with Win32 error code: 0x%X.\n", error);
	print_win32_error(L"\nProcess creation has failed: %s\n", error);
	my_print(L"ERROR: Process ``%s´´could not be created!\n\n", command.c_str());
	CLOSE_HANDLE(redir_file);
	return NULL;
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

	if (count > 0)
	{
		const DWORD num_handels = g_interrupt_event ? (count + 1) : count;
		const DWORD ret = WaitForMultipleObjects(num_handels, &handles[0], FALSE, (options::process_timeout > 0) ? options::process_timeout : INFINITE);
		if ((ret >= WAIT_OBJECT_0) && (ret < WAIT_OBJECT_0 + count))
		{
			return index[ret - WAIT_OBJECT_0];
		}
		if (ret == WAIT_OBJECT_0 + count)
		{
			interrupted = true;
			return MAXDWORD;
		}
		if ((ret == WAIT_TIMEOUT) && (options::process_timeout > 0))
		{
			timeout = true;
			puts_log(L"WaitForMultipleObjects() failed with WAIT_TIMEOUT error! (dwMilliseconds = %u)\n", options::process_timeout);
			return MAXDWORD;
		}
	}

	puts_log(L"WaitForMultipleObjects() failed with Win32 error code: 0x%X.\n", GetLastError());
	return MAXDWORD;
}

//Run processes
static int run_processes(void)
{
	DWORD slot = 0, exit_code = 0;
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
					exit_code = std::max(exit_code, DWORD(1));
					interrupted = aborted = true;
					my_print(L"\nERROR: Interrupted by user, exiting!\n\n");
					break;
				}
			}
			const std::wstring next_command = g_queue.front();
			g_queue.pop();
			if (const HANDLE process = start_next_process(next_command))
			{
				g_process_count++;
				while (g_isrunning[slot])
				{
					slot = (slot + 1) % options::max_instances;
				}
				g_processes[slot] = process;
				g_isrunning[slot] = true;
			}
			else
			{
				if (options::abort_on_failure)
				{
					exit_code = std::max(exit_code, DWORD(1));
					aborted = true;
					my_print(L"\nERROR: Process creation failed, aborting!\n\n");
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
				g_process_count--;
				DWORD temp;
				if (GetExitCodeProcess(g_processes[index], &temp))
				{
					exit_code = std::max(exit_code, temp);
					puts_log(L"Process 0x%X terminated with error code 0x%X.\n", GetProcessId(g_processes[index]), temp);
					if ((temp != 0) && (!options::ignore_exitcode))
					{
						if (options::abort_on_failure)
						{
							aborted = true;
							my_print(L"\nERROR: Command failed, aborting! (ExitCode: %u)\n\n", temp);
							break;
						}
						my_print(L"\nERROR: The command has failed! (ExitCode: %u)\n\n", temp);
					}
				}
				else
				{
					puts_log(L"Exit code for process 0x%X could not be determined.\n", GetProcessId(g_processes[index]));
				}
				CLOSE_HANDLE(g_processes[index]);
				g_processes[index] = NULL;
				g_isrunning[index] = false;
			}
			else
			{
				if (timeout)
				{
					my_print(L"\nERROR: Timeout encountered, terminating running process!\n\n");
					terminate_processes();
					if (options::abort_on_failure)
					{
						aborted = true;
						break;
					}
				}
				else if (interrupted)
				{
					exit_code = std::max(exit_code, DWORD(1));
					aborted = true;
					my_print(L"\nERROR: Interrupted by user, exiting!\n\n");
					break;
				}
				else
				{
					exit_code = std::max(exit_code, DWORD(1));
					aborted = true;
					my_print(L"\nERROR: Failed to wait for running process!\n\n");
					break;
				}
			}
		}
	}

	//Wait for the pending processes
	while ((g_process_count > 0) && (!interrupted))
	{
		bool timeout = false;
		const DWORD index = wait_for_process(timeout, interrupted);
		if (index != MAXDWORD)
		{
			g_process_count--;
			CLOSE_HANDLE(g_processes[index]);
			g_processes[index] = NULL;
			g_isrunning[index] = false;
		}
		else
		{
			my_print(L"ERROR: Failed to wait for running process!\n");
			terminate_processes();
			break;
		}
	}

	//Terminate all processes still running at this point
	terminate_processes();
	return exit_code;
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
	g_processes_completed = 0;
	g_process_count = 0;

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
		my_print(L"Failed to parse command-line arguments. Run with option \"--help\" for guidance!\n\n");
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
			my_print(L"Failed to read commands from specified input file!\n\n");
			return FATAL_EXIT_CODE;
		}
	}

	//Parse jobs from STDIN
	if (options::read_stdin_lines)
	{
		parse_commands_file(stdin);
	}

	//Valid queue?
	if (g_queue.size() < 1)
	{
		my_print(L"Nothing to do. Run with option \"--help\" for guidance!\n\n");
		return FATAL_EXIT_CODE;
	}

	//No more logo after this point
	g_logo_printed = true;

	//Logging
	puts_log(L"Commands in queue: %zu\n", g_queue.size());
	puts_log(L"Maximum parallel instances: %u\n", options::max_instances);

	//Create job object
	if (!options::disable_jobctrl)
	{
		g_job_object = create_job_object();
		if (!g_job_object)
		{
			my_print(L"WARNING: Failed to create the job object!\n\n");
		}
	}

	//Run processes
	const clock_t timestamp_enter = clock();
	const int retval = run_processes();
	const clock_t timestamp_leave = clock();

	//Close the job object
	if (g_job_object)
	{
		TerminateJobObject(g_job_object, FATAL_EXIT_CODE);
		CLOSE_HANDLE(g_job_object);
	}

	//Logging
	const double total_time = double(timestamp_leave - timestamp_enter) / double(CLOCKS_PER_SEC);
	my_print(L"\n--------\n\nExecuted %u tasks in %.2f seconds.\n\n", g_processes_completed, total_time);

	//Close the log file
	if (g_log_file)
	{
		fclose(g_log_file);
		g_log_file = NULL;
	}

	return retval;
}

int wmain(const int argc, const wchar_t *const argv[])
{
	SetErrorMode(SetErrorMode(0x3) | 0x3);
	__try
	{
		_set_error_mode(_OUT_TO_STDERR);
		_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
		_set_invalid_parameter_handler(my_invalid_parameter_handler);
		setvbuf(stderr, NULL, _IONBF, 0);
		FILE *file[3] = { stdin, stdout, stderr };
		for (size_t i = 0; i < 3; i++)
		{
			_setmode(_fileno(file[i]), _O_U8TEXT);
		}
		return mparallel_main(argc, argv);
	}
	__except (1)
	{
		fatal_exit(L"\n\nFATAL: Unhandeled exception error!\n\n");
		return FATAL_EXIT_CODE;
	}
}

