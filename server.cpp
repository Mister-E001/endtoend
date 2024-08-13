// https://github.com/galacticoder
#include "headers/header-files/fileAndDirHandler.h"
#include "headers/header-files/hostHttp.h"
#include "headers/header-files/leave.h"
#include "headers/header-files/serverMenuAndEncry.h"
#include <algorithm>
#include <atomic>
#include <boost/asio.hpp>
#include <chrono>
#include <cryptopp/base64.h>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fmt/core.h>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <queue>
#include <regex>
#include <sstream>
#include <stdlib.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#define userPath "txt-files/usersActive.txt"
#define OKSIG "OKAYSIGNAL"
#define joinSignal "JOINED"
#define conSig "C"
#define pong "pong"

using boost::asio::ip::tcp;

using namespace std::chrono;
using namespace filesystem;

mutex clientsMutex;

std::vector<int> connectedClients;
std::vector<std::string> clientUsernames;
std::vector<int> clientHashVerifiedClients;
std::vector<SSL *> tlsSocks;
SSL_CTX *ctx;
std::map<std::string, std::chrono::seconds::rep> timeMap;
std::map<std::string, short int> triesIp;
unsigned short limOfUsers = 2;
int serverSocket;
short timeLimit = 90;
short running;
short joins;

const std::string limReached =
    "The limit of users has been reached for this chat. Exiting..";
const std::string notVerified = "Wrong password. You have been kicked.#N"; // #N
const std::string verified = "You have joined the server#V";               // #V

bool isPav(int port) {
  int pavtempsock;
  struct sockaddr_in addr;
  bool available = false;

  pavtempsock = socket(AF_INET, SOCK_STREAM, 0);

  if (pavtempsock < 0) {
    std::cerr << "Cannot create socket to test port availability" << std::endl;
    return false;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (bind(pavtempsock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    available = false;
  } else {
    available = true;
  }

  close(pavtempsock);
  return available;
}

void opensslclean() {
  EVP_cleanup();
  ERR_free_strings();
  CRYPTO_cleanup_all_ex_data();
}

void cleanUpServer() {
  std::lock_guard<std::mutex> lock(clientsMutex);
  SSL_CTX_free(ctx);
  close(serverSocket);

  leave(S_PATH, SERVER_KEYPATH);
  leaveFile(userPath);

  opensslclean();

  exit(1);
}

void signalHandleServer(int signum) {
  cout << eraseLine;
  cout << "Server has been shutdown" << endl;
  cleanUpServer();
  exit(signum);
}

void broadcastMessage(const string &message, SSL *senderSocket,
                      int &senderSock) {
  lock_guard<mutex> lock(clientsMutex);
  for (unsigned int i = 0; i < connectedClients.size(); i++) {
    if (connectedClients[i] != senderSock) {
      std::cout << "Sending msg to tls sock: " << tlsSocks[i] << std::endl;
      SSL_write(tlsSocks[i], message.c_str(), message.length());
    }
  }
}

std::string countUsernames(string &clientsNamesStr) {
  clientsNamesStr.clear();
  if (clientsNamesStr.empty()) {
    for (unsigned int i = 0; i < clientUsernames.size(); ++i) {
      if (clientUsernames.size() >= 2) {
        clientsNamesStr.append(clientUsernames[i] + ",");
      } else {
        clientsNamesStr.append(clientUsernames[i]);
      }
    }
  }
  if (clientUsernames.size() >= 2) {
    clientsNamesStr.pop_back();
  }

  return clientsNamesStr;
}

void updateActiveFile(auto data) {
  ifstream read(userPath);
  string active;

  ofstream write(userPath);

  if (write.is_open()) {
    write << data;
    cout << "Updated usersActive.txt file: " << data << endl;
  } else {
    cout << "Could not open usersActive.txt file to update" << endl;
  }
}

void setupSignalHandlers() { signal(SIGINT, signalHandleServer); }

void leaveCl(SSL *clientSocket, int &clsock, unsigned int id = -1,
             const std::string &username = "none") {
  std::lock_guard<std::mutex> lock(clientsMutex);
  SSL_shutdown(clientSocket);
  SSL_free(clientSocket);
  close(clsock);

  auto it =
      std::remove(connectedClients.begin(), connectedClients.end(), clsock);
  connectedClients.erase(it, connectedClients.end());

  auto ittls = std::remove(tlsSocks.begin(), tlsSocks.end(), clientSocket);
  tlsSocks.erase(ittls, tlsSocks.end());

  if ((int)id != -1 && id < clientHashVerifiedClients.size()) {
    clientHashVerifiedClients.erase(clientHashVerifiedClients.begin() + id);
  }
  if (username != "none" && clientUsernames.size()) {
    auto it =
        std::find(clientUsernames.begin(), clientUsernames.end(), username);
    if (it != clientUsernames.end()) {
      clientUsernames.erase(it);
    }
  }
}

void waitTimer(const std::string hashedClientIp) {
  std::lock_guard<std::mutex> lock(mutex);
  static std::default_random_engine generator(time(0));
  static std::uniform_int_distribution<int> distribution(10, 30);
  //---
  int additionalDelay = distribution(generator);
  timeLimit += additionalDelay;
  //--- put here means the more they attempt to join the more the timer adds |
  // if put in running condition then it keeps the time the same even if they
  // attempt to keep joining
  if (running == 0) {
    std::cout << "Starting timer timeout for user with hash ip: "
              << hashedClientIp << std::endl;
    running = 1;
    int len = hashedClientIp.length();
    while (timeLimit != 0) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      timeLimit--;
      std::cout << fmt::format("Timer user [{}..]: ",
                               hashedClientIp.substr(0, len / 4))
                << timeLimit << std::endl;
      std::cout << "\x1b[A";
      std::cout << eraseLine;
    }

    if (timeLimit == 0) {
      triesIp[hashedClientIp] = 0;
      timeMap[hashedClientIp] =
          std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::system_clock::now().time_since_epoch())
              .count();
      timeLimit = 90;
      running = 0;
      std::cout
          << fmt::format(
                 "Tries for IP hash ({}) has been resetted and can now join",
                 hashedClientIp)
          << std::endl;
    }
  }
}

