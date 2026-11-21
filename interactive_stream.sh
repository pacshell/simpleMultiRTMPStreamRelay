#!/usr/bin/env bash
#
# interactive_stream.sh
#
# Interactive launcher for multi_rtmp_stream.
#
# The script:
#   - Compiles multi_rtmp_stream.c if the binary does not exist
#   - Lets you choose between a file/URL input or OBS RTMP input
#   - Asks for multiple RTMP output endpoints
#   - Shows the final configuration before starting
#
# This version supports RTMP only.
#
# Usage:
#
#   chmod +x interactive_stream.sh
#   ./interactive_stream.sh
#

set -e

BINARY_NAME="multi_rtmp_stream"
SOURCE_NAME="multi_rtmp_stream.c"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

BINARY_PATH="$SCRIPT_DIR/$BINARY_NAME"
SOURCE_PATH="$SCRIPT_DIR/$SOURCE_NAME"


echo
echo "========================================"
echo "       Multi RTMP Stream Relay"
echo "========================================"
echo


# ------------------------------------------------------------
# 1. Check dependencies
# ------------------------------------------------------------

check_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Error: required command '$1' was not found."
        exit 1
    fi
}


check_command gcc
check_command pkg-config


# ------------------------------------------------------------
# 2. Compile the binary if necessary
# ------------------------------------------------------------

if [ ! -x "$BINARY_PATH" ]; then

    echo "Binary not found."

    if [ ! -f "$SOURCE_PATH" ]; then
        echo "Error: source file not found:"
        echo "  $SOURCE_PATH"
        exit 1
    fi

    echo "Compiling $SOURCE_NAME..."
    echo

    gcc "$SOURCE_PATH" \
        -o "$BINARY_PATH" \
        $(pkg-config --cflags --libs \
            libavformat \
            libavcodec \
            libavutil)

    echo
    echo "Compilation completed successfully."
    echo

fi


# ------------------------------------------------------------
# 3. Select input mode
# ------------------------------------------------------------

echo "Select input mode:"
echo
echo "  1) File or URL"
echo "  2) Receive RTMP directly from OBS"
echo

read -rp "Choose [1/2]: " INPUT_MODE

echo


LISTEN_FLAG=""


case "$INPUT_MODE" in

    1)

        read -rp "Enter the input file path or URL: " INPUT_SOURCE

        if [ -z "$INPUT_SOURCE" ]; then
            echo "Error: input cannot be empty."
            exit 1
        fi

        ;;


    2)

        LISTEN_FLAG="-listen"

        read -rp "Listening port [1935]: " LISTEN_PORT
        LISTEN_PORT="${LISTEN_PORT:-1935}"

        read -rp "Application name [live]: " APP_NAME
        APP_NAME="${APP_NAME:-live}"

        read -rp "Stream key [obs]: " STREAM_KEY
        STREAM_KEY="${STREAM_KEY:-obs}"

        INPUT_SOURCE="rtmp://0.0.0.0:${LISTEN_PORT}/${APP_NAME}/${STREAM_KEY}"

        echo
        echo "Configure OBS with:"
        echo
        echo "  Server:"
        echo "    rtmp://<RELAY_IP>:${LISTEN_PORT}/${APP_NAME}"
        echo
        echo "  Stream Key:"
        echo "    ${STREAM_KEY}"
        echo

        ;;


    *)

        echo "Error: invalid input mode."
        exit 1

        ;;

esac


# ------------------------------------------------------------
# 4. Ask for the number of RTMP destinations
# ------------------------------------------------------------

read -rp "How many RTMP output endpoints? [minimum 2]: " OUTPUT_COUNT


if ! [[ "$OUTPUT_COUNT" =~ ^[0-9]+$ ]]; then
    echo "Error: enter a valid number."
    exit 1
fi


if [ "$OUTPUT_COUNT" -lt 2 ]; then
    echo "Error: at least two RTMP outputs are required."
    exit 1
fi


if [ "$OUTPUT_COUNT" -gt 16 ]; then
    echo "Error: the relay currently supports a maximum of 16 outputs."
    exit 1
fi


echo


# ------------------------------------------------------------
# 5. Read RTMP endpoints
# ------------------------------------------------------------

OUTPUTS=()


for ((i = 1; i <= OUTPUT_COUNT; i++)); do

    while true; do

        read -rp "RTMP endpoint #$i: " ENDPOINT

        if [ -z "$ENDPOINT" ]; then
            echo "Error: endpoint cannot be empty."
            continue
        fi


        if [[ "$ENDPOINT" != rtmp://* ]]; then
            echo
            echo "Error: this version supports RTMP only."
            echo
            echo "The endpoint must start with:"
            echo "  rtmp://"
            echo
            continue
        fi


        OUTPUTS+=("$ENDPOINT")

        break

    done

done


# ------------------------------------------------------------
# 6. Display configuration
# ------------------------------------------------------------

echo
echo "========================================"
echo "          Stream Configuration"
echo "========================================"
echo

echo "Input:"
echo "  $INPUT_SOURCE"
echo


if [ -n "$LISTEN_FLAG" ]; then
    echo "Mode:"
    echo "  RTMP server / waiting for encoder"
    echo
fi


echo "Outputs:"

for ((i = 0; i < ${#OUTPUTS[@]}; i++)); do
    echo "  $((i + 1))) ${OUTPUTS[$i]}"
done


echo
echo "========================================"
echo


# ------------------------------------------------------------
# 7. Confirmation
# ------------------------------------------------------------

read -rp "Start streaming? [y/N]: " CONFIRM


if [[ ! "$CONFIRM" =~ ^[yY]$ ]]; then
    echo
    echo "Cancelled."
    exit 0
fi


# ------------------------------------------------------------
# 8. Start the relay
# ------------------------------------------------------------

echo
echo "Starting Multi RTMP Stream Relay..."
echo


if [ -n "$LISTEN_FLAG" ]; then

    echo "Waiting for an incoming RTMP connection..."
    echo "Press Ctrl+C to stop."
    echo

else

    echo "Streaming input to configured endpoints..."
    echo "Press Ctrl+C to stop."
    echo

fi


"$BINARY_PATH" \
    $LISTEN_FLAG \
    "$INPUT_SOURCE" \
    "${OUTPUTS[@]}"
