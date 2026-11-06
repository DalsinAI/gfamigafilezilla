#!/bin/sh
# Host tests for libfilezilla on our own libraries, against the original on
# GnuTLS and Nettle (Debian/Ubuntu: nettle-dev libgnutls28-dev libgmp-dev
# vsftpd openssl, and sudo for the FTPS server's user):
#   1. cryptotest: hashes, HMAC, PBKDF2, SHA-1 state, X25519, Ed25519 must
#      give the same bytes as Nettle; encryption must decrypt both ways.
#   2. ftpstest: FTPS (explicit TLS) through tls_layer against a local
#      vsftpd that insists on session reuse: certificate details, download,
#      upload, read back, with and without the CA in the system trust store
#      and under a host name the certificate does not name.
#   GFOPENTLS=path/to/gfopentls (a checkout) WORK=dir tests/host/run.sh
# Copyright (c) 2026 AmiCradle Team
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
. "$HERE/filezilla.lock"
OT=${GFOPENTLS:?a gfopentls checkout}; WORK=${WORK:-$HERE/build-host}
mkdir -p "$WORK/dl"; cd "$WORK"
[ -f dl/lfz.tar.xz ] || curl -fsSL -o dl/lfz.tar.xz "$LFZ_URL"
echo "$LFZ_SHA256  dl/lfz.tar.xz" | sha256sum -c - >/dev/null
cmake -S "$OT" -B otb -DCMAKE_POSITION_INDEPENDENT_CODE=ON >/dev/null
cmake --build otb -j8 --target opentls opencrypto >/dev/null
for v in ref ours; do
    rm -rf "lfz-$v"; mkdir "lfz-$v"; tar xf dl/lfz.tar.xz -C "lfz-$v" --strip-components=1
    if [ $v = ours ]; then
        patch -s -d lfz-ours -p1 < "$HERE/patches/libfilezilla-0007-own-libraries.patch"
        set -- GMP_CFLAGS=-DFZ_NO_GMP GMP_LIBS=-lm NETTLE_CFLAGS="-I$OT/include" NETTLE_LIBS="-L$WORK/otb -lopencrypto" \
            HOGWEED_CFLAGS="-I$OT/include" HOGWEED_LIBS=-lm GNUTLS_CFLAGS="-I$OT/include -I$OT/host/include" \
            GNUTLS_LIBS="-L$WORK/otb -lopentls -lbearssl -lopencrypto"
    else
        set --
    fi
    (cd "lfz-$v" && ./configure --prefix="$WORK/inst-$v" --disable-doxygen-doc --disable-locales CXXFLAGS="-O2 -fPIC" "$@" >configure.log 2>&1 \
        && make -j8 -C lib >make.log 2>&1 && make -C lib install >/dev/null 2>&1) || { echo "libfilezilla ($v) failed"; exit 1; }
    for t in cryptotest ftpstest; do
        g++ -std=c++17 -O1 "$HERE/tests/host/$t.cpp" -I"inst-$v/include" -L"inst-$v/lib" -lfilezilla -lpthread \
            -Wl,-rpath,"$WORK/inst-$v/lib" -o "$t-$v"
    done
done

./cryptotest-ref det > det-ref.txt; ./cryptotest-ours det > det-ours.txt
cmp det-ref.txt det-ours.txt && echo "crypto: $(wc -l < det-ref.txt) results the same as Nettle's"
./cryptotest-ref enc e-ref.txt; ./cryptotest-ours enc e-ours.txt
./cryptotest-ours dec e-ref.txt; ./cryptotest-ref dec e-ours.txt

# FTPS server: a test CA, a P-256 server certificate for localhost
mkdir -p pki; cd pki
openssl req -x509 -newkey rsa:2048 -nodes -keyout ca.key -out ca.crt -days 30 -subj "/CN=Test CA/O=AmiCradle Test" 2>/dev/null
openssl req -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -keyout srv.key -out srv.csr -subj "/CN=localhost/O=Test FTP" 2>/dev/null
printf "subjectAltName=DNS:localhost,DNS:*.test.local,IP:127.0.0.1\n" > ext
openssl x509 -req -in srv.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out srv.crt -days 30 -extfile ext 2>/dev/null
cat srv.crt ca.crt > chain.pem; cd ..
sudo mkdir -p /srv/fztest/pub /etc/fztls /var/run/vsftpd/empty
head -c 3000000 /dev/urandom | sudo tee /srv/fztest/pub/big.bin >/dev/null
echo hello | sudo tee /srv/fztest/pub/small.txt >/dev/null
id fztest >/dev/null 2>&1 || sudo useradd -d /srv/fztest -s /bin/bash fztest
echo 'fztest:fzpass123' | sudo chpasswd; sudo chown -R fztest /srv/fztest
sudo cp pki/chain.pem pki/srv.key /etc/fztls/
sed "s#@PKI@#/etc/fztls#g; s#@WORK@#$WORK#g" "$HERE/tests/host/vsftpd.conf.in" | sudo tee /etc/fztls/vsftpd.conf >/dev/null
sudo pkill vsftpd || :; sudo /usr/sbin/vsftpd /etc/fztls/vsftpd.conf; sleep 1
grep -q wrong.example /etc/hosts || echo "127.0.0.1 wrong.example" | sudo tee -a /etc/hosts >/dev/null
for v in ref ours; do
    echo "== $v"; ./ftpstest-$v | grep -E "certificate event|^== |RESULT"
    FZHOST=wrong.example ./ftpstest-$v | grep -E "certificate event|RESULT"
done
