/* 
* Copyright 2021 Michael Glum
* 
 * A custom shell that uses fork() and execvp() for running commands
 * in serially or in parallel.
 * 
 */

#include <unistd.h>
#include <sys/wait.h>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <iomanip>
#include "ChildProcess.h"

// A vector of strings to ease running programs with command-line
// arguments.
using StrVec = std::vector<std::string>;

// A vector of integers to hold child process ID's when operating in
// parallel mode.
using IntVec = std::vector<int>;

// Header for processCmds method implemented below.
void processCmds(std::istream& is, std::ostream& os, bool parMode,
    const std::string prompt);

/** Convenience method to split a given line into individual words.

    \param[in] line The line to be split into individual words.

    \return A vector strings containing the list of words.
 */
StrVec split(const std::string& line) {
    StrVec words;  // The list to be created
    // Use a string stream to read individual words 
    std::istringstream is(line);
    std::string word;
    while (is >> std::quoted(word)) {
        words.push_back(word);
    }
    return words;
}

/**
    This helper method waits for a child process to terminate and prints its
    exit code.
    \param[in] pid The pid of the child process.
    \param[in] os The output stream to be printed to.
*/
void waitAndPrint(const int pid, std::ostream& os) {
    int exitCode = 0;
    waitpid(pid, &exitCode, 0);
    os << "Exit code: " << exitCode << std::endl;
}

/**
    This helper method checks if the first word of the input is one of three
    special cases: "exit" (resulting in termination of the program), "SERIAL"
    (indicating that the commands should be processed serially from a file), 
    or "PARALLEL" (indicating that the commands should be processed from a file
    in parallel). In the case of the first word being "SERIAL" or "PARALLEL"
    the current instance of the processCmds method is terminated, and
    the method is called again with an input stream from a file denoted by
    the second word.
    \param[in] words The vector of strings containing each command line
    argument.
    \param[in] os The output stream to be passed back to processCmds

    \return This method returns true when there is a special first word,
    causing the current call of the processCmds method to be terminated.
*/
bool specialFirstWord(const StrVec& words, std::ostream& os) {
    bool b = false;
    if (words[0] == "exit") {
        // Return true, thereby terminating the program.
        b = true;
    } else if (words[0] == "SERIAL") {
        // Call a new instance of processCmds to process input from the
        // given file serially. Pass an empty string to the prompt parameter
        // so that it does not appear that the file input is being prompted.
        // Terminate the old instance of processCmds.
        std::ifstream in(words[1]);
        processCmds(in, os, false, "");
        b = true;
    } else if (words[0] == "PARALLEL") {
        // Call a new instance of processCmds to process input from the
        // given file in parallel. Pass an empty string to the prompt parameter
        // so that it does not appear that the file input is being prompted.
        // Terminate the old instance of processCmds.
        std::ifstream in(words[1]);
        processCmds(in, os, true, "");
        b = true;
    }
    return b;
}

/**
    This helper method calls forkNexec to create child processes and execute
    each of the commands as they are passed to this method line by line. It
    also prints output showing the running status of the commands, as
    well as the results of the commands when processed serially. 
    \param[in] words A vector of strings containing a command and
    its associated arguments.
    \param[in] pids A reference to a vector of integer for pids to be stored
    in when the commands are processed in parallel.
    \param[in] os The output stream for the results to be sent to
    \param[in] parmode Boolean indicating whether the commands should be
    processed in parallel.
*/
void runCmds(StrVec words, IntVec& pids, std::ostream& os, bool parMode) {
    // Print and format the command and arguments being run
    os << "Running: ";
    for (size_t i = 0; i < words.size() - 1; i++) {
        os << words[i] << " ";
    }
    os << words[words.size() - 1] << std::endl;
    // Create a ChildProcess object to be used to call forkNexec
    ChildProcess cp;
    // Call forkNexec to execute the command within a child process
    // and store its pid.
    int pid = cp.forkNexec(words);
    // If the commands are to be processed in parallel, add the pid
    // to the vector of pids to be used within processCmds.
    // Otherwise, call a helper method to call waitpid and print the results.
    if (parMode == true) {
        pids.push_back(pid);
    } else {
        waitAndPrint(pid, os);
    }
}

/**
    This method prompts the user for input, processes that input
    to ignore comments, then uses helper methods to execute commands
    written directly in the command line or read through a file
    for serial or parallel processing. It then outputs formatted results of
    the commands.
    \param[in] is The input stream to be processed (command line or file)
    \param[in] os The output stream for the results to be sent to
    \param[in] parMode Boolean indicating whether the commands should be
    processed in parallel.
    \param[in] prompt Either the text used to prompt the user for input, or
    an emptry string when reading lines from a file.
*/
void processCmds(std::istream& is, std::ostream& os, bool parMode,
    const std::string prompt) {
    std::string line;
    IntVec pids;
    // Prompt user and read input.
    while (os << prompt, std::getline(is, line)) {
        // Ignore empty or commented lines
        if (line.length() != 0 && line[0] != '#') {
            // Use split helper method to break the input line into a vector of
            // strings
            StrVec words = split(line);
            // Use helper method to check for exit command or serial vs
            // parallel option. If any of these are found, exit the while loop,
            //  as either no more data will be read or the method will be 
            // called again with a file passed as the input stream.
            if (specialFirstWord(words, os)) {
                break;
            }
            // Helper method to execute the commands
            runCmds(words, pids, os, parMode);
        }
    }
    // In the case that the commands are processed in parallel, the IntVec of
    // pids will be populated. Use a helper method to call waitpid for each
    // of the child processes and print the exit code.
    for (size_t i = 0; i < pids.size(); i++) {
        waitAndPrint(pids[i], os);
    }
    // After input has been read from a file, return to prompting the user
    // as normal.
    if (prompt == "") {
        os << "> ";
    }
}


/**
    Simple main method to call the processCmds method which operates the shell.
*/
int main() {
    processCmds(std::cin, std::cout, false, "> ");
}

