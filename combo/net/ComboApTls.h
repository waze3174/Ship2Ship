// Certificate trust for the Archipelago client's wss:// connections, and the handshake probe that tests it.
//
// What a connection to a server that is not this machine requires:
//   - the certificate chain ends in a root the player's system trusts, or in the bundled cacert.pem
//   - the leaf certificate is valid for the host name (or IP address) that was asked for
// A server on this machine (localhost, 127.0.0.1, ::1) is the player's own and has a self-signed certificate: its
// certificate is not checked (wswrap, combo/net/wswrap/wswrap_websocketpp.hpp, decides that).
//
// Where the roots come from, first match wins among the system sources, then the bundled file is added:
//   SSL_CERT_FILE / SSL_CERT_DIR   if set and readable (the same variables OpenSSL honours)
//   Windows                         the "ROOT" system store
//   macOS                           none: OpenSSL cannot read the Keychain, so the bundle is the trust
//   Linux                           the distribution's bundle file, else /etc/ssl/certs as a directory (a static
//                                   OpenSSL would look where it was built, which is not the host)
//   the bundled file                combo/net/cacert/cacert.pem, shipped beside the game (curl's CA list, MPL-2.0)
// Header-only: included by wswrap's header, so by the one translation unit that uses wswrap (ComboArchipelago.cpp).
#pragma once

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#ifdef _WIN32
#include <wincrypt.h>
#endif

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ComboApSync.h"

