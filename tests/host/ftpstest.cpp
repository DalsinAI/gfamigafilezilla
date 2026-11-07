// FTPS (explicit TLS) through libfilezilla's tls_layer: control connection,
// data connections resuming its session, a download and an upload.
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
#include <libfilezilla/util.hpp>
#include <libfilezilla/time.hpp>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
using namespace fz;

struct client : event_handler {
	thread_pool& pool; tls_system_trust_store& ts; stdout_logger log;
	std::unique_ptr<socket> cs, ds; std::unique_ptr<tls_layer> ctl, dtl;
	std::string inbuf, cmdline; int step{0}; std::string reply;
	std::vector<std::string> cmds; size_t ci{};
	hash_accumulator dh{hash_algorithm::sha256}; size_t dbytes{}; bool upload{}; std::string updata; size_t upos{};
	bool data_done{}, reply226{}; int result{1}; bool verified_once{};
	monotonic_clock t0;
	client(event_loop& l, thread_pool& p, tls_system_trust_store& t) : event_handler(l), pool(p), ts(t) {
		log.set_all(logmsg::type(logmsg::status|logmsg::error|logmsg::command|logmsg::reply|logmsg::debug_info|logmsg::debug_warning));
	}
	~client() { remove_handler(); }
	void start() {
		cs = std::make_unique<socket>(pool, this);
		cs->connect(getenv("FZHOST") ? getenv("FZHOST") : "localhost", 2121);
	}
	void send(std::string const& s) { printf("> %s\n", s.c_str()); std::string l = s + "\r\n"; int e; socket_interface& si = ctl ? static_cast<socket_interface&>(*ctl) : *cs; si.write(l.data(), l.size(), e); }
	void on_line(std::string const& l) {
		printf("< %s\n", l.c_str());
		if (l.size() < 4 || l[3] == '-') return;
		int code = std::stoi(l.substr(0,3));
		switch (step) {
		case 0: send("AUTH TLS"); step = 1; break;
		case 1: {
			if (code != 234) { fail("AUTH"); return; }
			ctl = std::make_unique<tls_layer>(event_loop_, this, *cs, &ts, log);
			ctl->set_min_tls_ver(tls_ver::v1_2);
			if (getenv("FZMAX12")) ctl->set_max_tls_ver(tls_ver::v1_2);
			if (!ctl->client_handshake(this)) { fail("handshake start"); return; }
			step = 2; break; }
		case 3: send("PASS fzpass123"); step = 4; break;
		case 4: if (code != 230) { fail("login"); return; } send("PBSZ 0"); step = 5; break;
		case 5: send("PROT P"); step = 6; break;
		case 6: if (code != 200) { fail("PROT"); return; } next_transfer(); break;
		case 7: { // PASV
			if (code != 227) { fail("PASV"); return; }
			auto p = l.find('('); int a,b,c,d,e,f; sscanf(l.c_str()+p+1, "%d,%d,%d,%d,%d,%d", &a,&b,&c,&d,&e,&f);
			ds = std::make_unique<socket>(pool, this);
			ds->connect(fzT("127.0.0.1"), e*256+f);
			dtl = std::make_unique<tls_layer>(event_loop_, this, *ds, &ts, log);
			if (!dtl->client_handshake(ctl->get_raw_certificate(), ctl->get_session_parameters(), ctl->get_hostname())) { fail("data handshake"); return; }
			send(cmds[ci]); step = 8; break; }
		case 8: if (code >= 400) { fail("transfer cmd"); return; } if (code == 226) { reply226 = true; maybe_next(); } break;
		case 9: result = 0; event_loop_.stop(); break;
		}
	}
	std::vector<std::string> plan{ "RETR pub/big.bin", "STOR up.bin", "RETR up.bin", "RETR pub/small.txt" };
	void next_transfer() {
		if (ci >= plan.size()) { send("QUIT"); step = 9; return; }
		cmds = plan; data_done = reply226 = false; dbytes = 0; dh.reinit();
		upload = plan[ci].rfind("STOR", 0) == 0;
		if (upload) { updata.assign(2500000, 0); for (size_t i = 0; i < updata.size(); ++i) updata[i] = char((i * 2654435761u) >> 13); upos = 0; }
		t0 = monotonic_clock::now();
		send("PASV"); step = 7;
	}
	void maybe_next() {
		if (data_done && reply226) {
			dtl.reset(); ds.reset();
			auto ms = (monotonic_clock::now() - t0).get_milliseconds();
			printf("== %s: %zu bytes sha256 %s in %lld ms, resumed=%d, %s %s %s %s\n", plan[ci].c_str(), dbytes, hex_encode<std::string>(dh.digest()).c_str(), (long long)ms, dtl ? 0 : -1,
				ctl->get_protocol().c_str(), ctl->get_cipher().c_str(), ctl->get_key_exchange().c_str(), ctl->get_mac().c_str());
			++ci; next_transfer();
		}
	}
	void fail(char const* w) { printf("FAIL: %s\n", w); result = 2; event_loop_.stop(); }
	void on_data(socket_event_flag t, int err) {
		if (err) { printf("data socket error %d\n", err); fail("data socket"); return; }
		if (t == socket_event_flag::connection) {
			printf("data TLS up, resumed=%d protocol %s\n", dtl->resumed_session(), dtl->get_protocol().c_str());
			if (!dtl->resumed_session()) { fail("no resumption"); return; }
			if (upload) t = socket_event_flag::write; else return;
		}
		if (upload && data_done) return;
		if (upload && t == socket_event_flag::write) {
			while (upos < updata.size()) {
				int e; int w = dtl->write(updata.data() + upos, std::min<size_t>(65536, updata.size() - upos), e);
				if (w < 0) { if (e == EAGAIN) return; printf("write err %d\n", e); fail("upload"); return; }
				dh.update(reinterpret_cast<uint8_t const*>(updata.data() + upos), w); upos += w; dbytes += w;
			}
			int r = dtl->shutdown();
			if (r == EAGAIN) printf("shutdown EAGAIN\n");
			if (r == EAGAIN) return;
			if (r) { fail("shutdown"); return; }
			data_done = true; maybe_next(); return;
		}
		if (!upload && t == socket_event_flag::read) {
			char buf[32768];
			for (;;) {
				int e; int r = dtl->read(buf, sizeof buf, e);
				if (r < 0) { if (e == EAGAIN) return; printf("read err %d\n", e); fail("download"); return; }
				if (!r) { dtl.reset(); ds.reset(); data_done = true; maybe_next(); return; }
				dh.update(reinterpret_cast<uint8_t*>(buf), r); dbytes += r;
			}
		}
	}
	void operator()(event_base const& ev) override {
		if (ev.derived_type() == certificate_verification_event::type()) {
			auto const& v = static_cast<certificate_verification_event const&>(ev).v_;
			auto const& info = std::get<1>(v);
			printf("certificate event: host %s port %u %s kx %s cipher %s mac %s mismatch=%d systemtrust=%d chain=%zu\n", info.get_host().c_str(), info.get_port(), info.get_protocol().c_str(), info.get_key_exchange().c_str(), info.get_session_cipher().c_str(), info.get_session_mac().c_str(), info.mismatched_hostname(), info.system_trust(), info.get_certificates().size());
			for (auto const& c : info.get_certificates()) {
				printf("  subject %s\n  issuer %s\n  valid %s .. %s serial %s\n  key %s %u sig %s selfsigned %d\n  sha256 %s\n", c.get_subject().c_str(), c.get_issuer().c_str(), c.get_activation_time().format("%Y-%m-%d %H:%M:%S", datetime::utc).c_str(), c.get_expiration_time().format("%Y-%m-%d %H:%M:%S", datetime::utc).c_str(), c.get_serial().c_str(), c.get_pubkey_algorithm().c_str(), c.get_pubkey_bits(), c.get_signature_algorithm().c_str(), c.self_signed(), c.get_fingerprint_sha256().c_str());
				for (auto const& n : c.get_alt_subject_names()) printf("  alt %s dns=%d\n", n.name.c_str(), n.is_dns);
			}
			std::get<0>(v)->set_verification_result(true);
			return;
		}
		if (ev.derived_type() != socket_event::type()) return;
		auto const& [src, t, err] = static_cast<socket_event const&>(ev).v_;
		if (dtl && src == dtl.get()) { on_data(t, err); return; }
		if (ds && src == ds.get()) return; // owned by dtl
		if (err) { printf("control error %d (%s)\n", err, socket_error_description(err).c_str()); fail("control"); return; }
		if (t == socket_event_flag::connection && ctl && src == ctl.get()) {
			printf("control TLS up: %s %s\n", ctl->get_protocol().c_str(), ctl->get_cipher().c_str());
			send("USER fztest"); step = 3; return;
		}
		if (t == socket_event_flag::read) {
			socket_interface& si = ctl ? static_cast<socket_interface&>(*ctl) : *cs;
			char buf[4096];
			for (;;) {
				int e; int r = si.read(buf, sizeof buf, e);
				if (r <= 0) { if (r < 0 && e != EAGAIN) fail("control read"); if (!r) fail("control eof"); return; }
				inbuf.append(buf, r);
				size_t p;
				while ((p = inbuf.find("\r\n")) != std::string::npos) { auto l = inbuf.substr(0, p); inbuf.erase(0, p + 2); on_line(l); if (step == 2 || result != 1) return; }
			}
		}
	}
};

int main()
{
	thread_pool pool; event_loop loop(event_loop::threadless); tls_system_trust_store ts(pool);
	client c(loop, pool, ts);
	c.start();
	loop.run();
	printf("RESULT %d\n", c.result);
	return c.result;
}
