// https://github.com/galacticoder
#include <iostream>
#include <vector>
#include <thread>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <fstream>
#include <cstring>
#include <mutex>
#include <fmt/core.h>
#include <cryptopp/osrng.h>
#include <cryptopp/hex.h>
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <cryptopp/filters.h>
#include <cryptopp/base64.h>
#include <sstream>
#include <boost/asio.hpp>
#include <ctime>
#include <chrono>
#include <regex>
#include <stdlib.h>
#include <unistd.h>
#include <filesystem>
#include "serverSendRecv.h"
#include <map>

// add certain length of username allow only

// To run: g++ -std=c++20 -o server server.cpp -lcryptopp -lfmt

//std::vector<std::vector<int>> userandclsocket

// map<string, int> userAndClSocket;

// // Insert some values into the map
// mp["one"] = 1;
// mp["two"] = 2;
// mp["three"] = 3;

// // Get an iterator pointing to the first element in the
// // map
// map<string, int>::iterator it = mp.begin();

// // Iterate through the map and print the elements
// while (it != mp.end()) {
//     cout << "Key: " << it->first
//          << ", Value: " << it->second << endl;
//     ++it;
// }

#define RED_TEXT "\033[31m" // red text color
#define GREEN_TEXT "\033[32m" // green text color
#define BRIGHT_BLUE_TEXT "\033[94m" // bright blue text color
#define RESET_TEXT "\033[0m" // reset color to default

using boost::asio::ip::tcp;

using namespace std;
using namespace CryptoPP;
using namespace std::chrono;
using namespace filesystem;


vector<int> connectedClients;
vector<int> uids;
vector<string> clientUsernames;
mutex clientsMutex;
// const stringlenOfUser;

//std::string encodedData = receiveBase64Data(clientSocket);
//std::vector<uint8_t> decodedData = base64Decode(encodedData);
//saveFile(filePath, decodedData);
// using namespace std::chrono_literals;

bool isPav(int port)
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

    if (bind(pavtempsock, (struct sockaddr*)&addr, sizeof(addr)) < 0)
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

static void delIt(const string& formatPath) {
    int del1 = 0;
    auto del2 = std::filesystem::directory_iterator(formatPath);
    int counter = 0;
    for (auto& del1 : del2) {
        if (del1.is_regular_file()) {
            std::filesystem::remove(del1);
            counter++;
        }
    }

    if (counter == 0) {
        cout << fmt::format("There was nothing to delete from path '{}'", formatPath) << endl;
    }
    if (counter == 1) {
        cout << fmt::format("{} key in filepath ({}) have been deleted", counter, formatPath) << endl;
    }
    else if (counter > 1) {
        cout << fmt::format("{} keys in filepath ({}) have been deleted", counter, formatPath) << endl;
    }
}

static bool createDir(const string& dirName)
{
    if (!create_directories(dirName))
    {
        if (exists(dirName))
        {
            cout << fmt::format("The directory ({}) already exists", dirName) << endl;
            return true;
        }
        cout << fmt::format("couldnt make directory: {}", dirName) << endl;
        return false;
    }
    return true;
}

void broadcastMessage(const string& message, int senderSocket = -1)
{
    lock_guard<mutex> lock(clientsMutex);
    for (int clientSocket : connectedClients)
    {
        if (clientSocket != senderSocket)
        {
            send(clientSocket, message.c_str(), message.length(), 0);
        }
    }
}

short int broadcastFile(string& filepath, string& serverpath, const string& username, short int* usersendersocket, int senderSocket = -1) {
    static short int index;
    for (int i = 0; i < clientUsernames.size(); i++) {
        if (clientUsernames[i] == username) {
            std::cout << "i: " << i << endl;
            index = i; //append index to str
            break;
        }
    }

    Send sendtoclient;
    static string fpFormatted = filepath.substr(8 + 2, filepath.length() - 1);
    static const string message = fmt::format("|{} wants to send you a file named '{}' would you like to recieve it?(y/n): ", username, fpFormatted);

    std::cout << "index is: " << index << endl;
    if (index == 1) { //this is the second client | then the client socket of index == the same index
        static const int clSock = connectedClients[index - 1];
        send(clSock, message.c_str(), message.length(), 0);
        std::cout << "sent message from client 2 to client 1" << endl;
        *(usersendersocket) = index;
        return index - 1;
    }
    else if (index == 0) { //this is the second client | then the client socket of index == the same index
        static const int clSockCl1 = connectedClients[index + 1];
        send(clSockCl1, message.c_str(), message.length(), 0);
        std::cout << "sent message from client 1 to client 2" << endl;
        *(usersendersocket) = index; //sender socket stored in here
        return index + 1;
    }
    else {
        cout << "error" << endl;
    }

    return 1;
}

