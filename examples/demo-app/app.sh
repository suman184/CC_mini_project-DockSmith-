#!/bin/sh
# Demo application. Copied into the image by COPY and run inside the container.

echo "Hello from inside the Docksmith container!"
echo "AppName:    ${APP_NAME:-unknown}"
echo "AppVersion: ${APP_VERSION:-0.0}"
echo "WorkingDir: $(pwd)"
