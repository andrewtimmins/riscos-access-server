# ShareFS - Windows Installer Script
# Built with NSIS (Nullsoft Scriptable Install System)
#
# One executable is installed, not three. sharefs.exe is the window, the
# server, and the thing that installs the service, chosen by what it is asked
# to do; see src/cli.h. Earlier versions shipped sharefs-server.exe,
# sharefs-service.exe and sharefs-admin.exe, so the upgrade path here removes
# them and re-registers the service against the new binary.

!define PRODUCT_NAME "ShareFS"
!ifndef PRODUCT_VERSION
!define PRODUCT_VERSION "0.1.1"
!endif
!ifndef WINDOWS_RELEASE_DIR
!define WINDOWS_RELEASE_DIR "releases\windows\x64"
!endif
!define PRODUCT_PUBLISHER "Andy Timmins"
!define PRODUCT_WEB_SITE "https://github.com/andrewtimmins/sharefs-server"
!define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
!define SERVICE_NAME "ShareFSServer"

# MUI Settings
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"


# Installer settings
Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
!ifndef ARCH_SUFFIX
  !define ARCH_SUFFIX ""
!endif
OutFile "sharefs-server_${PRODUCT_VERSION}${ARCH_SUFFIX}-setup.exe"
InstallDir "C:\ShareFS"
InstallDirRegKey HKLM "Software\ShareFS" "InstallDir"
RequestExecutionLevel admin
ShowInstDetails show
ShowUnInstDetails show

# MUI interface settings
!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT
!define MUI_WELCOMEFINISHPAGE_BITMAP "${NSISDIR}\Contrib\Graphics\Wizard\nsis3-branding.bmp"

# Installer pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

# Finish page with options
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Open ShareFS"
!define MUI_FINISHPAGE_RUN_FUNCTION "LaunchShareFS"
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\README.txt"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "View README"
!insertmacro MUI_PAGE_FINISH

# Uninstaller pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

# Languages
!insertmacro MUI_LANGUAGE "English"

# Version information
VIProductVersion "${PRODUCT_VERSION}.0"
VIAddVersionKey "ProductName" "${PRODUCT_NAME}"
VIAddVersionKey "ProductVersion" "${PRODUCT_VERSION}"
VIAddVersionKey "CompanyName" "${PRODUCT_PUBLISHER}"
VIAddVersionKey "FileDescription" "ShareFS Installer"
VIAddVersionKey "FileVersion" "${PRODUCT_VERSION}"

