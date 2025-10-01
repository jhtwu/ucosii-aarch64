#!/bin/sh

sudo ip link add br-lan type bridge
sudo ip tuntap add tap-lan mode tap user root
sudo brctl addif br-lan enp0s3
sudo brctl addif br-lan tap-lan
sudo ip addr flush dev enp0s3
sudo ifconfig br-lan 192.168.1.134
sudo ifconfig br-lan up
sudo ifconfig tap-lan up

sudo ip link add br-wan type bridge
sudo ip tuntap add tap-wan mode tap user root
sudo brctl addif br-wan enp0s8
sudo brctl addif br-wan tap-wan
sudo ip addr flush dev enp0s3
sudo ifconfig br-wan 172.17.5.134
sudo ifconfig br-wan up
sudo ifconfig tap-wan up

