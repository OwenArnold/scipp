#!/bin/sh

# C++17
sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
sudo apt-get update -qq
pyenv global system 3.7
sudo apt install clang-format-5.0
sudo apt install python3-pip
echo "Before pip upgrade"
pip install --upgrade pip --user
echo "after pip upgrade"
pip --version
python3 -m pip install flake8
