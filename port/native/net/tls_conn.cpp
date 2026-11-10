// See tls_conn.h.
#include "tls_conn.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h> // (only for its error codes: the socket is ours)
#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#include <vector>

#include "transport.h" // NowMs

// mbedTLS built with MBEDTLS_PLATFORM_ZEROIZE_ALT (scripts/native/build_mbedtls.sh): its own
// would call explicit_bzero (GLIBC 2.25). A memset the compiler cannot drop: called through a
// volatile pointer, then the memory is said to be read.
extern "C" void mbedtls_platform_zeroize(void* buffer, size_t length)
{
    if (buffer && length)
    {
        static void* (*const volatile set)(void*, int, size_t) = memset;
        set(buffer, 0, length);
        __asm__ __volatile__("" : : "r"(buffer) : "memory");
    }
}

namespace gta2net
{

namespace
{
// Where the consoles keep their authorities: ROCKNIX and AmberELEC have /etc/ssl/cert.pem and
// /etc/pki/tls/certs/ca-bundle.crt, ROCKNIX also /etc/ssl/certs/ca-certificates.crt (Debian's).
const char* const kSystemBundles[] = {
    "/etc/ssl/certs/ca-certificates.crt",
    "/etc/ssl/cert.pem",
    "/etc/pki/tls/certs/ca-bundle.crt",
    "/etc/ssl/ca-bundle.pem",
};

std::string MbedError(int code)
{
    char text[160];
    mbedtls_strerror(code, text, sizeof(text));
    char out[200];
    snprintf(out, sizeof(out), "%s (-0x%04X)", text, static_cast<unsigned>(-code));
    return out;
}

// The random generator and the authorities, made once: the bundle is ~150 certificates, which
// the slowest console reads in a fraction of a second, once rather than at each connection.
struct Shared
{
    bool ready = false;
    std::string error;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_x509_crt authorities;
    std::string authoritiesFrom; // the file they were read from ("" = not read yet)
    int authorityCount = 0;
};

Shared& Common(std::string* error)
{
    static Shared s;
    if (!s.ready && s.error.empty())
    {
        const psa_status_t psa = psa_crypto_init(); // TLS 1.3 needs PSA
        mbedtls_entropy_init(&s.entropy);
        mbedtls_ctr_drbg_init(&s.drbg);
        mbedtls_x509_crt_init(&s.authorities);
        static const char kSeed[] = "gta2-online";
        const int rc = mbedtls_ctr_drbg_seed(&s.drbg, mbedtls_entropy_func, &s.entropy,
                                             reinterpret_cast<const unsigned char*>(kSeed), sizeof(kSeed) - 1);
        if (psa != PSA_SUCCESS)
            s.error = "TLS: the crypto library did not start (" + std::to_string(psa) + ")";
        else if (rc != 0)
            s.error = "TLS: no random numbers: " + MbedError(rc);
        else
            s.ready = true;
    }
    if (!s.ready)
        *error = s.error;
    return s;
}

// The authorities from caFile, or the first of the console's bundles (SSL_CERT_FILE first).
bool LoadAuthorities(Shared& s, const std::string& caFile, std::string* error)
{
    std::string tried;
    std::vector<std::string> files;
    if (!caFile.empty())
    {
        files.push_back(caFile);
    }
    else
    {
        if (const char* env = getenv("SSL_CERT_FILE"); env && *env)
            files.push_back(env);
        for (const char* f : kSystemBundles)
            files.push_back(f);
    }
    for (const std::string& f : files)
    {
        if (f == s.authoritiesFrom)
            return true;
        if (access(f.c_str(), R_OK) != 0)
        {
            tried += (tried.empty() ? "" : ", ") + f;
            continue;
        }
        mbedtls_x509_crt_free(&s.authorities);
        mbedtls_x509_crt_init(&s.authorities);
        // > 0: that many certificates were not understood, the others are kept
        const int rc = mbedtls_x509_crt_parse_file(&s.authorities, f.c_str());
        int count = 0;
        for (const mbedtls_x509_crt* c = &s.authorities; c && c->raw.p; c = c->next)
            ++count;
        if (rc < 0 || count == 0)
        {
            s.authoritiesFrom.clear();
            *error = "TLS: no certificate could be read from " + f + (rc < 0 ? ": " + MbedError(rc) : "");
            return false;
        }
        s.authoritiesFrom = f;
        s.authorityCount = count;
        fprintf(stderr, "gta2: net: TLS: %d certificate authorities from %s%s\n", count, f.c_str(),
                rc > 0 ? (" (" + std::to_string(rc) + " skipped)").c_str() : "");
        return true;
    }
    *error = caFile.empty() ? "TLS: no certificate authorities on this console (" + tried +
                                  "): set ca_file or tls_fingerprint in conf/net.ini"
                            : "TLS: cannot read ca_file " + caFile;
    return false;
}

std::string HexSha256(const mbedtls_x509_crt* crt)
{
    unsigned char digest[32];
    mbedtls_sha256(crt->raw.p, crt->raw.len, digest, 0);
    std::string out = "sha256:";
    char hex[3];
    for (unsigned char b : digest)
    {
        snprintf(hex, sizeof(hex), "%02x", b);
        out += hex;
    }
    return out;
}

// "sha256:AB:cd..." → "sha256:abcd..."; "" when it is not 64 hex digits.
std::string NormalFingerprint(const std::string& text)
{
    std::string hex;
    std::string rest = text;
    if (rest.compare(0, 7, "sha256:") == 0 || rest.compare(0, 7, "SHA256:") == 0)
        rest = rest.substr(7);
    for (char c : rest)
    {
        if (c == ':' || c == ' ')
            continue;
        if (!isxdigit(static_cast<unsigned char>(c)))
            return "";
        hex += static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
    return hex.size() == 64 ? "sha256:" + hex : "";
}

int CallbackSend(void* ctx, const unsigned char* data, size_t length)
{
    const int fd = *static_cast<int*>(ctx);
    const ssize_t n = send(fd, data, length, MSG_NOSIGNAL);
    if (n >= 0)
        return static_cast<int>(n);
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    return errno == EPIPE || errno == ECONNRESET ? MBEDTLS_ERR_NET_CONN_RESET : MBEDTLS_ERR_NET_SEND_FAILED;
}

int CallbackRecv(void* ctx, unsigned char* data, size_t length)
{
    const int fd = *static_cast<int*>(ctx);
    const ssize_t n = recv(fd, data, length, 0);
    if (n >= 0)
        return static_cast<int>(n);
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return MBEDTLS_ERR_SSL_WANT_READ;
    return errno == ECONNRESET ? MBEDTLS_ERR_NET_CONN_RESET : MBEDTLS_ERR_NET_RECV_FAILED;
}

// A TCP connection (waiting until connected or timed out), then non-blocking, no Nagle delay.
int TcpConnect(const std::string& host, const std::string& port, double timeoutMs, std::string* error)
{
    addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    const int rc = getaddrinfo(host.c_str(), port.c_str(), &hints, &found);
    if (rc != 0)
    {
        *error = "cannot find " + host + ": " + gai_strerror(rc);
        return -1;
    }
    int fd = -1;
    std::string last = "no address";
    for (addrinfo* a = found; a && fd < 0; a = a->ai_next)
    {
        fd = socket(a->ai_family, a->ai_socktype | SOCK_CLOEXEC, a->ai_protocol);
        if (fd < 0)
        {
            last = strerror(errno);
            continue;
        }
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        if (connect(fd, a->ai_addr, a->ai_addrlen) != 0 && errno != EINPROGRESS)
        {
            last = strerror(errno);
            close(fd);
            fd = -1;
            continue;
        }
        int soerr = 0;
        socklen_t len = sizeof(soerr);
        if (WaitFor(fd, POLLOUT, timeoutMs) <= 0 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &len) != 0 || soerr)
        {
            last = soerr ? strerror(soerr) : "timed out";
            close(fd);
            fd = -1;
        }
    }
    freeaddrinfo(found);
    if (fd < 0)
    {
        *error = "cannot connect to " + host + ":" + port + ": " + last;
        return -1;
    }
    const int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    return fd;
}
} // namespace

int WaitFor(int fd, short events, double timeoutMs)
{
    pollfd p = {fd, events, 0};
    return poll(&p, 1, timeoutMs < 0 ? 0 : static_cast<int>(timeoutMs));
}

struct Conn::Impl
{
    int fd = -1;
    bool tls = false;
    short waits = POLLIN;
    std::string error;
    std::string fingerprint;
    std::string pinned; // tls_fingerprint, normalised
    size_t pendingWrite = 0; // the length of the write TLS must be called again with
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;

