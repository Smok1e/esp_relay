#pragma once

#include <cerrno>
#include <cstring>

#include "esp_netif.h"
#include "esp_log.h"

//=======================================================

// Parse IPv4 address from string to esp_ip4_addr sturcture
esp_err_t StrToIP4Addr(const char* str, esp_ip4_addr* addr);

// Check socket operation status
int NetCheck(int status, const char* tag, const char* expr);

#define NET_CHECK(expr) NetCheck(expr, TAG, #expr)

//=======================================================