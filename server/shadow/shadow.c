/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 *
 * Copyright 2014 Marc-Andre Moreau <marcandre.moreau@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <winpr/crt.h>
#include <winpr/cmdline.h>

#include <freerdp/version.h>

#ifndef _WIN32
#include <sys/select.h>
#include <sys/signal.h>
#endif

#include "shadow.h"

#ifdef WITH_X11
#define WITH_SHADOW_X11
#endif

#ifdef WITH_SHADOW_X11
extern rdpShadowSubsystem* X11_ShadowCreateSubsystem(rdpShadowServer* server);
#endif

static COMMAND_LINE_ARGUMENT_A shadow_args[] =
{
	{ "port", COMMAND_LINE_VALUE_REQUIRED, "<number>", NULL, NULL, -1, NULL, "Server port" },
	{ "monitors", COMMAND_LINE_VALUE_OPTIONAL, "<0,1,2...>", NULL, NULL, -1, NULL, "Select or list monitors" },
	{ "version", COMMAND_LINE_VALUE_FLAG | COMMAND_LINE_PRINT_VERSION, NULL, NULL, NULL, -1, NULL, "Print version" },
	{ "help", COMMAND_LINE_VALUE_FLAG | COMMAND_LINE_PRINT_HELP, NULL, NULL, NULL, -1, "?", "Print help" },
	{ NULL, 0, NULL, NULL, NULL, -1, NULL, NULL }
};

int shadow_server_print_command_line_help(int argc, char** argv)
{
	char* str;
	int length;
	COMMAND_LINE_ARGUMENT_A* arg;

	printf("Usage: %s [options]\n", argv[0]);
	printf("\n");

	printf("Syntax:\n");
	printf("    /flag (enables flag)\n");
	printf("    /option:<value> (specifies option with value)\n");
	printf("    +toggle -toggle (enables or disables toggle, where '/' is a synonym of '+')\n");
	printf("\n");

	arg = shadow_args;

	do
	{
		if (arg->Flags & COMMAND_LINE_VALUE_FLAG)
		{
			printf("    %s", "/");
			printf("%-20s", arg->Name);
			printf("\t%s\n", arg->Text);
		}
		else if ((arg->Flags & COMMAND_LINE_VALUE_REQUIRED) || (arg->Flags & COMMAND_LINE_VALUE_OPTIONAL))
		{
			printf("    %s", "/");

			if (arg->Format)
			{
				length = (int) (strlen(arg->Name) + strlen(arg->Format) + 2);
				str = (char*) malloc(length + 1);
				sprintf_s(str, length + 1, "%s:%s", arg->Name, arg->Format);
				printf("%-20s", str);
				free(str);
			}
			else
			{
				printf("%-20s", arg->Name);
			}

			printf("\t%s\n", arg->Text);
		}
		else if (arg->Flags & COMMAND_LINE_VALUE_BOOL)
		{
			length = (int) strlen(arg->Name) + 32;
			str = (char*) malloc(length + 1);
			sprintf_s(str, length + 1, "%s (default:%s)", arg->Name,
					arg->Default ? "on" : "off");

			printf("    %s", arg->Default ? "-" : "+");

			printf("%-20s", str);
			free(str);

			printf("\t%s\n", arg->Text);
		}
	}
	while ((arg = CommandLineFindNextArgumentA(arg)) != NULL);

	return 1;
}

int shadow_server_command_line_status_print(rdpShadowServer* server, int argc, char** argv, int status)
{
	if (status == COMMAND_LINE_STATUS_PRINT_VERSION)
	{
		printf("FreeRDP version %s (git %s)\n", FREERDP_VERSION_FULL, GIT_REVISION);
		return COMMAND_LINE_STATUS_PRINT_VERSION;
	}
	else if (status == COMMAND_LINE_STATUS_PRINT)
	{
		return COMMAND_LINE_STATUS_PRINT;
	}
	else if (status < 0)
	{
		shadow_server_print_command_line_help(argc, argv);
		return COMMAND_LINE_STATUS_PRINT_HELP;
	}

	return 1;
}

int shadow_server_parse_command_line(rdpShadowServer* server, int argc, char** argv)
{
	int status;
	DWORD flags;
	COMMAND_LINE_ARGUMENT_A* arg;

	if (argc < 2)
		return 1;

	CommandLineClearArgumentsA(shadow_args);

	flags = COMMAND_LINE_SEPARATOR_COLON;
	flags |= COMMAND_LINE_SIGIL_SLASH | COMMAND_LINE_SIGIL_PLUS_MINUS;

	status = CommandLineParseArgumentsA(argc, (const char**) argv, shadow_args, flags, server, NULL, NULL);

	if (status < 0)
		return status;

	arg = shadow_args;

	do
	{
		if (!(arg->Flags & COMMAND_LINE_ARGUMENT_PRESENT))
			continue;

		CommandLineSwitchStart(arg)

		CommandLineSwitchCase(arg, "port")
		{
			server->port = (DWORD) atoi(arg->Value);
		}
		CommandLineSwitchDefault(arg)
		{
		}

		CommandLineSwitchEnd(arg)
	}
	while ((arg = CommandLineFindNextArgumentA(arg)) != NULL);

	arg = CommandLineFindArgumentA(shadow_args, "monitors");

	if (arg)
	{
		if (arg->Flags & COMMAND_LINE_VALUE_PRESENT)
		{
			/* Select monitors */
		}
		else
		{
			int index;
			int width, height;
			MONITOR_DEF* monitor;
			rdpShadowSubsystem* subsystem = server->subsystem;

			/* List monitors */

			for (index = 0; index < subsystem->monitorCount; index++)
			{
				monitor = &(subsystem->monitors[index]);

				width = monitor->right - monitor->left;
				height = monitor->bottom - monitor->top;

				printf("      %s [%d] %dx%d\t+%d+%d\n",
					(monitor->flags == 1) ? "*" : " ", index,
					width, height, monitor->left, monitor->top);
			}

			status = COMMAND_LINE_STATUS_PRINT;
		}
	}

	return status;
}

