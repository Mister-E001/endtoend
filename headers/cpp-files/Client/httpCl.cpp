#include <iostream>
#include <fstream>
#include <curl/curl.h>
#include <unistd.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <boost/asio.hpp>
#include <boost/asio/error.hpp>
#include <boost/beast.hpp>
#include <iomanip>
#include <fmt/core.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <arpa/inet.h>
#include <thread>
#include "../../header-files/Client/httpCl.h"

namespace asio = boost::asio;
namespace beast = boost::beast;
using tcp = boost::asio::ip::tcp;

int serverSd = 0;
int clientPort = 8080;

bool isPortAvailable(int port)
{
    int pavtempsock;
    struct sockaddr_in addr;
    bool available = false;

    pavtempsock = socket(AF_INET, SOCK_STREAM, 0);

    if (pavtempsock < 0)
    {
        std::cerr << "Cannot create socket to test port availability" << std::endl;
        return false;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(pavtempsock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        available = false;
    }
    else
    {
        available = true;
    }

    close(pavtempsock);
    return available;
}

size_t writeCallBack(void *contents, size_t size, size_t nmemb, void *userp)
{
    std::ofstream *outfile = static_cast<std::ofstream *>(userp);
    size_t totalSize = size * nmemb;
    outfile->write(static_cast<char *>(contents), totalSize);
    return totalSize;
}

size_t writePing(void *contents, size_t size, size_t nmemb, void *userp)
{
    std::string *result = static_cast<std::string *>(userp);
    size_t totalSize = size * nmemb;
    result->append(static_cast<char *>(contents), totalSize);
    return totalSize;
}

int http::fetchAndSave(const std::string &site, const std::string &outfile)
{
    CURL *curl;
    CURLcode request;
    std::ofstream outFile(outfile, std::ios::binary);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init(); // init libcurl

    if (curl)
    {
        std::cout << "Curl has started" << std::endl;
        curl_easy_setopt(curl, CURLOPT_URL, site.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallBack);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outFile);
        request = curl_easy_perform(curl);

        if (request != CURLE_OK)
        {
            curl_easy_strerror(request);
            return 1;
        }
        curl_easy_cleanup(curl);
        outFile.close();
        return 0;
    }
    curl_global_cleanup();
    return 0;
}

void http::pingServer(const char *host, unsigned short port)
{
    int update_secs = 1;

    const auto wait_duration = std::chrono::seconds(update_secs);
    while (1)
    {
        try
        {
            int clientSocket = socket(AF_INET, SOCK_STREAM, 0);

            sockaddr_in serverAddress;
            serverAddress.sin_family = AF_INET;
            serverAddress.sin_port = htons(port);

            if (inet_pton(AF_INET, host, &serverAddress.sin_addr) <= 0)
            {
                raise(SIGINT);
            }

            if (connect(clientSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0)
            {
                close(clientSocket);
            }

            const std::string pingMsg = "ping";
            send(clientSocket, pingMsg.c_str(), pingMsg.length(), 0);

            char buffer[8] = {0};
            int valread = read(clientSocket, buffer, 8);
            buffer[valread] = '\0';
            std::string readStr(buffer);

            if (readStr == "pong")
            {
                close(clientSocket);
            }
            else
            {
                close(clientSocket);
                raise(SIGINT);
            }
            std::this_thread::sleep_for(wait_duration);
        }
        catch (const std::exception &e)
        {
            std::cout << "Exception caught in [httpCl.cpp] http::pingServer: " << e.what() << std::endl;
        }
    }
}

void http::serverMake()
{
    std::thread t1([&]()
                   {
    if (isPortAvailable(clientPort) == false) {
      for (unsigned short i = 49152; i <= 65535; i++) {
          if (isPortAvailable(i) != false)
          {
              clientPort = i;
              break;
          }
      }
    } });
    t1.join();

    sockaddr_in servAddr;
    bzero((char *)&servAddr, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servAddr.sin_port = htons(clientPort);

    serverSd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSd < 0)
    {
        std::cout << "Error establishing the server socket [CLSERVER] [httpCl.cpp]" << std::endl;
        raise(SIGINT);
    }

    int bindStatus = bind(serverSd, (struct sockaddr *)&servAddr, sizeof(servAddr));

    if (bindStatus < 0)
    {
        std::cout << "Error binding socket to local address [CLSERVER] [httpCl.cpp]" << std::endl;
        raise(SIGINT);
    }
    std::cout << fmt::format("Client server has started on port [{}]", clientPort) << std::endl;

    listen(serverSd, 2);

    while (1)
    {
        sockaddr_in newSockAddr;
        socklen_t newSockAddrSize = sizeof(newSockAddr);

        int newSd = accept(serverSd, (sockaddr *)&newSockAddr, &newSockAddrSize);
        if (newSd < 0)
        {
            std::cerr << "Error accepting request from client! [CLSERVER] [httpCl.cpp]" << std::endl;
            raise(SIGINT);
        }

        char buffer[8] = {0};
        ssize_t bytesRead = recv(newSd, buffer, sizeof(buffer), 0);
        buffer[bytesRead] = '\0';
        std::string statusCheck(buffer);

        const std::string statusUp = "S>UP";

        if (statusCheck == "SCHECK")
        {
            send(newSd, statusUp.c_str(), statusUp.size(), 0);
        }
        close(newSd);
    }
}
