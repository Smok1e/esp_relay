#include <cstdlib>

#include "utils.hpp"

//=======================================================

esp_err_t StrToIP4Addr(const char* str, esp_ip4_addr* addr)
{
	for (size_t i = 0; i < 4; i++)
	{
		char* end = nullptr;
		unsigned byte = strtoul(str, &end, 10);

		if (!end || byte > 0xFF)
			return ESP_FAIL;

		reinterpret_cast<uint8_t*>(&addr->addr)[i] = byte;
		str = end + 1;
	}

	return ESP_OK;
}

//=======================================================