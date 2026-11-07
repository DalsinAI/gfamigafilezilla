// fzprobe: SFTP, FTP and FTPS through the same libfilezilla and fzssh that
// FileZilla links, from the Shell, with every log line timed. For finding
// where a connection stops, and for timing one against a public server:
//   fzprobe sftp HOST PORT USER PASSWORD FILE
//   fzprobe ftps HOST PORT USER PASSWORD FILE     (explicit TLS, data on TLS)
//   fzprobe ftp  HOST PORT USER PASSWORD FILE
// SFTP takes KEX= and CIPHER= from the environment to pick one algorithm.
// Copyright (c) 2026 AmiCradle Team
#include <libfilezilla/event_handler.hpp>
#include <libfilezilla/event_loop.hpp>
#include <libfilezilla/socket.hpp>
#include <libfilezilla/thread_pool.hpp>
#include <libfilezilla/tls_layer.hpp>
#include <libfilezilla/tls_info.hpp>
#include <libfilezilla/tls_system_trust_store.hpp>
#include <libfilezilla/hash.hpp>
#include <libfilezilla/encode.hpp>
#include <libfilezilla/logger.hpp>
#include <libfilezilla/time.hpp>
#include <libfilezilla/util.hpp>
#include <fzssh/client.hpp>
#include <fzssh/sftp/sftp_client.hpp>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <cctype>
#include <unistd.h>

using namespace std::literals;

static fz::monotonic_clock g_start;
static long long ms() { return (fz::monotonic_clock::now() - g_start).get_milliseconds(); }

struct timed_logger final : fz::logger_interface {
	timed_logger() { set_all(fz::logmsg::type(-1)); }
	void do_log(fz::logmsg::type t, std::wstring&& msg) override {
		printf("%7lld [%x] %s\n", ms(), unsigned(t), fz::to_utf8(msg).c_str());
		fflush(stdout);
	}
};

static void say(char const* fmt, std::string const& a = {}) { printf("%7lld ", ms()); printf(fmt, a.c_str()); printf("\n"); fflush(stdout); }