namespace ComboApTls {

inline constexpr const char* kLinuxBundleFiles[] = { "/etc/ssl/certs/ca-certificates.crt",
                                                     "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/cert.pem",
                                                     "/etc/ssl/ca-bundle.pem" };
inline constexpr const char* kLinuxCertificateDirectory = "/etc/ssl/certs";
inline constexpr const char* kEnvironmentCertificateFile = "SSL_CERT_FILE";
inline constexpr const char* kEnvironmentCertificateDirectory = "SSL_CERT_DIR";

inline bool IsLoopbackHost(const std::string& host) {
    return host == "localhost" || host == "127.0.0.1" || host == "::1";
}

namespace detail {

inline bool AddFile(asio::ssl::context& context, const std::string& path) {
    std::error_code error;
    if (path.empty() || !std::filesystem::is_regular_file(path, error)) {
        return false;
    }
    asio::error_code loadError;
    context.load_verify_file(path, loadError);
    return !loadError;
}

inline bool AddDirectory(asio::ssl::context& context, const std::string& path) {
    std::error_code error;
    if (path.empty() || !std::filesystem::is_directory(path, error)) {
        return false;
    }
    asio::error_code loadError;
    context.add_verify_path(path, loadError);
    return !loadError;
}

inline void Note(std::string& sources, const std::string& what) {
    sources += (sources.empty() ? "" : ", ") + what;
}

#ifdef _WIN32
// Adds every certificate of the "ROOT" system store to the context's own store.
inline bool AddWindowsRoots(asio::ssl::context& context) {
    HCERTSTORE systemStore = CertOpenSystemStoreA(0, "ROOT");
    if (systemStore == nullptr) {
        return false;
    }
    X509_STORE* store = SSL_CTX_get_cert_store(context.native_handle());
    bool any = false;
    PCCERT_CONTEXT certificate = nullptr;
    while ((certificate = CertEnumCertificatesInStore(systemStore, certificate)) != nullptr) {
        const unsigned char* der = certificate->pbCertEncoded;
        if (X509* parsed = d2i_X509(nullptr, &der, static_cast<long>(certificate->cbCertEncoded))) {
            any = X509_STORE_add_cert(store, parsed) == 1 || any;
            X509_free(parsed);
        }
    }
    CertCloseStore(systemStore, 0);
    return any;
}
#endif

inline const char* EnvironmentValue(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? value : nullptr;
}

// The system's roots, as a list of what was loaded ("" when nothing was).
inline std::string AddSystemRoots(asio::ssl::context& context) {
    std::string sources;
    const char* file = EnvironmentValue(kEnvironmentCertificateFile);
    const char* directory = EnvironmentValue(kEnvironmentCertificateDirectory);
    if (file != nullptr && AddFile(context, file)) {
        Note(sources, std::string(kEnvironmentCertificateFile) + " " + file);
    }
    if (directory != nullptr && AddDirectory(context, directory)) {
        Note(sources, std::string(kEnvironmentCertificateDirectory) + " " + directory);
    }
    if (!sources.empty()) {
        return sources;
    }
#if defined(_WIN32)
    if (AddWindowsRoots(context)) {
        Note(sources, "the Windows certificate store");
    }
#elif !defined(__APPLE__)
    for (const char* bundle : kLinuxBundleFiles) {
        if (AddFile(context, bundle)) {
            Note(sources, bundle);
            return sources;
        }
    }
    if (AddDirectory(context, kLinuxCertificateDirectory)) {
        Note(sources, kLinuxCertificateDirectory);
    }
#endif
    return sources;
}

} // namespace detail

// What the verification callback writes when it refuses a certificate. Shared between the connection and its handshake.
using Refusal = std::shared_ptr<std::string>;

inline Refusal NewRefusal() {
    return std::make_shared<std::string>();
}

// Sets the protocol floor, and (for a server that is not this machine) the roots and the certificate and host check.
// `refusal` is cleared, and holds the reason after a handshake that was refused.
inline void ConfigureContext(asio::ssl::context& context, const std::string& host, bool verify,
                             const std::string& bundledRoots, const Refusal& refusal) {
    refusal->clear();
    asio::error_code error;
    context.set_options(asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv2 |
                            asio::ssl::context::no_sslv3 | asio::ssl::context::no_tlsv1 |
                            asio::ssl::context::no_tlsv1_1 | asio::ssl::context::single_dh_use,
                        error);
    if (!verify) {
        return;
    }
    std::string sources = detail::AddSystemRoots(context);
    if (detail::AddFile(context, bundledRoots)) {
        detail::Note(sources, std::filesystem::path(bundledRoots).filename().string());
    }
    if (sources.empty()) {
        sources = "none found";
    }
    context.set_verify_mode(asio::ssl::verify_peer, error);
    context.set_verify_callback([hostCheck = asio::ssl::host_name_verification(host), host, sources,
                                 refusal](bool preverified, asio::ssl::verify_context& verifyContext) {
        const bool accepted = hostCheck(preverified, verifyContext);
        if (!accepted && refusal->empty()) {
            const std::string reason =
                preverified ? "it is not for " + host
                            : X509_verify_cert_error_string(X509_STORE_CTX_get_error(verifyContext.native_handle()));
            *refusal = std::string(ComboApSync::kCertificateRefusalPrefix) + ": " + reason +
                       ComboApSync::kTrustedRootsNote + sources + ")";
        }
        return accepted;
    });
}

enum class ProbeOutcome {
    kVerified,    // the handshake completed and the certificate chain and host were accepted
    kRefused,     // the server answered and its certificate was not accepted
    kUnreachable, // no answer: not resolved, not connected, or the handshake did not finish
};

struct ProbeResult {
    ProbeOutcome outcome = ProbeOutcome::kUnreachable;
    std::string text;
};

// One TLS handshake with full verification, through the same context setup the client uses. `verifyHost` is the name
// the certificate has to be valid for when it is not the one connected to (empty: the same); it is how a wrong host is
// tested.
inline ProbeResult Probe(const std::string& host, const std::string& port, const std::string& bundledRoots,
                         const std::string& verifyHost, std::chrono::seconds timeout) {
    ProbeResult result;
    asio::io_context io;
    asio::error_code error;
    asio::ip::tcp::resolver resolver(io);
    const auto endpoints = resolver.resolve(host, port, error);
    if (error) {
        result.text = "cannot resolve " + host + ": " + error.message();
        return result;
    }
    const Refusal refusal = NewRefusal();
    asio::ssl::context context(asio::ssl::context::sslv23);
    ConfigureContext(context, verifyHost.empty() ? host : verifyHost, true, bundledRoots, refusal);
    asio::ssl::stream<asio::ip::tcp::socket> stream(io, context);
    SSL_set_tlsext_host_name(stream.native_handle(), host.c_str());

    bool timedOut = false;
    asio::steady_timer deadline(io, timeout);
    deadline.async_wait([&](const asio::error_code& expired) {
        if (!expired) {
            timedOut = true;
            asio::error_code ignored;
            stream.lowest_layer().close(ignored);
        }
    });
    asio::error_code failure;
    asio::async_connect(stream.lowest_layer(), endpoints, [&](const asio::error_code& connectError, const auto&) {
        if (connectError) {
            failure = connectError;
            deadline.cancel();
            return;
        }
        stream.async_handshake(asio::ssl::stream_base::client, [&](const asio::error_code& handshakeError) {
            failure = handshakeError;
            deadline.cancel();
        });
    });
    io.run();

    if (!failure) {
        result.outcome = ProbeOutcome::kVerified;
        result.text = "the certificate of " + host + " was accepted for " + (verifyHost.empty() ? host : verifyHost);
    } else if (!refusal->empty()) {
        result.outcome = ProbeOutcome::kRefused;
        result.text = *refusal;
    } else {
        result.text =
            "no TLS handshake with " + host + ":" + port + ": " + (timedOut ? "timed out" : failure.message());
    }
    return result;
}

} // namespace ComboApTls
