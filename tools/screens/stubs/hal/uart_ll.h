#pragma once
#include <cstdint>
typedef struct { int x; } uart_dev_t;
#define UART_LL_GET_HW(n) ((uart_dev_t*)nullptr)
#define UART_LL_FIFO_DEF_LEN 128
inline uint32_t uart_ll_get_txfifo_len(uart_dev_t*){return 128;}
inline void uart_ll_write_txfifo(uart_dev_t*,const uint8_t*,uint32_t){}
