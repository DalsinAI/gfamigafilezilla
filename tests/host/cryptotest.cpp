#include <libfilezilla/buffer.hpp>
#include <libfilezilla/hash.hpp>
#include <libfilezilla/encode.hpp>
#include <libfilezilla/encryption.hpp>
#include <libfilezilla/signature.hpp>
#include <libfilezilla/util.hpp>
#include <cstdio>
#include <fstream>
#include <string>
using namespace fz;
static std::string hx(std::vector<uint8_t> const& v){ return hex_encode<std::string>(v); }
int main(int argc, char** argv)
{
	std::string mode = argv[1];
	if (mode == "det") {
		std::string msgs[] = { "", "abc", std::string(55,'a'), std::string(56,'b'), std::string(63,'c'), std::string(64,'d'), std::string(111,'e'), std::string(112,'f'), std::string(127,'g'), std::string(128,'h'), std::string(1000,'i') };
		for (auto& m : msgs) {
			printf("%zu md5 %s\n", m.size(), hx(md5(m)).c_str());
			printf("%zu sha1 %s\n", m.size(), hx(sha1(m)).c_str());
			printf("%zu sha256 %s\n", m.size(), hx(sha256(m)).c_str());
			printf("%zu sha384 %s\n", m.size(), hx(sha384(m)).c_str());
			printf("%zu sha512 %s\n", m.size(), hx(sha512(m)).c_str());
			for (auto a : {hash_algorithm::sha3_256, hash_algorithm::sha3_384, hash_algorithm::sha3_512}) {
				hash_accumulator h(a); h.update(m); printf("%zu sha3 %s\n", m.size(), hx(h.digest()).c_str());
			}
			printf("%zu hmac1 %s\n", m.size(), hx(hmac_sha1(std::string("key"), m)).c_str());
			printf("%zu hmac256 %s\n", m.size(), hx(hmac_sha256(std::string(200,'k'), m)).c_str());
			hash_accumulator h512(hmac_algorithm::sha512, std::string("kk"));
			// split updates
			for (size_t i = 0; i < m.size(); i += 7) h512.update(m.substr(i, 7));
			printf("%zu hmac512 %s\n", m.size(), hx(h512.digest()).c_str());
			hash_accumulator hs(hash_algorithm::sha256);
			for (size_t i = 0; i < m.size(); i += 13) hs.update(m.substr(i, 13));
			printf("%zu sha256split %s\n", m.size(), hx(hs.digest()).c_str());
		}
		auto pw = std::vector<uint8_t>{'p','a','s','s'};
		auto salt = std::vector<uint8_t>(32, 7);
		printf("pbkdf2 %s\n", hx(pbkdf2_hmac_sha256(std::basic_string_view<uint8_t>(pw.data(), pw.size()), std::basic_string_view<uint8_t>(salt.data(), salt.size()), 40, 1000)).c_str());
		auto p5 = pbkdf2(hmac_algorithm::sha512, "password", "saltsalt", 64, 50);
		printf("pbkdf2-512 %s\n", hex_encode<std::string>(p5.to_view()).c_str());
		hash_accumulator s1(hash_algorithm::sha1); s1.update(std::string(150,'x'));
		printf("sha1state %s\n", hx(s1.export_state()).c_str());
		auto priv = private_key::from_password(pw, salt, 100000);
		printf("pub %s\n", priv.pubkey().to_base64().c_str());
		auto sk = private_signing_key::from_base64(base64_encode(std::string(32, 'S')));
		printf("vk %s\n", sk.pubkey().to_base64().c_str());
		printf("sig %s\n", hx(sign(std::string("hello"), sk)).c_str());
		return 0;
	}
	auto pw = std::vector<uint8_t>{'p','a','s','s'};
	auto salt = std::vector<uint8_t>(32, 7);
	auto priv = private_key::from_password(pw, salt, 100000);
	auto sym = symmetric_key::from_base64(base64_encode(std::string(64, 'Q')));
	auto sk = private_signing_key::from_base64(base64_encode(std::string(32, 'S')));
	if (mode == "enc") {
		std::ofstream f(argv[2]);
		for (size_t n : {0, 1, 15, 16, 17, 100, 1000}) {
			std::string plain(n, 'z'); for (size_t i = 0; i < n; ++i) plain[i] = char(i * 7);
			f << n << " " << hx(encrypt(plain, priv.pubkey(), true)) << " " << hx(encrypt(plain, priv.pubkey(), false))
			  << " " << hx(encrypt(plain, priv.pubkey(), std::string_view("aad-data"))) << " " << hx(encrypt(plain, sym)) << " " << hx(encrypt(plain, sym, std::string_view("ad"))) << "\n";
		}
		return 0;
	}
	if (mode == "dec") {
		std::ifstream f(argv[2]);
		size_t n; std::string a, b, c, d, e; int bad = 0, ok = 0;
		while (f >> n >> a >> b >> c >> d >> e) {
			std::string plain(n, 'z'); for (size_t i = 0; i < n; ++i) plain[i] = char(i * 7);
			std::vector<uint8_t> pv(plain.begin(), plain.end());
			auto chk = [&](std::vector<uint8_t> const& r, char const* what) { if (r != pv) { printf("FAIL %s n=%zu\n", what, n); ++bad; } else ++ok; };
			chk(decrypt(hex_decode(a), priv, true), "pk-gcm");
			chk(decrypt(hex_decode(b), priv, false), "pk-ctr");
			chk(decrypt(hex_decode(c), priv, std::vector<uint8_t>{'a','a','d','-','d','a','t','a'}), "pk-gcm-ad");
			chk(decrypt(hex_decode(d), sym), "sym");
			chk(decrypt(hex_decode(e), sym, std::vector<uint8_t>{'a','d'}), "sym-ad");
			// tampered tag must fail
			auto t = hex_decode(a); if (!t.empty()) { t.back() ^= 1; if (!decrypt(t, priv, true).empty()) { printf("FAIL tamper n=%zu\n", n); ++bad; } }
		}
		printf("decrypt ok %d bad %d\n", ok, bad);
		return bad;
	}
	return 1;
}
