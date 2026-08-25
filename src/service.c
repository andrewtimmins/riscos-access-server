/*
  ShareFS Server - Windows Service Wrapper

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

#include "config.h"
#include "handle.h"
#include "log.h"
#include "net.h"
#include "platform.h"
#include "printer.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define SERVICE_NAME "ShareFSServer"
#define SERVICE_DISPLAY_NAME "ShareFS Server"

// Global service state
static SERVICE_STATUS g_service_status = {0};
static SERVICE_STATUS_HANDLE g_status_handle = NULL;
static HANDLE g_stop_event = NULL;
static HANDLE g_server_thread = NULL;

// Server state
static sfs_config g_config;
static sfs_net g_net;
static sfs_handle_table g_handles;
static volatile int g_running = 0;

// Find configuration file
static const char *find_config_file(void) {
  // Windows service config search paths
    static const char *search_paths[] = {
      "C:\\ShareFS\\sharefs.conf",
      "C:\\ProgramData\\ShareFS\\sharefs.conf",
      "sharefs.conf",
      NULL};

  for (const char **p = search_paths; *p != NULL; ++p) {
    FILE *f = fopen(*p, "r");
    if (f) {
      fclose(f);
      return *p;
    }
  }
  return NULL;
}

// Server thread function
static DWORD WINAPI server_thread_func(LPVOID param) {
  (void)param;

  // Run the server
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
      RegisterServiceCtrlHandlerEx(SERVICE_NAME, service_ctrl_handler, NULL);
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

  // Find config file
  const char *config_path = find_config_file();
  if (!config_path) {
    report_service_status(SERVICE_STOPPED, ERROR_FILE_NOT_FOUND, 0);
    return;
  }

  // Initialize platform
  if (sfs_platform_init() != 0) {
    report_service_status(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 0);
    CloseHandle(g_stop_event);
    return;
  }

  // Initialize logging
  sfs_log_init();

  // Load and validate config
  if (sfs_config_load(config_path, &g_config) != 0) {
    sfs_log(SFS_LOG_ERROR, "Failed to load config: %s", config_path);
    sfs_platform_shutdown();
    report_service_status(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 0);
    CloseHandle(g_stop_event);
    return;
  }

  if (sfs_config_validate(&g_config) != 0) {
    sfs_log(SFS_LOG_ERROR, "Invalid configuration");
    sfs_config_unload(&g_config);
    sfs_platform_shutdown();
    report_service_status(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 0);
    CloseHandle(g_stop_event);
    return;
  }

  sfs_log_set_level(sfs_log_level_from_string(g_config.server.log_level));
  sfs_log(SFS_LOG_INFO, "Service starting with config %s", config_path);

  // Open network
  if (g_config.server.bind_ip) {
    sfs_log(SFS_LOG_INFO, "Binding to specific address: %s",
            g_config.server.bind_ip);
  }
  if (sfs_net_open(&g_net, g_config.server.bind_ip) != 0) {
    sfs_log(SFS_LOG_ERROR, "Failed to open network sockets");
    sfs_config_unload(&g_config);
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

  // Create server thread
  g_running = 1;
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
  g_running = 0;

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

// Install the service
static int install_service(void) {
  SC_HANDLE scm, service;
  char path[MAX_PATH];

  // Get full path to this executable
  if (!GetModuleFileName(NULL, path, MAX_PATH)) {
    fprintf(stderr, "Error: Cannot determine executable path (%lu)\n",
            GetLastError());
    return 1;
  }

  // Open SCM
  scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
  if (!scm) {
    fprintf(stderr, "Error: Cannot open Service Control Manager (%lu)\n",
            GetLastError());
    fprintf(stderr, "Are you running as Administrator?\n");
    return 1;
  }

  // Create service
  service = CreateService(scm, SERVICE_NAME, SERVICE_DISPLAY_NAME,
                          SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                          SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, path, NULL,
                          NULL, "Tcpip\0", NULL, NULL);

  if (!service) {
    DWORD err = GetLastError();
    CloseServiceHandle(scm);
    if (err == ERROR_SERVICE_EXISTS) {
      fprintf(stderr, "Error: Service already exists\n");
      fprintf(stderr, "Use 'sharefs-service.exe uninstall' first\n");
    } else {
      fprintf(stderr, "Error: Cannot create service (%lu)\n", err);
    }
    return 1;
  }

  // Set description
  SERVICE_DESCRIPTION desc;
  desc.lpDescription =
      "Provides ShareFS file sharing for RISC OS clients";
  ChangeServiceConfig2(service, SERVICE_CONFIG_DESCRIPTION, &desc);

  CloseServiceHandle(service);
  CloseServiceHandle(scm);

  // Create config directory if needed
  CreateDirectory("C:\\ProgramData\\ShareFS", NULL);

  printf("Service installed successfully.\n");
  printf("Configuration file: C:\\ProgramData\\ShareFS\\sharefs.conf\n");
  printf("To start: sharefs-service.exe start\n");
  printf("The service is configured to start automatically on boot.\n");
  return 0;
}

// Uninstall the service
static int uninstall_service(void) {
  SC_HANDLE scm, service;

  scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
  if (!scm) {
    fprintf(stderr, "Error: Cannot open Service Control Manager (%lu)\n",
            GetLastError());
    fprintf(stderr, "Are you running as Administrator?\n");
    return 1;
  }

  service = OpenService(scm, SERVICE_NAME, DELETE | SERVICE_QUERY_STATUS);
  if (!service) {
    DWORD err = GetLastError();
    CloseServiceHandle(scm);
    if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
      fprintf(stderr, "Error: Service is not installed\n");
    } else {
      fprintf(stderr, "Error: Cannot open service (%lu)\n", err);
    }
    return 1;
  }

  // Check if service is running
  SERVICE_STATUS status;
  if (QueryServiceStatus(service, &status)) {
    if (status.dwCurrentState != SERVICE_STOPPED) {
      fprintf(stderr, "Error: Service is running\n");
      fprintf(stderr, "Use 'sharefs-service.exe stop' first\n");
      CloseServiceHandle(service);
      CloseServiceHandle(scm);
      return 1;
    }
  }

  // Delete service
  if (!DeleteService(service)) {
    fprintf(stderr, "Error: Cannot delete service (%lu)\n", GetLastError());
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return 1;
  }

  CloseServiceHandle(service);
  CloseServiceHandle(scm);

  printf("Service uninstalled successfully.\n");
  return 0;
}

// Start the service
static int start_service_cmd(void) {
  SC_HANDLE scm, service;

  scm = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
  if (!scm) {
    fprintf(stderr, "Error: Cannot open Service Control Manager (%lu)\n",
            GetLastError());
    return 1;
  }

  service =
      OpenService(scm, SERVICE_NAME, SERVICE_START | SERVICE_QUERY_STATUS);
  if (!service) {
    DWORD err = GetLastError();
    CloseServiceHandle(scm);
    if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
      fprintf(stderr, "Error: Service is not installed\n");
      fprintf(stderr, "Use 'sharefs-service.exe install' first\n");
    } else {
      fprintf(stderr, "Error: Cannot open service (%lu)\n", err);
    }
    return 1;
  }

  // Check current status
  SERVICE_STATUS status;
  if (QueryServiceStatus(service, &status)) {
    if (status.dwCurrentState != SERVICE_STOPPED) {
      fprintf(stderr, "Error: Service is already running\n");
      CloseServiceHandle(service);
      CloseServiceHandle(scm);
      return 1;
    }
  }

  // Start service
  if (!StartService(service, 0, NULL)) {
    fprintf(stderr, "Error: Cannot start service (%lu)\n", GetLastError());
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return 1;
  }

  CloseServiceHandle(service);
  CloseServiceHandle(scm);

  printf("Service started successfully.\n");
  return 0;
}

// Stop the service
static int stop_service_cmd(void) {
  SC_HANDLE scm, service;
  SERVICE_STATUS status;

  scm = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
  if (!scm) {
    fprintf(stderr, "Error: Cannot open Service Control Manager (%lu)\n",
            GetLastError());
    return 1;
  }

  service = OpenService(scm, SERVICE_NAME, SERVICE_STOP | SERVICE_QUERY_STATUS);
  if (!service) {
    DWORD err = GetLastError();
    CloseServiceHandle(scm);
    if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
      fprintf(stderr, "Error: Service is not installed\n");
    } else {
      fprintf(stderr, "Error: Cannot open service (%lu)\n", err);
    }
    return 1;
  }

  // Check current status
  if (QueryServiceStatus(service, &status)) {
    if (status.dwCurrentState == SERVICE_STOPPED) {
      fprintf(stderr, "Service is already stopped.\n");
      CloseServiceHandle(service);
      CloseServiceHandle(scm);
      return 0;
    }
  }

  // Stop service
  if (!ControlService(service, SERVICE_CONTROL_STOP, &status)) {
    fprintf(stderr, "Error: Cannot stop service (%lu)\n", GetLastError());
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return 1;
  }

  CloseServiceHandle(service);
  CloseServiceHandle(scm);

  printf("Service stopped successfully.\n");
  return 0;
}

// Print usage
static void print_usage(void) {
  printf("ShareFS Server - Windows Service Manager\n\n");
  printf("Usage:\n");
  printf("  sharefs-service.exe install    Install the service\n");
  printf("  sharefs-service.exe uninstall  Uninstall the service\n");
  printf("  sharefs-service.exe start      Start the service\n");
  printf("  sharefs-service.exe stop       Stop the service\n");
  printf("\n");
  printf("Configuration file: C:\\ProgramData\\ShareFS\\sharefs.conf\n");
  printf("(Falls back to ./sharefs.conf in the executable directory)\n");
}

// Main entry point
int main(int argc, char **argv) {
  if (argc < 2) {
    // No arguments - called by SCM
    SERVICE_TABLE_ENTRY dispatch_table[] = {{SERVICE_NAME, service_main},
                                            {NULL, NULL}};

    if (!StartServiceCtrlDispatcher(dispatch_table)) {
      DWORD err = GetLastError();
      if (err == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
        // Not running as service, show usage
        print_usage();
        return 1;
      }
    }
    return 0;
  }

  // Command-line mode
  const char *cmd = argv[1];

  if (_stricmp(cmd, "install") == 0) {
    return install_service();
  } else if (_stricmp(cmd, "uninstall") == 0) {
    return uninstall_service();
  } else if (_stricmp(cmd, "start") == 0) {
    return start_service_cmd();
  } else if (_stricmp(cmd, "stop") == 0) {
    return stop_service_cmd();
  } else {
    print_usage();
    return 1;
  }
}

#else
#error "This file is Windows-only"
#endif // _WIN32