// ---------------------------------------------------------------- SFTP
class sftp_probe final : public fz::event_handler, public fz::ssh::sftp::response_handler
{
public:
	sftp_probe(fz::logger_interface& log, fz::thread_pool& pool, fz::event_loop& l, char** a)
		: fz::event_handler(l), log_(log), pool_(pool), user_(a[4]), pass_(a[5]), file_(a[6])
	{
		if (getenv("KEX")) params_.kex_ = getenv("KEX");
		if (getenv("CIPHER")) params_.cipher_ = getenv("CIPHER");
		params_.single_channel_ = true;
		say("offering ciphers %s", params_.cipher_);
		s_ = std::make_unique<fz::socket>(pool_, this);
		say("connect %s", std::string(a[2]) + ":" + a[3]);
		if (s_->connect(fz::to_native(std::string(a[2])), std::atoi(a[3]))) fail("connect");
	}
	~sftp_probe() { remove_handler(); }
	int result{1};

private:
	void fail(char const* w) { say("FAIL %s", w); fflush(stdout); _exit(10); }
	void operator()(fz::event_base const& ev) override {
		fz::dispatch<fz::socket_event, fz::ssh::auth_done_event, fz::ssh::session_done_event, fz::ssh::hostkey_verification_event,
			fz::ssh::auth_requested_event, fz::ssh::sftp::sftp_client::ready_event, fz::ssh::sftp::sftp_client::done_event>(ev, this,
			&sftp_probe::on_socket, &sftp_probe::on_auth_done, &sftp_probe::on_session_done, &sftp_probe::on_hostkey,
			&sftp_probe::on_auth_requested, &sftp_probe::on_ready, &sftp_probe::on_done);
	}
	void on_socket(fz::socket_event_source* s, fz::socket_event_flag t, int error) {
		if (error) { say("socket error %s", fz::socket_error_description(error)); fail("socket"); return; }
		if (s == s_.get() && t == fz::socket_event_flag::connection) {
			say("TCP connected");
			ssh_ = std::make_unique<fz::ssh::client>(params_, user_, *s_, *this, log_);
		}
	}
	void on_hostkey(fz::ssh::client* c, std::unique_ptr<fz::ssh::public_key> const& k, fz::ssh::algorithm_info const& ai) {
		say("host key: %s", "kex=" + ai.kex_ + " cipher=" + ai.cipher_c2s_ + "/" + ai.cipher_s2c_ + " mac=" + ai.mac_c2s_
			+ " hostkey=" + ai.hostkey_signature_ + " " + k->fingerprint());
		kex_ = ms();
		c->hostkey_decision(true);
	}
	void on_auth_requested(fz::ssh::client*, std::string const& methods, bool partial) {
		say("auth requested: %s", methods + (partial ? " (partial)" : ""));
		if (tried_) { fail("password refused"); return; }
		tried_ = true;
		ssh_->auth_with_password(pass_);
		say("password sent");
	}
	void on_auth_done(fz::ssh::client*) {
		login_ = ms();
		say("auth done, opening sftp");
		auto si = ssh_->open_channel(fz::ssh::channel_type::subsystem, "sftp"sv);
		if (!si) { fail("channel"); return; }
		sftp_ = std::make_unique<fz::ssh::sftp::sftp_client>(std::move(si), *this, log_);
	}
	void on_session_done(fz::ssh::session*) { if (!finished_) fail("session ended"); }
	void on_ready(fz::ssh::sftp::sftp_client*) {
		say("sftp ready, open %s", file_);
		t0_ = ms();
		sftp_->open(this, file_, fz::ssh::sftp::file_flags::SSH_FXF_READ);
	}
	void on_done(fz::ssh::sftp::sftp_client*) { say("sftp done"); }
	fz::ssh::sftp::continuation process_handle(std::string_view h) override {
		handle_ = h; sftp_->read(this, handle_, 0, 32768);
		return fz::ssh::sftp::continuation::next;
	}
	fz::ssh::sftp::continuation process_data(std::string_view d) override {
		hash_.update(d); got_ += d.size(); sftp_->read(this, handle_, got_, 32768);
		return fz::ssh::sftp::continuation::next;
	}
	fz::ssh::sftp::continuation process_status(fz::ssh::sftp::status_code st, std::string_view msg) override {
		if (!closing_ && st == fz::ssh::sftp::status_code::SSH_FX_EOF) {
			long long t1 = ms();
			printf("RESULT sftp kex_ms=%lld login_ms=%lld download_ms=%lld bytes=%llu sha256=%s\n", kex_, login_, t1 - t0_,
				(unsigned long long)got_, fz::hex_encode<std::string>(hash_.digest()).c_str());
			fflush(stdout);
			closing_ = true; sftp_->close(this, handle_);
			return fz::ssh::sftp::continuation::next;
		}
		if (closing_) { finished_ = true; result = 0; event_loop_.stop(); return fz::ssh::sftp::continuation::next; }
		say("sftp status %s", std::to_string(int(st)) + " " + std::string(msg));
		fail("sftp");
		return fz::ssh::sftp::continuation::next;
	}

	fz::logger_interface& log_;
	fz::thread_pool& pool_;
	fz::ssh::client_parameters params_;
	std::string user_, pass_, file_, handle_;
	std::unique_ptr<fz::socket> s_;
	std::unique_ptr<fz::ssh::client> ssh_;
	std::unique_ptr<fz::ssh::sftp::sftp_client> sftp_;
	fz::hash_accumulator hash_{fz::hash_algorithm::sha256};
	uint64_t got_{};
	long long kex_{}, login_{}, t0_{};
	bool tried_{}, closing_{}, finished_{};
};

