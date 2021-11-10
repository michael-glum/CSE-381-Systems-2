/**
 * Copyright 2021 Michael Glum
 * 
 * A program to detect potential attempts at trying to break into
 * accounts by scanning logs on a Linux machine. Breakin attempts are
 * detected using the two rules listed further below.
 *
 *   1. If an IP is in the "banned list", then it is flagged as a
 *      break in attempt.
 *
 *   2. unless an user is in the "authorized list", if an user has
 *      attempted to login more than 3 times in a span of 20 seconds,
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <boost/asio.hpp>

// Convenience namespace declarations to streamline the code below
using namespace boost::asio;
using namespace boost::asio::ip;
using namespace std;

/** Synonym for an unordered map that is used to track banned IPs and
 * authorized users. For example, the key in this map would be IP addresses
 * and the value is just a place holder (is always set to true).
 */
using LookupMap = std::unordered_map<std::string, bool>;

/**
 * An unordered map to track the seconds for each log entry associated
 * with each user. The user ID is the key into this unordered map.
 * The values is a list of timestamps of log entries associated with
 * an user. For example, if a user "bob" has 3 login at "Aug 29 11:01:01",
 * "Aug 29 11:01:02", and "Aug 29 11:01:03" (one second apart each), then
 * logins["bill"] will be a vector with values {1630249261, 1630249262,
 * 1630249263}.
 */
using LoginTimes = std::unordered_map<std::string, std::vector<long>>;

/**
 * Helper method to load data from a given file into an unordered map.
 *
 * @param fileName The file name from words are are to be read by this
 * method. The parameter value is typically "authorized_users.txt" or
 * "banned_ips.txt".
 *
 * @return Return an unordered map with the
 */
LookupMap loadLookup(const std::string& fileName) {
    // Open the file and check to ensure that the stream is valid
    std::ifstream is(fileName);
    if (!is.good()) {
        throw std::runtime_error("Error opening file " + fileName);
    }
    // The look up map to be populated by this method.
    LookupMap lookup;
    // Load the entries into the unordered map
    for (std::string entry; is >> entry;) {
        lookup[entry] = true;
    }
    // Return the loaded unordered map back to the caller.
    return lookup;
}

/**
 * This method is used to convert a timestamp of the form "Jun 10
 * 03:32:36" to seconds since Epoch (i.e., 1900-01-01 00:00:00). This
 * method assumes by default, the year is 2021.
 *
 * \param[in] timestamp The timestamp to be converted to seconds.  The
 * timestamp must be in the format "Month day Hour:Minutes:Seconds",
 * e.g. "Jun 10 03:32:36".
 *
 * \param[in] year An optional year associated with the date. By
 * default this value is assumed to be 2021.
 *
 * \return This method returns the seconds elapsed since Epoch.
 */
long toSeconds(const std::string& timestamp, const int year = 2021) {
    // Initialize the time structure with specified year.
    struct tm tstamp = { .tm_year = year - 1900 };
    // Now parse out the values from the supplied timestamp
    strptime(timestamp.c_str(), "%B %d %H:%M:%S", &tstamp);
    // Use helper method to return seconds since Epoch
    return mktime(&tstamp);
}


/**
 * Helper method to detect hacking due to a login time violation.
 * Specifically, this method detects if there are over 3 login attempts
 * by a single user ID within a 20 second period.
 *
 * @param loginTimes Unordered map containing login time records for each user.
 *   authorizedUsers Unordered map containing authorizedUser IDs to be
 *                   referenced.
 *            userID User ID to be checked for a login time violation.
 *
 * @return True if there is a violation. False if not.
 */
bool isLoginTimeViolation(LoginTimes& loginTimes,
    const LookupMap& authorizedUsers, const std::string& userID) {
    bool isViolation = false;
    // First check if the user is authorized
    if (authorizedUsers.find(userID) == authorizedUsers.end()) {
        // If there are less than 4 login attempts by a user,
        // there cannot be a violation.
        if (loginTimes[userID].size() > 3) {
            // If there are at least 4 login attempts within 20 seconds of
            // each other, there is a violation (login times are listed 
            // chronologically).
            size_t i = loginTimes[userID].size() - 1;
            if (loginTimes[userID][i] - loginTimes[userID][i - 3] <= 20) {
                isViolation = true;
            }
        }
    }
    return isViolation;
}

/**
 * Helper method to populate an unordered map with the login time
 * records for each user ID.
 * 
 * @param month, day, time, userID Strings containing the month,
 *        day, and time of the login, as well as the userID      
 *        associated with the login attempt.
 *        loginTimes Reference to the unordered map to be populated.
 */
void processLoginTime(const std::string& month, const std::string& day,
    const std::string& time, const std::string& userID,
    LoginTimes& loginTimes) {
    std::string timeStamp = month + " " + day + " " + time;
    // Link each userID (key) to a vector containing times of attempted
    // login (value).
    long seconds = toSeconds(timeStamp);
    if (loginTimes.find(userID) == loginTimes.end()) {
        std::vector<long> vec = {seconds};
        loginTimes[userID] = vec;
    } else {
        loginTimes[userID].push_back(seconds);
    }
}
/**
 * Process login data obtained from a web-server and detect possible
 * hacking attempts due to login by a banned IP address or 
 * excessive login frequency from a single unauthorized user.
 * 
 * \param[in]   is The input stream from where the data is to
 *                 be read.
 *       bannedIPS Unordered map containing banned IP addresses to be
 *                 referenced.
 * authorizedUsers Unordered map containing authorizedUser IDs to be
 *                 referenced
 * 
 * Print the results of the hack detection
 */
void processData(std::istream& is, const LookupMap& bannedIPs,
    const LookupMap& authorizedUsers) {
    std::string line, month, day, time, userID, ip, dummy;
    int lineCount = 0, hackCount = 0;
    LoginTimes loginTimes;
    // Read input from data stream
    while (std::getline(is, line)) {
        // Keep track of only the important elements of each line (month, day,
        // time, userID, and IP).
        std::istringstream(line) >> month >> day >> time >> dummy >> dummy
            >> dummy >> dummy >> dummy >> userID >> dummy >> ip;
        // Check for hacking attempts from banned IP addresses.
        if (bannedIPs.find(ip) != bannedIPs.end()) {
            hackCount++;
            std::cout << "Hacking due to banned IP. Line: " << line << '\n';
        } else {
            // Populate unordered map containing login time information
            processLoginTime(month, day, time, userID, loginTimes);
            // Check for hacking attempts due to login time violations
            if (isLoginTimeViolation(loginTimes, authorizedUsers, userID)) {
                hackCount++;
                std::cout << "Hacking due to frequency. Line: " << line << '\n';
            }
        }
        // Keep track of the number of lines processed
        lineCount++;
    }
    std::cout << "Processed " << lineCount << " lines. Found " << hackCount
        << " possible hacking attempts." << '\n';
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
void serveClient(const std::string& url) {
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
    // Load banned IP and authorized user data into two unordered maps.
    LookupMap bannedIPs = loadLookup("banned_ips.txt");
    LookupMap authorizedUsers = loadLookup("authorized_users.txt");
    // Process data from the file to detect possible hacking attempts.
    processData(data, bannedIPs, authorizedUsers);
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
int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::cout << "URL not specified. See video on setting command-line "
                  << "arguments in NetBeans on Canvas.\n";
        return 1;
    }
    const std::string url = argv[1];
    // Download file with login records from url and call helper methods to
    // process data.
    serveClient(url);
}
