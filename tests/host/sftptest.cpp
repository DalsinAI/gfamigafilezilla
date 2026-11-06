// SFTP round trip for fzssh on the host: connect with one named key exchange,
// cipher, MAC and host key algorithm, log in with a key file, download a file,
// upload a file, print the algorithms, SHA-256 of what came down and times.
#include <libfilezilla/event_handler.hpp>
#include <libfilezilla/event_loop.hpp>
#include <libfilezilla/file.hpp>
#include <libfilezilla/hash.hpp>
#include <libfilezilla/encode.hpp>
#include <libfilezilla/logger.hpp>
#include <libfilezilla/socket.hpp>
#include <libfilezilla/thread_pool.hpp>
#include <libfilezilla/time.hpp>
#include <libfilezilla/util.hpp>
#include <fzssh/sftp/sftp_client.hpp>
#include <fzssh/client.hpp>
#include <fzssh/privkey.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <optional>
#include <string.h>

using namespace std::literals;
using namespace fz::ssh;

static std::string readfile(char const* p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }

struct quiet final : fz::logger_interface {
	bool verbose{};
	void do_log(fz::logmsg::type t, std::wstring&& msg) override {
		if (verbose || t == fz::logmsg::error) std::wcerr << msg << L"\n";
	}
};

class runner final : public fz::event_handler, public sftp::response_handler
{
public:
	runner(fz::logger_interface & log, fz::thread_pool & pool, fz::event_loop& l, char** a)
		: fz::event_handler(l), log_(log), pool_(pool)
	{
		user_ = a[2]; keyfile_ = a[3];
		params_.kex_ = a[4]; params_.cipher_ = a[5]; params_.mac_ = a[6]; params_.hostkey_signatures_ = a[7];
		params_.single_channel_ = true;
		down_ = a[8]; up_ = a[9]; upsize_ = std::stoul(a[10]);
		s_ = std::make_unique<fz::socket>(pool_, this);
		start_ = fz::monotonic_clock::now();
		if (s_->connect(fz::to_native(std::string("127.0.0.1")), std::stoi(a[1]))) { fail("connect"); }
	}
	~runner() { remove_handler(); }

	void fail(char const* what) { std::cout << "FAIL " << what << std::endl; exit(1); }

	void operator()(fz::event_base const& ev) override {
		fz::dispatch<fz::socket_event, auth_done_event, session_done_event, hostkey_verification_event, auth_requested_event,
			auth_signature_failure_event, sftp::sftp_client::ready_event, sftp::sftp_client::done_event>(ev, this,
			&runner::on_socket, &runner::on_auth_done, &runner::on_session_done, &runner::on_hostkey, &runner::on_auth_requested,
			&runner::on_sigfail, &runner::on_sftp_ready, &runner::on_sftp_done);
	}
	void on_socket(fz::socket_event_source* s, fz::socket_event_flag t, int error) {
		if (error) fail("socket");
		if (s == s_.get() && t == fz::socket_event_flag::connection) {
			ssh_ = std::make_unique<client>(params_, user_, *s_, *this, log_);
		}
	}
	void on_hostkey(client* c, std::unique_ptr<public_key> const& k, algorithm_info const& ai) {
		std::cout << "kex=" << ai.kex_ << " cipher=" << ai.cipher_c2s_ << "/" << ai.cipher_s2c_ << " mac=" << ai.mac_c2s_
			<< " hostkey=" << ai.hostkey_signature_ << " (" << k->name() << " " << k->fingerprint() << ")" << std::endl;
		kex_done_ = fz::monotonic_clock::now();
		c->hostkey_decision(true);
	}
	void on_auth_requested(client*, std::string const&, bool) {
		auto keys = load_private_keys(readfile(keyfile_.c_str()), log_);
		if (keys.empty()) fail("key file");
		key_ = std::move(keys[0]);
		ssh_->auth_with_key(key_, true);
	}
	void on_sigfail(client*) { fail("signature"); }
	void on_auth_done(client*) {
		auth_done_ = fz::monotonic_clock::now();
		auto si = ssh_->open_channel(channel_type::subsystem, "sftp"sv);
		sftp_ = std::make_unique<sftp::sftp_client>(std::move(si), *this, log_);
	}
	void on_session_done(session*) { if (!finished_) fail("session ended"); exit(0); }
	void on_sftp_ready(sftp::sftp_client*) {
		t0_ = fz::monotonic_clock::now();
		phase_ = 1;
		sftp_->open(this, down_, sftp::file_flags::SSH_FXF_READ);
	}
	void on_sftp_done(sftp::sftp_client*) {}

