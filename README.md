# Smallsh
Description: Creation of a small shell program for running commands from the command line.  The program does the following:
  1. Reads input from the command line OR from a file if a valid CMD line argument file is provided.
  2. First word in the command is a built-in executable command (cd or exit) or a linux executable program.
  3. Enables input/output redirection -
  3a.  Uses < + input file to read input from a file
  3b.  Uses > + write file to write (replace) output to a file
  3c.  Uses >> + append file to append output to a file
  4. If the last word in the command is the & symbol, it will run the process in the background.
  5. If no background, symbol, it will perform a blocking wait on the execution of the foreground process.
  6. Will monitor the status of all processes and provide outputs for their pid's and exit statuses.
  7. Provides the following signal handling:
  7a.  Will ignore ALL SIGTSTP signals
  7b.  Will ignore ALL SIGINT signals except when reading commands from the command line
  7c.  Will reset all signals in each child process
 
  The wordsplit, param_scan, build_str, and expand methods were provided by the professor.  It would have been easier to build my own methods, but wanted to demonstrate the ability to read and understand existing code and build functionality on top of someone elses code.