// void broadcastFile(string& filename, int senderSocket = -1)
// {
// {
// lock_guard<mutex> lock(clientsMutex);
// for (int clientSocket : connectedClients)
// {
// if (clientSocket == senderSocket) {
// continue;
// }
// else if (clientSocket != senderSocket)
// {
// //send file to all clients but the sender
// sendFile(filename);
// }
// }
// }
// }

void updatePort(int PORT) {
    string portfile = "FILEPORT.TXT";
    std::ofstream file(portfile);
    if (file.is_open())
    {
        file << PORT;
        file.close();
    }
    else
    {
        cout << "Cannot write file server port to file" << endl;
        return;
    }
}

string countUsernames(string clientsNamesStr)
{ // make the func erase the previous string and make it empty to add all users
    clientsNamesStr.clear();
    if (clientsNamesStr.empty())
    {
        for (int i = 0; i < clientUsernames.size(); ++i)
        {
            if (clientUsernames.size() >= 2)
            {
                // for the last index dont print a comma
                clientsNamesStr.append(clientUsernames[i] + ","); // find the userbname and the before start pos and after end pos should be a comma
            }
            else
            {
                clientsNamesStr.append(clientUsernames[i]);
            }
        }
    }
    if (clientUsernames.size() >= 2)
    {
        clientsNamesStr.pop_back(); // to pop extra comma in the end
    }

    return clientsNamesStr;
}

// int showfp() {
// int PORT;
// string portfile = "FILEPORT.TXT";
// ifstream fileport(portfile);
// if (!fileport.is_open()) {
// cerr << "Could not open port file" << endl;
// return 0;
// }
// string PORTSTR;
// getline(fileport, PORTSTR);
// istringstream(PORTSTR) >> PORT;
// return PORT;
// }

void updateActiveFile(auto data) {
    std::ofstream file("usersActive.txt");
    file.open("usersActive.txt", std::ofstream::out | std::ofstream::trunc);
    file.close();

    ofstream fp("usersActive.txt");

    if (fp.is_open())
    {
        fp << data;
        fp.close();
    }
}

