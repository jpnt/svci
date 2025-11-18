# svci - service manager interface

## what is a init system?

"Init system" is a broad term with too much noise around it.

Lets clear things out. To give a composable, objetive definition: a init system is 4 elements:

1. init; program the kernel loads at boot (/sbin/init); its job is to start pid 1.
2. pid 1; first userspace process; global responsibility for process lifecycle: kill zombies and shutdown/reboot.
3. supervisor; executes daemons in a controlled environment: restarts them on failure, manage their stdout/stderr, etc.
4. service manager; orchestration layer: define boot order, dependencies, readiness, enable/disable state, "start/stop/restart" logic, transitions the machine from nothing running to fully up.

## and what is svci?

svci is a unified interface for the service manager layer. The objective is to work
on multiple init systems using the same command.

Think like this if you prefer: svci is the frontend to whatever service manager 
backend your system is currently running.

## but why?

I use various Linux distros, some use systemd, others openrc, in my main
PCs I use runit and I am experimenting with s6.

Naturally, I get confused sometimes. I want a program with a unified interface
to manage my services without me having to think which service manager I am working
under. Most of the time I am just doing simple service obvervation: list, start, stop, restart
services.

This program is not an excuse to not learn the actual CLI of your service manager.
This software is for lazy people doing simple tasks. This tool is also designed to
work in programs that cannot make assumptions about the host init system.

## Architecture and API

#### Architecture guide to software:

(TODO) (Design it! + Release it! + The Art of UNIX programming + my own opinions)
https://www.jpnt.github.io/posts/software-architecture.md

#### API:

[svci header file](./svci.h)
Defined in the header file as an interface/contract. Libraries may use these public functions.
