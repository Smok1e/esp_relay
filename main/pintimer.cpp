#include "pintimer.hpp"

#include "esp_log.h"

static const char* TAG = "pintimer";

//=======================================================

PinTimer::PinTimer(gpio_num_t pin):
	m_pin(pin)
{
	xTaskCreate(PinTimer::TimerTaskProc, "Relay controller timer", 4096, this, 5, nullptr);
}

//=======================================================

void PinTimer::TimerTaskProc(void *arg)
{
	reinterpret_cast<PinTimer*>(arg)->startTimer();
}

[[noreturn]]
void PinTimer::startTimer()
{
	ESP_LOGI(TAG, "timer task for GPIO %02d started", static_cast<int>(m_pin));

	gpio_reset_pin(m_pin);
	gpio_set_direction(m_pin, GPIO_MODE_OUTPUT);

	setPinState(false);

	while (true)
	{
		if (xTaskGetTickCount() >= m_activate_until)
			setPinState(false);

		vTaskDelay(10 / portTICK_PERIOD_MS);
	}
}

void PinTimer::setPinState(bool active)
{
	if (active != m_pin_state)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		gpio_set_level(m_pin, m_pin_state = active);
	}
}

//=======================================================

void PinTimer::activateFor(TickType_t timeout)
{
	m_activate_until = xTaskGetTickCount() + timeout;
	setPinState(true);
}

//=======================================================