// ---------------------------------------------------------------- FTP, FTPS
class ftp_probe final : public fz::event_handler
{
public:
	ftp_probe(fz::logger_interface& log, fz::thread_pool& pool, fz::event_loop& l, fz::tls_system_trust_store& ts, bool tls, char** a)
		: fz::event_handler(l), log_(log), pool_(pool), ts_(ts), tls_(tls), host_(a[2]), user_(a[4]), pass_(a[5]), file_(a[6])
	{
		cs_ = std::make_unique<fz::socket>(pool_, this);
		say("connect %s", host_ + ":" + a[3]);
		if (cs_->connect(fz::to_native(host_), std::atoi(a[3]))) fail("connect");
	}
	~ftp_probe() { remove_handler(); }
	int result{1};

private:
	fz::socket_interface& ctl() { return ctl_ ? static_cast<fz::socket_interface&>(*ctl_) : *cs_; }
	void fail(char const* w) { say("FAIL %s", w); fflush(stdout); _exit(10); }
	void send(std::string const& s) {
		say("> %s", s.rfind("PASS", 0) == 0 ? "PASS ****"s : s);
		std::string l = s + "\r\n"; int e;
		if (ctl().write(l.data(), l.size(), e) < 0) say("write error %s", std::to_string(e));
	}
	void on_line(std::string const& l) {
		say("< %s", l);
		// a reply of several lines runs from "ddd-" to "ddd "
		bool coded = l.size() >= 4 && isdigit((unsigned char)l[0]) && isdigit((unsigned char)l[1]) && isdigit((unsigned char)l[2]);
		if (multi_.empty()) {
			if (!coded) return;
			if (l[3] == '-') { multi_ = l.substr(0, 3); return; }
		}
		else {
			if (!coded || l.compare(0, 3, multi_) || l[3] != ' ') return;
			multi_.clear();
		}
		int code = std::atoi(l.substr(0, 3).c_str());
		switch (step_) {
		case 0:
			if (code != 220) { fail("welcome"); return; }
			welcome_ = ms();
			if (tls_) { send("AUTH TLS"); step_ = 1; } else { send("USER " + user_); step_ = 3; }
			break;
		case 1:
			if (code != 234) { fail("AUTH TLS"); return; }
			ctl_ = std::make_unique<fz::tls_layer>(event_loop_, this, *cs_, &ts_, log_);
			say("TLS handshake starts");
			if (!ctl_->client_handshake(this, {}, fz::to_native(host_))) { fail("handshake start"); return; }
			step_ = 2;
			break;
		case 3:
			if (code != 331 && code != 230) { fail("USER"); return; }
			if (code == 230) { logged_in(); break; }
			send("PASS " + pass_); step_ = 4;
			break;
		case 4:
			if (code != 230) { fail("login"); return; }
			logged_in();
			break;
		case 5: send(tls_ ? "PROT P" : "TYPE I"); step_ = tls_ ? 6 : 7; break;
		case 6: if (code != 200) { fail("PROT"); return; } send("TYPE I"); step_ = 7; break;
		case 7: send("PASV"); step_ = 8; break;
		case 8: {
			if (code != 227) { fail("PASV"); return; }
			auto p = l.find('('); int a, b, c, d, e, f;
			if (p == std::string::npos || sscanf(l.c_str() + p + 1, "%d,%d,%d,%d,%d,%d", &a, &b, &c, &d, &e, &f) != 6) { fail("PASV reply"); return; }
			t0_ = ms();
			ds_ = std::make_unique<fz::socket>(pool_, this);
			// the control connection's address: servers behind NAT give their inside one
			// DATAIP=1: the address PASV gave, as FileZilla's own default does
			std::string dh = getenv("DATAIP") ? std::to_string(a) + "." + std::to_string(b) + "." + std::to_string(c) + "." + std::to_string(d) : host_;
			say("data connect %s", dh + ":" + std::to_string(e * 256 + f));
			if (int err = ds_->connect(fz::to_native(dh), e * 256 + f)) { say("data connect error %s", fz::socket_error_description(err)); fail("data connect"); return; }
			if (tls_) {
				dtl_ = std::make_unique<fz::tls_layer>(event_loop_, this, *ds_, &ts_, log_);
				if (!dtl_->client_handshake(ctl_->get_raw_certificate(), ctl_->get_session_parameters(), ctl_->get_hostname())) { fail("data handshake"); return; }
			}
			send("RETR " + file_); step_ = 9;
			break; }
		case 9:
			if (code >= 400) { fail("RETR"); return; }
			if (code == 226) { reply226_ = true; maybe_done(); }
			break;
		case 10: result = 0; event_loop_.stop(); break;
		}
	}
	void logged_in() { login_ = ms(); if (tls_) { send("PBSZ 0"); step_ = 5; } else { send("TYPE I"); step_ = 7; } }
	void maybe_done() {
		if (!data_done_ || !reply226_) return;
		long long t1 = ms();
		printf("RESULT %s welcome_ms=%lld tls_ms=%lld login_ms=%lld download_ms=%lld bytes=%llu sha256=%s%s\n", tls_ ? "ftps" : "ftp",
			welcome_, tls_up_, login_, t1 - t0_, (unsigned long long)got_, fz::hex_encode<std::string>(hash_.digest()).c_str(), info_.c_str());
		fflush(stdout);
		send("QUIT"); step_ = 10;
	}
	void on_data(fz::socket_event_flag t, int err) {
		if (err) { say("data error %s", fz::socket_error_description(err)); fail("data"); return; }
		if (t == fz::socket_event_flag::connection) {
			say("data connection up%s", dtl_ ? (dtl_->resumed_session() ? " (TLS resumed)"s : " (TLS not resumed)"s) : ""s);
			return;
		}
		if (t != fz::socket_event_flag::read) return;
		fz::socket_interface& si = dtl_ ? static_cast<fz::socket_interface&>(*dtl_) : *ds_;
		char buf[32768];
		for (;;) {
			int e; int r = si.read(buf, sizeof buf, e);
			if (r < 0) { if (e == EAGAIN) return; say("data read error %s", std::to_string(e)); fail("download"); return; }
			if (!r) { dtl_.reset(); ds_.reset(); data_done_ = true; maybe_done(); return; }
			hash_.update(reinterpret_cast<uint8_t*>(buf), r); got_ += r;
		}
	}
	void operator()(fz::event_base const& ev) override {
		if (ev.derived_type() == fz::certificate_verification_event::type()) {
			auto const& v = static_cast<fz::certificate_verification_event const&>(ev).v_;
			auto const& info = std::get<1>(v);
			say("certificate: %s", info.get_protocol() + " " + info.get_session_cipher() + " " + info.get_key_exchange()
				+ " chain=" + std::to_string(info.get_certificates().size()) + " systemtrust=" + std::to_string(info.system_trust()));
			info_ = " " + info.get_protocol() + " " + info.get_session_cipher();
			std::get<0>(v)->set_verification_result(true);
			return;
		}
		if (ev.derived_type() != fz::socket_event::type()) return;
		auto const& [src, t, err] = static_cast<fz::socket_event const&>(ev).v_;
		if (dtl_ && src == dtl_.get()) { on_data(t, err); return; }
		if (ds_ && src == ds_.get()) { if (!dtl_) on_data(t, err); return; }
		if (err) { say("control error %s", fz::socket_error_description(err)); fail("control"); return; }
		if (t == fz::socket_event_flag::connection) {
			if (ctl_ && src == ctl_.get()) { tls_up_ = ms(); say("control TLS up: %s", ctl_->get_protocol() + " " + ctl_->get_cipher()); send("USER " + user_); step_ = 3; }
			else say("TCP connected");
			return;
		}
		if (t != fz::socket_event_flag::read) return;
		char buf[4096];
		for (;;) {
			int e; int r = ctl().read(buf, sizeof buf, e);
			if (r < 0) {
				if (e == EAGAIN) return;
				if (step_ == 10) { result = 0; event_loop_.stop(); return; }
				say("control read error %s", std::to_string(e)); fail("control read"); return;
			}
			if (!r) { if (step_ != 10) fail("control closed"); else { result = 0; event_loop_.stop(); } return; }
			in_.append(buf, r);
			size_t p;
			while ((p = in_.find("\r\n")) != std::string::npos) { std::string l = in_.substr(0, p); in_.erase(0, p + 2); on_line(l); }
		}
	}

