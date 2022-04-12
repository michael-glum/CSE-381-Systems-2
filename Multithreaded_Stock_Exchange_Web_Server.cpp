/* 
 * Copyright 2021 Michael Glum
 * 
 * A simple online stock exchange web-server.  
 * 
 * This multithreaded web-server performs simple stock trading
 * transactions on stocks.  Stocks are maintained in an
 * unordered_map.
 *
 */

#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <iomanip>
#include <vector>
#include "Stock.h"

// Setup a server socket to accept connections on the socket
using namespace boost::asio;
using namespace boost::asio::ip;

// Shortcut to smart pointer with TcpStream
using TcpStreamPtr = std::shared_ptr<tcp::iostream>;

// Prototype for helper method defined in main.cpp
std::string url_decode(std::string);

// unique_lock to help with multithreading
using Lock = std::unique_lock<std::mutex>;

// The name space to hold all of the information that is shared
// between multiple threads.
namespace sm {
    // Unordered map including stock's name as the key (std::string)
    // and the actual Stock entry as the value.
    std::unordered_map<std::string, Stock> stockMap;

    // Conditional variables for use in sleep wait approach for multithreading
    std::condition_variable buyCond;
    std::condition_variable countCond;

    // Mutex to control number of threads produced by the server
    std::mutex serverMutex;

    // Shared variable to keep track of the number of detached threads
    std::atomic<int> threadCount = ATOMIC_VAR_INIT(0);

}  // namespace sm

/**
 * This method is called from clientThread to process a "reset" transaction.
 * It deletes all stocks in the unordered map.
 *
 * \return Message response regarding the transaction
 */
std::string reset() {
    sm::stockMap.clear();
    return "Stocks reset";
}

/**
 * This method is called from clientThread to process a "create" transaction.
 * It creates a new stock with the given stock name and balance if the stock
 * does not already exist.
 *
 * \param[in] stock The name of the stock to be created
 *
 * \param[in] amount The balance of the stock to be created
 *
 * \return Message response regarding the transaction
 */
std::string create(std::string stock, unsigned int amount) {
    std::string msg;
    // Check if stock exists
    if (sm::stockMap.find(stock) == sm::stockMap.end()) {
        // Create stock
        sm::stockMap[stock].name = stock;
        sm::stockMap[stock].balance = amount;
        msg = "Stock " + stock + " created with balance = "
            + std::to_string(amount);
    } else {
        msg = "Stock " + stock + " already exists";
    }
    // Return result of transaction
    return msg;
}

/**
 * This method is called from clientThread to process a "buy" transaction.
 * It reduces the balance associated with the stock and returns a message with
 * the new balance, or a message saying that the stock could not be found.
 *
 * \param[in] stock The stock to be processed
 *
 * \param[in] amount The amount of the stock to be sold
 *
 * \return Message response regarding the transaction
 */
std::string buy(std::string stock, unsigned int amount) {
    std::string msg;
    // Check if stock exists
    if (sm::stockMap.find(stock) != sm::stockMap.end()) {
        // Use a mutex unique_lock to create a critical section where the
        // balance of the stock can be changed without creating a race
        // condition when multithreading.
        // This also ensures that a stock can not be bought more times
        // than it is available using a sleep wait approach.
        Lock lock(sm::stockMap[stock].mutex);
        sm::buyCond.wait(lock,
            [stock, amount] {return amount <= 
            sm::stockMap.at(stock).balance; });
        // Update the balance
        sm::stockMap[stock].balance -= amount;
        msg = "Stock " + stock + "'s balance updated";
    } else {
        msg = "Stock not found";
    }
    // Return result of transaction
    return msg;
}

/**
 * This method is called from clientThread to process a "sell" transaction.
 * It increases the balance associated with the stock and returns a message with
 * the new balance or a message saying that the stock could not be found.
 *
 * \param[in] stock The stock to be processed
 * 
 * \param[in] amount The amount of the stock to be sold
 *
 * \return Message response regarding the transaction
 */
std::string sell(std::string stock, unsigned int amount) {
    std::string msg;
    // Check if the stock exists
    if (sm::stockMap.find(stock) != sm::stockMap.end()) {
        // Use a mutex unique_lock to create a critical section where the
        // balance of the stock can be changed without creating a race
        // condition when multithreading
        Lock lock(sm::stockMap[stock].mutex);
        // Update the balance
        sm::stockMap[stock].balance += amount;
        // Notify the threads waiting to process a buy transaction that
        // more stock is available
        sm::buyCond.notify_all();
        msg = "Stock " + stock + "'s balance updated";
    } else {
        msg = "Stock not found";
    }
    // Return result of transaction
    return msg;
}

/**
 * This method is called from clientThread to return the balance of a stock
 * or a message saying the stock does not exist.
 *
 * \param[in] stock The stock to be processed
 *
 * \return Message response regarding the transaction
 */