bool checkPassHash(const std::string &passGetArg, SSL *clientSocket, int clsock,
                   std::unordered_map<int, string> &serverHash, int &pnInt,
                   int &indexClientOut, const std::string &username) {
  std::string notVerified = "You have been kicked from the server for not "
                            "inputting the correct password#N";
  try {
    if (clientHashVerifiedClients.size() < 3) {
      if (bcrypt::validatePassword(passGetArg, serverHash[1]) != 1 &&
          pnInt != 2) {
        const string newP =
            fmt::format("{}{}-pubkeyfromclient.pem", SRCPATH, username);
        if (is_regular_file(newP)) {
          LoadKey loadp;
          encServer enc;
          EVP_PKEY *keyLoading = loadp.LoadPubOpenssl(newP);
          string notVENC = "";
          if (keyLoading) {
            const string encd =
                enc.Base64Encode(enc.Enc(keyLoading, notVerified));
            notVENC += encd;
            EVP_PKEY_free(keyLoading);
          } else {
            std::cout << "Server shutting down due to server key not loading"
                      << std::endl;
            raise(SIGINT);
          }

          EVP_PKEY *keyLoading2 = loadp.LoadPubOpenssl(newP);
          if (keyLoading2) {
            if (notVENC != "err") {
              SSL_write(clientSocket, notVENC.c_str(), notVENC.length());
              std::this_thread::sleep_for(std::chrono::seconds(1));
              leaveCl(clientSocket, clsock, indexClientOut, username);

              std::cout << "Disconnected client for non verified password"
                        << std::endl;

              EVP_PKEY_free(keyLoading2);

              return true;
            }
          } else {
            SSL_write(clientSocket, notVerified.c_str(), notVerified.length());
            std::this_thread::sleep_for(std::chrono::seconds(1));
            leaveCl(clientSocket, clsock, indexClientOut, username);
            std::cout << "Disconnected client for non verified password"
                      << std::endl;
            return true;
          }
        } else {
          SSL_write(clientSocket, notVerified.c_str(), notVerified.length());
          std::this_thread::sleep_for(std::chrono::seconds(1));
          leaveCl(clientSocket, clsock, indexClientOut, username);
          std::cout << "Disconnected client for non verified password"
                    << std::endl;
          return true;
        }
      }
    }
  } catch (exception &e) {
    SSL_write(clientSocket, notVerified.c_str(), notVerified.length());
    std::this_thread::sleep_for(std::chrono::seconds(1));
    leaveCl(clientSocket, clsock, indexClientOut, username);
    std::cout << "Disconnected client for non verified password" << std::endl;
    std::cout << "Exception was: " << e.what() << std::endl;
    return true;
  }
  return true;
}

// void test()
// {
//     if (bytesReceived <= 0)
//     {
//         {
//             leaveCl(clientSocket, clsock, indexClientOut);
//             std::cout << exitMsg << endl;
//         }
//     }
// }

