// Simulator stand-in for the GPIO driver surface used by the control API.
#pragma once

#include <stdint.h>

typedef int gpio_num_t;
#define GPIO_NUM_NC ((gpio_num_t)-1)

inline int gpio_input_enable(gpio_num_t) { return 0; }