    ~Impl()
    {
        if (tls)
        {
            mbedtls_ssl_close_notify(&ssl); // best effort: the socket does not wait
            mbedtls_ssl_free(&ssl);
            mbedtls_ssl_config_free(&conf);
        }
        if (fd >= 0)
            close(fd);
    }

    // With tls_fingerprint, the server's own certificate decides alone: the chain above it,
    // its authority and its name are not asked for (the fingerprint is the trust).
    static int Verify(void* ctx, mbedtls_x509_crt* crt, int depth, uint32_t* flags)
    {
        Impl* self = static_cast<Impl*>(ctx);
        if (depth == 0)
        {
            self->fingerprint = HexSha256(crt);
            if (!self->pinned.empty())
                *flags = self->fingerprint == self->pinned ? 0 : MBEDTLS_X509_BADCERT_NOT_TRUSTED;
        }
        else if (!self->pinned.empty())
        {
            *flags = 0;
        }
        return 0;
    }

    // What a failed check means, for the player (short: the lobby shows it; the log has the rest)
    std::string VerifyError(const std::string& host, uint32_t flags) const
    {
        if (!pinned.empty())
            return "The server's certificate does not match tls_fingerprint (net.ini).";
        if (flags & (MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE))
        {
            char now[32];
            const time_t t = time(nullptr);
            strftime(now, sizeof(now), "%Y-%m-%d", gmtime(&t));
            return std::string("The server's certificate is ") +
                   (flags & MBEDTLS_X509_BADCERT_EXPIRED ? "out of date" : "not valid yet") +
                   ", or the console's clock is wrong (" + now + ").";
        }
        if (flags & MBEDTLS_X509_BADCERT_CN_MISMATCH)
            return "The server's certificate is for another name than " + host + ".";
        if (flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED)
            return "The server's certificate is not from a known authority: set ca_file or tls_fingerprint "
                   "in net.ini.";
        char info[256];
        mbedtls_x509_crt_verify_info(info, sizeof(info), "", flags);
        std::string text = info;
        while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
            text.pop_back();
        for (char& c : text)
        {
            if (c == '\n')
                c = ';';
        }
        return "The server's certificate was refused: " + text + ".";
    }

