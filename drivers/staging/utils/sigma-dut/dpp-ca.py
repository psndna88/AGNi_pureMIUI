#!/usr/bin/env python3
#
# Sigma Control API DUT (DPP CA)
# Copyright (c) 2020, The Linux Foundation
# All Rights Reserved.
# Licensed under the Clear BSD license. See README for more details.

import base64
import OpenSSL
import os
import subprocess
import sys

def dpp_sign_cert(cacert, cakey, csr_der):
    csr = OpenSSL.crypto.load_certificate_request(OpenSSL.crypto.FILETYPE_ASN1,
                                                  csr_der)
    cert = OpenSSL.crypto.X509()
    cert.set_serial_number(12345)
    cert.gmtime_adj_notBefore(-10)
    cert.gmtime_adj_notAfter(100000)
    cert.set_pubkey(csr.get_pubkey())
    dn = csr.get_subject()
    cert.set_subject(dn)
    cert.set_version(2)
    cert.add_extensions([
        OpenSSL.crypto.X509Extension(b"basicConstraints", True,
                                     b"CA:FALSE"),
        OpenSSL.crypto.X509Extension(b"subjectKeyIdentifier", False,
                                     b"hash", subject=cert),
        OpenSSL.crypto.X509Extension(b"authorityKeyIdentifier", False,
                                     b"keyid:always", issuer=cacert),
    ])
    cert.set_issuer(cacert.get_subject())
    cert.sign(cakey, "sha256")
    return cert

def main():
    if len(sys.argv) < 2:
        print("No certificate directory path provided")
        sys.exit(-1)

    cert_dir = sys.argv[1]
    cacert_file = os.path.join(cert_dir, "dpp-ca.pem")
    cakey_file = os.path.join(cert_dir, "dpp-ca.key")
    csr_file = os.path.join(cert_dir, "dpp-ca-csr")
    cert_file = os.path.join(cert_dir, "dpp-ca-cert")
    pkcs7_file = os.path.join(cert_dir, "dpp-ca-pkcs7")
    certbag_file = os.path.join(cert_dir, "dpp-ca-certbag")

    with open(cacert_file, "rb") as f:
        res = f.read()
        cacert = OpenSSL.crypto.load_certificate(OpenSSL.crypto.FILETYPE_PEM,
                                                 res)

    with open(cakey_file, "rb") as f:
        res = f.read()
        cakey = OpenSSL.crypto.load_privatekey(OpenSSL.crypto.FILETYPE_PEM, res)

    if not os.path.exists(csr_file):
        print("No CSR file: %s" % csr_file)
        sys.exit(-1)

    with open(csr_file) as f:
        csr_b64 = f.read()

    csr = base64.b64decode(csr_b64)
    if not csr:
        print("Could not base64 decode CSR")
        sys.exit(-1)

    cert = dpp_sign_cert(cacert, cakey, csr)
    with open(cert_file, 'wb') as f:
        f.write(OpenSSL.crypto.dump_certificate(OpenSSL.crypto.FILETYPE_PEM,
                                                cert))

    subprocess.check_call(['openssl', 'crl2pkcs7', '-nocrl',
                           '-certfile', cert_file,
                           '-certfile', cacert_file,
                           '-outform', 'DER', '-out', pkcs7_file])

    with open(pkcs7_file, 'rb') as f:
        pkcs7_der = f.read()
        certbag = base64.b64encode(pkcs7_der)
    with open(certbag_file, 'wb') as f:
        f.write(certbag)

if __name__ == "__main__":
    main()
