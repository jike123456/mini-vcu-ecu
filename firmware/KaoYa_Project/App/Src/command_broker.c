#include "command_broker.h"

#include "FreeRTOS.h"
#include "task.h"

static CmdVel_t s_latest_command;

void CommandBroker_Publish(const CmdVel_t *command)
{
    if (command == NULL) {
        return;
    }

    taskENTER_CRITICAL();
    s_latest_command = *command;
    taskEXIT_CRITICAL();
}

CmdVel_t CommandBroker_GetLatest(void)
{
    CmdVel_t command;

    taskENTER_CRITICAL();
    command = s_latest_command;
    taskEXIT_CRITICAL();

    return command;
}