void handleClient(int clientSocket, int serverSocket) {
    string clientsNamesStr = "";
    {
        lock_guard<mutex> lock(clientsMutex);
        connectedClients.push_back(clientSocket);
    }

    char buffer[4096] = { 0 };
    ssize_t bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    buffer[bytesReceived] = '\0';
    std::string userStr(buffer);

    int index = userStr.find("|");
    string pubkeyseri = userStr.substr(index + 1);
    userStr = userStr.substr(0, index);

    // clientsNamesStr = countUsernames(clientsNamesStr); //something is being added making the vecotr 2
    // cout << "Connected clients: (";// (sopsijs,SOMEONE,ssjss,)
    // cout << clientsNamesStr;
    // cout << ")" << endl;
    if (userStr.find(' '))
    {
        for (int i = 0; i < userStr.length(); i++)
        {
            if (userStr[i] == ' ')
            {
                userStr[i] = '_';
            } //also check for slashes
        }
        send(clientSocket, userStr.c_str(), userStr.length(), 0);
    }

    const string exists = "\nUsername already exists. You are being kicked.";
    // set a username length max and detect if user already exists
    if (clientUsernames.size() != 1 || clientUsernames.size() != 0)
    {
        if (clientUsernames.size() > 0)
        {
            if (std::find(clientUsernames.begin(), clientUsernames.end(), userStr) != clientUsernames.end())
            {
                std::cout << "2 clients of the same username detected" << endl;
                std::cout << "Client Username vector size: " << clientUsernames.size() << endl;
                std::cout << "New user entered name that already exists. Kicking..." << endl;
                send(clientSocket, exists.data(), sizeof(exists), 0);
                std::lock_guard<std::mutex> lock(clientsMutex);
                std::cout << "Starting deletion of user socket" << endl;
                auto it = std::remove(connectedClients.begin(), connectedClients.end(), clientSocket);
                connectedClients.erase(it, connectedClients.end());
                std::cout << "Removed client socket from vector" << endl; // probnably getting a segmentation fault because of deletion of client socket before userename making it non accessable
                // std::lock_guard<std::mutex> lock(clientsMutex);

                // for(int i=clientsNamesStr.length();i<0;i--){
                // if(clientsNamesStr.find())
                // }

                close(clientSocket);
            }
        }
    }

    // cout << "Connected clients: (";// (sopsijs,SOMEONE,ssjss,)
    // cout << clientsNamesStr;
    // cout << ")" << endl;

    if (userStr.empty())
    {
        close(clientSocket);
    }

    else
    {
        clientUsernames.push_back(userStr); //first user index is 0 and the size is going to be 1 right here
        // clientUsernames.push_back(userStr);
        cout << "username added to client vector usernames" << endl;
        cout << connectedClients[0] << endl;
        //send pub key
        updateActiveFile(clientUsernames.size());
        // userAndClSocket[userStr + to_string(clientUsernames.size() - 1)] = clientSocket; //clientUsernames.size() - 1 should give us the index of the user that just joined the index for user 1 would be 0
        cout << "client SIZE: " << clientUsernames.size() << endl;

        Send usersactive;
        //send the active users txt file to client
        std::vector<uint8_t> activeBuf = usersactive.readFile("usersActive.txt"); //file path is a string to the file path
        std::string ed = usersactive.b64EF(activeBuf);
        usersactive.sendBase64Data(clientSocket, ed);

        std::string joinMsg = fmt::format("{} has joined the chat", userStr);
        string lenOfUser;
        std::string userJoinMsg = fmt::format("You have joined the chat as {}\n", userStr); // limit of string?????

        const string only = "\nYou are the only user in this chat you cannot send messages until another user joins";

        string pub = fmt::format("keys-server/{}-pubkeyserver.der", userStr);

        // recvServer(pub);
        //recieve the pub key file from the client and save it
        Recieve pubrecvserver;

        static string serverRecv;

        if (clientUsernames.size() == 1) {
            serverRecv = fmt::format("server-recieved-client-keys/{}-pubkeyfromclient.der", userStr);
        }
        else if (clientUsernames.size() > 1) {
            serverRecv = fmt::format("server-recieved-client-keys/{}-pubkeyfromclient.der", clientUsernames[1]);
        }
        cout << "starting encoded data" << endl;
        std::string encodedData = pubrecvserver.receiveBase64Data(clientSocket);
        cout << "done encoded data" << endl;
        std::vector<uint8_t> decodedData = pubrecvserver.base64Decode(encodedData);
        cout << "SERVERECV PATH: " << serverRecv << endl;
        cout << "gonna save file" << endl;
        // std::ofstream pubcheck(serverRecv, std::ios::binary);
        cout << "decoded writing: " << decodedData.data() << endl;
        pubrecvserver.saveFile(serverRecv, decodedData);
        // if (!pubcheck.is_open())
        // {
        // throw std::runtime_error("Could not open file to write");
        // }
        // else{
        // cout << "KEY FILE RECIEVED OPENED" << endl;
        // pubcheck.close();
        // }

        cout << "recv" << endl;
        cout << "Encoded key: " << encodedData << endl;


        //file paths
        static string sendToClient2 = fmt::format("server-recieved-client-keys/{}-pubkeyfromclient.der", clientUsernames[0]); //this path is to send the pub key of client 1 to the client that connects 
        static string clientSavePathAs = fmt::format("keys-from-server/{}-pubkeyfromserver.der", clientUsernames[0]);
        // string sendToClient1 = fmt::format("server-recieved-client-keys/{}-pubkeyfromclient.der", clientUsernames[1]); //file path of client 2 pub key | segmentation fault 
        // string client1toSavePathAs;

        // string fir = fmt::format("keys-server/{}-pubkeyserver.der", clientUsernames[0]);
        // string sec = fmt::format("keys-server/{}-pubkeyserver.der", userStr); //change userstr to new user without segmentation fault

        // string firSend = fmt::format("keys-server/{}-pubkeyserverfromser.der", clientUsernames[0]);

        //send file name to client
        Send sendtoclient;

        const string con = fmt::format("\nUsers connected: {}\n", clientUsernames.size());
        if (clientUsernames.size() == 2) {
            std::cout << fmt::format("sending {} from user {} to user {}", sendToClient2, clientUsernames[0], userStr) << endl;
            //send the file path to save as on client side
            send(clientSocket, clientSavePathAs.data(), clientSavePathAs.length(), 0); //problem was with length calculations
            cout << "sleeping 1 sec" << endl;
            // sleep(1); //dont gotta wait a sec no more
            // sendFile(fir);//this works for the second user only so make it also work for user 1
            std::vector<uint8_t> fi = sendtoclient.readFile(sendToClient2); //file path is a string to the file path
            std::string encodedData = sendtoclient.b64EF(fi);
            sendtoclient.sendBase64Data(clientSocket, encodedData); //send encoded key

            // char buf1[8] = { 0 };
            // ssize_t bt = recv(clientSocket, buf1, sizeof(buf1) - 1, 0);
            // buf1[bt] = '\0';
            // std::string ms1(buf1);
            // cout << "client 2 ms is: " << ms1 << endl;
        }
        else if (clientUsernames.size() == 1) {
            // send(clientSocket, con.c_str(), con.length(), 0);
            // send(clientSocket, only.c_str(), only.length(), 0);
            cout << "waiting for another client to connect to continue" << endl;
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                if (clientUsernames.size() > 1) {
                    cout << "Another user connected, proceeding..." << endl;
                    break;
                }
            }
            // string sec = fmt::format("keys-server/{}-pubkeyserver.der", clientUsernames[1]); //change userstr to new user without segmentation fault | fixed //second client sending the pub key to the first client
            if (clientUsernames.size() == 2) {
                string client1toSavePathAs = fmt::format("keys-from-server/{}-pubkeyfromserver.der", clientUsernames[1]); //file path client 1 needs to save as

                cout << fmt::format("sending to user 1: {}", client1toSavePathAs) << endl;
                //sending the file name to save as for client side
                send(clientSocket, client1toSavePathAs.data(), client1toSavePathAs.length(), 0); //problem was with length calculations | fixed
            }
            cout << "SENDING TO CLIENT 1" << endl;
            sleep(1); //gets connection error if dont sleep for 1s because server not ready yet
            // sendFile(sec);
            string sendToClient1 = fmt::format("server-recieved-client-keys/{}-pubkeyfromclient.der", clientUsernames[1]);
            std::vector<uint8_t> fi2 = sendtoclient.readFile(sendToClient1); //file path is a string to the file path //error when reading the file
            std::string encodedDataClient = sendtoclient.b64EF(fi2);
            sendtoclient.sendBase64Data(clientSocket, encodedDataClient); //send encoded key
            cout << "file to CLIENT 1 SENT" << endl;
            // char buf2[8] = { 0 };
            // ssize_t bt2 = recv(clientSocket, buf2, sizeof(buf2) - 1, 0);
            // buf2[bt2] = '\0';
            // std::string ms2(buf2);
            // cout << "client 1 ms is: " << ms2 << endl;
            // send(clientSocket, userJoinMsg.data(), userJoinMsg.length(), 0);
            // broadcastMessage(joinMsg, clientSocket);
        }
        // send(clientSocket, userJoinMsg.data(), userJoinMsg.length(), 0);
        // broadcastMessage(joinMsg, clientSocket);

        // string newSecSend = fmt::format("keys-server/{}-pubkeyserver.der", clientUsernames[1]);
        // //check if port file server was on is still on if so send the file to it
        // int PORT; 
        std::cout << "for join" << endl;
        for (int i = 0; i < clientUsernames.size(); i++)
        {
            if (clientUsernames[i] == userStr)
            {
                std::cout << "i: " << clientUsernames[i] << endl;
                lenOfUser.append(clientUsernames[i]);
            }
        }
        std::cout << "LENGTH OF USER: " << lenOfUser.length() << endl;
        std::cout << "LENGTH OF USERSTR: " << userStr.length() << endl;
        std::cout << "------------" << endl;

        // update clients to print
        clientsNamesStr = countUsernames(clientsNamesStr);

        std::cout << "Connected clients: ("; // (sopsijs,SOMEONE,ssjss,)
        std::cout << clientsNamesStr;
        std::cout << ")" << endl;


        std::cout << "Client Username vector size: " << clientUsernames.size() << endl;
        std::cout << "------------" << endl;
        // string portfile = "FILEPORT.TXT";
        // sleep(1); //actually no need for chrono just use sleep
        // ifstream fileport(portfile);
        // if (!fileport.is_open()) {
        // cerr << "Could not open port file" << endl;
        // return;
        // }
        // string PORTSTR;
        // getline(fileport, PORTSTR);
        // istringstream(PORTSTR) >> PORT;

        // cout << "starting check of ports open" << endl;
        // cout << "original port is " << PORT << endl;
        // updatePort(PORT + 1); // check a different way

        // cout << "gonna send file " << newSecSend << endl;
        // cout << "checking port " << PORT << endl;
        // // cout << fmt::format("pOPEN 1: {}", pOpen) << endl;
        // sleep(1); //gets connection error if dont sleep for 1s because server not ready yet
        // if (sendFile(newSecSend) == false) {
        // updatePort(PORT - 1); // check a different
        // int newp = showfp();
        // cout << "checking port " << newp << endl;
        // cout << fmt::format("pOPEN 2: {}", newp) << endl;
        // if (sendFile(newSecSend) == false) { //if open check second port back
        // cout << fmt::format("file server is not open on port {}", newp) << endl;
        // }
        // else {
        // // updatePort(PORT );
        // cout << "file to CLIENT 1 SENT (A2) on port " << newp << endl;
        // //send file to client that has their opened port 
        // }
        // }
        // else {
        // cout << "file to CLIENT 1 SENT (A3) on port " << PORT << endl;
        // // send file to client that has their opened port
        // }
        // else {
        // // updatePort(PORT + 1);
        // // //send file to client that has their opened port
        // // cout << "SENDING TO CLIENT (A3)" << endl;
        // // sleep(1); //gets connection error if dont sleep for 1s because server not ready yet
        // // sendFile(newSecSend);
        // cout << "file to CLIENT 1 SENT (A3) on port " << PORT << endl;
        // }
        // }

        // recvServer(pub);



        // string userName = userStr;

        // string some;

        // int counterUsers = 0;
        // thread t1([&]() {
        // });
        // t1.join(); 
        // check if username already exists if it does then kick them

        // broadcastMessage(joinMsg, clientSocket);

        bool isConnected = true;

        // if (clientUsernames.size() > 1) {
        // string ms = "";

        //add join messages somewhere
        //also send the username and time of the message attached to the message by delimeters | done

        while (isConnected)
        {
            //gonna iterate in infinitly

            // if (clientUsernames.size() > 1) {
            // send(clientSocket, only.c_str(), only.length(), 0);

            // }
            bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);
            if (bytesReceived <= 0 || strcmp(buffer, "quit") == 0)
            {
                isConnected = false;
                {
                    // erase socket
                    std::lock_guard<std::mutex> lock(clientsMutex);
                    std::cout << fmt::format("User client socket deletion: BEFORE: {}", connectedClients.size()) << endl;
                    auto it = std::remove(connectedClients.begin(), connectedClients.end(), clientSocket);
                    connectedClients.erase(it, connectedClients.end());
                    std::cout << fmt::format("User client socket deleted: AFTER: {}", connectedClients.size()) << endl;
                    std::cout << "------------" << endl;
                    std::cout << fmt::format("{} has left the chat", userStr) << endl;
                    // erase username
                    auto user = find(clientUsernames.rbegin(), clientUsernames.rend(), userStr);
                    if (user != clientUsernames.rend()) {
                        clientUsernames.erase((user + 1).base());
                    }
                    std::cout << "Clients connected: (" << countUsernames(clientsNamesStr) << ")" << endl;
                    std::cout << fmt::format("Clients in chat: {} ", clientUsernames.size()) << endl;


                }

                std::string exitMsg = fmt::format("{} has left the chat", userStr);
                // std::cout << exitMsg << std::endl;
                std::cout << "------------" << endl;
                // add if statment if useerrname of user isnt in vector twice
                // string lenOfUser;
                if (clientUsernames.size() < 1) {
                    close(serverSocket);
                    delIt("server-recieved-client-keys");
                    exit(1);
                }

                if (lenOfUser.length() == userStr.length() && lenOfUser == userStr) {
                    updateActiveFile(clientUsernames.size());
                    broadcastMessage(exitMsg, clientSocket);
                }
                else {
                    // cout << fmt::format("Clients connected: ({})", clientsNamesStr) << endl;
                    std::cout << "Disconnected client with same username" << endl;
                    close(clientSocket);
                }

                lenOfUser.clear();
                // last execution of username exists
            }

            else {
                buffer[bytesReceived] = '\0';
                std::string receivedData(buffer);
                // cout << "______________________________" << endl;
                std::cout << "Received data: " << receivedData << std::endl;
                // cout << "______________________________" << endl;
                cout << "ciphertext length on server: " << receivedData.length() << endl;
                std::string cipherText = receivedData; //fix server shutdown

                // string newenc = benc(newdec);
                // cout << "mem addr of recieveddata: " << &receivedData << endl;

                static string fpFormatted2 = "";
                short int senderSockIndex2 = 0;
                static int senderSocket = connectedClients[senderSockIndex2];
                short int clSock2 = 0;


                if (cipherText.substr(0, 8 + 1) == "/sendfile") {// /sendfile something.txt //find the last slash plus one
                    if (!filesystem::exists("server-recieved-files")) { //the user reply is recieved here
                        createDir("server-recieved-files");
                    }
                    Recieve cl;

                    string clfile = cipherText.substr(8 + 2, cipherText.length() - 1);
                    static string fpFormatted = fmt::format("server-recieved-files/{}", clfile);
                    fpFormatted2.append(fpFormatted);

                    std::string encodedData = cl.receiveBase64Data(clientSocket);
                    // std::vector<uint8_t> decodedData = cl.base64Decode(encodedData);
                    std::ofstream file(fpFormatted);

                    if (file.is_open()) {
                        file << encodedData;
                        file.close();
                        cout << fmt::format("file written to {}", fpFormatted) << endl;
                    }
                    // cl.saveFile(fpFormatted, decodedData);
                    if (is_regular_file(fpFormatted)) {
                        cout << fpFormatted << " has been opened and sending message" << endl;
                        short int senderSockIndex;
                        short int clSock = broadcastFile(clfile, fpFormatted, userStr, &senderSockIndex, clientSocket); //basically the index of the username that wants to send the file is the same index in the connectedClients vector
                        senderSockIndex2 += senderSockIndex;
                        clSock2 += clSock;

                        // cipherText.clear();
                        cout << "DONE WITH SEND MESSAGE" << endl;
                    }
                    else {
                        cout << "file does not exis cannot send: " << fpFormatted << endl;
                    }

                }

                if (cipherText == "y") { //MAKE A PUBLIC KEY FOR SERVER SO NO MESSAGES ARE EVER PLAIN AND IF THE MESSAGE ISNT ABLE TO DECRYP THEN IT ISNT OUR MESSAGE SO RIGHT WHEN USER JOINS THEY SEND THEIR PUB KEY TO SERVER AND THE SERVER SENDS ITS PUB KEY IN CASE THE CLIENT NEEDS TO COMMUNICATE TO SERVER PRIVATELY
                    cout << "cipher was y" << endl;
                    if (is_regular_file(fpFormatted2)) {
                        // std::vector<uint8_t> fi2 = sendtoclient.readFile(fpFormatted2); //file path is a string to the file path //error when reading the file
                        cout << "done reading file to send" << endl;
                        std::ifstream filetosend(fpFormatted2);
                        std::string str;
                        std::string file_contents; //encoded base 64
                        while (getline(filetosend, str)) {
                            file_contents += str;
                            file_contents.push_back('\n');
                        }
                        // std::string encodedDataClient = sendtoclient.b64EF(fi2);
                        cout << "file to send: " << file_contents << endl;
                        // sendtoclient.sendBase64Data(clSock2, file_contents); //send encoded key
                        send(clSock2, file_contents.c_str(), file_contents.length(), 0);

                        cout << "file sent to user" << endl;
                        static const string yes = "User has accepted your file. File has been sent to user";
                        send(senderSocket, yes.c_str(), yes.length(), 0);
                    }
                }
                else if (cipherText == "n") {
                    cout << "cipher was n" << endl;
                    static const string no = "The user did not accept the file you have sent\n"; //istead of user say the username didnt accept the file you attempted to send
                    send(senderSocket, no.c_str(), no.length(), 0);
                }

                else if (!cipherText.empty() && cipherText.length() > 30) { //when sneing somehow losig data when sending | fixed //this may be a problem to why the message is being sent like weirdly
                    // cout << "cipher: " << cipherText << endl;;
                    //time
                    auto now = std::chrono::system_clock::now();
                    std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
                    std::tm* localTime = std::localtime(&currentTime);

                    bool isPM = localTime->tm_hour >= 12;
                    string stringFormatTime = asctime(localTime);

                    int tHour = (localTime->tm_hour > 12) ? (localTime->tm_hour - 12) : ((localTime->tm_hour == 0) ? 12 : localTime->tm_hour);

                    stringstream ss;
                    ss << tHour << ":" << (localTime->tm_min < 10 ? "0" : "") << localTime->tm_min << " " << (isPM ? "PM" : "AM");
                    string formattedTime = ss.str();

                    std::regex time_pattern(R"(\b\d{2}:\d{2}:\d{2}\b)");
                    std::smatch match;
                    if (regex_search(stringFormatTime, match, time_pattern))
                    {
                        string str = match.str(0);
                        size_t pos = stringFormatTime.find(str);
                        stringFormatTime.replace(pos, str.length(), formattedTime);
                    }
                    string formattedCipher = userStr + "|" + stringFormatTime + "|" + cipherText;
                    broadcastMessage(formattedCipher, clientSocket);
                }
            }
        }
    }
}