    bool Handshake(const std::string& host, const TlsOptions& o, double timeoutMs, std::string* out)
    {
        Shared& s = Common(out);
        if (!s.ready)
            return false;
        if (!o.fingerprint.empty())
        {
            pinned = NormalFingerprint(o.fingerprint);
            if (pinned.empty())
            {
                *out = "tls_fingerprint in net.ini is not sha256: and 64 hex digits.";
                return false;
            }
        }
        else if (!LoadAuthorities(s, o.caFile, out))
        {
            return false;
        }
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&conf);
        tls = true;
        int rc = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                             MBEDTLS_SSL_PRESET_DEFAULT);
        if (rc != 0)
        {
            *out = "TLS: " + MbedError(rc);
            return false;
        }
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        // (pinned: an empty chain, since mbedTLS refuses none at all before asking Verify)
        static mbedtls_x509_crt none = [] {
            mbedtls_x509_crt c;
            mbedtls_x509_crt_init(&c);
            return c;
        }();
        mbedtls_ssl_conf_ca_chain(&conf, pinned.empty() ? &s.authorities : &none, nullptr);
        mbedtls_ssl_conf_verify(&conf, &Impl::Verify, this);
        mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &s.drbg);
        if ((rc = mbedtls_ssl_setup(&ssl, &conf)) != 0 || (rc = mbedtls_ssl_set_hostname(&ssl, host.c_str())) != 0)
        {
            *out = "TLS: " + MbedError(rc);
            return false;
        }
        mbedtls_ssl_set_bio(&ssl, &fd, CallbackSend, CallbackRecv, nullptr);
        const double end = NowMs() + timeoutMs;
        for (;;)
        {
            rc = mbedtls_ssl_handshake(&ssl);
            if (rc == 0)
                break;
            if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                const uint32_t flags = mbedtls_ssl_get_verify_result(&ssl);
                if (rc == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED && flags != 0 && flags != static_cast<uint32_t>(-1))
                {
                    *out = VerifyError(host, flags);
                    char info[256];
                    mbedtls_x509_crt_verify_info(info, sizeof(info), "", flags);
                    for (char& c : info)
                    {
                        if (c == '\n')
                            c = ' ';
                    }
                    fprintf(stderr, "gta2: net: TLS: %s refused, certificate %s (tls_fingerprint takes it): %s\n",
                            host.c_str(), fingerprint.c_str(), info);
                }
                else if (rc == MBEDTLS_ERR_SSL_CONN_EOF || rc == MBEDTLS_ERR_NET_CONN_RESET)
                    *out = "The server closed the connection in the TLS handshake: is it in TLS? (try ws://)";
                else
                    *out = "TLS handshake with " + host + " failed: " + MbedError(rc);
                return false;
            }
            const short events = rc == MBEDTLS_ERR_SSL_WANT_READ ? POLLIN : POLLOUT;
            if (NowMs() > end || WaitFor(fd, events, end - NowMs()) <= 0)
            {
                *out = "The TLS handshake with " + host + " timed out: is the server in TLS?";
                return false;
            }
        }
        fprintf(stderr, "gta2: net: %s with %s (%s), certificate %s\n", mbedtls_ssl_get_version(&ssl), host.c_str(),
                mbedtls_ssl_get_ciphersuite(&ssl), fingerprint.c_str());
        return true;
    }
};

