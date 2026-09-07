# 02 - Calling hash and encryption functions

This example calculates the MD5 digest of `hello`, then encrypts and decrypts
a buffer containing that text with ChaCha20-Poly1305. It demonstrates basic
calls from `hal/security/hal_crypto.h`, enabled by `HAL_ENABLE_CRYPTO`.

The calculations run once in `app_start()`.
It is an API example, not a cryptographic correctness test. The MD5 call
excludes the terminating NUL byte; encryption includes the whole buffer,
including that byte.

**The all-zero key and nonce are demonstration values only.** Do not use them
to protect real data or as a model for application key and nonce management.

## Build

Run from the repository root:

```bash
vscode/entry/jh-vscode build \
  --project examples/02_crypto --target rp2040 --board pico
```

The project configuration also includes RP2350 ARM, RP2350 RISC-V, and
STM32G474. No additional hardware is required.