void handleClient(SSL *clientSocket, int clsock, int serverSocket,
                  unordered_map<int, string> serverHash, int pnInt,
                  const string serverKeysPath, const string serverPrvKeyPath,
                  const string serverPubKeyPath, std::string hashedIp,
                  int &val) {
  encServer enc;
  DecServer dec;
  Send send;
  Receive receive;
  LoadKey load;

  try {
    char pingbuf[200] = {0};
    ssize_t pingb = SSL_read(clientSocket, pingbuf, sizeof(pingbuf) - 1);
    pingbuf[pingb] = '\0';
    std::string pingStr(pingbuf);

    if (pingStr == "C") {
      try {
        short found;

        for (const auto &pair : timeMap) {
          if (pair.first == hashedIp) {
            found = 1;
            auto now = std::chrono::system_clock::now();
            auto newNowTime = std::chrono::duration_cast<std::chrono::seconds>(
                                  now.time_since_epoch())
                                  .count();

            auto storedTime =
                timeMap[pair.first]; // take time stored in map and check how
                                     // long its been since last connection
            auto elapsed = newNowTime - storedTime;

            if (elapsed < 90 &&
                triesIp[hashedIp] >=
                    4) // set the seconds needed // if joined over 3 times in
                       // under 90 seconds then kick them and give them a small
                       // time limit they can join back in
            {
              // this dooes not get reached no more
              thread(waitTimer, hashedIp).detach(); // starts the wait time
              const std::string rateLimited = fmt::format(
                  "Rate limit reached. Try again in {} seconds", timeLimit);
              std::string encodedV = enc.Base64Encode(rateLimited);
              encodedV.append("RATELIMITED");

              std::cout << "Encoded sending rate limit: " << encodedV
                        << std::endl;
              SSL_write(clientSocket, encodedV.c_str(), encodedV.size());
              std::this_thread::sleep_for(std::chrono::seconds(1));
              leaveCl(clientSocket, clsock);
              std::cout << "Client kicked for attempting to join too frequently"
                        << std::endl;
              break;
            }
          }
        }

        if (found != 1) {
          timeMap[hashedIp] =
              std::chrono::duration_cast<std::chrono::seconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count(); // if not found in timemap then make a new user in
                            // there using their hashed ip
        }
        if (val != 3) {
          std::cout << fmt::format("Sending hashed ip [{}..] signal okay",
                                   hashedIp.substr(0, hashedIp.length() / 4))
                    << std::endl;
          std::string encoded = enc.Base64Encode((std::string)OKSIG);
          encoded.append("OK");
          SSL_write(clientSocket, encoded.c_str(), encoded.size());
        } else if (val == 4) {
          std::cout << fmt::format("Sending hashed ip [{}..] signal okay",
                                   hashedIp.substr(0, hashedIp.length() / 4))
                    << std::endl;
          std::string encoded = enc.Base64Encode((std::string)OKSIG);
          encoded.append("OK");
          SSL_write(clientSocket, encoded.c_str(), encoded.size());
        }

        {
          lock_guard<mutex> lock(clientsMutex);
          connectedClients.push_back(clsock);
          tlsSocks.push_back(clientSocket);
        }

        if (clientUsernames.size() == limOfUsers) // never reached again
        {
          std::string encoded = enc.Base64Encode(limReached);
          encoded.append("LIM");
          SSL_write(clientSocket, limReached.c_str(), limReached.length());
          leaveCl(clientSocket, clsock);
          std::cout << "Kicked user who attempted to join past client limit"
                    << std::endl;
          return;
        }

        int pnS = 1;
        int pnO = 2;
        std::string passGetArg;

        auto itCl = find(connectedClients.begin(), connectedClients.end(),
                         clsock); // find clientSocket index
        int indexClientOut = itCl - connectedClients.begin();

        if (clientHashVerifiedClients.size() != 3) {
          clientHashVerifiedClients.push_back(0);
        }

        cout << "Size of clientHashVerifiedClients vector: "
             << clientHashVerifiedClients.size() << endl;

        if (clientHashVerifiedClients.size() < 3) {
          if (pnInt == 1 && clientHashVerifiedClients[indexClientOut] != 1) {
            cout << "Sending pass verify signal" << endl;
            SSL_write(clientSocket, to_string(pnS).c_str(),
                      to_string(pnS).size());
            std::cout << "Waiting to receive password from client.."
                      << std::endl;

            std::string passGet = receive.receiveBase64Data(clientSocket);
            std::cout << "Pass cipher recieved from client: " << passGet
                      << std::endl;

            std::cout << fmt::format(
                             "Loading server private key from path [{}]",
                             serverPrvKeyPath)
                      << std::endl;

            LoadKey loadServerKey;
            EVP_PKEY *serverPrivate =
                loadServerKey.LoadPrvOpenssl(serverPrvKeyPath);

            if (serverPrivate) {
              std::cout << "Loaded server private key for decryption of passGet"
                        << std::endl;
            } else {
              std::cout << "Could not load server private key for decryption. "
                           "Killing server."
                        << std::endl;
              raise(SIGINT);
            }
            std::cout << "Decoding pass cipher" << std::endl;
            std::string decodedPassGet = dec.Base64Decode(passGet);

            if (passGet.size() == 0) {
              std::cout << "User password is 0 bytes. Kicking.." << std::endl;
              leaveCl(clientSocket, clsock, indexClientOut);
              std::cout << "Kicked user with error password." << std::endl;
              return;
            }

            std::cout << "Password cipher size: " << passGet.size()
                      << std::endl;
            passGet = dec.dec(serverPrivate, decodedPassGet);
            EVP_PKEY_free(serverPrivate);

            passGetArg = passGet;

            std::cout << "Validating password hash sent by user" << std::endl;

            if (bcrypt::validatePassword(passGet, serverHash[1]) == 1 &&
                !passGet.empty()) {
              SSL_write(clientSocket, verified.c_str(), verified.length());
              clientHashVerifiedClients[indexClientOut] = 1;
              std::cout << "User password verified and added to "
                           "clientHashVerifiedClients vector"
                        << std::endl;
              std::cout << "Updated vector size: "
                        << clientHashVerifiedClients.size() << std::endl;
            } else {
              SSL_write(
                  clientSocket, notVerified.c_str(),
                  notVerified.length()); // sends them the not verified message
              std::this_thread::sleep_for(std::chrono::seconds(1));
              leaveCl(clientSocket, clsock, indexClientOut);
              std::cout << fmt::format(
                               "User with hashed ip [{}..] has entered the "
                               "wrong password and has been kicked",
                               hashedIp.substr(0, hashedIp.length() / 4))
                        << std::endl;
              return;
            }
          } else if (pnInt == 2) {
            std::cout << fmt::format("Sending hashed ip [{}..] signal okay [2]",
                                     hashedIp.substr(0, hashedIp.length() / 4))
                      << std::endl;
            std::string encoded = enc.Base64Encode((std::string)OKSIG);
            encoded.append("OK");
            SSL_write(clientSocket, encoded.c_str(), encoded.size());
          }
        } else if (pnInt == 2 &&
                   clientHashVerifiedClients[indexClientOut] != 1) {
          SSL_write(clientSocket, to_string(pnO).c_str(),
                    to_string(pnO).length()); // send no password needed signal
        }

        unsigned int valid = std::find(connectedClients.begin(),
                                  connectedClients.end(), clsock) -
                            connectedClients.begin();
                            
        if (valid != (long int)connectedClients.size()) {
          std::string clientsNamesStr;

          char buffer[4096] = {0};
          std::cout << "Recieving username from client.." << std::endl;
          ssize_t bytesReceived =
              SSL_read(clientSocket, buffer, sizeof(buffer) - 1);
          buffer[bytesReceived] = '\0';
          std::string userStr(buffer);

          if (clientHashVerifiedClients.size() < 3) {
            if (bcrypt::validatePassword(passGetArg, serverHash[1]) != 1 &&
                pnInt != 2) {
              SSL_write(clientSocket, notVerified.c_str(),
                        notVerified.length());
              std::this_thread::sleep_for(std::chrono::seconds(1));
              leaveCl(clientSocket, clsock, indexClientOut, userStr);
              std::cout << "Disconnected user with unverified password"
                        << std::endl;
              return;
            }
          }

          const string exists =
              "Username already exists. You are have been kicked.@";
          if (clientUsernames.size() > 0) // checks if username already exists
          {
            for (unsigned int i = 0; i < clientUsernames.size(); i++) {
              if (clientUsernames[i] == userStr) {
                cout << "Client with the same username detected. kicking.."
                     << endl;
                SSL_write(clientSocket, exists.c_str(), exists.length());
                std::this_thread::sleep_for(std::chrono::seconds(1));
                leaveCl(clientSocket, clsock, indexClientOut);
                cout << "Kicked client with same username kicked" << endl;
                return;
              }
            }
          }
          if (userStr.find(' ')) {
            for (unsigned int i = 0; i < userStr.length(); i++) {
              if (userStr[i] == ' ') {
                userStr[i] = '_';
              }
            }
            SSL_write(clientSocket, userStr.c_str(), userStr.length());
          }

          if (userStr.empty()) {
            leaveCl(clientSocket, clsock, indexClientOut);
            std::cout << "Disconnected user with empty name" << std::endl;
          } else {
            if (checkPassHash(passGetArg, clientSocket, clsock, serverHash,
                              pnInt, indexClientOut, userStr) != false) {
              clientUsernames.push_back(userStr);
              std::cout << "Client username added to clientUsernames vector"
                        << std::endl;
              updateActiveFile(clientUsernames.size()); //
              {
                string activeBuf = send.readFile(
                    userPath); // file path is a string to the file path
                string ed = send.b64EF(activeBuf);
                send.sendBase64Data(clientSocket, ed);
              }
              joins++;

              std::string userPubPath =
                  fmt::format("keys-server/{}-pubkeyserver.pem", userStr);
              std::string serverRecv;

              if (clientUsernames.size() == 1) {
                serverRecv = fmt::format(
                    "server-recieved-client-keys/{}-pubkeyfromclient.pem",
                    userStr);
              } else if (clientUsernames.size() > 1) {
                serverRecv = fmt::format(
                    "server-recieved-client-keys/{}-pubkeyfromclient.pem",
                    clientUsernames[1]);
              }

              { // receive the client pub key
                std::string encodedData =
                    receive.receiveBase64Data(clientSocket);
                std::string decodedData = receive.base64Decode(encodedData);
                receive.saveFilePem(serverRecv, decodedData);
              }

              std::string messagetouseraboutpub =
                  "Public key that you sent to server cannot be loaded on "
                  "server";

              if (is_regular_file(serverRecv)) {
                EVP_PKEY *pkeyloader = load.LoadPubOpenssl(serverRecv);

                if (!pkeyloader) {
                  std::cout << fmt::format("Cannot load user [{}] public key",
                                           userStr)
                            << std::endl;
                  messagetouseraboutpub =
                      enc.Base64Encode(messagetouseraboutpub);
                  messagetouseraboutpub.append("MSGNO");
                  SSL_write(clientSocket, messagetouseraboutpub.data(),
                            messagetouseraboutpub.length());
                  std::this_thread::sleep_for(std::chrono::seconds(1));
                  leaveCl(clientSocket, clsock, indexClientOut, userStr);
                  std::cout << fmt::format("Kicked user [{}]", userStr)
                            << std::endl;
                  return;
                }
                EVP_PKEY_free(pkeyloader);
              } else {
                std::cout
                    << fmt::format(
                           "User [{}] public key file on server does not exist",
                           userStr)
                    << std::endl;
                messagetouseraboutpub = enc.Base64Encode(messagetouseraboutpub);
                messagetouseraboutpub.append("EXISTERR");
                SSL_write(clientSocket, messagetouseraboutpub.data(),
                          messagetouseraboutpub.length());
                std::this_thread::sleep_for(std::chrono::seconds(1));
                leaveCl(clientSocket, clsock, indexClientOut, userStr);
                std::cout << fmt::format("Kicked user [{}]", userStr)
                          << std::endl;
                return;
              }
              SSL_write(clientSocket, OKSIG, strlen(OKSIG));

              // file paths
              string sendToClient2 = fmt::format(
                  "server-recieved-client-keys/{}-pubkeyfromclient.pem",
                  clientUsernames[0]); // this path is to send the pub key of
                                       // client 1 to the client that connects
              string clientSavePathAs =
                  fmt::format("keys-from-server/{}-pubkeyfromserver.pem",
                              clientUsernames[0]);

              if (clientUsernames.size() == 2) {
                std::cout << fmt::format("Sending {} from user {} to user {}",
                                         sendToClient2, clientUsernames[0],
                                         userStr)
                          << endl;
                SSL_write(clientSocket, clientSavePathAs.data(),
                          clientSavePathAs.length());
                std::string fi = receive.read_pem_key(sendToClient2);
                std::string encodedData = send.b64EF(fi);
                send.sendBase64Data(clientSocket, encodedData);
              } else if (clientUsernames.size() == 1) {
                cout << "1 client connected. Waiting for another client to "
                        "connect to continue"
                     << endl;
                while (1) {
                  std::this_thread::sleep_for(std::chrono::seconds(2));
                  // do some checking to see if client is still connected
                  // //maybe make a thread and detach it and let that handle
                  // client disconnections in the background if (/*condition
                  // that it can read something if not then do the code under*/)
                  // {
                  //     {
                  //         leaveCl(clientSocket, clsock, indexClientOut,
                  //         userStr); std::cout << "Server shutting down due to
                  //         no users connected" << std::endl; raise(SIGINT);
                  //
                  if (clientUsernames.size() > 1) {
                    std::cout << "Another user connected, proceeding..."
                              << std::endl;
                    break;
                  }
                }

                if (clientUsernames.size() == 2) {
                  string client1toSavePathAs =
                      fmt::format("keys-from-server/{}-pubkeyfromserver.pem",
                                  clientUsernames[1]);
                  SSL_write(clientSocket, client1toSavePathAs.data(),
                            client1toSavePathAs.length());
                  std::this_thread::sleep_for(std::chrono::seconds(1));
                }
                string sendToClient1 = fmt::format(
                    "server-recieved-client-keys/{}-pubkeyfromclient.pem",
                    clientUsernames[1]);
                std::string fi2 = receive.read_pem_key(sendToClient1);
                std::string encodedDataClient = send.b64EF(fi2);
                send.sendBase64Data(clientSocket,
                                    encodedDataClient); // send encoded key
                cout << "Sent client 2's public key to client 1" << endl;
              } else {
                return;
              }

              if (joins > 2) {
                for (SSL *client : tlsSocks) {
                  std::cout << "size tlssocks: " << tlsSocks.size()
                            << std::endl;
                  if (client != tlsSocks[1]) {
                    std::cout << "Sending pub key to sock: " << client
                              << std::endl;
                    std::string user1msg = "Pubkeysendingcl1";
                    user1msg = enc.Base64Encode(user1msg);
                    user1msg.append("PSE");
                    SSL_write(client, user1msg.c_str(), user1msg.size());
                    string sendToClient1 = fmt::format(
                        "server-recieved-client-keys/{}-pubkeyfromclient.pem",
                        clientUsernames[1]);
                    std::string cl1path =
                        fmt::format("keys-from-server/{}-pubkeyfromclient.pem",
                                    clientUsernames[1]);
                    SSL_write(client, cl1path.c_str(), cl1path.size());
                    std::string cl2again = receive.read_pem_key(sendToClient1);
                    std::cout << "KEYTOSEND: " << cl2again << std::endl;
                    std::string encodedDataClient2 = send.b64EF(cl2again);
                    send.sendBase64Data(client,
                                        encodedDataClient2); // send encoded key
                    cout << "Sent client 2's public key to client 1" << endl;
                  } else {
                    std::cout << "Ignore" << std::endl;
                  }
                }
              }

              clientsNamesStr = countUsernames(clientsNamesStr);

              std::string joinMsgForServer =
                  fmt::format("{} has joined the chat", userStr);

              auto itUserIndex =
                  find(clientUsernames.begin(), clientUsernames.end(), userStr);
              int userIndex = itUserIndex - clientUsernames.begin();
              std::string joinMsg;

              bool isConnected = true;

              if (userIndex < 1) {
                joinMsg = fmt::format("{} has joined the chat",
                                      clientUsernames[userIndex + 1]);
              } else {
                joinMsg = fmt::format("{} has joined the chat",
                                      clientUsernames[userIndex - 1]);
              }

              EVP_PKEY *userKey = load.LoadPubOpenssl(fmt::format(
                  "server-recieved-client-keys/{}-pubkeyfromclient.pem",
                  userStr));
              if (userKey) {
                std::string encJoin = enc.Enc(userKey, joinMsg);
                encJoin = enc.Base64Encode(encJoin);
                EVP_PKEY_free(userKey);
                SSL_write(clientSocket, encJoin.c_str(), encJoin.size());
                std::cout << joinMsgForServer << std::endl;
              } else {
                std::cout << "Cannot load user key for sending join message"
                          << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(1));
                leaveCl(clientSocket, clsock, indexClientOut, userStr);
                return;
              }

              std::cout << "Connected clients: (";
              std::cout << clientsNamesStr;
              std::cout << ")" << endl;

              while (isConnected) {

                if (checkPassHash(passGetArg, clientSocket, clsock, serverHash,
                                  pnInt, indexClientOut, userStr) != false) {
                  std::string exitMsg =
                      fmt::format("{} has left the chat", userStr);
                  bytesReceived =
                      SSL_read(clientSocket, buffer, sizeof(buffer));
                  if (bytesReceived <= 0) {
                    isConnected = false;
                    {
                      leaveCl(clientSocket, clsock, indexClientOut);
                      std::cout << exitMsg << endl;
                    }

                    if (clientUsernames.size() > 1) {
                      if (clientUsernames[0] == userStr) {
                        int index = 0 + 1;
                        string pathpub =
                            fmt::format("server-recieved-client-keys/"
                                        "{}-pubkeyfromclient.pem",
                                        clientUsernames[index]);
                        EVP_PKEY *keyLoad = load.LoadPubOpenssl(pathpub);
                        if (keyLoad) {
                          string op64 =
                              enc.Base64Encode(enc.Enc(keyLoad, exitMsg));
                          if (op64 != "err") {
                            std::cout
                                << fmt::format(
                                       "Broadcasting user [{}]'s exit message",
                                       clientUsernames[0])
                                << std::endl;
                            broadcastMessage(op64, clientSocket, clsock);
                            EVP_PKEY_free(keyLoad);
                          }
                        } else {
                          std::cout << fmt::format(
                                           "User [{}] pub key cannot be loaded",
                                           clientUsernames[0])
                                    << std::endl;
                          leaveCl(clientSocket, clsock, indexClientOut,
                                  userStr);
                          std::cout << fmt::format("User [{}] has been kicked "
                                                   "due to key not loading",
                                                   clientUsernames[0])
                                    << std::endl;
                          return;
                        }
                      } else if (clientUsernames[1] == userStr) {
                        string pathpub2 =
                            fmt::format("server-recieved-client-keys/"
                                        "{}-pubkeyfromclient.pem",
                                        clientUsernames[1]);
                        EVP_PKEY *keyLoad2 = load.LoadPubOpenssl(pathpub2);

                        if (keyLoad2) {
                          std::string op642 =
                              enc.Base64Encode(enc.Enc(keyLoad2, exitMsg));
                          if (op642 != "err") {
                            std::cout
                                << fmt::format(
                                       "Broadcasting user [{}]'s exit message",
                                       clientUsernames[1])
                                << std::endl;
                            broadcastMessage(op642, clientSocket, clsock);
                            EVP_PKEY_free(keyLoad2);
                          }
                        } else {
                          std::cout << fmt::format(
                                           "User [{}] pub key cannot be loaded",
                                           clientUsernames[1])
                                    << std::endl;
                          leaveCl(clientSocket, clsock, indexClientOut,
                                  userStr);
                          std::cout << fmt::format("User [{}] has been kicked "
                                                   "due to key not loading",
                                                   clientUsernames[1])
                                    << std::endl;
                          return;
                        }
                      }
                    }

                    if (clientUsernames.size() > 0) {
                      auto user = find(clientUsernames.rbegin(),
                                       clientUsernames.rend(), userStr);

                      if (user != clientUsernames.rend()) {
                        clientUsernames.erase((user + 1).base());
                      }
                    }

                    updateActiveFile(clientUsernames.size());
                    std::cout << "Clients connected: ("
                              << countUsernames(clientsNamesStr) << ")"
                              << std::endl;
                    string pubfiletodel = fmt::format(
                        "server-recieved-client-keys/{}-pubkeyfromclient.pem",
                        userStr);

                    std::cout << "Deleting user pubkey" << std::endl;
                    remove(pubfiletodel);
                    if (!is_regular_file(pubfiletodel)) {
                      cout << fmt::format(
                                  "Client's pubkey file ({}) has been deleted",
                                  pubfiletodel)
                           << endl;
                    } else if (is_regular_file(pubfiletodel)) {
                      cout << fmt::format(
                                  "Client's pubkey file ({}) cannot be deleted",
                                  pubfiletodel)
                           << endl;
                    }

                    if (clientUsernames.size() < 1) {
                      break;
                    }
                  }

                  else {
                    buffer[bytesReceived] = '\0';
                    std::string receivedData(buffer);
                    std::cout << "Received data: " << receivedData << std::endl;
                    cout << "Ciphertext message length: "
                         << receivedData.length() << endl;
                    std::string cipherText = receivedData;

                    if (!cipherText.empty() && cipherText.length() > 30 &&
                        cipherText.length() < 4096) {
                      auto now = std::chrono::system_clock::now();
                      std::time_t currentTime =
                          std::chrono::system_clock::to_time_t(now);
                      std::tm *localTime = std::localtime(&currentTime);

                      bool isPM = localTime->tm_hour >= 12;
                      string stringFormatTime = asctime(localTime);

                      int tHour = (localTime->tm_hour > 12)
                                      ? (localTime->tm_hour - 12)
                                      : ((localTime->tm_hour == 0)
                                             ? 12
                                             : localTime->tm_hour);

                      stringstream ss;
                      ss << tHour << ":" << (localTime->tm_min < 10 ? "0" : "")
                         << localTime->tm_min << " " << (isPM ? "PM" : "AM");
                      string formattedTime = ss.str();

                      std::regex time_pattern(R"(\b\d{2}:\d{2}:\d{2}\b)");
                      std::smatch match;
                      if (regex_search(stringFormatTime, match, time_pattern)) {
                        string str = match.str(0);
                        size_t pos = stringFormatTime.find(str);
                        stringFormatTime.replace(pos, str.length(),
                                                 formattedTime);
                      }
                      string formattedCipher =
                          userStr + "|" + stringFormatTime + "|" + cipherText;
                      broadcastMessage(formattedCipher, clientSocket, clsock);
                    } else {
                      leaveCl(clientSocket, clsock, indexClientOut, userStr);
                      std::cout << "Kicked user for invalid message length"
                                << std::endl;
                    }
                  }
                }
              }
              if (clientUsernames.size() < 1) {
                cout << "Shutting down server due to no users." << endl;
                raise(SIGINT);
              }
            }
          }
        }
      } catch (exception &e) {
        std::cout << "Server has been killed due to error (1): " << e.what()
                  << std::endl;
        raise(SIGINT);
      }
    } else {
      leaveCl(clientSocket, clsock);
      std::cout << "Kicked user due to not sending connection signal"
                << std::endl;
      return;
    }
  } catch (exception &e) {
    std::cout << "Server has been killed due to error (2): " << e.what()
              << std::endl;
    raise(SIGINT);
  }
}

