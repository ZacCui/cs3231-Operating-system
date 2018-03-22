set can-use-hw-watchpoints 0
define asst0
dir ~/cs3231/asst0-src/kern/compile/ASST0
target remote unix:.sockets/gdb
b panic
end
