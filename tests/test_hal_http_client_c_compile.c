#include "hal/network/http/hal_http_client.h"

int main(void) {
  hal_http_client_request_t request;
  hal_http_client_response_t response = {0};
  return hal_http_client_request_init(&request) == HAL_OK &&
                 response.status_code == 0u
             ? 0
             : 1;
}