int main() {
  unsigned long pingCount = 0;
  std::queue<std::string> joinRequests;
  int pnInt;
  int val;
  const std::string serverKeysPath = SERVER_KEYPATH;
  unordered_map<int, string> serverHash;
  initMenu startMenu;
  std::string hash = startMenu.initmenu(serverHash);
  signal(SIGINT, signalHandleServer);

  if (hash == "PNINT4") {
    val = 4;
  } else if (hash.size() > 0 &&
             hash.substr(hash.length() - 6, hash.length()) == "PNINT3") {
    pnInt = 3;
    val = 3;
  }

  // cout << "some: " << hash.substr(hash.length() - 7, hash.length()) << endl;

  if (!hash.empty()) {
    // std::cout << "val: " << val << std::endl;
    if (val == 3) {
      serverHash[1] = hash.substr(0, hash.length() - 6);
      pnInt = 1;
    } else {
      if (val != 4) {
        serverHash[1] = hash;
      }
    }
  }

  switch (serverHash.empty()) {
  case 0:      // if not empty
    pnInt = 1; // 1 means pass
    break;
  case 1:      // if empty
    pnInt = 2; // 2 means no pass
    break;
  }

  cout << "Pnint is: " << pnInt << endl;

  unsigned short PORT = 8080;

  thread t1([&]() {
    if (isPav(PORT) == false) {
      cout << fmt::format("Port {} is not usable searching for port to use..",
                          PORT)
           << endl;
      for (unsigned short i = 49152; i <= 65535; i++) {
        if (isPav(i) != false) {
          PORT = i;
          break;
        }
      }
    }
  });
  t1.join();

  serverSocket = socket(AF_INET, SOCK_STREAM, 0);

  if (serverSocket < 0) {
    std::cerr << "Error opening server socket" << std::endl;
    return 1;
  }

  sockaddr_in serverAddress;
  int opt = 1;

  if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                 sizeof(opt))) {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }

  serverAddress.sin_family = AF_INET;
  serverAddress.sin_port = htons(PORT);
  serverAddress.sin_addr.s_addr = INADDR_ANY;

  if (bind(serverSocket, (struct sockaddr *)&serverAddress,
           sizeof(serverAddress)) < 0) {
    cout << "Chosen port isn't available. Killing server" << endl;
    raise(SIGINT);
  }

  listen(serverSocket, 5);
  std::cout << fmt::format("Server listening on port {}", PORT) << "\n";

  createDir(S_PATH);
  createDir(SERVER_KEYPATH);

  string server_priv_path = serverKeysPath + "/server-privkey.pem";
  string server_pub_path = serverKeysPath + "/server-pubkey.pem";
  string server_cert_path = serverKeysPath + "/server-cert.pem";

  std::cout << "Generating server keys.." << std::endl;
  makeServerKey serverKey(server_priv_path, server_cert_path, server_pub_path);
  std::cout << fmt::format("Saved server keys in path '{}'", serverKeysPath)
            << std::endl;

  LoadKey loadServerKeys;

  EVP_PKEY *prvKey = loadServerKeys.LoadPrvOpenssl(server_priv_path);

  if (prvKey) {
    std::cout << "Server's private key (cert) has been loaded" << std::endl;
    EVP_PKEY_free(prvKey);
  } else {
    std::cout << "Cannot load server's private key (cert). Killing server."
              << std::endl;
    raise(SIGINT);
  }

  initOpenSSL inito;
  inito.InitOpenssl();
  ctx = inito.createCtx();
  std::cout << "Configuring server ctx" << std::endl;
  inito.configCtx(ctx, server_cert_path, server_priv_path);
  std::cout << "Done configuring server ctx" << std::endl;

  std::cout << "Server is now accepting connections" << std::endl;

  std::cout << "Started hosting server cert key" << std::endl;
  thread(startHost).detach();

  while (true) {
    // check if its a ping or user
    sockaddr_in clientAddress;
    socklen_t clientLen = sizeof(clientAddress);
    int clientSocket =
        accept(serverSocket, (struct sockaddr *)&clientAddress, &clientLen);

    char pingbuf[200] = {0};
    ssize_t pingb = read(clientSocket, pingbuf, sizeof(pingbuf) - 1);
    pingbuf[pingb] = '\0';
    std::string pingStr(pingbuf);

    if (pingStr == conSig) {
      send(clientSocket, OKSIG, strlen(OKSIG), 0);
      SSL *ssl_cl = SSL_new(ctx);

      SSL_set_fd(ssl_cl, clientSocket);
      if (SSL_accept(ssl_cl) <= 0) {
        ERR_print_errors_fp(stderr);
        // leaveCl(ssl_cl, clientSocket);
        SSL_free(ssl_cl);
        close(clientSocket);
        std::cout << "Closed user that failed at ssl/tls handshake"
                  << std::endl;
      } else {
        // get ip and hash it of client here
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        getpeername(clientSocket, (struct sockaddr *)&client_addr, &client_len);

        char clientIp[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, clientIp, INET_ADDRSTRLEN);
        encServer encIp;
        const std::string hashedIp = encIp.hash_data(clientIp);
        clientIp[0] = '\0';

        short int conrun;
        encServer encodeMsg;

        std::cout << "Hashed ip updated amount of tries: " << triesIp[hashedIp]
                  << std::endl;

        if (clientUsernames.size() == limOfUsers) {
          conrun = 0;
          std::string encodedtext = encodeMsg.Base64Encode(limReached);
          encodedtext.append("LIM");
          SSL_write(ssl_cl, encodedtext.c_str(), encodedtext.length());
          std::this_thread::sleep_for(std::chrono::seconds(1));
          SSL_shutdown(ssl_cl);
          SSL_free(ssl_cl);
          close(clientSocket);
          std::cout << "Kicked user that tried to join over users limit"
                    << std::endl;
        }
        // check for timeout on ip
        else if (triesIp[hashedIp] >= 3) {
          thread(waitTimer, hashedIp).detach();
          conrun = 0;
          const std::string rateLimited = fmt::format(
              "Rate limit reached. Try again in {} seconds", timeLimit);

          // std::this_thread::sleep_for(std::chrono::seconds(1));
          std::string encodedV = encodeMsg.Base64Encode(rateLimited);
          encodedV.append("RATELIMITED");
          SSL_write(ssl_cl, encodedV.c_str(), encodedV.size());
          std::this_thread::sleep_for(std::chrono::milliseconds(500));
          leaveCl(ssl_cl, clientSocket);
          std::cout << "Client kicked for attempting to join too frequently"
                    << std::endl;
        } else {
          conrun = 1;
          SSL_write(ssl_cl, OKSIG, strlen(OKSIG));
        }

        auto it = triesIp.find(hashedIp);

        if (it == triesIp.end()) {
          triesIp[hashedIp] = 1;
          std::cout << "Added new user to triesIp map" << std::endl;
        } else if (it != triesIp.end()) {
          triesIp[hashedIp]++;
        }

        if (val == 3 &&
            triesIp[hashedIp] <
                4) // check if users need to request the server to join
        {
          SSL_write(ssl_cl, OKSIG, strlen(OKSIG));
          encServer enc;
          const std::string req = "Server needs to accept your join request. "
                                  "Waiting for server to accept..";
          const std::string notAccepted =
              "Your join request to server has been rejected";
          const std::string accepted =
              "Your join request to server has been accepted";
          std::string encodeMsg = enc.Base64Encode(req).append("REQ");
          SSL_write(ssl_cl, encodeMsg.c_str(), encodeMsg.size());
          std::this_thread::sleep_for(std::chrono::seconds(1));
          joinRequests.push(clientIp);
          std::string ans;
          // while (ans.size() == 0)
          // {
          std::string msg =
              fmt::format("User from ip [{}..] is requesting to join the "
                          "server. Accept or no?(y/n): ",
                          hashedIp.substr(0, hashedIp.length() / 4));
          ans = getinput_getch(MODE_N, 1, msg);
          // }
          if (ans == "y") {
            std::string encodeAccept = enc.Base64Encode(accepted).append("ACC");
            SSL_write(ssl_cl, encodeAccept.c_str(), encodeAccept.size());
            joinRequests.pop();
            std::cout << eraseLine;
            std::cout << "User has been allowed in server" << std::endl;
          } else if (ans == "n") {
            std::string encodeDenied =
                enc.Base64Encode(notAccepted).append("DEC");
            SSL_write(ssl_cl, encodeDenied.c_str(), encodeDenied.size());
            joinRequests.pop();
            std::cout << eraseLine;
            std::cout << "User has been not allowed in server" << std::endl;
            leaveCl(ssl_cl, clientSocket);
            conrun = 0;
          } else {
            std::string encodeDenied =
                enc.Base64Encode(notAccepted).append("DEC");
            SSL_write(ssl_cl, encodeDenied.c_str(), encodeDenied.size());
            joinRequests.pop();
            std::cout << eraseLine;
            std::cout << "Invalid answer given" << std::endl;
            std::cout << "User has been not allowed in server" << std::endl;
            leaveCl(ssl_cl, clientSocket);
            conrun = 0;
          }
        } else {
          SSL_write(ssl_cl, OKSIG, strlen(OKSIG));
          std::this_thread::sleep_for(std::chrono::seconds(1));
          SSL_write(ssl_cl, OKSIG, strlen(OKSIG));
        }

        if (conrun != 0) {
          thread(handleClient, ssl_cl, clientSocket, serverSocket, serverHash,
                 pnInt, serverKeysPath, server_priv_path, server_pub_path,
                 hashedIp, std::ref(val))
              .detach();
        }
      }
    } else if (pingStr == "ping") {
      send(clientSocket, pong, strlen(pong), 0);
      close(clientSocket);
      pingCount++;
      std::cout << fmt::format("Server has been pinged [{}]", pingCount)
                << std::endl;
      std::cout << "\x1b[A";
      std::cout << eraseLine;
    } else {
      close(clientSocket);
      std::cout << "Closed connection of unknown user" << std::endl;
    }
  }
  // thread(serverCommands).de..

  raise(SIGINT);

  return 0;
}
