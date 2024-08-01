#pragma once

#include <mutex>

#include "FreeRTOS/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"

//=======================================================

class PinTimer
{
public:
	PinTimer(gpio_num_t pin);

	void activateFor(TickType_t timeout);

private:
	gpio_num_t m_pin;
	bool       m_pin_state      {false};
	TickType_t m_activate_until {0};

	std::mutex m_mutex {};

	static void TimerTaskProc(void* arg);

	[[noreturn]]
	void startTimer();

	void setPinState(bool active);

};

//=======================================================