Conn::Conn(std::unique_ptr<Impl> impl) : m(std::move(impl))
{
}

Conn::~Conn() = default;

std::unique_ptr<Conn> Conn::Open(const std::string& host, const std::string& port, bool tls,
                                 const TlsOptions& options, double timeoutMs, std::string* error)
{
    auto impl = std::make_unique<Impl>();
    const double end = NowMs() + timeoutMs;
    impl->fd = TcpConnect(host, port, timeoutMs, error);
    if (impl->fd < 0)
        return nullptr;
    if (tls && !impl->Handshake(host, options, end - NowMs(), error))
        return nullptr;
    return std::unique_ptr<Conn>(new Conn(std::move(impl)));
}

long Conn::Read(void* buffer, size_t length)
{
    if (!m->tls)
    {
        const ssize_t n = recv(m->fd, buffer, length, 0);
        if (n >= 0)
            return n;
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        {
            m->waits = POLLIN;
            return kAgain;
        }
        m->error = strerror(errno);
        return kError;
    }
    for (;;)
    {
        const int rc = mbedtls_ssl_read(&m->ssl, static_cast<unsigned char*>(buffer), length);
        if (rc > 0)
            return rc;
        if (rc == 0 || rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || rc == MBEDTLS_ERR_SSL_CONN_EOF)
            return 0;
        if (rc == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) // TLS 1.3, after the handshake
            continue;
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            m->waits = rc == MBEDTLS_ERR_SSL_WANT_READ ? POLLIN : POLLOUT;
            return kAgain;
        }
        m->error = rc == MBEDTLS_ERR_NET_CONN_RESET ? "connection reset" : "TLS: " + MbedError(rc);
        return kError;
    }
}

long Conn::Write(const void* buffer, size_t length)
{
    if (length == 0)
        return 0;
    if (!m->tls)
    {
        const ssize_t n = send(m->fd, buffer, length, MSG_NOSIGNAL);
        if (n >= 0)
            return n;
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        {
            m->waits = POLLOUT;
            return kAgain;
        }
        m->error = strerror(errno);
        return kError;
    }
    // After WANT_WRITE, mbedtls_ssl_write must get the same length again: it sends the rest of
    // the record it made, then says that many bytes were written.
    size_t n = m->pendingWrite ? m->pendingWrite : length;
    if (n > length)
        n = length; // (cannot happen: the caller keeps the bytes until written)
    const int rc = mbedtls_ssl_write(&m->ssl, static_cast<const unsigned char*>(buffer), n);
    if (rc >= 0)
    {
        m->pendingWrite = 0;
        return rc;
    }
    if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
    {
        m->pendingWrite = n;
        m->waits = rc == MBEDTLS_ERR_SSL_WANT_READ ? POLLIN : POLLOUT;
        return kAgain;
    }
    m->error = rc == MBEDTLS_ERR_NET_CONN_RESET ? "connection reset" : "TLS: " + MbedError(rc);
    return kError;
}

int Conn::Fd() const
{
    return m->fd;
}

short Conn::Waits() const
{
    return m->waits;
}

const std::string& Conn::Error() const
{
    return m->error;
}

const std::string& Conn::Fingerprint() const
{
    return m->fingerprint;
}

} // namespace gta2net
