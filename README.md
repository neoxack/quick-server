quick-server
============
quick-server is a library written in C that enables the user to develop servers quickly and easily. It manages client connections, sends and receives data asynchronously and organizes a pool of threads.
quick-server based on Input/Output Completion Port (IOCP) mechanism. Commonly acknowledged to be the most powerful tool for building servers in Windows OS, this mechanism allows quick-server to serve up to several thousand queries per second, making it the best in terms of performance.

IPv6 support
------------
To use ipv6 #define USE_IPV6 
in qs_lib.h

status
------
beta

