#!/bin/sh
# Not a build: the native comparator for the lab timings. Run as a Cradle
# job on the Linux host (target tests/native), it fetches the same file from
# test.rebex.net over FTP, FTPS and SFTP with the host's curl, three times
# each, and prints the times in ms in the same shape fzprobe does.
# Copyright (c) 2026 AmiCradle Team
set -u
F=${F:-pub/example/WinFormClient.png}
curl -V | head -2
t() { # proto url extra...
    p=$1; u=$2; shift 2
    for i in 1 2 3; do
        out=$(curl -s -o /dev/null --max-time 60 "$@" -w '%{time_connect} %{time_appconnect} %{time_pretransfer} %{time_starttransfer} %{time_total} %{size_download} %{http_code}' "$u" 2>&1) || out="$out (curl rc $?)"
        echo "NATIVE $p $out" | awk '{ if (NF >= 8) printf "NATIVE %s connect_ms=%.0f tls_ms=%.0f login_ms=%.0f first_byte_ms=%.0f total_ms=%.0f bytes=%s\n", $2, $3*1000, $4*1000, $5*1000, $6*1000, $7*1000, $8; else print }'
    done
}
t ftp "ftp://demo:password@test.rebex.net/$F"
t ftps "ftp://demo:password@test.rebex.net/$F" --ssl-reqd
t sftp "sftp://demo:password@test.rebex.net/$F" --insecure
exit 0
