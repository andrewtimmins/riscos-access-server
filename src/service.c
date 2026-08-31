/*
  ShareFS Server - Windows Service Integration

  Copyright (C) 2025-2026 Andy Timmins

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifdef _WIN32

#include "service.h"

#include "config.h"
#include "handle.h"
#include "log.h"
#include "net.h"
#include "paths.h"
#include "platform.h"
#include "printer.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

// Global service state
static SERVICE_STATUS g_service_status = {0};
static SERVICE_STATUS_HANDLE g_status_handle = NULL;
static HANDLE g_stop_event = NULL;
static HANDLE g_server_thread = NULL;

// Server state
static sfs_config g_config;
static sfs_net g_net;
static sfs_handle_table g_handles;

// Server thread function
static DWORD WINAPI server_thread_func(LPVOID param) {
  (void)param;
  sfs_server_run(&g_config, &g_net, &g_handles);
  return 0;
}

// Report service status to SCM
static void report_service_status(DWORD current_state, DWORD exit_code,
                                  DWORD wait_hint) {
  static DWORD checkpoint = 1;

  g_service_status.dwCurrentState = current_state;
  g_service_status.dwWin32ExitCode = exit_code;
  g_service_status.dwWaitHint = wait_hint;

  if (current_state == SERVICE_START_PENDING) {
    g_service_status.dwControlsAccepted = 0;
  } else {
    g_service_status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
  }

  if ((current_state == SERVICE_RUNNING) ||
      (current_state == SERVICE_STOPPED)) {
    g_service_status.dwCheckPoint = 0;
  } else {
    g_service_status.dwCheckPoint = checkpoint++;
  }

  SetServiceStatus(g_status_handle, &g_service_status);
}

// Service control handler
static DWORD WINAPI service_ctrl_handler(DWORD ctrl_code, DWORD event_type,
                                         LPVOID event_data, LPVOID context) {
  (void)event_type;
  (void)event_data;
  (void)context;

  switch (ctrl_code) {
  case SERVICE_CONTROL_STOP:
    report_service_status(SERVICE_STOP_PENDING, NO_ERROR, 3000);
    SetEvent(g_stop_event);
    return NO_ERROR;

  case SERVICE_CONTROL_INTERROGATE:
    return NO_ERROR;

  default:
    return ERROR_CALL_NOT_IMPLEMENTED;
  }
}

// Service main function (called by SCM)
static VOID WINAPI service_main(DWORD argc, LPTSTR *argv) {
  (void)argc;
  (void)argv;

  // Register control handler
  g_status_handle =
      RegisterServiceCtrlHandlerEx(SFS_SERVICE_NAME, service_ctrl_handler, NULL);
  if (!g_status_handle) {
    return;
  }

  // Initialize service status
  g_service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  g_service_status.dwServiceSpecificExitCode = 0;
  report_service_status(SERVICE_START_PENDING, NO_ERROR, 3000);

  // Create stop event
  g_stop_event = CreateEvent(NULL, TRUE, FALSE, NULL);
  if (!g_stop_event) {
    report_service_status(SERVICE_STOPPED, GetLastError(), 0);
    return;
  }

  // The same search the GUI and the command line use; see src/paths.h. This
  // used to be a private list in this file, in a different order from the one
  // in main.c, so the service could read a different file from the one the
  // GUI was editing.
  char config_path[SFS_PATH_MAX];
  int created_config = 0;
  if (sfs_paths_find_config(config_path, sizeof(config_path)) != 0) {
    // Nothing to find, which happens when the service is installed by hand
    // rather than by the installer. Write a working configuration instead of
    // refusing to start, as `sharefs serve` does; the service runs as
    // LocalSystem, so the default location is the machine-wide one.
    if (sfs_paths_default_config(config_path, sizeof(config_path)) != 0 ||
        sfs_paths_write_default_config(config_path, NULL, NULL, NULL, 0) != 0) {
      report_service_status(SERVICE_STOPPED, ERROR_FILE_NOT_FOUND, 0);
      CloseHandle(g_stop_event);
      return;
    }
    created_config = 1;
  }

  // Initialize platform
  if (sfs_platform_init() != 0) {
    report_service_status(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 0);
    CloseHandle(g_stop_event);
    return;
  }

  // Load and validate config before opening the log, because the config is
  // what says where the log goes. Opening it first meant log_file was ignored
  // for the service but honoured for the server, which is the sort of
  // difference nobody finds until they go looking for the log.
  if (sfs_config_load(config_path, &g_config) != 0) {
    sfs_platform_shutdown();
    report_service_status(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 0);
    CloseHandle(g_stop_event);
    return;
  }

  if (sfs_config_validate(&g_config) != 0) {
    sfs_config_unload(&g_config);
    sfs_platform_shutdown();
    report_service_status(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 0);
    CloseHandle(g_stop_event);
    return;
  }

  sfs_log_set_path(g_config.server.log_file);
  sfs_log_init();
  sfs_log_set_level(sfs_log_level_from_string(g_config.server.log_level));
  sfs_log(SFS_LOG_INFO, "ShareFS %s starting as a service", SHAREFS_VERSION);
  sfs_log(SFS_LOG_INFO, "Configuration: %s", config_path);
  if (created_config)
    sfs_log(SFS_LOG_INFO, "First run: that file has just been created");

  // Open network
  if (g_config.server.bind_ip) {
    sfs_log(SFS_LOG_INFO, "Binding to specific address: %s",
            g_config.server.bind_ip);
  }
  if (sfs_net_open(&g_net, g_config.server.bind_ip) != 0) {
    sfs_log(SFS_LOG_ERROR, "Failed to open network sockets");
    sfs_config_unload(&g_config);
    sfs_log_shutdown();
    sfs_platform_shutdown();
    report_service_status(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 0);
    CloseHandle(g_stop_event);
    return;
  }

  // Initialize handle table
  sfs_handles_init(&g_handles);

  // Report running status
  report_service_status(SERVICE_RUNNING, NO_ERROR, 0);
  sfs_log(SFS_LOG_INFO, "Service running");

  // A previous run in this process may have left the flag set; the service
  // only runs once per process today, but clearing it is what makes that an
  // implementation detail rather than a trap.
  sfs_server_clear_stop();

  // Create server thread
  g_server_thread = CreateThread(NULL, 0, server_thread_func, NULL, 0, NULL);

  if (!g_server_thread) {
    sfs_log(SFS_LOG_ERROR, "Failed to create server thread");
    sfs_handles_free(&g_handles);
    sfs_net_close(&g_net);
    sfs_printers_shutdown();
    sfs_config_unload(&g_config);
    sfs_log_shutdown();
    sfs_platform_shutdown();
    report_service_status(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 0);
    CloseHandle(g_stop_event);
    return;
  }

  // Wait for stop event
  WaitForSingleObject(g_stop_event, INFINITE);

  // Cleanup
  sfs_log(SFS_LOG_INFO, "Service stopping");
  sfs_server_request_stop();

  // Allow server thread some time to finish
  WaitForSingleObject(g_server_thread, 5000);
  CloseHandle(g_server_thread);

  sfs_handles_free(&g_handles);
  sfs_net_close(&g_net);
  sfs_printers_shutdown();
  sfs_config_unload(&g_config);
  sfs_log(SFS_LOG_INFO, "Service stopped");
  sfs_log_shutdown();
  sfs_platform_shutdown();

  CloseHandle(g_stop_event);
  report_service_status(SERVICE_STOPPED, NO_ERROR, 0);
}

int sfs_service_dispatch(void) {
  SERVICE_TABLE_ENTRY dispatch_table[] = {{SFS_SERVICE_NAME, service_main},
                                          {NULL, NULL}};

  if (StartServiceCtrlDispatcher(dispatch_table))
    return 0;

  // Anything else means we were run from a shell or a shortcut rather than by
  // the SCM, and the caller should behave like an ordinary program.
  return -1;
}

int sfs_service_install(void) {
  SC_HANDLE scm, service;
  char path[MAX_PATH];

  if (!GetModuleFileName(NULL, path, MAX_PATH)) {
    fprintf(stderr, "Error: Cannot determine executable path (%lu)\n",
            GetLastError());
    return 1;
  }

  // The service runs the same binary in service mode, which the SCM reaches by
  // passing no arguments at all; the quoted path plus "service run" is
  // explicit about it and survives a path containing spaces.
  char command[MAX_PATH + 32];
  snprintf(command, sizeof(command), "\"%s\" service run", path);

  scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
  if (!scm) {
    fprintf(stderr, "Error: Cannot open Service Control Manager (%lu)\n",
            GetLastError());
    fprintf(stderr, "Are you running as Administrator?\n");
    return 1;
  }

  service = CreateService(scm, SFS_SERVICE_NAME, SFS_SERVICE_DISPLAY_NAME,
                          SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                          SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, command,
                          NULL, NULL, "Tcpip\0", NULL, NULL);

  if (!service) {
    DWORD err = GetLastError();
    CloseServiceHandle(scm);
    if (err == ERROR_SERVICE_EXISTS) {
      fprintf(stderr, "Error: the service is already installed.\n");
      fprintf(stderr, "Run 'sharefs service uninstall' first.\n");
    } else {
      fprintf(stderr, "Error: Cannot create service (%lu)\n", err);
    }
    return 1;
  }

  SERVICE_DESCRIPTION desc;
  desc.lpDescription = "Shares files with RISC OS machines over ShareFS";
  ChangeServiceConfig2(service, SERVICE_CONFIG_DESCRIPTION, &desc);

  CloseServiceHandle(service);
  CloseServiceHandle(scm);

  char config_path[SFS_PATH_MAX];
  if (sfs_paths_find_config(config_path, sizeof(config_path)) != 0)
    sfs_paths_default_config(config_path, sizeof(config_path));

  printf("Service installed.\n");
  printf("Configuration file: %s\n", config_path);
  printf("To start it now: sharefs service start\n");
  printf("It will also start automatically at boot.\n");
  return 0;
}

int sfs_service_uninstall(void) {
  SC_HANDLE scm, service;

  scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
  if (!scm) {
    fprintf(stderr, "Error: Cannot open Service Control Manager (%lu)\n",
            GetLastError());
    fprintf(stderr, "Are you running as Administrator?\n");
    return 1;
  }

  service = OpenService(scm, SFS_SERVICE_NAME, DELETE | SERVICE_QUERY_STATUS);
  if (!service) {
    DWORD err = GetLastError();
    CloseServiceHandle(scm);
    if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
      fprintf(stderr, "Error: the service is not installed.\n");
    } else {
      fprintf(stderr, "Error: Cannot open service (%lu)\n", err);
    }
    return 1;
  }

  SERVICE_STATUS status;
  if (QueryServiceStatus(service, &status)) {
    if (status.dwCurrentState != SERVICE_STOPPED) {
      fprintf(stderr, "Error: the service is running.\n");
      fprintf(stderr, "Run 'sharefs service stop' first.\n");
      CloseServiceHandle(service);
      CloseServiceHandle(scm);
      return 1;
    }
  }

  if (!DeleteService(service)) {
    fprintf(stderr, "Error: Cannot delete service (%lu)\n", GetLastError());
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return 1;
  }

  CloseServiceHandle(service);
  CloseServiceHandle(scm);

  printf("Service uninstalled.\n");
  return 0;
}

int sfs_service_start(void) {
  SC_HANDLE scm, service;

  scm = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
  if (!scm) {
    fprintf(stderr, "Error: Cannot open Service Control Manager (%lu)\n",
            GetLastError());
    return 1;
  }

  service =
      OpenService(scm, SFS_SERVICE_NAME, SERVICE_START | SERVICE_QUERY_STATUS);
  if (!service) {
    DWORD err = GetLastError();
    CloseServiceHandle(scm);
    if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
      fprintf(stderr, "Error: the service is not installed.\n");
      fprintf(stderr, "Run 'sharefs service install' first.\n");
    } else {
      fprintf(stderr, "Error: Cannot open service (%lu)\n", err);
    }
    return 1;
  }

  SERVICE_STATUS status;
  if (QueryServiceStatus(service, &status)) {
    if (status.dwCurrentState != SERVICE_STOPPED) {
      printf("The service is already running.\n");
      CloseServiceHandle(service);
      CloseServiceHandle(scm);
      return 0;
    }
  }

  if (!StartService(service, 0, NULL)) {
    fprintf(stderr, "Error: Cannot start service (%lu)\n", GetLastError());
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return 1;
  }

  CloseServiceHandle(service);
  CloseServiceHandle(scm);

  printf("Service started.\n");
  return 0;
}

int sfs_service_stop(void) {
  SC_HANDLE scm, service;
  SERVICE_STATUS status;

  scm = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
  if (!scm) {
    fprintf(stderr, "Error: Cannot open Service Control Manager (%lu)\n",
            GetLastError());
    return 1;
  }

  service = OpenService(scm, SFS_SERVICE_NAME, SERVICE_STOP | SERVICE_QUERY_STATUS);
  if (!service) {
    DWORD err = GetLastError();
    CloseServiceHandle(scm);
    if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
      fprintf(stderr, "Error: the service is not installed.\n");
    } else {
      fprintf(stderr, "Error: Cannot open service (%lu)\n", err);
    }
    return 1;
  }

  if (QueryServiceStatus(service, &status)) {
    if (status.dwCurrentState == SERVICE_STOPPED) {
      printf("The service is already stopped.\n");
      CloseServiceHandle(service);
      CloseServiceHandle(scm);
      return 0;
    }
  }

  if (!ControlService(service, SERVICE_CONTROL_STOP, &status)) {
    fprintf(stderr, "Error: Cannot stop service (%lu)\n", GetLastError());
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return 1;
  }

  CloseServiceHandle(service);
  CloseServiceHandle(scm);

  printf("Service stopped.\n");
  return 0;
}

int sfs_service_is_installed(void) {
  SC_HANDLE scm = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
  if (!scm)
    return 0;
  SC_HANDLE service = OpenService(scm, SFS_SERVICE_NAME, SERVICE_QUERY_STATUS);
  int installed = (service != NULL);
  if (service)
    CloseServiceHandle(service);
  CloseServiceHandle(scm);
  return installed;
}

int sfs_service_is_running(void) {
  SC_HANDLE scm = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
  if (!scm)
    return 0;
  SC_HANDLE service = OpenService(scm, SFS_SERVICE_NAME, SERVICE_QUERY_STATUS);
  if (!service) {
    CloseServiceHandle(scm);
    return 0;
  }
  SERVICE_STATUS status;
  int running = 0;
  if (QueryServiceStatus(service, &status))
    running = (status.dwCurrentState == SERVICE_RUNNING);
  CloseServiceHandle(service);
  CloseServiceHandle(scm);
  return running;
}

#endif // _WIN32