# Installer sections
Section "Core Files" SecCore
  SectionIn RO  ; Read-only, always installed

  # Ensure all-user folders/registry are in effect during install
  SetShellVarContext all
  SetRegView 64
  
  SetOutPath "$INSTDIR"
  
  # One executable. Run it to open the window, or with a command to do
  # anything else: `sharefs serve`, `sharefs service install`, `sharefs status`.
  File "${WINDOWS_RELEASE_DIR}\sharefs.exe"

  # An upgrade from 0.1.7 or earlier finds three executables here and a service
  # registered against one of them. Take the old service out of the register
  # before the new one goes in, then remove the binaries.
  IfFileExists "$INSTDIR\sharefs-service.exe" 0 NoLegacyService
    DetailPrint "Removing the service registered by the previous version..."
    nsExec::ExecToLog '"$INSTDIR\sharefs-service.exe" stop'
    Sleep 1500
    nsExec::ExecToLog '"$INSTDIR\sharefs-service.exe" uninstall'
  NoLegacyService:
  Delete "$INSTDIR\sharefs-service.exe"
  Delete "$INSTDIR\sharefs-server.exe"
  Delete "$INSTDIR\sharefs-admin.exe"
  
  # Install documentation (but not config - that goes to ProgramData)
  SetOutPath "$INSTDIR"
  File /oname=README.txt "README.md"
  File "LICENSE"
  
  # Install firewall script if present
  IfFileExists "${WINDOWS_RELEASE_DIR}\configure-firewall-windows.bat" 0 +2
    File "${WINDOWS_RELEASE_DIR}\configure-firewall-windows.bat"
  
  # Create install directories
  CreateDirectory "$INSTDIR"
  CreateDirectory "$INSTDIR\Shares"
  CreateDirectory "$INSTDIR\Shares\Public"
  SetOutPath "$INSTDIR"

  # The configuration lives in ProgramData: it is the first place both the
  # window and the server look (see src/paths.c), and a service running as
  # LocalSystem can read it. Older versions put it in the install directory,
  # which is third in that list, so an existing one is moved rather than
  # shadowed - otherwise an upgrade would silently start serving the defaults
  # instead of the user's shares.
  CreateDirectory "$APPDATA\ShareFS"

  IfFileExists "$APPDATA\ShareFS\sharefs.conf" PreserveSharefs 0
  IfFileExists "$INSTDIR\sharefs.conf" MigrateSharefs InstallConfig

  MigrateSharefs:
    CopyFiles /SILENT "$INSTDIR\sharefs.conf" "$APPDATA\ShareFS\sharefs.conf"
    Delete "$INSTDIR\sharefs.conf"
    DetailPrint "Moved your configuration to $APPDATA\ShareFS\sharefs.conf"
    Goto ConfigDone
  InstallConfig:
    SetOutPath "$APPDATA\ShareFS"
    File /oname=sharefs.conf "${WINDOWS_RELEASE_DIR}\sharefs.conf"
    SetOutPath "$INSTDIR"
    DetailPrint "Installed default configuration to $APPDATA\ShareFS\sharefs.conf"
    Goto ConfigDone
  PreserveSharefs:
    DetailPrint "Preserving existing configuration at $APPDATA\ShareFS\sharefs.conf"
  ConfigDone:

  # Set share permissions for Public share
  nsExec::ExecToLog 'icacls "$INSTDIR\Shares" /grant Users:(OI)(CI)(M)'
  nsExec::ExecToLog 'icacls "$INSTDIR\Shares\Public" /grant Users:(OI)(CI)(M)'
  
  # Store installation folder
  WriteRegStr HKLM "Software\${PRODUCT_NAME}" "InstallDir" "$INSTDIR"
  
  # Create uninstaller
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  
  # Registry entries for Add/Remove Programs
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayName" "${PRODUCT_NAME}"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "UninstallString" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayIcon" "$INSTDIR\sharefs.exe"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "URLInfoAbout" "${PRODUCT_WEB_SITE}"
  WriteRegDWORD HKLM "${PRODUCT_UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${PRODUCT_UNINST_KEY}" "NoRepair" 1
  
  # Get installation size
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${PRODUCT_UNINST_KEY}" "EstimatedSize" "$0"
  
SectionEnd

Section "Start Menu Shortcuts" SecShortcuts
  CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
  CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\ShareFS.lnk" "$INSTDIR\sharefs.exe"
  CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\README.lnk" "$INSTDIR\README.txt"
SectionEnd

Section "Install Windows Service" SecService
  DetailPrint "Installing Windows service..."
  
  # Install the service
  nsExec::ExecToLog '"$INSTDIR\sharefs.exe" service install'
  Pop $0
  
  ${If} $0 == 0
    DetailPrint "Service installed successfully"
  ${Else}
    DetailPrint "Warning: Service installation returned code $0"
  ${EndIf}
SectionEnd

Section "Configure Firewall" SecFirewall
  DetailPrint "Configuring Windows Firewall..."
  
  # Add firewall rules for the server
  nsExec::ExecToLog 'netsh advfirewall firewall add rule name="ShareFS Server - UDP 32770" dir=in action=allow protocol=UDP localport=32770'
  nsExec::ExecToLog 'netsh advfirewall firewall add rule name="ShareFS Server - UDP 32771" dir=in action=allow protocol=UDP localport=32771'
  nsExec::ExecToLog 'netsh advfirewall firewall add rule name="ShareFS Server - UDP 49171" dir=in action=allow protocol=UDP localport=49171'
  
  DetailPrint "Firewall rules added"
