% MParallel
% Created by LoRd_MuldeR &lt;<mulder2@gmx>&gt; &ndash; check <http://muldersoft.com/> for news and updates!


# Introduction #

**MParallel** is a batch processor with *multi-threading* support, i.e. it will run multiple tasks *in parallel*. This can be very useful, not only, to take full advantage of multi-processor (multi-core) machines.


# Synopsis #

**MParallel** can be invoked in three different forms:

    MParallel.exe [options] <command_1> : <command_2> : ... : <command_n>
    MParallel.exe [options] --input=commands.txt
    GenerateCommands.exe [parameters] | MParallel.exe [options] --stdin

The *first* form takes commands directly from the command-line, delimited by colon (`:`) characters. The *second* from reads commands from a text file, line by line. And the *third* form reads commands from the standard input stream (stdin), which is typically used to process the output from another program using a pipe (`|`). The three forms can be combined freely.


# Examples

The best way to get started with **MParallel** is looking at some examples:

## Example 1

This basic example uses MParallel to run *multiple* "ping" commands *in parallel*:

    MParallel.exe --count=3 ping.exe -n 16 fsf.org : ping.exe -n 16 gnu.org : ping.exe -n 16 w3c.org

Note how the distinct commands and their related arguments are delimited by colon (`:`) characters. Also note how the  command-line options specific to MParallel have to go *before* the very first command string.

## Example 2

A slightly more advanced example, using a *command pattern* to express the above command-line more elegantly:

    MParallel.exe --count=3--pattern="ping.exe -n 16 {{0}}" fsf.org : gnu.org : w3c.org

Note that, in this example, the `{{0}}` placeholder will be replaced with the corresponding command tokens.

## Example 3

It is also possible to read your commands (or the parameters for your command pattern) from a text file:

    MParallel.exe --count=3 --input=my_commands.txt

The content of the file "my_commands.txt" may look like this, for example:

    ping.exe -n 16 fsf.org
    ping.exe -n 16 gnu.org
    ping.exe -n 16 w3c.org

## Example 4

Now let's read the output of the "dir" command to *copy* all "&ast;.jpg" files in the current directory to "&ast;.png":

    dir /b *.jpg | MParallel.exe --shell --stdin ---pattern="copy {{0}} {{0:N}}.png"

Note that here we need to use the `--shell` option, because `copy` is a built-in shell function. Also note that we would need to add `--no-split-lines` and `--auto-wrap` in order to correctly handle file names containing spaces!


# Options

The following **MParallel** options are currently available:

## `--count=<N>`

Run at most **N** instances in parallel. MParallel will start **N** commands in parallel, provided that there are at least **N** commands in the queue. If there are *less* than **N** commands in the queue, it will start as many commands in parallel as there are in the queue. If there are *more* than **N** commands in the queue, MParallel will start the first **N** commands in parallel and, at each time that any of the running commands completes, it will start the next command. This way, always **N** commands will be running in parallel, unless the queue is running empty. Note that **N** defaults to the number of available processors (CPU cores), if *not* specified explicitly &ndash; taking into account the processor affinity mask.

## `--pattern=<PATTERN>`

Generate commands from the specified **PATTERN** string. If a **PATTERN** string has been specified, the commands passed to MParallel on the command-line, read from a file or read from the STDIN stream will *not* be executed "as-is". Instead, any given command-tokens will then be interpreted as input *parameters* for transforming the given **PATTERN** string.

The **PATTERN** string should contain placeholders in the `{{k}}` form. Placeholders of that from will be replaced by the **k**-th command-token. Note that the placeholder indices **k** are *zero-based*, i.e.use `{{0}}`, `{{1}}`, `{{2}}` and so on. In addition, if the **k**-th command-token represents a valid file path, the placeholders `{{k:F}}`, `{{k:D}}`, `{{k:P}}`, `{{k:N}}` and `{{k:X}}` will be replaced with the file's *full path* (expanded relative to working directory), *drive letter* (with trailing colon), *directory name* (with trailing backslash), *file name* (without extension) and *extension* (including dot), respectively.