	fz::logger_interface& log_;
	fz::thread_pool& pool_;
	fz::tls_system_trust_store& ts_;
	bool tls_;
	std::string host_, user_, pass_, file_, in_, info_, multi_;
	std::unique_ptr<fz::socket> cs_, ds_;
	std::unique_ptr<fz::tls_layer> ctl_, dtl_;
	int step_{};
	fz::hash_accumulator hash_{fz::hash_algorithm::sha256};
	uint64_t got_{};
	long long welcome_{}, tls_up_{}, login_{}, t0_{};
	bool data_done_{}, reply226_{};
};

int main(int argc, char** argv)
{
	if (argc != 7) { fprintf(stderr, "fzprobe sftp|ftps|ftp HOST PORT USER PASSWORD FILE\n"); return 20; }
	g_start = fz::monotonic_clock::now();
	timed_logger log;
	fz::thread_pool pool;
	fz::event_loop loop(fz::event_loop::threadless);
	std::string mode = argv[1];
	int r = 20;
	if (mode == "sftp") { sftp_probe p(log, pool, loop, argv); loop.run(); r = p.result; }
	else if (mode == "ftps" || mode == "ftp") {
		fz::tls_system_trust_store ts(pool);
		ftp_probe p(log, pool, loop, ts, mode == "ftps", argv); loop.run(); r = p.result;
	}
	say("exit %s", std::to_string(r));
	return r ? 10 : 0;
}
