#include <array>
#include <mutex>

#include "FreeRTOS/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"

#include "utils.hpp"
#include "pintimer.hpp"

static const char* TAG = "main";

#define WIFI_CONNECTED_BIT BIT0
#define MAX_IP4_LEN        16

//=======================================================

int NetCheck(int status, const char* expr)
{
	if (status < 0)
	{
		ESP_LOGE(TAG, "%s failed with errno %d: %s; Aborting", expr, errno, strerror(errno));
		abort();
	}

	return status;
}

#define __net_check(expr) NetCheck(expr, #expr)

//=======================================================

class Main
{
public:
	Main() = default;

	void run();

private:
	PinTimer m_relay_timer { static_cast<gpio_num_t>(CONFIG_GPIO_RELAY_PIN) };
	PinTimer m_led_timer   { static_cast<gpio_num_t>(CONFIG_GPIO_LED_PIN)   };

	[[noreturn]]
	void startTcpServer();

	static void ServeClientTaskProc(void* arg);
	void serveClient(int client);
	void onDataReceived(int client, const char* data, size_t len);

	static void WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
	void onWifiEvent(int32_t event_id, void* event_data);
	void onIpEvent(int32_t event_id, void* event_data);

	void initNVS();
	void initWifi();

};

//======================================================= Server

[[noreturn]]
void Main::startTcpServer()
{
	int server = __net_check(socket(AF_INET, SOCK_STREAM, 0));

	// Binding to preferred port
	sockaddr_in server_addr = {};
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(CONFIG_SERVER_PORT);
	__net_check(bind(server, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)));

	// Allow up to 5 pending connections
	__net_check(listen(server, 5));

	ESP_LOGI(TAG, "listening for connections port %d", CONFIG_SERVER_PORT);
	while (true)
	{
		int client = __net_check(accept(server, nullptr, nullptr));

		std::pair<Main*, int> info(this, client);
		xTaskCreate(Main::ServeClientTaskProc, "Client server", 4096, &info, 5, nullptr);
	}
}

void Main::ServeClientTaskProc(void *arg)
{
	auto& info = *reinterpret_cast<std::pair<Main*, int>*>(arg);
	info.first->serveClient(info.second);
}

void Main::serveClient(int client)
{
	sockaddr_in client_addr = {};
	socklen_t client_addr_len = sizeof(client_addr);
	getpeername(client, reinterpret_cast<sockaddr*>(&client_addr), &client_addr_len);

	char ip[MAX_IP4_LEN] = "";
	inet_ntop(AF_INET, &client_addr.sin_addr, ip, std::size(ip));

	printf("[%s]: Client connected\n", ip);

	char buffer[1024] = "";
	ssize_t len;
	while ((len = recv(client, buffer, std::size(buffer), 0)) > 0)
		onDataReceived(client, buffer, len);

	if (!len)
		printf("[%s]: Client disconnected\n", ip);

	else
		printf("[%s]: Connection lost: %s\n", ip, strerror(errno));

	vTaskDelete(nullptr);
}

void Main::onDataReceived(int client, const char* data, size_t len)
{
	if (len != sizeof(uint32_t))
	{
		ESP_LOGW(TAG, "received %zu bytes instead of %zu", len, sizeof(uint32_t));
		return;
	}

	// Just interpreting received data as 32 bit unsigned
	const auto& timeout_ms = *reinterpret_cast<const uint32_t*>(data);
	m_relay_timer.activateFor(timeout_ms / portTICK_PERIOD_MS);

	// Blink builtin led for 100 ms to indicate that we have received request
	m_led_timer.activateFor(100 / portTICK_PERIOD_MS);
}

//======================================================= Wifi

void Main::initNVS()
{
	auto result = nvs_flash_init();
	if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
		ESP_ERROR_CHECK(nvs_flash_erase());
		result = nvs_flash_init();
	}
	ESP_ERROR_CHECK(result);

	ESP_LOGI(TAG, "nvs initialized");
}

void Main::WifiEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
	Main* instance = reinterpret_cast<Main*>(arg);

	if      (event_base == WIFI_EVENT) instance->onWifiEvent(event_id, event_data);
	else if (event_base == IP_EVENT  ) instance->onIpEvent  (event_id, event_data);
}

void Main::onWifiEvent(int32_t event_id, void *event_data)
{
	switch (event_id)
	{
		case WIFI_EVENT_STA_START:
			esp_wifi_connect();
			ESP_LOGI(TAG, "connecting to %s...", CONFIG_WIFI_SSID);
			break;

		case WIFI_EVENT_STA_DISCONNECTED:
			esp_wifi_connect();
			ESP_LOGI(TAG, "reconnecting to %s...", CONFIG_WIFI_SSID);
			break;
	}
}

void Main::onIpEvent(int32_t event_id, void *event_data)
{
	switch (event_id)
	{
		case IP_EVENT_STA_GOT_IP:
		{
			auto* event = reinterpret_cast<ip_event_got_ip_t*>(event_data);
			ESP_LOGI(TAG, "successfully connected to the AP; got ip: " IPSTR, IP2STR(&event->ip_info.ip));
			break;
		}
	}
}

void Main::initWifi()
{
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	auto* netif = esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	esp_event_handler_instance_t instance_any_id, instance_got_ip;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(
		WIFI_EVENT,
		ESP_EVENT_ANY_ID,
		&Main::WifiEventHandler,
		this,
		&instance_any_id
	));

	ESP_ERROR_CHECK(esp_event_handler_instance_register(
		IP_EVENT,
		IP_EVENT_STA_GOT_IP,
		&Main::WifiEventHandler,
		this,
		&instance_got_ip
	));

	wifi_config_t wifi_config = {};
	wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

	strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid    ), CONFIG_WIFI_SSID,     sizeof(wifi_config.sta.ssid    ));
	strncpy(reinterpret_cast<char*>(wifi_config.sta.password), CONFIG_WIFI_PASSWORD, sizeof(wifi_config.sta.password));

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());

	ESP_LOGI(TAG, "wifi station initialized");

#if	(CONFIG_WIFI_DISABLE_DHCP)
	// Disabling DHCP and setting preferred ip address
	ESP_LOGI(TAG, "stopping DHCP client");

	esp_netif_ip_info_t ip_info = {};
	ESP_ERROR_CHECK(StrToIP4Addr(CONFIG_WIFI_IP,      &ip_info.ip     ));
	ESP_ERROR_CHECK(StrToIP4Addr(CONFIG_WIFI_GATEWAY, &ip_info.gw     ));
	ESP_ERROR_CHECK(StrToIP4Addr(CONFIG_WIFI_NETMASK, &ip_info.netmask));

	ESP_ERROR_CHECK(esp_netif_dhcpc_stop(netif));
	ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip_info));
#endif
}

//=======================================================

void Main::run()
{
	initNVS();
	initWifi();

	startTcpServer();
}

//=======================================================

extern "C" void app_main()
{
	Main instance;
	instance.run();
}

//=======================================================