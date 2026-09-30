/*
 *
 *    Copyright (c) 2022-2024 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include "AppCLIBase.h"
#include "AppTaskBase.h"
#include <ChipShellCollection.h>
#include <app/server/Server.h>
#include <cstring>
#include <lib/shell/Engine.h>
#include <platform/CHIPDeviceLayer.h>

#if (CHIP_DEVICE_CONFIG_ENABLE_WPA && CHIP_ENABLE_OPENTHREAD)
#include <platform/OpenThread/GenericThreadStackManagerImpl_OpenThread.h>
#endif

#if CONFIG_CHIP_APP_JF_ADMIN
#include "JFAManager.h"
#endif // CONFIG_CHIP_APP_JF_ADMIN

#define MATTER_CLI_LOG(message) (chip::Shell::streamer_printf(chip::Shell::streamer_get(), message))

static CHIP_ERROR commissioningManager(int argc, char * argv[])
{
    CHIP_ERROR error = CHIP_NO_ERROR;
    if (strncmp(argv[0], "on", 2) == 0)
    {
        chip::NXP::App::GetAppTask().StartCommissioningHandler();
    }
    else if (strncmp(argv[0], "off", 3) == 0)
    {
        chip::NXP::App::GetAppTask().StopCommissioningHandler();
    }
    else
    {
        MATTER_CLI_LOG("wrong args should be either \"mattercommissioning on\" or \"mattercommissioning off\"");
        error = CHIP_ERROR_INVALID_ARGUMENT;
    }
    return error;
}

static CHIP_ERROR cliFactoryReset(int argc, char * argv[])
{
    chip::NXP::App::GetAppTask().FactoryResetHandler();
    return CHIP_NO_ERROR;
}

static CHIP_ERROR cliReset(int argc, char * argv[])
{
    chip::NXP::App::GetAppCLI().ResetCmdHandle();
    return CHIP_NO_ERROR;
}

#if CONFIG_CHIP_APP_JF_ADMIN
// Dispatch client cluster commands to peer nodes on the joint fabric.
//
// Subcommands:
//   control onoff [on|off|toggle] <nodeId-hex> <endpointId>
//   control fan [on|off] <nodeId-hex> <endpointId>
//   control contact read <nodeId-hex> <endpointId>
//   control level <level 0-254> <transitionTime 1/10s> <nodeId-hex> <endpointId>
static CHIP_ERROR cliController(int argc, char * argv[])
{
    if (argc < 1)
    {
        MATTER_CLI_LOG("usage: control <subcommand> ...\r\n"
                       "  onoff [on|off|toggle] <nodeId-hex> <endpointId>\r\n"
                       "  fan [on|off] <nodeId-hex> <endpointId>\r\n"
                       "  contact read <nodeId-hex> <endpointId>\r\n"
                       "  level <level 0-254> <transitionTime 1/10s> <nodeId-hex> <endpointId>\r\n");
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    if (strncmp(argv[0], "onoff", 5) == 0)
    {
        if (argc < 4)
        {
            MATTER_CLI_LOG("usage: control onoff [on|off|toggle] <nodeId-hex> <endpointId>\r\n");
            return CHIP_ERROR_INVALID_ARGUMENT;
        }
        chip::NodeId nodeId = static_cast<chip::NodeId>(strtoull(argv[2], nullptr, 16));
        chip::EndpointId ep = static_cast<chip::EndpointId>(atoi(argv[3]));
        if (strncmp(argv[1], "toggle", 6) == 0)
        {
            return chip::JFAMgr().SendOnOffToggle(nodeId, ep);
        }
        bool on = (strncmp(argv[1], "on", 2) == 0);
        return chip::JFAMgr().SendOnOff(nodeId, ep, on);
    }

    if (strncmp(argv[0], "fan", 3) == 0)
    {
        if (argc < 4)
        {
            MATTER_CLI_LOG("usage: control fan [on|off] <nodeId-hex> <endpointId>\r\n");
            return CHIP_ERROR_INVALID_ARGUMENT;
        }
        bool on             = (strncmp(argv[1], "on", 2) == 0);
        chip::NodeId nodeId = static_cast<chip::NodeId>(strtoull(argv[2], nullptr, 16));
        chip::EndpointId ep = static_cast<chip::EndpointId>(atoi(argv[3]));
        return chip::JFAMgr().SendFanOnOff(nodeId, ep, on);
    }

    if (strncmp(argv[0], "contact", 7) == 0)
    {
        if (argc < 4)
        {
            MATTER_CLI_LOG("usage: control contact read <nodeId-hex> <endpointId>\r\n");
            return CHIP_ERROR_INVALID_ARGUMENT;
        }
        chip::NodeId nodeId = static_cast<chip::NodeId>(strtoull(argv[2], nullptr, 16));
        chip::EndpointId ep = static_cast<chip::EndpointId>(atoi(argv[3]));
        return chip::JFAMgr().ReadContactState(nodeId, ep);
    }

    if (strncmp(argv[0], "level", 5) == 0)
    {
        if (argc < 5)
        {
            MATTER_CLI_LOG("usage: control level <level 0-254> <transitionTime 1/10s> <nodeId-hex> <endpointId>\r\n");
            return CHIP_ERROR_INVALID_ARGUMENT;
        }
        unsigned long levelArg          = strtoul(argv[1], nullptr, 10);
        unsigned long transitionTimeArg = strtoul(argv[2], nullptr, 10);
        if (levelArg > UINT8_MAX || transitionTimeArg > UINT16_MAX)
        {
            MATTER_CLI_LOG("level must fit in uint8 (0-254) and transitionTime must fit in uint16\r\n");
            return CHIP_ERROR_INVALID_ARGUMENT;
        }
        uint8_t level           = static_cast<uint8_t>(levelArg);
        uint16_t transitionTime = static_cast<uint16_t>(transitionTimeArg);
        chip::NodeId nodeId     = static_cast<chip::NodeId>(strtoull(argv[3], nullptr, 16));
        chip::EndpointId ep     = static_cast<chip::EndpointId>(atoi(argv[4]));
        return chip::JFAMgr().SendLevelControlMoveToLevel(nodeId, ep, level, transitionTime);
    }

    MATTER_CLI_LOG("unknown subcommand. Use onoff, fan, contact, or level\r\n");
    return CHIP_ERROR_INVALID_ARGUMENT;
}
#endif // CONFIG_CHIP_APP_JF_ADMIN

void chip::NXP::App::AppCLIBase::RegisterDefaultCommands(void)
{
    static const chip::Shell::shell_command_t kCommands[] = {
        {
            .cmd_func = commissioningManager,
            .cmd_name = "mattercommissioning",
            .cmd_help = "Open/close the commissioning window. Usage : mattercommissioning [on|off]",
        },
        {
            .cmd_func = cliFactoryReset,
            .cmd_name = "matterfactoryreset",
            .cmd_help = "Perform a factory reset on the device",
        },
        {
            .cmd_func = cliReset,
            .cmd_name = "matterreset",
            .cmd_help = "Reset the device",
        }
    };

    /* Register common shell commands */
    cmd_misc_init();
    cmd_otcli_init();
#if CHIP_SHELL_ENABLE_CMD_SERVER
    cmd_app_server_init();
#endif /* CHIP_SHELL_ENABLE_CMD_SERVER */
    chip::Shell::Engine::Root().RegisterCommands(kCommands, sizeof(kCommands) / sizeof(kCommands[0]));

#if CONFIG_CHIP_APP_JF_ADMIN
    static const chip::Shell::shell_command_t kJFAClientCommands[] = {
        {
            .cmd_func = cliController,
            .cmd_name = "control",
            .cmd_help = "Send client cluster commands to peer nodes. "
                        "Subcommands: onoff [on|off|toggle] <nodeId-hex> <ep> | "
                        "fan [on|off] <nodeId-hex> <ep> | "
                        "contact read <nodeId-hex> <ep> | "
                        "level <level 0-254> <transitionTime 1/10s> <nodeId-hex> <ep>",
        },
    };
    chip::Shell::Engine::Root().RegisterCommands(kJFAClientCommands, sizeof(kJFAClientCommands) / sizeof(kJFAClientCommands[0]));
#endif // CONFIG_CHIP_APP_JF_ADMIN
}
