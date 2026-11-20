# Multi RTMP Stream Relay



A lightweight multi-destination RTMP relay written in C using FFmpeg libraries.



The application receives a single RTMP stream, typically from OBS Studio, and remuxes it to multiple RTMP destinations simultaneously.



No transcoding is performed.



```text

                         OBS Studio

                             |

                             | RTMP

                             v

                    multi_rtmp_stream

                     /       |       \

                    /        |        \

                   v         v         v

               YouTube    Twitch    Other RTMP

```



## Features



- Receive an RTMP stream directly from OBS Studio

- Relay the same stream to multiple RTMP destinations

- No video or audio transcoding

- Low CPU usage

- Automatic reconnection for failed output endpoints

- Automatic input reconnection when OBS disconnects

- Clean shutdown with `Ctrl+C`

- Up to 16 output destinations

- Compatible with common H.264 + AAC live streaming workflows

- Designed for lightweight Linux servers, virtual machines, and LXC containers



## Project Scope



This repository is intentionally focused on **RTMP only**.



RTMPS/TLS support is outside the scope of this version.



The goal is to provide a small, simple, lightweight, and easy-to-debug RTMP fan-out relay.



---



# Architecture



A typical deployment looks like this:



```text

OBS Studio

    |

    | RTMP

    v

+-------------------------+

| multi_rtmp_stream       |

|                         |

| FFmpeg libavformat      |

| packet remuxing only    |

+-----------+-------------+

            |

      +-----+-----+

      |           |

      v           v

   YouTube      Twitch

     RTMP         RTMP

```



The relay does not decode or re-encode the stream.



Encoded audio and video packets received from OBS are copied directly to each configured RTMP output.



---



# Requirements



The project is intended for Linux systems with FFmpeg development libraries installed.



It has primarily been tested and developed on Debian-based systems.



Install the required packages:



```bash

apt update



apt install -y \

    gcc \

    pkg-config \

    libavformat-dev \

    libavcodec-dev \

    libavutil-dev \

    ffmpeg \

    ca-certificates

```



## Package Overview



| Package | Purpose |

|---|---|

| `gcc` | Compiles the C source code |

| `pkg-config` | Locates FFmpeg headers and libraries |

| `libavformat-dev` | RTMP, FLV, muxing, demuxing, and network I/O |

| `libavcodec-dev` | Codec parameter handling |

| `libavutil-dev` | FFmpeg utilities, memory management, timestamps, and errors |

| `ffmpeg` | Useful for testing and troubleshooting streams |

| `ca-certificates` | Standard system certificate store |



The application links dynamically against the FFmpeg libraries installed by the operating system.



---



# Building



Clone the repository:



```bash

git clone https://github.com/YOUR_USERNAME/multi-rtmp-stream.git

cd multi-rtmp-stream

```



Compile:



```bash

gcc multi_rtmp_stream.c \

    -o multi_rtmp_stream \

    $(pkg-config --cflags --libs \

        libavformat \

        libavcodec \

        libavutil)

```



Verify that the executable was created:



```bash

ls -lh multi_rtmp_stream

```



Inspect the dynamically linked libraries:



```bash

ldd ./multi_rtmp_stream

```



Typical FFmpeg dependencies should include:



```text

libavformat.so

libavcodec.so

libavutil.so

```



---



# Usage



Basic syntax:



```bash

./multi_rtmp_stream [-listen] \

    <input> \

    <rtmp_output_1> \

    <rtmp_output_2> \

    [more outputs...]

```



At least two output destinations are required.



---



# OBS Server Mode



The most common use case is `-listen` mode.



In this mode, `multi_rtmp_stream` acts as an RTMP server and waits for OBS Studio or another encoder to connect.



Example:



```bash

./multi_rtmp_stream -listen \

    "rtmp://0.0.0.0:1935/live/obs" \

    "rtmp://a.rtmp.youtube.com/live2/YOUTUBE_STREAM_KEY" \

    "rtmp://TWITCH_INGEST_SERVER/app/TWITCH_STREAM_KEY"

```



The application will wait for an incoming connection:



```text

Waiting for incoming RTMP connection on rtmp://0.0.0.0:1935/live/obs ...

```