	void next_read() { sftp_->read(this, handle_, offset_, 32768); }
	void next_write() {
		if (written_ >= upsize_) { sftp_->close(this, handle_); phase_ = 4; return; }
		size_t n = std::min<size_t>(32768, upsize_ - written_);
		std::string chunk(n, '\0');
		for (size_t i = 0; i < n; ++i) chunk[i] = static_cast<char>((written_ + i) * 31 % 251);
		up_hash_.update(chunk);
		sftp_->write(this, handle_, written_, chunk);
		written_ += n;
	}

	sftp::continuation process_handle(std::string_view h) override {
		handle_ = h;
		if (phase_ == 1) next_read(); else next_write();
		return sftp::continuation::next;
	}
	sftp::continuation process_data(std::string_view d) override {
		down_hash_.update(d); offset_ += d.size(); next_read();
		return sftp::continuation::next;
	}
	sftp::continuation process_status(sftp::status_code st, std::string_view msg) override {
		if (phase_ == 1 && st == sftp::status_code::SSH_FX_EOF) {
			t1_ = fz::monotonic_clock::now();
			sftp_->close(this, handle_); phase_ = 2; return sftp::continuation::next;
		}
		if (phase_ == 2 && st == sftp::status_code::SSH_FX_OK) {
			phase_ = 3; t2_ = fz::monotonic_clock::now();
			sftp_->open(this, up_, sftp::file_flags(uint32_t(sftp::file_flags::SSH_FXF_WRITE) | uint32_t(sftp::file_flags::SSH_FXF_CREAT) | uint32_t(sftp::file_flags::SSH_FXF_TRUNC)));
			return sftp::continuation::next;
		}
		if (phase_ == 3 && st == sftp::status_code::SSH_FX_OK) { next_write(); return sftp::continuation::next; }
		if (phase_ == 4 && st == sftp::status_code::SSH_FX_OK) {
			auto t3 = fz::monotonic_clock::now();
			std::cout << "down " << offset_ << " sha256=" << fz::hex_encode<std::string>(down_hash_.digest())
				<< "\nup " << written_ << " sha256=" << fz::hex_encode<std::string>(up_hash_.digest())
				<< "\nms kex=" << (kex_done_ - start_).get_milliseconds() << " login=" << (auth_done_ - start_).get_milliseconds()
				<< " down=" << (t1_ - t0_).get_milliseconds() << " up=" << (t3 - t2_).get_milliseconds() << "\nOK" << std::endl;
			finished_ = true;
			exit(0);
		}
		std::cout << "status " << int(st) << " " << msg << " in phase " << phase_ << std::endl;
		fail("sftp");
		return sftp::continuation::next;
	}

	fz::logger_interface & log_;
	fz::thread_pool & pool_;
	client_parameters params_;
	std::string user_, keyfile_, down_, up_;
	size_t upsize_{}, written_{};
	uint64_t offset_{};
	std::unique_ptr<fz::socket> s_;
	std::unique_ptr<client> ssh_;
	std::unique_ptr<private_key> key_;
	std::unique_ptr<sftp::sftp_client> sftp_;
	std::string handle_;
	int phase_{};
	bool finished_{};
	fz::hash_accumulator down_hash_{fz::hash_algorithm::sha256}, up_hash_{fz::hash_algorithm::sha256};
	fz::monotonic_clock start_, kex_done_, auth_done_, t0_, t1_, t2_;
};

int main(int argc, char** argv)
{
	if (argc != 11) { std::cerr << "port user keyfile kex cipher mac hostkeyalg downfile upfile upsize\n"; return 2; }
	quiet log; log.verbose = getenv("V") != nullptr; if (log.verbose) log.set_all(fz::logmsg::type(-1));
	fz::thread_pool pool;
	fz::event_loop loop(fz::event_loop::threadless);
	runner r(log, pool, loop, argv);
	loop.run();
	return 1;
}
