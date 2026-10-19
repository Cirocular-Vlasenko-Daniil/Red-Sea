#pragma once
typedef void* esp_timer_handle_t;
struct esp_timer_create_args_t { void(*callback)(void*)=nullptr; void* arg=nullptr; int dispatch_method=0; const char* name=nullptr; bool skip_unhandled_events=false; };
inline int esp_timer_create(const esp_timer_create_args_t*,esp_timer_handle_t*h){*h=nullptr;return 0;}
inline int esp_timer_start_periodic(esp_timer_handle_t,uint64_t){return 0;}
inline int esp_timer_start_once(esp_timer_handle_t,uint64_t){return 0;}
inline int esp_timer_stop(esp_timer_handle_t){return 0;}
