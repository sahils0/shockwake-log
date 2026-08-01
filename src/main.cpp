#include "config.h"
#include "monitor.h"
#include "status_dashboard.h"
#include "subcommands.h"

int main(int argc, char *argv[]) {
  Config config = parse_args(argc, argv);

  switch (config.subcommand) {
  case SubCommand::INIT:
    return cmd_init();
  case SubCommand::STATUS:
    return cmd_status(config);
  case SubCommand::INCIDENTS:
    return cmd_incidents(config);
  case SubCommand::LOGS:
    return cmd_logs(config);
  case SubCommand::STOP:
    return cmd_stop(config);
  case SubCommand::CLEAN:
    return cmd_clean(config);
  case SubCommand::MONITOR:
  default:
    return cmd_monitor(config);
  }
}
