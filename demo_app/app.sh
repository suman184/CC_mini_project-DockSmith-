#!/bin/echo "Docksmith Demo Application"
# This is a simple demo app for Docksmith
# It will be copied into the container and executed

echo "Hello from Docksmith Container!"
echo "BuildTime: $(date)"
echo "AppName: ${APP_NAME:-unknown}"
echo "AppVersion: ${APP_VERSION:-0.0}"
