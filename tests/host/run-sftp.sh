#!/bin/sh
# Host tests for fzssh on GFOpenCrypto, against the original on Nettle, GMP
# and Argon2. Run tests/host/run.sh first (it builds both libfilezillas and
# GFOpenCrypto in WORK). Needs meson, ninja, libargon2-dev, libgmp-dev,
# openssh-server and sudo.
#   1. shaketest: SHAKE128/256 (ML-KEM's) against Python's hashlib.
#   2. fzssh's own key file test (it expects Ed448, DES and 3DES refused).
#   3. sftptest against a local OpenSSH: every key exchange, cipher, MAC,
#      host key and login key OpenSSH has, each a 4 MB download and a 1 MB
#      upload checked by SHA-256, ours next to the reference, with times.
#   GFOPENTLS=path/to/gfopentls WORK=dir tests/host/run-sftp.sh
# Copyright (c) 2026 AmiCradle Team
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
. "$HERE/filezilla.lock"
OT=${GFOPENTLS:?a gfopentls checkout}; WORK=${WORK:-$HERE/build-host}
cd "$WORK"
[ -d inst-ours ] && [ -d inst-ref ] && [ -f otb/libopencrypto.a ] || { echo "run tests/host/run.sh first"; exit 1; }
NV=3.10.2; NURL=$U/main/n/nettle/nettle_$NV.orig.tar.gz; NSHA=fe9ff51cb1f2abb5e65a6b8c10a92da0ab5ab6eaf26e7fc2b675c45f1fb519b5
[ -f dl/nettle.tar.gz ] || curl -fsSL -o dl/nettle.tar.gz "$NURL"
echo "$NSHA  dl/nettle.tar.gz" | sha256sum -c - >/dev/null
if [ ! -d nettle-inst ]; then
    rm -rf nettle; mkdir nettle; tar xf dl/nettle.tar.gz -C nettle --strip-components=1
    (cd nettle && ./configure --prefix="$WORK/nettle-inst" --disable-documentation --disable-openssl >/dev/null && make -j8 >/dev/null 2>&1 && make install >/dev/null 2>&1)
fi
NLIB=$(dirname "$(ls -d "$WORK"/nettle-inst/lib*/pkgconfig)")
mkdir -p pc
printf 'Name: gfopencrypto\nDescription: host build\nVersion: 2.4\nCflags: -I%s/include\nLibs: -L%s/otb -lopencrypto\n' "$OT" "$WORK" > pc/gfopencrypto.pc
[ -f dl/fzssh.tar.xz ] || curl -fsSL -o dl/fzssh.tar.xz "$FZSSH_URL"
echo "$FZSSH_SHA256  dl/fzssh.tar.xz" | sha256sum -c - >/dev/null
for v in ref ours; do
    rm -rf "fzssh-$v" "fzb-$v"; mkdir "fzssh-$v"; tar xf dl/fzssh.tar.xz -C "fzssh-$v" --strip-components=1
    if [ $v = ours ]; then
        patch -s -d fzssh-ours -p1 < "$HERE/patches/fzssh-0001-own-libraries.patch"
        P="$WORK/pc:$WORK/inst-ours/lib/pkgconfig"
    else
        P="$NLIB/pkgconfig:$WORK/inst-ref/lib/pkgconfig"
    fi
    (cd "fzssh-$v" && PKG_CONFIG_PATH=$P meson setup --buildtype=release "$WORK/fzb-$v" >/dev/null && ninja -C "$WORK/fzb-$v" >/dev/null) || { echo "fzssh ($v) failed"; exit 1; }
    g++ -std=c++17 -O2 "$HERE/tests/host/sftptest.cpp" -I"fzssh-$v/lib" -I"fzb-$v" -I"inst-$v/include" \
        -L"fzb-$v" -lfzssh-client -lfzssh -L"inst-$v/lib" -lfilezilla -o "sftptest-$v"
done
LP_ours="$WORK/fzb-ours:$WORK/inst-ours/lib"; LP_ref="$WORK/fzb-ref:$WORK/inst-ref/lib:$NLIB"

g++ -std=c++17 -I fzssh-ours/lib -I fzssh-ours/lib/crypt -I inst-ours/include -I "$OT/include" "$HERE/tests/host/shaketest.cpp" \
    fzssh-ours/lib/crypt/util.cpp -L inst-ours/lib -lfilezilla otb/libopencrypto.a -o shaketest
LD_LIBRARY_PATH=$LP_ours ./shaketest > shake.txt
python3 -I -c "
import hashlib,sys
m=bytes((i*13+5)&255 for i in range(300)); bad=0; n=0
for l in open(sys.argv[1]):
    v,L,h=l.split(); f=hashlib.shake_128 if v=='128' else hashlib.shake_256
    n+=1; bad+=f(m[:int(L)]).hexdigest(400)!=h
print('shake: %d cases, %d wrong'%(n,bad)); sys.exit(bad!=0)" shake.txt
(cd fzb-ours && LD_LIBRARY_PATH=$LP_ours meson test -t 10 >/dev/null 2>&1) && echo "keyfile_test: OK" || { echo "keyfile_test: FAIL"; exit 1; }

