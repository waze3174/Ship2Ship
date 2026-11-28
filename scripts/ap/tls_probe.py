"""The certificate test of Ship2Ship's wss:// client, against the real archipelago.gg, through whichever binary is given.

The binary does one TLS handshake with the Archipelago client's own certificate checks (combo/net/ComboApTls.h) and says
the result as its exit code (ComboArchipelago::RunTlsProbe): 0 verified, 1 certificate refused, 2 no answer. Only the
handshake is made: no room, no upload, no request.

  tls_probe.py --ca-bundle combo/net/cacert/cacert.pem --ca-flag=--cert-store -- build/combo/comboApClientHarness --tls-probe
  tls_probe.py --expect-bundled -- ./Ship2Ship-0.3.0-linux-x86_64.AppImage --ap-tls-probe

What is checked:
  roots         archipelago.gg verifies against the roots the binary finds (the system's, and the bundled cacert.pem)
  wrong host    the same certificate is refused when it has to be valid for example.org, and the refusal names the host
  bundle alone  (needs --ca-flag) the bundled file alone verifies archipelago.gg, which is all macOS has
  other root    (needs --ca-flag) a root that did not sign archipelago.gg's certificate does not verify it
  bundled seen  (--expect-bundled) the binary found its cacert.pem by itself: the refusal's list of roots names it

With no network (no answer from archipelago.gg) the script says so and passes: a runner without internet cannot test this.
"""
import argparse
import os
import pathlib
import re
import subprocess
import sys
import tempfile

HOST = "archipelago.gg:443"
WRONG_HOST = "example.org"
EXIT_VERIFIED, EXIT_REFUSED, EXIT_NO_ANSWER = 0, 1, 2
TIMEOUT_SECONDS = 60
FIRST_CERTIFICATE = re.compile(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", re.DOTALL)


def run(command, environment=None):
    env = {**os.environ, **(environment or {})}
    result = subprocess.run(command, capture_output=True, text=True, timeout=TIMEOUT_SECONDS, env=env, check=False)
    return result.returncode, (result.stdout + result.stderr).strip()


class Report:
    def __init__(self):
        self.failures = 0
        self.no_network = False

    def expect(self, name, command, expected_exit, expected_text=None, environment=None):
        code, text = run(command, environment)
        if code == EXIT_NO_ANSWER and expected_exit != EXIT_NO_ANSWER:
            self.no_network = True
            print(f"SKIP {name}: no answer from the server ({text})", flush=True)
            return
        problems = []
        if code != expected_exit:
            problems.append(f"exit {code}, wanted {expected_exit}")
        if expected_text and expected_text not in text:
            problems.append(f"output lacks '{expected_text}'")
        print(f"{'FAIL' if problems else 'PASS'} {name}: {text}" + (f"  [{'; '.join(problems)}]" if problems else ""),
              flush=True)
        self.failures += bool(problems)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ca-bundle", help="cacert.pem, for the checks that need the bundle as a file")
    parser.add_argument("--ca-flag", help="the binary's option that adds that file as trusted roots (the test harness's "
                                          "--cert-store); the launcher finds its own and has none")
    parser.add_argument("--expect-bundled", action="store_true", help="the binary has to find its cacert.pem itself")
    parser.add_argument("--host", default=HOST)
    parser.add_argument("command", nargs="+", help="the binary and its probe option, after --")
    args = parser.parse_args()
    binary = pathlib.Path(args.command[0])
    if binary.is_file():
        args.command[0] = str(binary.resolve())  # Windows does not find "folder/program.exe" without a ./ or an absolute path
    ca = [args.ca_flag, args.ca_bundle] if args.ca_flag and args.ca_bundle else []
    report = Report()

    report.expect(f"{args.host} verifies", [*args.command, args.host, *ca], EXIT_VERIFIED)
    report.expect(f"{args.host} is refused for {WRONG_HOST}", [*args.command, args.host, "--verify-host", WRONG_HOST, *ca],
                  EXIT_REFUSED, f"it is not for {WRONG_HOST}")
    if args.expect_bundled:
        report.expect("the binary found its bundled roots", [*args.command, args.host, "--verify-host", WRONG_HOST],
                      EXIT_REFUSED, "cacert.pem")
    if ca:
        report.expect("the bundled roots alone verify it", [*args.command, args.host, *ca], EXIT_VERIFIED,
                      environment={"SSL_CERT_FILE": args.ca_bundle})
        other_root = FIRST_CERTIFICATE.search(pathlib.Path(args.ca_bundle).read_text(encoding="utf-8")).group(0)
        with tempfile.TemporaryDirectory() as folder:
            unrelated = pathlib.Path(folder) / "unrelated-root.pem"
            unrelated.write_text(other_root + "\n", encoding="ascii")
            report.expect("a root that did not sign it does not verify it", [*args.command, args.host],
                          EXIT_REFUSED, "unable to get local issuer certificate",
                          environment={"SSL_CERT_FILE": str(unrelated)})
    if report.no_network:
        print("::warning::no network: the certificate checks against archipelago.gg were skipped")
    return 1 if report.failures else 0


if __name__ == "__main__":
    sys.exit(main())