---



# OBS Configuration



Assume the relay server has the following IP address:



```text

192.168.1.100

```



Configure OBS Studio using a custom streaming server.



## Server



```text

rtmp://192.168.1.100:1935/live

```



## Stream Key



```text

obs

```



Together, OBS connects to:



```text

rtmp://192.168.1.100:1935/live/obs

```



This matches the listener configured in the relay:



```text

rtmp://0.0.0.0:1935/live/obs

```



`0.0.0.0` means that the relay listens on all available network interfaces.



---



# Example: YouTube and Twitch



```bash

./multi_rtmp_stream -listen \

    "rtmp://0.0.0.0:1935/live/obs" \

    "rtmp://a.rtmp.youtube.com/live2/YOUR_YOUTUBE_STREAM_KEY" \

    "rtmp://YOUR_TWITCH_INGEST_SERVER/app/YOUR_TWITCH_STREAM_KEY"

```



Replace the example ingest servers and stream keys with the values provided by each platform.



Never commit real stream keys to a public repository.



---



# File Input Mode



The relay can also read from a file or another input supported by FFmpeg.



Example:



```bash

./multi_rtmp_stream \

    input.mp4 \

    "rtmp://SERVER_1/app/STREAM_KEY_1" \

    "rtmp://SERVER_2/app/STREAM_KEY_2"

```



This mode is useful for testing.



For actual live broadcasting, `-listen` mode with OBS is usually the preferred configuration.



---



# Recommended OBS Settings



Because the relay does not transcode, every output receives exactly the same encoded stream.



A broadly compatible configuration is:



```text

Video codec:       H.264

Audio codec:       AAC

Resolution:        1920x1080

Frame rate:        30 FPS

Rate control:      CBR

Keyframe interval: 2 seconds

Audio sample rate: 48 kHz

Audio channels:    Stereo

```



The configured bitrate must be accepted by all destination platforms.



If one destination has a lower bitrate limit than the others, configure OBS according to the most restrictive destination.



---



# How It Works



The program uses FFmpeg libraries directly.



The input stream is opened through `libavformat`.



Audio and video packets are then copied to each active output.



```text

Incoming RTMP

     |

     v

+--------------------+

| libavformat input  |

+--------------------+

     |

     | encoded packets

     v

+--------------------+

| multi_rtmp_stream  |

+--------------------+

     |

     +----------+----------+

     |          |          |

     v          v          v

   RTMP       RTMP       RTMP

```



No decoder or encoder is involved.



That means there is:



- no video re-encoding

- no audio re-encoding

- no resolution conversion

- no bitrate conversion

- no codec conversion



This significantly reduces CPU usage compared with transcoding-based multistreaming solutions.



---



# Output Reconnection



Each destination is managed independently.



If one RTMP output fails, the application:



1. marks that destination as disconnected

2. keeps the remaining outputs active

3. waits for the configured retry interval

4. attempts to reconnect the failed destination



Example:



```text

YouTube     CONNECTED

Twitch      CONNECTED

Endpoint 3  DISCONNECTED

               |

               v

          retry after 5s

```



A failed destination does not automatically stop the remaining streams.



---



# OBS Reconnection



When running in `-listen` mode, the application also handles incoming source reconnections.



If OBS disconnects:



```text

OBS

 |

 X

 |

multi_rtmp_stream

```



the program returns to listening mode.



When OBS reconnects:



```text

OBS

 |

 v

multi_rtmp_stream

```



the stream mappings are rebuilt and packet forwarding resumes.



The relay process does not need to be restarted manually.



---



# Running in the Background



For testing, the relay can be started with `nohup`.



Example:



```bash

nohup ./multi_rtmp_stream -listen \

    "rtmp://0.0.0.0:1935/live/obs" \

    "rtmp://SERVER_1/app/KEY_1" \

    "rtmp://SERVER_2/app/KEY_2" \

    > multistream.log 2>&1 &

```



Check whether it is running:



```bash

pgrep -af multi_rtmp_stream

```



Follow the log:



```bash

tail -f multistream.log

```



Stop it cleanly:



```bash

pkill -INT multi_rtmp_stream

```



For permanent deployments, a `systemd` service is recommended.