# OpenSSH on 127.0.0.1:2222 with every host key type, the test user's keys
D=/etc/fzssh; sudo mkdir -p $D /run/sshd
for t in rsa ed25519; do [ -f $D/host_$t ] || sudo ssh-keygen -q -t $t -N "" -f $D/host_$t; done
for b in 256 384 521; do [ -f $D/host_ecdsa$b ] || sudo ssh-keygen -q -t ecdsa -b $b -N "" -f $D/host_ecdsa$b; done
mkdir -p keys
for t in rsa ed25519; do [ -f keys/id_$t ] || ssh-keygen -q -t $t -N "" -f keys/id_$t; done
for b in 256 384 521; do [ -f keys/id_ecdsa$b ] || ssh-keygen -q -t ecdsa -b $b -N "" -f keys/id_ecdsa$b; done
cat keys/*.pub | sudo tee $D/authorized_keys >/dev/null
sudo tee $D/sshd_config >/dev/null <<CFG
Port 2222
ListenAddress 127.0.0.1
HostKey $D/host_rsa
HostKey $D/host_ed25519
HostKey $D/host_ecdsa256
HostKey $D/host_ecdsa384
HostKey $D/host_ecdsa521
AuthorizedKeysFile $D/authorized_keys
PasswordAuthentication no
StrictModes no
UsePAM no
PidFile $D/sshd.pid
Subsystem sftp internal-sftp
KexAlgorithms +diffie-hellman-group14-sha1,diffie-hellman-group-exchange-sha1,diffie-hellman-group16-sha512,diffie-hellman-group18-sha512,diffie-hellman-group14-sha256,diffie-hellman-group-exchange-sha256,ecdh-sha2-nistp256,ecdh-sha2-nistp384,ecdh-sha2-nistp521,curve25519-sha256
Ciphers +aes128-cbc,aes192-cbc,aes256-cbc
MACs +hmac-sha1-96,hmac-sha1,hmac-sha2-512
HostKeyAlgorithms +ssh-rsa
PubkeyAcceptedAlgorithms +ssh-rsa
CFG
[ -f $D/sshd.pid ] && sudo kill "$(cat $D/sshd.pid)" 2>/dev/null || :
sleep 1; sudo /usr/sbin/sshd -f $D/sshd_config -E $D/sshd.log; sleep 1
id fztest >/dev/null 2>&1 || sudo useradd -d /srv/fztest -s /bin/bash fztest
sudo mkdir -p /srv/fztest/sftp; head -c 4194304 /dev/urandom | sudo tee /srv/fztest/sftp/down.bin >/dev/null
sudo chown -R fztest /srv/fztest; DSHA=$(sha256sum < /srv/fztest/sftp/down.bin | cut -c1-64)

fails=0
run() { # name kex cipher mac hostkey key
    line="$1"
    for v in ours ref; do
        eval lp=\$LP_$v
        out=$(LD_LIBRARY_PATH=$lp timeout 300 ./sftptest-$v 2222 fztest keys/$6 "$2" "$3" "$4" "$5" sftp/down.bin sftp/up-$v.bin 1000000 2>&1) || :
        if echo "$out" | grep -q '^OK' && [ "$(echo "$out" | sed -n 's/^down .*sha256=//p')" = "$DSHA" ] \
            && [ "$(sudo sha256sum < /srv/fztest/sftp/up-$v.bin | cut -c1-64)" = "$(echo "$out" | sed -n 's/^up .*sha256=//p')" ]; then
            line="$line | PASS $(echo "$out" | sed -n 's/^ms //p')"
        else
            line="$line | FAIL"; [ $v = ours ] && fails=$((fails + 1))
        fi
    done
    echo "$line"
}
echo "case | ours | reference (milliseconds)"
for k in curve25519-sha256 curve25519-sha256@libssh.org diffie-hellman-group14-sha256 diffie-hellman-group16-sha512 diffie-hellman-group18-sha512 diffie-hellman-group-exchange-sha256 diffie-hellman-group-exchange-sha1 diffie-hellman-group14-sha1 ecdh-sha2-nistp256 ecdh-sha2-nistp384 ecdh-sha2-nistp521; do
    run "kex $k" $k aes256-gcm@openssh.com hmac-sha2-256 ssh-ed25519 id_ed25519
done
for c in aes128-gcm@openssh.com aes256-gcm@openssh.com aes128-ctr aes192-ctr aes256-ctr aes128-cbc aes192-cbc aes256-cbc; do
    run "cipher $c" curve25519-sha256 $c hmac-sha2-256 ssh-ed25519 id_ed25519
done
for m in hmac-sha2-256 hmac-sha2-512 hmac-sha1 hmac-sha1-96 hmac-sha2-256-etm@openssh.com; do
    run "mac $m" curve25519-sha256 aes256-ctr $m ssh-ed25519 id_ed25519
done
for h in ssh-ed25519 ecdsa-sha2-nistp256 ecdsa-sha2-nistp384 ecdsa-sha2-nistp521 rsa-sha2-256 rsa-sha2-512 ssh-rsa; do
    run "hostkey $h" curve25519-sha256 aes256-gcm@openssh.com hmac-sha2-256 $h id_ed25519
done
for key in id_ed25519 id_ecdsa256 id_ecdsa384 id_ecdsa521 id_rsa; do
    run "login key $key" curve25519-sha256 aes256-gcm@openssh.com hmac-sha2-256 ssh-ed25519 $key
done
echo "sftp: $fails failures"; [ $fails = 0 ]
