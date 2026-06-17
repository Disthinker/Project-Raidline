#!/usr/bin/env bash
set -euo pipefail

sudo apt-get update
sudo apt-get install -y \
  build-essential \
  ninja-build \
  pkg-config \
  autoconf \
  autoconf-archive \
  automake \
  libtool \
  libx11-dev \
  libxext-dev \
  libxrandr-dev \
  libxrender-dev \
  libxfixes-dev \
  libxi-dev \
  libxcursor-dev \
  libxinerama-dev \
  libxss-dev \
  libxxf86vm-dev \
  libwayland-dev \
  wayland-protocols \
  libxkbcommon-dev \
  libegl1-mesa-dev \
  libgl1-mesa-dev \
  libgles2-mesa-dev \
  libglvnd-dev \
  libdrm-dev \
  libgbm-dev \
  libibus-1.0-dev \
  libdbus-1-dev \
  libudev-dev \
  libasound2-dev \
  libpulse-dev \
  libpipewire-0.3-dev \
  libdecor-0-dev