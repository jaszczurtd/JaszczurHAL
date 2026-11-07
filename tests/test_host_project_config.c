#include "fixtures/project_config/parent/include_hal.h"

int compiler_project_config_probe(void);
int compiler_project_config_probe(void) {
  return JH_TEST_PROJECT_CONFIG_SELECTED;
}