---



# Network Requirements



The default examples use TCP port:



```text

1935

```



The OBS workstation must be able to reach this port on the relay server.



Check whether the application is listening:



```bash

ss -lntp | grep 1935

```



From another machine, connectivity can be tested with:



```bash

nc -vz RELAY_IP 1935

```



---



# Firewall



The RTMP listener should preferably be accessible only from trusted devices.



For example, with UFW:



```bash

ufw allow from OBS_IP_ADDRESS to any port 1935 proto tcp

```



Avoid exposing TCP port `1935` directly to the public Internet unless it is intentionally required and properly protected.



---



# Security



## Stream Keys



Stream keys are credentials.



Do not commit real stream keys to Git.



Bad:



```text

rtmp://example.com/app/real-secret-stream-key

```



Use placeholders instead:



```text

rtmp://example.com/app/YOUR_STREAM_KEY

```



Local configuration files containing credentials should be excluded from Git.



Example `.gitignore`:



```gitignore

.env

*.conf

endpoints.txt

multistream.log

multi_rtmp_stream

```



## RTMP Listener



The built-in listener does not provide authentication.



A recommended deployment is:



```text

Internet

    X

    |

 Firewall

    |

 Local Network

    |

    +---- OBS

    |

    +---- RTMP Relay

```



Restrict access to the RTMP listener whenever possible.



---



# Testing an RTMP Destination



Before troubleshooting the relay, verify the destination directly with FFmpeg.



Example:



```bash

ffmpeg \

    -re \

    -i input.mp4 \

    -c copy \

    -f flv \

    "rtmp://DESTINATION/app/STREAM_KEY"

```



If the direct FFmpeg command fails, investigate the destination, ingest URL, stream key, firewall, or network connection before troubleshooting `multi_rtmp_stream`.



---



# Troubleshooting



## OBS Cannot Connect



Check whether the relay process is running:



```bash

pgrep -af multi_rtmp_stream

```



Check whether TCP port `1935` is listening:



```bash

ss -lntp | grep 1935

```



Check firewall rules.



Also verify that the OBS server and stream key match the configured listener path.



Example relay input:



```text

rtmp://0.0.0.0:1935/live/obs

```



OBS should use:



```text

Server:

rtmp://RELAY_IP:1935/live



Stream Key:

obs

```



---



## The Relay Connects but the Platform Receives No Video



Verify the incoming codecs.



Recommended:



```text

Video: H.264

Audio: AAC

```



The relay does not transcode unsupported codecs.



---



## One Output Keeps Disconnecting



Test the destination directly:



```bash

ffmpeg \

    -re \

    -i input.mp4 \

    -c copy \

    -f flv \

    "rtmp://DESTINATION/app/STREAM_KEY"

```



Also verify:



- ingest server address

- stream key

- network connectivity

- bitrate limits

- codec requirements

- keyframe interval



---



## Check FFmpeg



Display the installed version:



```bash

ffmpeg -version

```



Check RTMP protocol support:



```bash

ffmpeg -hide_banner -protocols | grep rtmp

```



Check the development library versions:



```bash

pkg-config --modversion libavformat

pkg-config --modversion libavcodec

pkg-config --modversion libavutil

```



---



# LXC Deployment



The relay works well inside an LXC container because no GPU or hardware encoder is required.



Example:



```text

Proxmox VE

    |

    +-- LXC

         |

         +-- Debian

         +-- FFmpeg libraries

         +-- multi_rtmp_stream

         |

         +-- TCP 1935 <- OBS

         |

         +-- RTMP -> YouTube

         +-- RTMP -> Twitch

```



Because the application only remuxes packets, CPU requirements are relatively low.



Network bandwidth is usually more important than CPU capacity.



---



# Bandwidth Considerations



The relay receives one copy of the stream and sends one copy to every destination.



For example:



```text

OBS bitrate: 6 Mbps

Destinations: 3

```



Approximate traffic:



```text

Incoming:

6 Mbps



Outgoing:

18 Mbps



Total:

approximately 24 Mbps

```



Protocol overhead is not included in this simplified example.



The relay server's upload connection must be capable of handling the combined outgoing bitrate.



