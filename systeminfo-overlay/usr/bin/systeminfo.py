#!/usr/bin/env python3

import ipaddress
import json
import os
import re
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from datetime import datetime


def read_cpu_times():
    try:
        with open("/proc/stat", "r") as file:
            values = file.readline().split()[1:]

        times = [int(value) for value in values]
        idle = times[3]
        if len(times) > 4:
            idle += times[4]

        return sum(times), idle
    except (OSError, ValueError, IndexError):
        return 0, 0


def get_datetime():
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def get_uptime():
    try:
        with open("/proc/uptime", "r") as file:
            return int(float(file.read().split()[0]))
    except (OSError, ValueError, IndexError):
        return 0


def get_cpu_info():
    model = "Unknown"
    speed_mhz = 0

    try:
        with open("/proc/cpuinfo", "r") as file:
            for line in file:
                if ":" not in line:
                    continue

                key, value = line.split(":", 1)
                key = key.strip().lower()
                value = value.strip()

                if key == "model name":
                    model = value
                elif model == "Unknown" and key in ("processor", "hardware"):
                    if not value.isdigit():
                        model = value
                if speed_mhz == 0 and key == "cpu mhz":
                    speed_mhz = round(float(value), 2)
    except (OSError, ValueError):
        pass

    try:
        frequency_path = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq"
        with open(frequency_path, "r") as file:
            speed_mhz = round(int(file.read().strip()) / 1000, 2)
    except (OSError, ValueError):
        pass

    total_before, idle_before = read_cpu_times()
    time.sleep(0.1)
    total_after, idle_after = read_cpu_times()

    total_delta = total_after - total_before
    idle_delta = idle_after - idle_before

    if total_delta > 0:
        usage_percent = 100 * (total_delta - idle_delta) / total_delta
        usage_percent = round(max(0.0, min(100.0, usage_percent)), 1)
    else:
        usage_percent = 0.0

    return {
        "model": model,
        "speed_mhz": speed_mhz,
        "usage_percent": usage_percent
    }


def get_memory_info():
    values = {}

    try:
        with open("/proc/meminfo", "r") as file:
            for line in file:
                key, value = line.split(":", 1)
                values[key] = int(value.split()[0])
    except (OSError, ValueError, IndexError):
        return {
            "total_mb": 0,
            "used_mb": 0
        }

    total_kb = values.get("MemTotal", 0)
    available_kb = values.get("MemAvailable")

    if available_kb is None:
        available_kb = (
            values.get("MemFree", 0)
            + values.get("Buffers", 0)
            + values.get("Cached", 0)
        )

    used_kb = max(0, total_kb - available_kb)

    return {
        "total_mb": total_kb // 1024,
        "used_mb": used_kb // 1024
    }


def get_os_version():
    try:
        with open("/proc/version", "r") as file:
            return file.read().strip()
    except OSError:
        return "Unknown"


def get_process_list():
    processes = []

    try:
        entries = os.listdir("/proc")
    except OSError:
        return processes

    for entry in entries:
        if not entry.isdigit():
            continue

        try:
            with open(f"/proc/{entry}/comm", "r") as file:
                name = file.read().strip()

            processes.append({
                "pid": int(entry),
                "name": name
            })
        except (OSError, ValueError):
            continue

    processes.sort(key=lambda process: process["pid"])
    return processes


def get_disks():
    disks = []

    try:
        devices = os.listdir("/sys/block")
    except OSError:
        return disks

    for device in sorted(devices):
        if device.startswith(("loop", "ram", "sr")):
            continue

        try:
            with open(f"/sys/block/{device}/size", "r") as file:
                sectors = int(file.read().strip())
            if sectors <= 0:
                continue


            disks.append({
                "device": f"/dev/{device}",
                "size_mb": sectors * 512 // (1024 * 1024)
            })
        except (OSError, ValueError):
            continue

    return disks


def get_usb_devices():
    usb_devices = []
    usb_path = "/sys/bus/usb/devices"

    try:
        devices = os.listdir(usb_path)
    except OSError:
        return usb_devices

    for port in sorted(devices):
        if "-" not in port or ":" in port or port.startswith("usb"):
            continue

        description_parts = []

        for filename in ("manufacturer", "product"):
            try:
                with open(f"{usb_path}/{port}/{filename}", "r") as file:
                    value = file.read().strip()
                if value:
                    description_parts.append(value)
            except OSError:
                continue

        if not description_parts:
            continue

        usb_devices.append({
            "port": port,
            "description": " ".join(description_parts)
        })

    return usb_devices


def get_network_adapters():
    network_path = "/sys/class/net"

    try:
        interfaces = sorted(os.listdir(network_path))
    except OSError:
        return []

    local_addresses = []
    current_address = None

    try:
        with open("/proc/net/fib_trie", "r") as file:
            for line in file:
                match = re.search(r"(\d{1,3}(?:\.\d{1,3}){3})", line)
                if match:
                    current_address = match.group(1)
                elif "host LOCAL" in line and current_address:
                    try:
                        ipaddress.IPv4Address(current_address)
                        if current_address not in local_addresses:
                            local_addresses.append(current_address)
                    except ipaddress.AddressValueError:
                        pass
    except OSError:
        pass

    routes = []

    try:
        with open("/proc/net/route", "r") as file:
            next(file)

            for line in file:
                fields = line.split()
                if len(fields) < 8:
                    continue

                interface = fields[0]
                destination = int.from_bytes(bytes.fromhex(fields[1]), "little")
                mask = int.from_bytes(bytes.fromhex(fields[7]), "little")

                if mask != 0:
                    routes.append((interface, destination, mask))
    except (OSError, ValueError, StopIteration):
        pass

    addresses_by_interface = {interface: "" for interface in interfaces}

    for address in sorted(local_addresses, key=ipaddress.IPv4Address):
        if address.startswith("127.") and "lo" in addresses_by_interface:
            addresses_by_interface["lo"] = address
            continue

        address_value = int(ipaddress.IPv4Address(address))
        matching_routes = []

        for interface, destination, mask in routes:
            if interface not in addresses_by_interface:
                continue
            if address_value & mask == destination & mask:
                matching_routes.append((bin(mask).count("1"), interface))

        if matching_routes:
            _, interface = max(matching_routes)
            if not addresses_by_interface[interface]:
                addresses_by_interface[interface] = address

    return [
        {
            "interface": interface,
            "ip_address": addresses_by_interface[interface]
        }
        for interface in interfaces
    ]


class StatusHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path != "/status":
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b"Not Found")
            return

        response = {
            "datetime": get_datetime(),
            "uptime_seconds": get_uptime(),
            "cpu": get_cpu_info(),
            "memory": get_memory_info(),
            "os_version": get_os_version(),
            "processes": get_process_list(),
            "disks": get_disks(),
            "usb_devices": get_usb_devices(),
            "network_adapters": get_network_adapters()
        }

        data = json.dumps(response, indent=2).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def run_server(port=8080):
    print(f"Servidor disponível em http://0.0.0.0:{port}/status")
    server = HTTPServer(("0.0.0.0", port), StatusHandler)
    server.serve_forever()


if __name__ == "__main__":
    run_server()
