#include "logs.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"


void log_error_code(const char* function_name, uint32_t error_code){
	if(error_code == NRF_SUCCESS){
		NRF_LOG_INFO("%s: %s", function_name, nrf_strerror_get(error_code));
	}
	else{
		NRF_LOG_ERROR("%s: %s", function_name, nrf_strerror_get(error_code));
	}
}

void log_init(){
    ret_code_t err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);
    NRF_LOG_DEFAULT_BACKENDS_INIT();
}