void* shadow_server_thread(rdpShadowServer* server)
{
	DWORD status;
	DWORD nCount;
	HANDLE events[32];
	HANDLE StopEvent;
	freerdp_listener* listener;
	rdpShadowSubsystem* subsystem;

	listener = server->listener;
	StopEvent = server->StopEvent;
	subsystem = server->subsystem;

	if (subsystem->Start)
	{
		subsystem->Start(subsystem);
	}

	while (1)
	{
		nCount = 0;

		if (listener->GetEventHandles(listener, events, &nCount) < 0)
		{
			fprintf(stderr, "Failed to get FreeRDP file descriptor\n");
			break;
		}

		events[nCount++] = server->StopEvent;

		status = WaitForMultipleObjects(nCount, events, FALSE, INFINITE);

		if (WaitForSingleObject(server->StopEvent, 0) == WAIT_OBJECT_0)
		{
			break;
		}

		if (!listener->CheckFileDescriptor(listener))
		{
			fprintf(stderr, "Failed to check FreeRDP file descriptor\n");
			break;
		}
	}

	listener->Close(listener);

	if (subsystem->Stop)
	{
		subsystem->Stop(subsystem);
	}

	ExitThread(0);

	return NULL;
}

int shadow_server_start(rdpShadowServer* server)
{
#ifndef _WIN32
	signal(SIGPIPE, SIG_IGN);
#endif

	if (server->listener->Open(server->listener, NULL, server->port))
	{
		server->thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)
				shadow_server_thread, (void*) server, 0, NULL);
	}

	return 0;
}

int shadow_server_stop(rdpShadowServer* server)
{
	if (server->thread)
	{
		SetEvent(server->StopEvent);
		WaitForSingleObject(server->thread, INFINITE);
		CloseHandle(server->thread);
		server->thread = NULL;

		server->listener->Close(server->listener);
	}

	return 0;
}

int shadow_server_init(rdpShadowServer* server)
{
	server->StopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	server->listener = freerdp_listener_new();

	if (!server->listener)
		return -1;

	server->listener->info = (void*) server;
	server->listener->PeerAccepted = shadow_client_accepted;

#ifdef WITH_SHADOW_X11
	server->CreateSubsystem = X11_ShadowCreateSubsystem;
#endif

	if (server->CreateSubsystem)
		server->subsystem = server->CreateSubsystem(server);

	if (!server->subsystem)
		return -1;

	if (server->subsystem->Init)
		server->subsystem->Init(server->subsystem);

	server->screen = shadow_screen_new(server);

	if (!server->screen)
		return -1;

	server->encoder = shadow_encoder_new(server);

	if (!server->encoder)
		return -1;

	return 1;
}

int shadow_server_uninit(rdpShadowServer* server)
{
	shadow_server_stop(server);

	if (server->listener)
	{
		freerdp_listener_free(server->listener);
		server->listener = NULL;
	}

	if (server->encoder)
	{
		shadow_encoder_free(server->encoder);
		server->encoder = NULL;
	}

	if (server->subsystem)
	{
		server->subsystem->Free(server->subsystem);
		server->subsystem = NULL;
	}

	return 1;
}

rdpShadowServer* shadow_server_new()
{
	rdpShadowServer* server;

	server = (rdpShadowServer*) calloc(1, sizeof(rdpShadowServer));

	if (!server)
		return NULL;

	server->port = 3389;

	return server;
}

void shadow_server_free(rdpShadowServer* server)
{
	if (!server)
		return;

	shadow_server_uninit(server);

	free(server);
}

int main(int argc, char* argv[])
{
	int status;
	DWORD dwExitCode;
	rdpShadowServer* server;

	server = shadow_server_new();

	if (!server)
		return 0;

	if (shadow_server_init(server) < 0)
		return 0;

	status = shadow_server_parse_command_line(server, argc, argv);

	status = shadow_server_command_line_status_print(server, argc, argv, status);

	if (status < 0)
		return 0;

	if (shadow_server_start(server) < 0)
		return 0;

	WaitForSingleObject(server->thread, INFINITE);

	GetExitCodeThread(server->thread, &dwExitCode);

	shadow_server_free(server);

	return 0;
}