---



# Resource Usage



Because no transcoding is performed, typical resource usage is modest.



The main requirements are:



- sufficient network bandwidth

- stable connectivity to destination ingest servers

- enough memory for FFmpeg buffers

- sufficient socket and file descriptor capacity



CPU requirements are significantly lower than solutions that encode multiple independent outputs.



---



# Current Limitations



- RTMP only

- No RTMPS/TLS support in this repository

- No transcoding

- No adaptive bitrate

- No per-destination resolution

- No per-destination bitrate

- No codec conversion

- No authentication on the incoming RTMP listener

- No web interface

- No HTTP API

- No persistent configuration file

- No built-in secret management



All destinations receive the same encoded audio and video.



---



# Possible Future Improvements



Potential additions include:



- configuration file support

- environment variable support

- per-output names

- per-output statistics

- packets sent counter

- bytes sent counter

- connection uptime

- reconnect counter

- last error tracking

- JSON status output

- HTTP status endpoint

- Prometheus metrics

- web dashboard

- systemd service template

- installation script

- health checks

- structured logging



---



# Repository Structure



A minimal repository can look like:



```text

multi-rtmp-stream/

|

├── multi_rtmp_stream.c

├── README.md

├── LICENSE

└── .gitignore

```



A more complete version could later include:



```text

multi-rtmp-stream/

|

├── src/

│   └── multi_rtmp_stream.c

|

├── systemd/

│   └── multi-rtmp-stream.service

|

├── scripts/

│   └── install.sh

|

├── README.md

├── LICENSE

└── .gitignore

```



---



# License



The source code in this repository is licensed under the **MIT License**.



See the `LICENSE` file for the full license text.



The MIT License applies only to the original source code and documentation contained in this repository.



It does not replace or modify the licenses of FFmpeg, Debian packages, OBS Studio, or any other third-party software.



---



# Third-Party Software



## FFmpeg



This project uses FFmpeg libraries through their public C APIs.



The following libraries are used:



```text

libavformat

libavcodec

libavutil

```



FFmpeg is a separate project and is not included as source code in this repository.



FFmpeg is primarily licensed under the GNU Lesser General Public License, version 2.1 or later.



Some FFmpeg builds may include GPL-licensed components, which can cause that particular FFmpeg build to be distributed under GPL terms.



For this reason, users and distributors should verify the licensing configuration of the FFmpeg build they use.



Official FFmpeg licensing information:



```text

https://ffmpeg.org/legal.html

```



This project dynamically links against the FFmpeg libraries provided by the operating system.



---



# Binary Distribution



This repository is intended primarily for **source distribution**.



If you distribute a compiled `multi_rtmp_stream` binary together with FFmpeg libraries, a container image, an LXC root filesystem, a virtual appliance, or another prebuilt environment, additional license obligations may apply to the included third-party software.



In particular:



- FFmpeg libraries retain their own licenses

- Debian packages retain their own licenses

- GPL/LGPL notice and source-code obligations may apply

- the entire container or operating system image should not be described as being licensed solely under MIT



The MIT License applies to this project's own source code, not to an entire Linux distribution or prebuilt system image.



---



# Debian and FFmpeg Packages



The recommended installation uses the standard Debian development packages:



```text

libavformat-dev

libavcodec-dev

libavutil-dev

```



This project does not require FFmpeg's optional `-extra` library variants.



If you redistribute Debian or FFmpeg binaries, consult the license information associated with the exact packages and versions being distributed.



---



# Contributing



Contributions, bug reports, and improvement suggestions are welcome.



When reporting an issue, include when possible:



```bash

ffmpeg -version

```



```bash

gcc --version

```



```bash

pkg-config --modversion libavformat

```



You may also include the relay output or relevant logs.



Never include real stream keys or other credentials in bug reports.



---



# Disclaimer



Streaming platforms may change their ingest servers, bitrate limits, codec requirements, authentication methods, and broadcasting specifications at any time.



Always verify the current requirements of each destination platform before using the relay in production.



This project is not affiliated with or endorsed by FFmpeg, OBS Studio, YouTube, Twitch, or any other streaming platform.



The software is provided without warranty under the terms of the MIT License.
