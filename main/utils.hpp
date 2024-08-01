#pragma once

#include "esp_netif.h"

//=======================================================

esp_err_t StrToIP4Addr(const char* str, esp_ip4_addr* addr);

//=======================================================