// A connection to the game server: TCP, plain (ws://, http://) or in TLS (wss://, https://),
// with mbedTLS built into gta2-online (scripts/native/build_mbedtls.sh): the consoles' own
// OpenSSL differ from one system to the next.
// The server's certificate is always checked: against the authorities of the console (its
// usual bundle files, or SSL_CERT_FILE), or of ca_file, and for the server's name; or, for a
// server with a certificate of its own making, it is exactly the one whose sha256 is given.
// Multiplayer build only (gta2-online).
#pragma once

#include <stddef.h>

#include <memory>
#include <string>

namespace gta2net
{

struct TlsOptions
{
    std::string caFile;      // the authorities trusted (PEM); "" = the console's
    std::string fingerprint; // "sha256:<64 hex digits>" (colons allowed): trust this certificate only
};

class Conn
{
  public:
    static constexpr long kAgain = -1; // nothing can be done now: wait for Waits() on Fd()
    static constexpr long kError = -2; // the connection is lost: Error() says why

    // Connects (and, with tls, makes the TLS handshake), waiting up to timeoutMs; then
    // non-blocking. nullptr, and why in *error, on failure.
    static std::unique_ptr<Conn> Open(const std::string& host, const std::string& port, bool tls,
                                      const TlsOptions& options, double timeoutMs, std::string* error);
    ~Conn();

    // > 0: bytes read; 0: closed by the server; kAgain or kError.
    long Read(void* buffer, size_t length);
    // > 0: bytes written (maybe fewer than length); kAgain or kError. After kAgain, call
    // again with the same bytes first (TLS sends again the record it had made of them).
    long Write(const void* buffer, size_t length);

    int Fd() const;
    short Waits() const; // POLLIN or POLLOUT, what the last kAgain waits for
    const std::string& Error() const;

    // sha256 of the server's certificate as tls_fingerprint takes it, "" in plain TCP
    const std::string& Fingerprint() const;

    struct Impl;

  private:
    explicit Conn(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m;
};

// Waits for events on fd, up to timeoutMs; > 0 when they came.
int WaitFor(int fd, short events, double timeoutMs);

} // namespace gta2net
