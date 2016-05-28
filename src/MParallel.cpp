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


//Const
const unsigned int MPARALLEL_VERSION_MAJOR = 1;
const unsigned int MPARALLEL_VERSION_MINOR = 0;

//CRT
#include <string>
#include <sstream>
#include <cstring>
#include <queue>
#include <algorithm>
#include <io.h>
#include <fcntl.h>

//Win32
#define NOMINMAX 1
#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>
#include <Shellapi.h>

//Utils
#define PRINT(...) fwprintf(stderr, __VA_ARGS__);
#define BOUND(MIN,VAL,MAX) std::min(std::max((MIN), (VAL)), (MAX));
#define MATCH(X,Y) (_wcsicmp((X), (Y)) == 0)

//Options
static DWORD        g_option_max_instances;
static bool         g_option_read_stdin_lines;
static bool         g_option_auto_quote_vars;
static std::wstring g_option_separator;
static std::wstring g_option_command_pattern;
static std::wstring g_option_input_file_name;

//Globals
static std::queue<std::wstring> g_queue;
static bool                     g_logo_printed;

// ==========================================================================
// TEXT OUTPUT
// ==========================================================================

#define my_print(...) do \
{ \
	if(!g_logo_printed) \
	{ \
		print_logo(); \
		g_logo_printed = true; \
	} \
	PRINT(__VA_ARGS__); \
} \
while(0)

static void print_logo(void)
{
	PRINT(L"===============================================================================\n");
	PRINT(L"MParallel - Parallel Batch Processor, Version %u.%02u [%S]\n", MPARALLEL_VERSION_MAJOR, MPARALLEL_VERSION_MINOR, __DATE__);
	PRINT(L"Copyright (c) 2016 LoRd_MuldeR <mulder2@gmx.de>. Some rights reserved.\n\n");
	PRINT(L"This program is free software: you can redistribute it and/or modify\n");
	PRINT(L"it under the terms of the GNU General Public License <http://www.gnu.org/>.\n");
	PRINT(L"Note that this program is distributed with ABSOLUTELY NO WARRANTY.\n");
	PRINT(L"=============================================================================== \n\n");
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
	TerminateProcess(GetCurrentProcess(), 666);
}

static void my_invalid_parameter_handler(wchar_t const*, wchar_t const*, wchar_t const*, unsigned int, uintptr_t)
{
	fatal_exit(L"\n\nFATAL: Invalid parameter handler invoked!\n\n");
}

// ==========================================================================
// SYSTEM INFO
// ==========================================================================