std::string status(std::string stock) {
    std::string msg;
    if (sm::stockMap.find(stock) != sm::stockMap.end()) {
        // Use a mutex unique_lock to create a critical section where the
        // balance of the stock can be accessed without creating a race
        // condition when multithreading
        Lock lock(sm::stockMap[stock].mutex);
        // Retrieve balance
        auto balance = sm::stockMap[stock].balance;
        msg = "Balance for stock " + stock + " = " + std::to_string(balance);
    } else {
        msg = "Stock not found";
    }
    // Return result of transaction
    return msg;
}

/**
 * This method is called from clientThread to format and output an HTTP
 * response based on transaction processed in clientThread.
 *
 * \param[in] msg The message created when processing the transaction
 *
 * \param[out] os The output stream to where the HTTP response is to
 * be written.
 */
void HTTPResponse(std::ostream& os, const std::string& msg) {
    os << "HTTP/1.1 200 OK\r\n" <<
        "Server: StockServer\r\nContent-Length: "
        << std::to_string(msg.length()) << "\r\n" <<
        "Connection: Close\r\n" <<
        "Content - Type: text / plain\r\n\r\n" << msg;
}

/**
 * This method assumes the input stream is an HTTP GET request (hence it is
 * important to understand the input format before implementing this
 * method).  This method extracts the URL to be processed from the 1st
 * line of the input stream.
 *
 * For example, if the 1st line of input is
 * "GET /http://localhost:8080/~raodm HTTP/1.1" then this method returns
 * "http://localhost:8080/~raodm"
 *
 * @return This method returns the path specified in the GET
 * request.
 */
std::string extractURL(std::istream& is) {
    std::string line, url;
    // Extract the GET request line from the input
    std::getline(is, line);
    // Read and skip HTTP headers. Without reading & skipping HTTP
    // headers, your program will not work correctly with
    // web-browsers.
    for (std::string hdr; std::getline(is, hdr)
        && !hdr.empty() && hdr != "\r";) {
    }
    // Do basic substring operation to extract the URL that is
    // delimited by space from the first line of input.
    line = line.substr(line.find('/') + 1);
    url = line.substr(0, line.find(' '));
    return url;
}

/**
 * This method is called from a separate detached/background thread
 * from the runServer() method.  This method processes 1 transaction
 * from a client.  This method extracts the transaction information
 * and processes the transaction by calling helper
 * methods.
 * 
 * \param[in] is The input stream from where the HTTP request is to be
 * read and processed.
 *
 * \param[out] os The output stream to where the HTTP response is to
 * be written.
 */
void clientThread(std::istream& is, std::ostream& os) {
    // Increment thread count for this detached thread
    sm::threadCount++;
    std::string request, trans, stock, message, dummy, msg;
    // Call helper method to extract URL from the GET request
    request = extractURL(is);
    // Call helper method to decode the URL
    request = url_decode(request);
    // Replace '&' and '=' characters in the request to make it
    // easier to parse out the data.
    std::replace(request.begin(), request.end(), '&', ' ');
    std::replace(request.begin(), request.end(), '=', ' ');
    // Create a string input stream to read words out.
    unsigned int amount = 0;
    // Read the important elements of the request into the desired variables
    std::istringstream(request) >> dummy >> trans >> dummy >> stock
        >> dummy >> amount;
    // Request is considered invalid by defualt
    msg = "Invalid request";
    // Process transaction if request is valid and retrieve the message
    if (trans == "reset") {
        msg = reset();
    } else if (trans == "create") {
        msg = create(stock, amount);
    } else if (trans == "buy") {
        msg = buy(stock, amount);
    } else if (trans == "sell") {
        msg = sell(stock, amount);
    } else if (trans == "status") {
        msg = status(stock);
    }
    // Call helper method to output the HTTP response
    HTTPResponse(os, msg);
    // Decrement threadCount as this detached thread finishes
    sm::threadCount--;
    // Allow another thread to be created as this thread terminates
    sm::countCond.notify_one();
}

/**
 * Top-level method to run a custom HTTP server to process stock trade
 * requests.
 *
 * Phase 1: Multithreading is not needed.
 *
 * Phase 2: This method must use multiple threads -- each request
 * should be processed using a separate detached thread. Optional
 * feature: Limit number of detached-threads to be <= maxThreads.
 *
 * \param[in] server The boost::tcp::acceptor object to be used to accept
 * connections from various clients.
 *
 * \param[in] maxThreads The maximum number of threads that the server
 * should use at any given time.
 */
void runServer(tcp::acceptor& server, const int maxThreads) {
    // Process client connections one-by-one...forever
    while (true) {
        // Creates garbage-collected connection on heap 
        TcpStreamPtr client = std::make_shared<tcp::iostream>();
        server.accept(*client->rdbuf());  // wait for client to connect
        // Now we have a I/O stream to talk to the client. Have a
        // conversation using the protocol.
        // Use sleep wait multithreading to prevent more detached threads from
        // being created than maxThreads allows
        Lock lock(sm::serverMutex);
        // Use conditional_variable to wait for threadCount to be
        // decremented as a detached thread terminates.
        sm::countCond.wait(lock,
            [maxThreads] {return sm::threadCount < maxThreads; });
        std::thread thr([client] { clientThread(*client, *client); });
        thr.detach();  // Process transaction independently
    }
}

// End of source code
