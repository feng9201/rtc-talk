#pragma once
#include "test.h"
#include <asio/ssl.hpp>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <filesystem>
#include <memory>
#include <unistd.h>

// Ephemeral test-only CA and leaf, generated at runtime. No real credential or
// reusable private key enters source control. Only the public CA is written.
class TlsFixture final {
  using Key = std::unique_ptr<EVP_PKEY,decltype(&EVP_PKEY_free)>;
  using Cert = std::unique_ptr<X509,decltype(&X509_free)>;
  static Key key() {
    auto ctx = std::unique_ptr<EVP_PKEY_CTX,decltype(&EVP_PKEY_CTX_free)>(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA,nullptr),EVP_PKEY_CTX_free);
    CHECK(ctx && EVP_PKEY_keygen_init(ctx.get()) == 1);
    CHECK(EVP_PKEY_CTX_set_rsa_keygen_bits(ctx.get(),2048) == 1);
    EVP_PKEY* raw = nullptr;
    CHECK(EVP_PKEY_keygen(ctx.get(),&raw) == 1);
    return Key(raw,EVP_PKEY_free);
  }
  static void extension(X509* cert, X509* issuer, int nid, const char* value) {
    X509V3_CTX ctx{};
    X509V3_set_ctx(&ctx,issuer,cert,nullptr,nullptr,0);
    auto ext = std::unique_ptr<X509_EXTENSION,decltype(&X509_EXTENSION_free)>(
      X509V3_EXT_conf_nid(nullptr,&ctx,nid,const_cast<char*>(value)),X509_EXTENSION_free);
    CHECK(ext && X509_add_ext(cert,ext.get(),-1) == 1);
  }
 public:
  TlsFixture() : ca_key_(key()), leaf_key_(key()), ca_(X509_new(),X509_free) {
    CHECK(ca_ && X509_set_version(ca_.get(),2) == 1);
    CHECK(ASN1_INTEGER_set(X509_get_serialNumber(ca_.get()),1) == 1);
    CHECK(X509_gmtime_adj(X509_getm_notBefore(ca_.get()),-60));
    CHECK(X509_gmtime_adj(X509_getm_notAfter(ca_.get()),86400));
    CHECK(X509_set_pubkey(ca_.get(),ca_key_.get()) == 1);
    auto name = X509_get_subject_name(ca_.get());
    CHECK(X509_NAME_add_entry_by_txt(name,"CN",MBSTRING_ASC,
      reinterpret_cast<const unsigned char*>("rtc-rk-test-ca"),-1,-1,0) == 1);
    CHECK(X509_set_issuer_name(ca_.get(),name) == 1);
    extension(ca_.get(),ca_.get(),NID_basic_constraints,"critical,CA:TRUE");
    extension(ca_.get(),ca_.get(),NID_key_usage,"critical,keyCertSign,cRLSign");
    CHECK(X509_sign(ca_.get(),ca_key_.get(),EVP_sha256()) > 0);
    std::string pattern = (std::filesystem::temp_directory_path() / "rtc-rk-tls-XXXXXX").string();
    CHECK(mkdtemp(pattern.data()));
    directory_ = pattern;
    ca_file_ = directory_ / "ca.pem";
    auto file = std::unique_ptr<FILE,decltype(&fclose)>(fopen(ca_file_.c_str(),"w"),fclose);
    CHECK(file && PEM_write_X509(file.get(),ca_.get()) == 1);
  }
  ~TlsFixture() {
    std::error_code ignored;
    std::filesystem::remove(ca_file_,ignored);
    std::filesystem::remove(directory_,ignored);
  }
  std::string ca_file() const { return ca_file_.string(); }
  std::shared_ptr<asio::ssl::context> server_context(bool expired = false) {
    Cert cert(X509_new(),X509_free);
    CHECK(cert && X509_set_version(cert.get(),2) == 1);
    CHECK(ASN1_INTEGER_set(X509_get_serialNumber(cert.get()),2) == 1);
    CHECK(X509_gmtime_adj(X509_getm_notBefore(cert.get()),expired ? -7200 : -60));
    CHECK(X509_gmtime_adj(X509_getm_notAfter(cert.get()),expired ? -3600 : 3600));
    CHECK(X509_set_pubkey(cert.get(),leaf_key_.get()) == 1);
    auto name = X509_get_subject_name(cert.get());
    CHECK(X509_NAME_add_entry_by_txt(name,"CN",MBSTRING_ASC,
      reinterpret_cast<const unsigned char*>("localhost"),-1,-1,0) == 1);
    CHECK(X509_set_issuer_name(cert.get(),X509_get_subject_name(ca_.get())) == 1);
    extension(cert.get(),ca_.get(),NID_basic_constraints,"critical,CA:FALSE");
    extension(cert.get(),ca_.get(),NID_subject_alt_name,"DNS:localhost");
    extension(cert.get(),ca_.get(),NID_ext_key_usage,"serverAuth");
    CHECK(X509_sign(cert.get(),ca_key_.get(),EVP_sha256()) > 0);
    auto ctx = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_server);
    CHECK(SSL_CTX_set_min_proto_version(ctx->native_handle(),TLS1_2_VERSION) == 1);
    CHECK(SSL_CTX_use_certificate(ctx->native_handle(),cert.get()) == 1);
    CHECK(SSL_CTX_use_PrivateKey(ctx->native_handle(),leaf_key_.get()) == 1);
    return ctx;
  }
 private:
  Key ca_key_, leaf_key_;
  Cert ca_;
  std::filesystem::path directory_, ca_file_;
};