SectionEnd

Section "Start Service" SecStart
  DetailPrint "Starting Windows service..."
  
  # Start the service
  nsExec::ExecToLog '"$INSTDIR\sharefs.exe" service start'
  Pop $0
  
  ${If} $0 == 0
    DetailPrint "Service started successfully"
  ${Else}
    DetailPrint "Warning: Service start returned code $0"
    DetailPrint "You can turn sharing on from the ShareFS window instead"
  ${EndIf}
SectionEnd

# Section descriptions
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecCore} "ShareFS itself (required)"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecShortcuts} "Start Menu shortcuts for easy access"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecService} "Keep sharing when nobody is logged in, and start at boot"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecFirewall} "Configure Windows Firewall to allow server traffic"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecStart} "Start the service immediately after installation"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

# Uninstaller section
Section "Uninstall"
  # The same all-users context the installer ran in, or $APPDATA below means
  # the uninstalling user's roaming folder rather than ProgramData, and the
  # configuration it is meant to remove is left behind.
  SetShellVarContext all
  SetRegView 64

  # Stop and remove service
  DetailPrint "Stopping the ShareFS service..."
  nsExec::ExecToLog '"$INSTDIR\sharefs.exe" service stop'
  Sleep 2000

  DetailPrint "Removing the ShareFS service..."
  nsExec::ExecToLog '"$INSTDIR\sharefs.exe" service uninstall'
  
  # Remove firewall rules
  DetailPrint "Removing firewall rules..."
  nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="ShareFS Server - UDP 32770"'
  nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="ShareFS Server - UDP 32771"'
  nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="ShareFS Server - UDP 49171"'
  
  # Remove Start Menu shortcuts
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\*.*"
  RMDir "$SMPROGRAMS\${PRODUCT_NAME}"
  
  # Remove installed files, including the three from before 0.1.8.
  Delete "$INSTDIR\sharefs.exe"
  Delete "$INSTDIR\sharefs-server.exe"
  Delete "$INSTDIR\sharefs-service.exe"
  Delete "$INSTDIR\sharefs-admin.exe"
  Delete "$INSTDIR\README.txt"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\configure-firewall-windows.bat"
  Delete "$INSTDIR\Uninstall.exe"

  # Ask user if they want to remove configuration and shares
  MessageBox MB_YESNO "Remove configuration and shares?$\n$\nThis deletes $INSTDIR and $APPDATA\ShareFS.$\n$\nChoose 'No' if you plan to reinstall later and want to keep your settings." IDYES RemoveConfig
    Goto SkipConfigDelete
  RemoveConfig:
    DetailPrint "Removing configuration and shares..."
    RMDir /r "$INSTDIR"
    RMDir /r "$APPDATA\ShareFS"
  SkipConfigDelete:
  
  # Remove registry keys
  DeleteRegKey HKLM "${PRODUCT_UNINST_KEY}"
  DeleteRegKey HKLM "Software\${PRODUCT_NAME}"
  
  SetAutoClose true
SectionEnd

# Open the window after installing.
Function LaunchShareFS
  Exec "$INSTDIR\sharefs.exe"
FunctionEnd

# Installer init function
Function .onInit
  # All-user shell folders (ProgramData/Common Start Menu)
  SetShellVarContext all

  # Write registry to 64-bit view to match Program Files location
  SetRegView 64

  # Check if already installed
  ReadRegStr $0 HKLM "${PRODUCT_UNINST_KEY}" "UninstallString"
  ${If} $0 != ""
    MessageBox MB_YESNO|MB_ICONQUESTION \
      "${PRODUCT_NAME} is already installed.$\n$\nWould you like to uninstall the existing version first?" \
      IDNO +3
      ExecWait '"$0" /S _?=$INSTDIR'
      Delete $0
  ${EndIf}
FunctionEnd