static DWORD my_popcount(DWORD number)
{
	number = number - ((number >> 1) & 0x55555555);
	number = (number & 0x33333333) + ((number >> 2) & 0x33333333);
	return (((number + (number >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;
}

static DWORD processor_count(void)
{
	DWORD procMask, sysMask;
	if (GetProcessAffinityMask(GetCurrentProcess(), &procMask, &sysMask))
	{
		const DWORD count = my_popcount(procMask);
		return BOUND(DWORD(1), count, DWORD(MAXIMUM_WAIT_OBJECTS));
	}
	return 1;
}

// ==========================================================================
// STRING SUPPORT ROUTINES
// ==========================================================================

//Parse unsigned integer
static bool parse_uint32(const wchar_t *str, DWORD &value)
{
	if (swscanf_s(str, L"%lu", &value) != 1)
	{
		my_print(L"ERROR: Argument \"%s\" doesn't look like a valid integer!\n\n", str);
		return false;
	}
	return true;
}

//Replace sub-strings
static bool replace_str(std::wstring& str, const std::wstring& needle, const std::wstring& replacement)
{
	bool okay = false;
	for (;;)
	{
		const size_t start_pos = str.find(needle);
		if (start_pos == std::string::npos)
		{
			break;
		}
		str.replace(start_pos, needle.length(), replacement);
		okay = true;
	}
	return okay;
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
static wchar_t *trim_eol(wchar_t *const str)
{
	size_t len = wcslen(str);
	while (len > 0)
	{
		const wchar_t c = str[--len];
		if ((c == L'\r') || (c == L'\n'))
		{
			str[len] = L'\0';
			continue;
		}
		break; /*no mor EOL*/
	}
	return str;
}

// ==========================================================================
// COMMAND-LINE HANDLING
// ==========================================================================

#define REQUIRE_VALUE() do \
{ \
	if ((!value) || (!value[0])) \
	{ \
		my_print(L"ERROR: Argumet for option \"--%s\" is missing!\n\n", option); \
		return false; \
	} \
} \
while(0)

//Parse commands (simple)
static void parse_commands_simple(const int argc, const wchar_t *const argv[], const int offset, const wchar_t *const separator)
{
	int i = offset;
	std::wstringstream command_buffer;
	while (i < argc)
	{
		const wchar_t *const current = argv[i++];
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
	int i = offset, k = 0;
	std::wstring command_buffer = pattern;
	while (i < argc)
	{
		const wchar_t *const current = argv[i++];
		if ((!separator) || wcscmp(current, separator))
		{
			
			std::wstringstream placeholder;
			placeholder << L"{{" << (k++) << L"}}";
			if (g_option_auto_quote_vars && contains_whitespace(current))
			{
				std::wstringstream replacement;
				replacement << L'"' << current << L'"';
				replace_str(command_buffer, placeholder.str(), replacement.str());
			}
			else
			{
				replace_str(command_buffer, placeholder.str(), current);
			}
		}
		else
		{
			if (!command_buffer.empty())
			{
				g_queue.push(command_buffer);
				k = 0;
				command_buffer = pattern;
			}
		}
	}
	if ((!command_buffer.empty()) && (k > 0))
	{
		g_queue.push(command_buffer);
	}
}

//Parse commands
static void parse_commands(int argc, const wchar_t *const argv[], const int offset, const wchar_t *const separator)
{
	if (!g_option_command_pattern.empty())
	{
		parse_commands_pattern(g_option_command_pattern, argc, argv, offset, separator);
	}
	else
	{
		parse_commands_simple(argc, argv, offset, separator);
	}
}

//Parse option
static bool parse_option_string(const wchar_t *const option, const wchar_t *const value)
{
	DWORD temp;

	if (MATCH(option, L"pattern"))
	{
		REQUIRE_VALUE();
		g_option_command_pattern = value;
		return true;
	}
	else if (MATCH(option, L"count"))
	{
		REQUIRE_VALUE();
		if (parse_uint32(value, temp))
		{
			g_option_max_instances = BOUND(DWORD(1), temp, DWORD(MAXIMUM_WAIT_OBJECTS));
			return true;
		}
		return false;
	}
	else if (MATCH(option, L"separator"))
	{
		REQUIRE_VALUE();
		g_option_separator = value;
		return true;
	}
	else if (MATCH(option, L"stdin"))
	{
		g_option_read_stdin_lines = true;
		return true;
	}
	else if (MATCH(option, L"input"))
	{
		REQUIRE_VALUE();
		g_option_input_file_name = value;
		return true;
	}
	else if (MATCH(option, L"auto-quote"))
	{
		g_option_auto_quote_vars = true;
		return true;
	}

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

//Parse arguments
static bool parse_arguments(const int argc, const wchar_t *const argv[])
{
	int i = 1;
	while(i < argc)
	{
		const wchar_t *const current = argv[i++];
		if ((current[0] == L'-') && (current[1] == L'-'))
		{
			if (current[2])
			{
				if (!parse_option_string(&current[2]))
				{
					return false;
				}
			}
			else
			{
				parse_commands(argc, argv, i, g_option_separator.c_str());
				break;
			}
		}
		else
		{
			parse_commands(argc, argv, --i, g_option_separator.c_str());
			break;
		}
	}
	return true;
}

//Read from file stream
static void parse_commands_file(FILE *const input)
{
	wchar_t line_buffer[32768];
	while (fgetws(line_buffer, 32768, input))
	{
		int argc;
		wchar_t *const *const argv = CommandLineToArgvW(trim_eol(line_buffer), &argc);
		if (!argv)
		{
			fatal_exit(L"Exit: CommandLineToArgvW() has failed!\n");
		}
		parse_commands(argc, argv, 0, NULL);
		LocalFree((HLOCAL)argv);
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
// MAIN FUNCTION
// ==========================================================================

static int mparallel_main(const int argc, const wchar_t *const argv[])
{
	//Initialize globals and options
	g_logo_printed = false;
	g_option_read_stdin_lines = false;
	g_option_auto_quote_vars = false;
	g_option_separator = L":";
	g_option_max_instances = processor_count();
	
	//Parse CLI arguments
	if (!parse_arguments(argc, argv))
	{
		my_print(L"Failed to parse command-line arguments. Run with option \"--help\" for guidance!\n\n");
		return EXIT_FAILURE;
	}

	//Parse jobs from file
	if (!g_option_input_file_name.empty())
	{
		if (!parse_commands_file(g_option_input_file_name.c_str()))
		{
			my_print(L"Failed to read commands from specified input file!\n\n");
			return EXIT_FAILURE;
		}
	}

	//Parse jobs from STDIN
	if (g_option_read_stdin_lines)
	{
		parse_commands_file(stdin);
	}

	if (g_queue.size() < 1)
	{
		my_print(L"Nothing to do. Run with option \"--help\" for guidance!\n\n");
		return EXIT_FAILURE;
	}

	my_print(L"COUNT: %u\n", g_option_max_instances);
	while (!g_queue.empty())
	{
		my_print(L"JOB:\n%s\n\n", g_queue.front().c_str());
		g_queue.pop();
	}

	return EXIT_SUCCESS;
}

int wmain(const int argc, const wchar_t *const argv[])
{
	SetErrorMode(SetErrorMode(0x3) | 0x3);
	__try
	{
		_set_invalid_parameter_handler(my_invalid_parameter_handler);
		int filenos[] = { _fileno(stderr), _fileno(stdout), _fileno(stdin), -1 };
		for (int i = 0; filenos[i] >= 0; i++) _setmode(filenos[i], _O_U8TEXT);
		return mparallel_main(argc, argv);
	}
	__except (1)
	{
		fatal_exit(L"\n\nFATAL: Unhandeled exception error!\n\n");
	}
}