Note that if the **PATTERN** string contains any whitespace characters, the **PATTERN** string as a whole needs to be wrapped in quotation marks (e.g.`--pattern="foo bar")`. Also note that any quotation marks *inside* the **PATTERN** string need to be escaped by a `\"` sequence (e.g. `--pattern="foo \"{{0}}\""`). However, using the `--auto-wrap` option can simplify building **PATTERN** strings. Finally note that any *excess* command-tokens will be discarded by MParallel!

## `--separator=<SEP>`

Set the command separator to **SEP**. The separator string is used to delimit the distinct commands, when they are passed to MParallel on the command-line. By default, a single colon character (`:`) is used as separator, but any suitable character sequence may be specified here. Note that **SEP** is *not* used for reading commands from a file or from the STDIN. When reading from a file or from the STDIN, there must be one command per line.

## `--input=<FILE>`

Read additional commands from the specified **FILE**. The specified **FILE** needs to be a plain text file, containing one command per line. Each *line* is interpreted like a full command-line. Using separators *within* a line is **not** required or supported, because commands are delimited by line breaks. If the file contains any characters other than plain US-ASCII, it is expected to be in *UTF-8* text encoding by default. Use the option `--utf16` in order to force UTF-16 encoding.

## `--stdin`

Read additional commands from the **STDIN** stream. The data on **STDIN** needs to be plain text, containing one command per line. Each *line* is interpreted like a full command-line. Using separators *within* a line is **not** required or supported, because commands are delimited by line breaks. If the stream contains any characters other than plain US-ASCII, it is expected to be in *UTF-8* text encoding by default. Use the option `--utf16` in order to force UTF-16 encoding.

This option is typically used to process lines produced by other programs or by shell functions. In the shell (cmd.exe) the **STDOUT** of another program can be "connected" to the **STDIN** of MParallel using a pipe operator (`|`). Note, however, that shell functions like `dir` may **not** output *UTF-8* by default. Set the shell to *UTF-8* mode (`chcp 65001`) in advance!

## `--logfile=<FILE>`

Save logfile to **FILE**. The logfile contains information about all processes that have been created an the result. By default, *no* logfile will be created. If the logfile already exists, MParallel *appends* to the existing file.

## `--out-path=<PATH>`

Redirect the STDOUT and STDERR of each sub-process to a file. MParallel will create a separate output file for each process in the **PATH** directory. File names are generated according to the `YYYYMMDD-HHMMSS-NNNNN.log` pattern. Note that directory **PATH** must be existing and writable. Also note that redirected outputs do *not* appear in the console!

## `--auto-wrap`

Automatically wrap all tokens that contain any whitespace characters in quotation marks. This applies to the expansion of placeholders, when the `--pattern` option is used. For example, if the **N**-th command token contains `foo bar`, then `{{N}}` will be replaced by `"foo bar"` instead of `foo bar`. This option has *no* effect, if `--pattern` is *not* used.

## `--no-split-lines`

Ignore whitespace characters when reading commands from a file. By default, when MParallel reads commands from a file or from the STDIN stream, each input line will be processed like a full command-line. This means that tokens within each line are *whitespace-delimited*, unless wrapped in quotation marks. If this option is set, *no* command-line splitting is performed on the input lines. Instead, each input line will be treated like *one* unbroken string.

## `--shell`

Start each command inside a new sub-shell (cmd.exe). Running each command in a new sub-shell implies a certain overhead, which is why this behavior is *disabled* by default. However, you *must* use this option, if your command uses any *built-in* shell functions, such as `echo`, `dir` or `copy`. You also  *must* use this option, if your command contains any shell operators, such as the pipe operator (`|`) or one of the redirection operators (`>`, `<`, etc).

## `--timeout=<TIMEOUT>`

  Kill processes after **TIMEOUT** milliseconds. By default, each command is allowed to run for an infinite amount of time. If this option is set, a command will be *aborted* if it takes longer than the specified timeout interval. Note that (by default) if a command was aborted due to timeout, other pending commands will still get a chance to run.

## `--priority=<VALUE>`

Run the commands (sub-processes) with the specified process priority. This can be one of the following values:

- **0**: Lowest priority
- **1**: Lower than normal priority
- **2**: Default priority
- **3**: Higher than normal priority
- **4**: Highest priority

## `--detached`

Run each sub-process in a *separate* console window. By default, all sub-processes are connected to the *same* console window as the main MParallel process. Thus, output from all processes will appear in the same console window, in an "interleaved" fashion. With this option set, each sub-process gets a separate console window.

## `--abort`

Abort batch, if any command failed to execute. By default, if any command failed, e.g. because the process could *not* be created or because it returned a *non-zero* exit code, other pending commands will still get a chance to run. If this option is set, the whole batch (queue) will be aborted, as soon as one command has failed.

## `--no-jobctrl`

Do *not* add new sub-processes to job object. By default, MParallel adds all new sub-processes to a *Job Object*, which makes sure that all sub-processes will die immediately when the MParallel process is terminated. If this option is set, sub-processes are *not* added to the Job Object and may continue running after the MParallel was terminated.

## `--ignore-exitcode`

Do *not* check the exit code of sub-processes. By default, MParallel checks the exit code of each sub-process. It assumes that the command has *failed*, if the process returned a *non-zero* exit code. If any command failed, this will be reported and, if `--abort` is set, any pending commands will *not* be executed. Setting this option causes MParallel to *ignore* exit codes. However, a command is still considered to have failed, if the processes could *not* be created.

## `--utf16`

Interpret the lines read from a file or from the STDIN stream with *UTF-16* text encoding. By default, MParallel will interpret the lines read from a file or from the STDIN with *UTF-8* text encoding. In most cases *UTF-8* is what you want.

## `--silent`

Disable all textual messages, also known as "silent mode". Note that *fatal* error messages may still appear under some circumstances. Also note that this option is mutually exclusive with the `--trace` option.

## `--no-colors`

Disables colored textual output to the console. By default, when MParallel is writing textual messages to the console window, it will apply the appropriate colors to them. Colors are always disabled when redirecting STDERR to a file.

## `--trace`

Enable more diagnostic outputs. This will print, e.g., the full command-line and the exit code for each task, which can be helpful for debugging purposes. Note that this option is mutually exclusive with the `--silent` option.

## `--help`

Print the help screen, also known as "manpage".


# Exit Code

**MParallel** returns `max(exitcode_1, exitcode_2, ..., exitcode_N)` as its exit code, where **exitcode_i** is the exit code that was returned by **i**-th sub-process. In general, a *zero* exit code indicates that all commands completed successfully, while a *non-zero* exit code indicates that at least one command has failed. Fatal errors are indicated by a **666** exit code.


# Source Code

**MParallel** source code is available from the official [**Git**](https://git-scm.com/) mirrors at:

* <https://github.com/lordmulder/MParallel.git>

* <https://bitbucket.org/lord_mulder/mparallel.git>

* <https://git.assembla.com/mparallel.git>


# License Terms #


**MParallel** is released under the *GNU General Public License*, Version 2.

    MParallel - Parallel Batch Processor
    Copyright (C) 2016 LoRd_MuldeR <mulder2@gmx.de>. Some rights reserved.
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

**<http://www.gnu.org/licenses/gpl-2.0.html>**


# Acknowledgement

Using **Application Lightning Icon**, created by *Fatcow Web Hosting*  
License: CC Attribution 4.0 license  
(Backlink to <http://www.fatcow.com/free-icons> required)

&nbsp;  

e.o.f.
