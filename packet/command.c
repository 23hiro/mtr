/*
    mtr  --  a network diagnostic tool
    Copyright (C) 2016  Matt Kimball

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2 as
    published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "command.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmdparse.h"
#include "platform.h"
#include "config.h"

/*
    Find a parameter with a particular name in a command_t structure.
    If no such parameter exists, return NULL.
*/
static
const char *find_parameter(
    const struct command_t *command,
    const char *name_request)
{
    const char *name;
    const char *value;
    int i;

    for (i = 0; i < command->argument_count; i++) {
        name = command->argument_name[i];
        value = command->argument_value[i];

        if (!strcmp(name, name_request)) {
            return value;
        }
    }

    return NULL;
}

/*  Given a feature name, return a string for the check-support reply  */
static
const char *check_support(
    const char *feature)
{
    if (!strcmp(feature, "version")) {
        return PACKAGE_VERSION;
    }

    if (!strcmp(feature, "ip-4")) {
        return "ok";
    }

    if (!strcmp(feature, "ip-6")) {
        return "ok";
    }

    if (!strcmp(feature, "send-probe")) {
        return "ok";
    }

    return "no";
}

/*  Handle a check-support request by checking for a particular feature  */
static
void check_support_command(
    const struct command_t *command)
{
    const char *feature;
    const char *support;

    feature = find_parameter(command, "feature");
    if (feature == NULL) {
        printf("%d invalid-argument\n", command->token);
        return;
    }

    support = check_support(feature);
    printf("%d feature-support support %s\n", command->token, support);
}

/*
    If a named send_probe argument is recognized, fill in the probe paramters
    structure with the argument value.
*/
static
bool decode_probe_argument(
    struct probe_param_t *param,
    const char *name,
    const char *value)
{
    char *endstr = NULL;

    /*  Pass IPv4 addresses as string values  */
    if (!strcmp(name, "ip-4")) {
        param->ip_version = 4;
        param->address = value;
    }

    /*  IPv6 address  */
    if (!strcmp(name, "ip-6")) {
        param->ip_version = 6;
        param->address = value;
    }

    /*  Time-to-live values  */
    if (!strcmp(name, "ttl")) {
        param->ttl = strtol(value, &endstr, 10);
        if (*endstr != 0) {
            return false;
        }
    }

    /*  Number of seconds to wait for a reply  */
    if (!strcmp(name, "timeout")) {
        param->timeout = strtol(value, &endstr, 10);
        if (*endstr != 0) {
            return false;
        }
    }

    return true;
}

/*  Handle "send-probe" commands  */
static
void send_probe_command(
    const struct command_t *command,
    struct net_state_t *net_state)
{
    struct probe_param_t param;
    int i;
    char *name;
    char *value;

    /*  We will prepare a probe_param_t for send_probe.  */
    memset(&param, 0, sizeof(struct probe_param_t));
    param.command_token = command->token;
    param.ttl = 255;
    param.timeout = 10;

    for (i = 0; i < command->argument_count; i++) {
        name = command->argument_name[i];
        value = command->argument_value[i];

        if (!decode_probe_argument(&param, name, value)) {
            printf("%d invalid-argument\n", command->token);
            return;
        }
    }

    /*  Send the probe using a platform specific mechanism  */
    send_probe(net_state, &param);
}

/*
    Given a parsed command, dispatch to the handler for specific
    command requests.
*/
static
void dispatch_command(
    const struct command_t *command,
    struct net_state_t *net_state)
{
    if (!strcmp(command->command_name, "check-support")) {
        check_support_command(command);
    } else if (!strcmp(command->command_name, "send-probe")) {
        send_probe_command(command, net_state);
    } else {
        /*  For unrecognized commands, respond with an error  */
        printf("%d unknown-command\n", command->token);
    }
}

/*
    With newly read data in our command buffer, dispatch all completed
    command requests.
*/
void dispatch_buffer_commands(
    struct command_buffer_t *buffer,
    struct net_state_t *net_state)
{
    struct command_t command;
    char *end_of_command;
    char full_command[COMMAND_BUFFER_SIZE];
    int command_length;
    int remaining_count;

    while (true) {
        assert(buffer->incoming_read_position < COMMAND_BUFFER_SIZE);

        /*  Terminate the buffer string  */
        buffer->incoming_buffer[buffer->incoming_read_position] = 0;

        /*  Find the next newline, which terminates command requests  */
        end_of_command = index(buffer->incoming_buffer, '\n');
        if (end_of_command == NULL) {
            /*
                No newlines found, so any data we've read so far is
                not yet complete.
            */
            break;
        }

        command_length = end_of_command - buffer->incoming_buffer;
        remaining_count = buffer->incoming_read_position - command_length - 1;

        /*  Copy the completed command  */
        memmove(full_command, buffer->incoming_buffer, command_length);
        full_command[command_length] = 0;

        /*
            Free the space used by the completed command by advancing the
            remaining requests within the buffer.
        */
        memmove(buffer->incoming_buffer, end_of_command + 1, remaining_count);
        buffer->incoming_read_position -= command_length + 1;

        if (parse_command(&command, full_command)) {
            /*  If the command fails to parse, respond with an error  */
            printf("0 command-parse-error\n");
        } else {
            dispatch_command(&command, net_state);
        }
    }

    if (buffer->incoming_read_position >= COMMAND_BUFFER_SIZE - 1) {
        /*
            If we've filled the buffer without a complete command, the
            only thing we can do is discard what we've read and hope that 
            new data is better formatted.
        */
        printf("0 command-buffer-overflow\n");
        buffer->incoming_read_position = 0;
    }
}
