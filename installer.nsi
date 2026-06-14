# ShareFS Server - Windows Installer Script
# Built with NSIS (Nullsoft Scriptable Install System)

!define PRODUCT_NAME "ShareFS Server"
!ifndef PRODUCT_VERSION
!define PRODUCT_VERSION "0.1.1"
!endif
!ifndef WINDOWS_RELEASE_DIR
!define WINDOWS_RELEASE_DIR "releases\windows\x64"
!endif
!define PRODUCT_PUBLISHER "Andrew Timmins"
!define PRODUCT_WEB_SITE "https://github.com/andrewtimmins/riscos-access-server"
!define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
!define SERVICE_NAME "ShareFSServer"

# MUI Settings
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"


# Installer settings
Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "sharefs-server_${PRODUCT_VERSION}-setup.exe"
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
!define MUI_FINISHPAGE_RUN_TEXT "Start Admin GUI"
!define MUI_FINISHPAGE_RUN_FUNCTION "LaunchAdminGUI"
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
VIAddVersionKey "FileDescription" "ShareFS Server Installer"
VIAddVersionKey "FileVersion" "${PRODUCT_VERSION}"

# Installer sections
Section "Core Files" SecCore
  SectionIn RO  ; Read-only, always installed

  # Ensure all-user folders/registry are in effect during install
  SetShellVarContext all
  SetRegView 64
  
  SetOutPath "$INSTDIR"
  
  # Install executables
  File "${WINDOWS_RELEASE_DIR}\sharefs-server.exe"
  File "${WINDOWS_RELEASE_DIR}\sharefs-service.exe"
  File "${WINDOWS_RELEASE_DIR}\sharefs-admin.exe"
  
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

  # Copy config to install dir if not exists (preserve existing config on upgrade)
  IfFileExists "$INSTDIR\sharefs.conf" PreserveSharefs InstallConfig
  InstallConfig:
    File /oname=sharefs.conf "${WINDOWS_RELEASE_DIR}\sharefs.conf"
    DetailPrint "Installed default configuration to $INSTDIR\sharefs.conf"
    Goto ConfigDone
  PreserveSharefs:
    DetailPrint "Preserving existing configuration at $INSTDIR\sharefs.conf"
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
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayIcon" "$INSTDIR\sharefs-admin.exe"
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
  CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\Admin GUI.lnk" "$INSTDIR\sharefs-admin.exe"
  CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\README.lnk" "$INSTDIR\README.txt"
SectionEnd

Section "Install Windows Service" SecService
  DetailPrint "Installing Windows service..."
  
  # Install the service
  nsExec::ExecToLog '"$INSTDIR\sharefs-service.exe" install'
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
  nsExec::ExecToLog '"$INSTDIR\sharefs-service.exe" start'
  Pop $0
  
  ${If} $0 == 0
    DetailPrint "Service started successfully"
  ${Else}
    DetailPrint "Warning: Service start returned code $0"
    DetailPrint "You can start the service manually from the Admin GUI"
  ${EndIf}
SectionEnd

# Section descriptions
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecCore} "Core server files (required)"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecShortcuts} "Start Menu shortcuts for easy access"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecService} "Install as Windows service for automatic startup"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecFirewall} "Configure Windows Firewall to allow server traffic"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecStart} "Start the service immediately after installation"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

# Uninstaller section
Section "Uninstall"
  # Stop and remove service
  DetailPrint "Stopping Windows service..."
  nsExec::ExecToLog '"$INSTDIR\sharefs-service.exe" stop'
  Sleep 2000
  
  DetailPrint "Removing Windows service..."
  nsExec::ExecToLog '"$INSTDIR\sharefs-service.exe" uninstall'
  
  # Remove firewall rules
  DetailPrint "Removing firewall rules..."
  nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="ShareFS Server - UDP 32770"'
  nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="ShareFS Server - UDP 32771"'
  nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="ShareFS Server - UDP 49171"'
  
  # Remove Start Menu shortcuts
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\*.*"
  RMDir "$SMPROGRAMS\${PRODUCT_NAME}"
  
  # Remove installed files
  Delete "$INSTDIR\sharefs-server.exe"
  Delete "$INSTDIR\sharefs-service.exe"
  Delete "$INSTDIR\sharefs-admin.exe"
  Delete "$INSTDIR\README.txt"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\configure-firewall-windows.bat"
  Delete "$INSTDIR\Uninstall.exe"

  # Ask user if they want to remove configuration and shares
  MessageBox MB_YESNO "Remove configuration and shares ($INSTDIR)?$\n$\nChoose 'No' if you plan to reinstall later and want to keep your settings." IDYES RemoveConfig
    Goto SkipConfigDelete
  RemoveConfig:
    DetailPrint "Removing configuration and shares..."
    RMDir /r "$INSTDIR"
  SkipConfigDelete:
  
  # Remove registry keys
  DeleteRegKey HKLM "${PRODUCT_UNINST_KEY}"
  DeleteRegKey HKLM "Software\${PRODUCT_NAME}"
  
  SetAutoClose true
SectionEnd

# Launch admin GUI function
Function LaunchAdminGUI
  Exec "$INSTDIR\sharefs-admin.exe"
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
