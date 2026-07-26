#pragma once

#include "config.h"

int cmd_init();
int cmd_incidents(const Config &config);
int cmd_logs(const Config &config);
int cmd_stop(const Config &config);
int cmd_clean(const Config &config);
