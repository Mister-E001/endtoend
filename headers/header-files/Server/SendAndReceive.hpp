#pragma once

#include <iostream>
#include <cstring>
#include <vector>
#include <fmt/core.h>
#include <netinet/in.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include "Keys.hpp"
#include "Encryption.hpp"
#include "CleanUp.hpp"

extern std::vector<SSL *> SSLsocks;
extern std::vector<std::string> clientsKeyContents;
extern std::vector<int> connectedClients;
extern std::vector<int> PasswordVerifiedClients;
extern std::vector<std::string> clientUsernames;

std::mutex mut;
class Send
{
public:
    Send() = default;
    static void SendKey(SSL *clientSocket, int &&clientSendIndex /*index of client to send the key to*/, unsigned int &clientIndex)
    {
        std::lock_guard<std::mutex> lock(mut);

        std::cout << fmt::format("Sending Client {}'s key to Client {}", clientUsernames[clientIndex], clientUsernames[clientSendIndex]) << std::endl;
        const std::string PublicKeyPath = PublicPath(clientUsernames[clientSendIndex]); // set the path for key to send
        std::cout << "PUBLICKEYPATH SENDING: " << PublicKeyPath << std::endl;
        Send::SendMessage(clientSocket, PublicKeyPath); // send path so client can get username of client

        std::string KeyContents = clientsKeyContents[0];

        if (KeyContents.empty())
            return;

        std::cout << fmt::format("Key contents sending to client {}: {}", clientUsernames[clientSendIndex], KeyContents) << std::endl;
        Send::SendMessage(clientSocket, KeyContents); // send the encoded key
    }

    static void SendMessage(SSL *socket, const std::string &message)
    { // send the full message without missing bytes
        try
        {
            unsigned long int totalBytesWritten = 0;
            while (totalBytesWritten < message.length())
            {
                int bytesWritten = SSL_write(socket, message.c_str() + totalBytesWritten, message.length() - totalBytesWritten);

                if (bytesWritten > 0)
                {
                    totalBytesWritten += bytesWritten;
                }
                else
                {
                    int errorCode = SSL_get_error(socket, bytesWritten);
                    std::cout << "Error occured during sending in SendMessage. SSL error: " << errorCode << std::endl;
                    break;
                }
            }
            return;
        }
        catch (const std::exception &e)
        {
            std::cout << "Exception caught in SendMessage: " << e.what() << std::endl;
        }
    }
    static void BroadcastMessage(SSL *senderSocket, const std::string &message)
    {
        for (SSL *socket : SSLsocks)
        {
            if (socket != senderSocket)
            {
                std::cout << "Sending message to tls sock [" << socket << "]" << std::endl;
                SendMessage(socket, message);
            }
        }
    }
    static void BroadcastEncryptedExitMessage(unsigned int &ClientIndex, int ClientToSendMsgIndex)
    {
        std::cout << "Broadcasting exit message of user " << clientUsernames[ClientIndex] << "to " << clientUsernames[ClientToSendMsgIndex] << std::endl;
        std::string UserExitMessage = fmt::format("{} has left the chat", clientUsernames[ClientIndex]);
        EVP_PKEY *LoadedUserPublicKey = LoadKey::LoadPublicKey(PublicPath(clientUsernames[ClientToSendMsgIndex])); // load other user public key
        if (!LoadedUserPublicKey)
        {
            std::cout << fmt::format("User [{}] pub key cannot be loaded for encrypted exit message", clientUsernames[ClientToSendMsgIndex]) << std::endl;
            CleanUp::CleanUpClient(ClientToSendMsgIndex);
            return;
        }

        std::string EncryptedExitMessage = Encrypt::EncryptData(LoadedUserPublicKey, UserExitMessage);
        EncryptedExitMessage = Encode::Base64Encode(EncryptedExitMessage);

        if (EncryptedExitMessage != "err" && !EncryptedExitMessage.empty())
        {
            std::cout << fmt::format("Broadcasting user [{}]'s exit message", clientUsernames[ClientIndex]) << std::endl;
            Send::BroadcastMessage(SSLsocks[ClientToSendMsgIndex], EncryptedExitMessage);
        }

        EVP_PKEY_free(LoadedUserPublicKey);
    }
};

struct Receive
{
    Receive() = default;
    static std::string ReceiveMessageSSL(SSL *socket)
    {
        try
        {
            char buffer[2048] = {0};
            ssize_t bytes = SSL_read(socket, buffer, sizeof(buffer) - 1);
            buffer[bytes] = '\0';
            std::string msg(buffer);

            if (bytes > 0)
            {
                return msg;
            }
            else
            {
                int error = SSL_get_error(socket, bytes);
                std::cout << "Error occured during reading in receiveMessage. SSL error: " << error << std::endl;
            }
        }
        catch (const std::exception &e)
        {
            std::cout << "Exception caught in receiveMessage: " << e.what() << std::endl;
        }
        return "";
    }
    static std::string ReceiveMessageTcp(int &clientTcpsocket)
    {
        try
        {
            char buffer[2048] = {0};
            ssize_t bytes = recv(clientTcpsocket, buffer, sizeof(buffer) - 1, 0);
            buffer[bytes] = '\0';
            std::string msg(buffer);

            if (bytes > 0)
                return msg;
        }
        catch (const std::exception &e)
        {
            std::cout << "Exception caught in ReceiveMessageTcp: " << e.what() << std::endl;
        }
        return "";
    }
};
