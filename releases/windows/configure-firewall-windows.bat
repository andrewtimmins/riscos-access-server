@echo off
echo ShareFS Server - Firewall Configuration
echo ==============================================
echo.
echo This script adds Windows Firewall rules for:
echo - UDP 32770 (Freeway)
echo - UDP 32771 (Access+)
echo - UDP 49171 (ShareFS)
echo.
echo NOTE: You must run this script as Administrator.
echo.
pause

echo Adding rules...
netsh advfirewall firewall add rule name="ShareFS Server (Freeway)" dir=in action=allow protocol=UDP localport=32770
netsh advfirewall firewall add rule name="ShareFS Server (Access+)" dir=in action=allow protocol=UDP localport=32771
netsh advfirewall firewall add rule name="ShareFS Server (ShareFS)" dir=in action=allow protocol=UDP localport=49171

echo.
echo Done. If you saw "Ok." messages above, checking is complete.
pause
