#!/bin/sh
# What the stove's C library, bsdsocket glue and pthreads give: the calls
# libfilezilla and fzssh look for. Prints one line for each.
#   TC=.../prefix scripts/probe.sh
# Copyright (c) 2026 AmiCradle Team
TC=${TC:?}
CC="$TC/bin/m68k-amigaos-gcc"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
have() {  # name, the headers, the call
    printf '%s\n' "$2" > "$T/t.c"
    printf 'int main(void){ %s; return 0; }\n' "$3" >> "$T/t.c"
    if "$CC" -noixemul -m68040 -o "$T/t" "$T/t.c" -lsocket -lpthread >/dev/null 2>"$T/err"; then echo "have $1"
    else echo "NO   $1: $(grep -m1 -E 'error|undefined' "$T/err" | cut -c1-140)"; fi
}
have poll '#include <poll.h>' 'struct pollfd p; poll(&p,1,0)'
have socketpair '#include <sys/socket.h>' 'int s[2]; socketpair(AF_UNIX,SOCK_STREAM,0,s)'
have pipe '#include <unistd.h>' 'int p[2]; pipe(p)'
have getaddrinfo '#include <netdb.h>' 'struct addrinfo *r; getaddrinfo("a","1",0,&r)'
have inet_pton '#include <arpa/inet.h>' 'char b[16]; inet_pton(AF_INET6,"::1",b)'
have inet_ntop '#include <arpa/inet.h>' 'char b[64]; inet_ntop(AF_INET,b,b,64)'
have getentropy '#include <unistd.h>' 'char b[8]; getentropy(b,8)'
have getrandom '#include <sys/random.h>' 'char b[8]; getrandom(b,8,0)'
have clock_gettime '#include <time.h>' 'struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t)'
have pthread_condattr_setclock '#include <pthread.h>' 'pthread_condattr_t a; pthread_condattr_setclock(&a,CLOCK_MONOTONIC)'
have fcntl_nonblock '#include <fcntl.h>' 'fcntl(0,F_SETFL,O_NONBLOCK)'
have ioctl_fionbio '#include <sys/ioctl.h>' 'long one=1; ioctl(0,FIONBIO,&one)'
have IoctlSocket '#include <proto/socket.h>' 'long one=1; IoctlSocket(0,FIONBIO,(char*)&one)'
have utimes '#include <sys/time.h>' 'struct timeval t[2]; utimes("a",t)'
have futimens '#include <sys/stat.h>' 'struct timespec t[2]; futimens(0,t)'
have statvfs '#include <sys/statvfs.h>' 'struct statvfs s; statvfs("a",&s)'
have iconv '#include <iconv.h>' 'iconv_open("UTF-8","UTF-16")'
have nl_langinfo '#include <langinfo.h>' 'nl_langinfo(CODESET)'
have getpwuid '#include <pwd.h>' 'getpwuid(0)'
have fork '#include <unistd.h>' 'fork()'
have mmap '#include <sys/mman.h>' 'mmap(0,1,0,0,0,0)'
have strerror_r '#include <string.h>' 'char b[8]; strerror_r(1,b,8)'
have localtime_r '#include <time.h>' 'time_t t=0; struct tm m; localtime_r(&t,&m)'
have timegm '#include <time.h>' 'struct tm m={0}; timegm(&m)'
have wcsnlen '#include <wchar.h>' 'wcsnlen(L"a",1)'
have mbsrtowcs '#include <wchar.h>' 'mbstate_t s={0}; const char*p="a"; wchar_t w[2]; mbsrtowcs(w,&p,2,&s)'
have setsockopt_nodelay '#include <sys/socket.h>
#include <netinet/tcp.h>' 'int o=1; setsockopt(0,IPPROTO_TCP,TCP_NODELAY,&o,sizeof o)'
have WaitSelect '#include <proto/socket.h>' 'WaitSelect(0,0,0,0,0,0)'
printf '#include <thread>\nint main(){ std::thread t([]{}); t.join(); return 0; }\n' > "$T/t.cpp"
if "$TC/bin/m68k-amigaos-g++" -std=c++17 -noixemul -m68040 -o "$T/t" "$T/t.cpp" -lpthread -latomic >/dev/null 2>"$T/err"; then echo "have std::thread"
else echo "NO   std::thread: $(grep -m1 -E 'error|undefined' "$T/err" | cut -c1-140)"; fi
echo "headers: $(for h in sys/socket.h netinet/in.h netdb.h poll.h sys/select.h pthread.h iconv.h langinfo.h; do [ -n "$(echo "#include <$h>" | "$CC" -noixemul -E -x c - 2>/dev/null | head -1)" ] && printf '%s ' "$h" || printf '!%s ' "$h"; done)"
# where the socket glue lives, and how it finds SocketBase per task
P="$TC/m68k-amigaos"
for l in $(find "$P/lib" "$TC/lib" -name 'libsocket*.a' -o -name 'libpthread*.a' 2>/dev/null | grep -v '/m68020\|/m68881\|/m68060\|/fpu\|/baserel' | head -4); do
    echo "lib $l: $("$TC/bin/m68k-amigaos-nm" -g "$l" 2>/dev/null | grep -E ' [TDBC] ' | grep -ciE 'socket|base') symbols;" \
        "$("$TC/bin/m68k-amigaos-nm" -g "$l" 2>/dev/null | grep -E ' [TDBC] ' | grep -iE 'SocketBase|__init|getaddrinfo|inet_pton|socketpair|ObtainSocket|errno' | awk '{print $3}' | sort -u | head -20 | tr '\n' ' ')"
