/**
 * Copyright 2021 Michael Glum
 * A program to use multiple threads to count words from data obtained
 * via a given set of URLs.
 */

#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <iterator>
#include <cctype>
#include <algorithm>
#include <thread>
#include <unordered_map>

// Using namespace to streamline working with Boost socket.
using namespace boost::asio;
using namespace boost::system;

// Shortcut an unordered_map of words. 
using Dictionary = std::unordered_map<std::string, bool>;

// Forward declaration for method. 
Dictionary loadDictionary(const std::string& filePath);


// The global dictionary of valid words 
const Dictionary dictionary = loadDictionary("english.txt");

Dictionary loadDictionary(const std::string& filePath) {
    Dictionary dictionary;
    std::ifstream ifs(filePath);
    std::string line;
    while (std::getline(ifs, line)) {
        dictionary[line] = true;
    }
    return dictionary;
}

void processData(std::istream& is, std::string& result) {
    std::string line, word;
    int wordCount = 0, englishWordCount = 0;
    while (std::getline(is, line)) {
        // Remove punctuations in a line
        std::replace_if(line.begin(), line.end(), ispunct, ' ');
        // Convert the word to lower case to check against the dictionary. 
        std::transform(line.begin(), line.end(), line.begin(), tolower);
        std::istringstream iss(line);
        while (iss >> word) {
            if (dictionary.find(word) != dictionary.end()) {
                englishWordCount++;
            }
            wordCount++;
        }
    }
    result += ": words=" + std::to_string(wordCount)
        + ", English words=" + std::to_string(englishWordCount);
}

/**
 * Helper method to break down a URL into hostname, port and path.
 *
 * @param url A string containing a valid URL. The port number in URL
 * is always optional.  The default port number is assumed to be 80.
 *
 * @return This method returns a std::tuple with 3 strings. The 3
 * strings are in the order: hostname, port, and path.
 */
std::tuple<std::string, std::string, std::string>
breakDownURL(const std::string& url) {
    // The values to be returned.
    std::string hostName, port = "80", path = "/";

    // Extract the substrings from the given url into the above
    // variables.
    std::size_t start = url.find("//") + 2;
    std::size_t end;
    hostName = url.substr(start);
    if (hostName.find(':') != std::string::npos) {
        end = hostName.find(':');
        port = hostName.substr(end + 1);
        port = port.substr(0, port.find('/'));
    } else {
        end = hostName.find('/');
    }
    path = hostName.substr(hostName.find('/'));
    hostName = hostName.substr(0, end);

    // Return 3-values encapsulated into 1-tuple.
    return { hostName, port, path };
}

/**
 * Helper method that uses the breakDownURL method to obtain the host, port,
 * and path of the data file to be processed. It then creates a GET request
 * to download the file from the URL, and performs the initial steps of
 * processing the data stream. Ultimately calls helper method to process data.
 *
 * @param url A string containing a valid URL.
 *
 */
void serveClient(const std::string& url, std::string& result) {
    std::string hostname, port, path;
    // Extract URL components
    std::tie(hostname, port, path) = breakDownURL(url);
    // Start the download of the file (that the user wants to be
    // processed) at the specified URL.
    boost::asio::ip::tcp::iostream data(hostname, port);
    data << "GET " << path << " HTTP/1.1\r\n"
        << "Host: " << hostname << "\r\n"
        << "Connection: Close\r\n\r\n";
    // Get the first HTTP response line and ensure it has 200 OK in it
    // to indicate that the data stream is good.
    std::string line;
    std::getline(data, line);
    if (line.find("200 OK") == std::string::npos) {
        return;
    }
    // Skip over HTTP response headers.
    for (std::string hdr; std::getline(data, hdr) && !hdr.empty()
        && hdr != "\r";) {
    }
    // Process data from the file.
    processData(data, result);
}

void threadMain(const std::string& fileName, std::string& result) {
    result = fileName;
    serveClient(fileName, result);
}

/**
 * The main function that begins the process of downloading and processing
 * log entries from the given URL and detecting potential hacking attempts.
 *
 * \param[in] argc The number of command-line arguments.  This program
 * requires exactly one command-line argument.
 *
 * \param[in] argv The actual command-line argument. This should be an URL.
 */
int main(int argc, char* argv[]) {
    std::vector<std::string> results(argc - 1, "");
    std::vector<std::thread> myThreads;
    for (int i = 1; i < argc; i++) {
        myThreads.push_back(std::thread(threadMain, argv[i],
            std::ref(results[i-1])));
    }
    for (auto& t : myThreads) {
        t.join();
    }
    for (int i = 0; i < argc - 1; i++) {
        std::cout << results[i] << std::endl;
    }
}
