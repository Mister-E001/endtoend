#ifndef HTTPGET
#define HTTPGET

#pragma once

#include <iostream>
#include <atomic>

std::string hash_data(const std::string &pt);
int fetchAndSave(const std::string &site, const std::string &outfile);
void pingServer(const char *host, unsigned short port, std::atomic<bool> &running, unsigned int update_secs);

#endif