done
echo "getaddrinfo in headers: $(grep -Rl 'getaddrinfo' "$P/include" "$P/sys-include" 2>/dev/null | head -5 | tr '\n' ' ')"
echo "SocketBase in headers: $(grep -RlE '\bSocketBase\b' "$P/include" "$P/sys-include" 2>/dev/null | head -6 | tr '\n' ' ')"
grep -n -E 'SocketBase|__thread|per.task|per task' "$P/sys-include/sys/socket.h" "$P/include/sys/socket.h" 2>/dev/null | head -8
for w in 'struct addrinfo' 'EAI_NONAME' 'AI_PASSIVE' 'NI_MAXHOST' 'socklen_t' 'struct in6_addr' 'AF_INET6' 'sockaddr_in6' 'sockaddr_storage' 'IPPROTO_TCP' 'TCP_NODELAY' 'SO_KEEPALIVE' 'MSG_NOSIGNAL' 'SHUT_WR' 'EINPROGRESS' 'EWOULDBLOCK' 'ECONNREFUSED' 'struct pollfd' 'POLLRDHUP' 'gethostbyname_r' 'struct statvfs' 'mbstate_t' 'wcrtomb' 'ssize_t'; do
    echo "def '$w': $(grep -RlF "$w" "$P/include" "$P/sys-include" 2>/dev/null | sed "s|$P/||" | head -3 | tr '\n' ' ')"
done
for f in netdb.h sys/socket.h netinet/in.h; do for d in "$P/sys-include" "$P/include"; do [ -f "$d/$f" ] && { echo "--- $d/$f: $(wc -l < "$d/$f") lines"; grep -nE '^(struct|extern|int|#define (AF_|PF_|SOCK_|EAI|AI_|IPPROTO))' "$d/$f" | head -30; }; done; done
# libfilezilla's file, directory and string calls: declared where, and in which library
LIBS=$(find "$P/lib" "$P/libnix/lib" "$TC/lib" -maxdepth 2 -name 'lib*.a' 2>/dev/null | grep -v '/m68020\|/m68881\|/m68060\|/fpu\|/baserel\|/libb\|/libm0' | head -40)
for s in ftruncate futimens utimensat utimes utime realpath readlink symlink lstat dirfd fdopendir fstatat \
    wcsncasecmp wcscasecmp mbsnrtowcs wcsnrtombs strcasecmp dlsym shm_open munmap mmap fsync getuid \
    __stdfiledes _stdfiledes __fdtofh fdtofh __getfh __fhtofd chmod mkdir access; do
    d=$(grep -RlE "\b$s\b *\(" "$P/include" "$P/sys-include" "$P/libnix/include" 2>/dev/null | sed "s|$P/||" | head -2 | tr '\n' ' ')
    l=$(for a in $LIBS; do "$TC/bin/m68k-amigaos-nm" -g "$a" 2>/dev/null | grep -qE " [TDBC] _?$s\$" && printf '%s ' "${a##*/}"; done)
    echo "sym $s: decl[$d] lib[$l]"
done
for d in "$P/sys-include" "$P/include" "$P/libnix/include"; do
    [ -f "$d/sys/stat.h" ] && { echo "--- $d/sys/stat.h"; grep -nE 'st_size|st_mtime|st_mtim|off_t|UTIME|AT_' "$d/sys/stat.h" | head -12; }
    [ -f "$d/fcntl.h" ] && { echo "--- $d/fcntl.h"; grep -nE 'define (O_|AT_|F_)' "$d/fcntl.h" | head -30; }
    [ -f "$d/stdio.h" ] && grep -nE 'stdfiledes|_fh|BPTR' "$d/stdio.h" | head -6
done
grep -RhE 'typedef .*off_t;' "$P/include" "$P/sys-include" 2>/dev/null | sort -u | head -4
echo "dlfcn: $(ls "$P"/*/dlfcn.h "$P"/include/dlfcn.h 2>/dev/null | tr '\n' ' ')"