int main() {
    static const string path = "server-recieved-client-keys";
    if (!exists(path)) {
        createDir(path);
    }
    else {
        delIt(path);
    }

    unsigned short PORT = 8080; //defualt port is set at 8080

    thread t1([&]()
        {
            if (isPav(PORT) == false) {
                cout << fmt::format("Port {} is not usable searching for port to use..", PORT) << endl;
                for (unsigned short i = 49152; i <= 65535; i++) {
                    if (isPav(i) != false) {
                        PORT = i;
                        break;
                    }
                }
            } });
            t1.join();

            int serverSocket = socket(AF_INET, SOCK_STREAM, 0);

            if (serverSocket < 0)
            {
                std::cerr << "Error opening server socket" << std::endl;
                return 1;
            }

            sockaddr_in serverAddress;
            int opt = 1;

            if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
                perror("setsockopt");
                exit(EXIT_FAILURE);
            }

            serverAddress.sin_family = AF_INET;
            serverAddress.sin_port = htons(PORT);
            serverAddress.sin_addr.s_addr = INADDR_ANY;

            if (bind(serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0)
            {
                cout << "Chosen port isn't available. Killing server" << endl;
                close(serverSocket);
                exit(true);
            }

            std::ofstream file("PORT.txt");
            if (file.is_open())
            {
                file << PORT;
                file.close();
            }
            else
            {
                std::cout << "Warning: cannot write port to file. You may need to configure clients port manually\n";
            }

            listen(serverSocket, 5);
            std::cout << fmt::format("Server listening on port {}", PORT) << "\n";

            while (true)
            {
                sockaddr_in clientAddress;
                socklen_t clientLen = sizeof(clientAddress);
                int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddress, &clientLen);

                std::thread(handleClient, clientSocket, serverSocket).detach();
            }

            //delete all keys from key recieves in server
            // auto dirIter = std::filesystem::directory_iterator("keys-server");
            // int fileCount = 0;

            // for (auto& entry : dirIter)
            // {
            // if (entry.is_regular_file())
            // {
            // std::filesystem::remove(entry);
            // ++fileCount;
            // }
            // }
            // cout << "file count is: " << fileCount << endl;


            close(serverSocket);
            return 0;
}
