# Mini Shell

## Description
This is a shell program that allows users to execute various commands via a command-line interface. It is designed to support basic shell functionality with added features for enhanced usability.

## Features
* Executes basic commands such as `ls`, `pwd`, and `echo`.
* Supports multiple commands separated by `;`.
* Supports environment variables.
* Counts how many valid commands and arguments have been executed so far.

## Additional Features
* Enables unlimited piped commands.
* Supports redirection - writing output to a file.
* Supports running processes in the background.

## Signals
* **Ctrl+Z**: Stops the currently running command (if one exists). To resume the stopped process, enter `bg`.

## Limitations
* Does not support `cd`.
* May have some bugs or unexpected behavior.

## How to Compile
```bash
gcc ex1.c -o ex1
```
## How to Run
```bash
./ex1
```
## Input
Linux shell commands.

## Output
If the input is valid and the command has output, it will be printed to the terminal.

## Exiting the Program
The shell program will free memory and exit under the following conditions:
1. The user presses enter 3 times consecutively.
2. A system error occurs.

