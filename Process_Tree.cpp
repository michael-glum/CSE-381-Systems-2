/**
 * Copyright 2021 Michael Glum
 */

#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include "hw4.h"

/** Helper method to load process information into 2 given unordered
    maps.
        
    This method is a helper method that is called once from
    main. This method reads line-by-line of process information
    from a given text file and stores pertinent information into
    2 separate unordered maps. This method loads data into pidPpid
    Map to store pid to ppid association for easy look-up later
    on. This method also loads data into pidCmd Map to store pid
    to command association for easy look-up later on.

    \param[in] is The input stream from where process information is
    to be loaded.
*/
void
ProcTree::loadProcessList(std::istream& is) {
    std::string line, cmd, dummy;
    int pid, ppid;
    // Skip header line
    std::getline(is, line);
    // Process input data
    while (std::getline(is, line)) {
        std::istringstream iss(line);
        // Read only the important data from the input stream
        iss >> dummy >> pid >> ppid >> dummy >> dummy
            >> dummy >> dummy;
        // Store what remains of the current line of input data in CMD,
        // since it can be separated by spaces
        std::getline(iss, cmd);
        // Cut of the leading space that results after using getline
        cmd = cmd.substr(1);
        // Populate two unordered maps with the PID being linked to its PPID in
        // one map, and being linked to the command in another map
        pidPpid[pid] = ppid;
        pidCmd[pid] = cmd;
    }
}

/** Recursive method to print the process hierarchy for a given process.
        
    This method is a recursive method that calls itself to print
    the process hierarchy for a given pid.  This method uses
    information into 2 given unordered_maps to print the process
    information. This method uses information in the 2 unordered
    maps in this class.
        
    \param[in] pid The PID whose information is to be printed.
    NOte that this method recursively calls itself to print the
    parent information.

    \param[in] printHeader Flag to indicate if the header line is
    to be printed prior to printing the process hierarchy.
*/
void
ProcTree::printProcessTree(const int pid, bool printHeader) const {
    // If PID is 0, the entire process tree has been traversed 
    if (pid == 0) {
        return;
    }
    // Only print the header on the first run of this recursive function
    if (printHeader) {
        std::cout << "Process tree for PID: " << pid << 
            "\nPID\tPPID\tCMD\n";
        printHeader = false;
    }
    // Recursively call this function using the given PID's PPID
    printProcessTree(pidPpid.at(pid), printHeader);
    // Print the PID, PPID, and CMD at each level of the process tree
    std::cout << pid << "\t" << pidPpid.at(pid) << "\t"
            << pidCmd.at(pid) << "\n";
}

/** Main method which calls the helper methods of this program
        
    Takes command line arguments specifying the file to be read and
 *  the PID who's process tree will be determined. Additionally, this
 *  method creates and instance of the ProcTree class to call helper
 *  methods to process the data.

    \param[in] argc The integer number of arguments passed
 *  \param[in] *argv[] A list of char pointers to the command line
 *  arguments
*/
int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Specify ProcessListFile and PIDs\n";
        return 1;
    }
    // Create file stream to process data
    std::ifstream is(argv[1]);
    // Create instance of ProcTree class for calling helper methods
    ProcTree pt;
    // Load data from file into an unordered map
    pt.loadProcessList(is);
    // Get the PID from the command line and convert it to an integer
    int pid = atoi(argv[2]);
    // Print the process tree associated with the given PID
    pt.printProcessTree(pid , true);
    return 0;
}
